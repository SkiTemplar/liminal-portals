#include "Portal/LiminalPortal.h"

#include "Camera/CameraTypes.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "LiminalPortals.h"
#include "LiminalPortalsSettings.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Portal/LiminalPortalMath.h"
#include "Portal/LiminalPortalSubsystem.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** El cubo básico del motor mide 100 cm de lado y está centrado en su origen. */
	constexpr double CubeSize = 100.0;
	/** Cuánto sobresale el marco por delante del plano del portal, en cm. */
	constexpr double FrameFrontOverhang = 5.0;
	constexpr double BackPlateThickness = 10.0;
	/** Separación mínima entre la caja de superficie extendida y la placa trasera, en cm. */
	constexpr float SurfaceToPlateMargin = 5.f;

	TAutoConsoleVariable<int32> CVarFlipClipPlane(
		TEXT("Liminal.Portal.FlipClipPlane"),
		0,
		TEXT("Diagnóstico de calibración: invierte la normal del clip plane de las capturas (1 = invertida)."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarCaptureReflectionMethod(
		TEXT("Liminal.Portal.CaptureReflectionMethod"),
		-1,
		TEXT("Experimento de coste (E5, D-30): método de reflejos de las capturas (-1 = el del proyecto, 0 = ninguno, ")
		TEXT("1 = Lumen, 2 = pantalla)."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarCaptureGIMethod(
		TEXT("Liminal.Portal.CaptureGIMethod"),
		-1,
		TEXT("Experimento de coste (D-36): iluminación global de las capturas (-1 = la del proyecto, 0 = ninguna, ")
		TEXT("1 = Lumen, 2 = en pantalla)."),
		ECVF_Default);

	TAutoConsoleVariable<FString> CVarCaptureShowFlags(
		TEXT("Liminal.Portal.CaptureShowFlags"),
		TEXT(""),
		TEXT("Experimento de coste (D-36): show flags de las capturas, \"Nombre=0+Nombre=1\" (con +: -ExecCmds parte por comas) ")
		TEXT("(p. ej. \"DistanceFieldAO=0+VolumetricFog=0\"; vacío = los de por defecto)."),
		ECVF_Default);

	void PlaceBox(UStaticMeshComponent* Component, const FVector& Center, const FVector& Size)
	{
		Component->SetRelativeLocation(Center);
		Component->SetRelativeScale3D(Size / CubeSize);
	}

	UStaticMeshComponent* CreateBox(AActor* Owner, FName Name, USceneComponent* Parent, UStaticMesh* Mesh)
	{
		UStaticMeshComponent* Component = Owner->CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Component->SetupAttachment(Parent);
		if (Mesh)
		{
			Component->SetStaticMesh(Mesh);
		}
		return Component;
	}

	/** Las superficies emisivas no deben inyectar luz en Lumen ni proyectar sombras. */
	void MakeVisualOnly(UStaticMeshComponent* Component)
	{
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Component->SetCastShadow(false);
		Component->bAffectDynamicIndirectLighting = false;
		Component->bAffectDistanceFieldLighting = false;
		Component->bReceivesDecals = false;
		Component->SetGenerateOverlapEvents(false);
	}

	/**
	 * Las capturas desactivan Lumen por defecto (SceneCaptureRendering.cpp fuerza GI y reflejos a
	 * None salvo que la propia captura los sobrescriba): sin esto, la imagen del portal sale sin luz
	 * indirecta ni luz de cielo. Se copian los métodos del proyecto en cada captura para seguir los
	 * cambios en caliente de r.DynamicGlobalIlluminationMethod y r.ReflectionMethod.
	 */
	void MatchProjectLighting(FPostProcessSettings& Settings)
	{
		static const TConsoleVariableData<int32>* GIMethod =
			IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DynamicGlobalIlluminationMethod"));
		static const TConsoleVariableData<int32>* ReflectionMethod =
			IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.ReflectionMethod"));

		const int32 CaptureGI = CVarCaptureGIMethod.GetValueOnGameThread();
		const bool bOverrideGI = CaptureGI >= EDynamicGlobalIlluminationMethod::None && CaptureGI <= EDynamicGlobalIlluminationMethod::ScreenSpace;
		const int32 ProjectGI = GIMethod ? GIMethod->GetValueOnGameThread() : EDynamicGlobalIlluminationMethod::Lumen;
		Settings.bOverride_DynamicGlobalIlluminationMethod = true;
		Settings.DynamicGlobalIlluminationMethod = static_cast<EDynamicGlobalIlluminationMethod::Type>(bOverrideGI ? CaptureGI : ProjectGI);
		const int32 CaptureReflection = CVarCaptureReflectionMethod.GetValueOnGameThread();
		const bool bOverrideReflection = CaptureReflection >= EReflectionMethod::None && CaptureReflection <= EReflectionMethod::ScreenSpace;
		const int32 ProjectReflection = ReflectionMethod ? ReflectionMethod->GetValueOnGameThread() : EReflectionMethod::Lumen;
		Settings.bOverride_ReflectionMethod = true;
		Settings.ReflectionMethod = static_cast<EReflectionMethod::Type>(bOverrideReflection ? CaptureReflection : ProjectReflection);
		// La caché de superficies de la captura va por defecto a media resolución; la vista principal, a 1.
		Settings.bOverride_LumenSurfaceCacheResolution = true;
		Settings.LumenSurfaceCacheResolution = 1.f;
	}

	UMaterialInterface* LoadOptionalMaterial(const TSoftObjectPtr<UMaterialInterface>& Path, const TCHAR* Label)
	{
		if (Path.IsNull() || !FPackageName::DoesPackageExist(Path.ToSoftObjectPath().GetLongPackageName()))
		{
			UE_LOG(LogLiminalPortals, Warning, TEXT("Material de %s no encontrado (%s): el portal sigue sin él."),
				Label, *Path.ToString());
			return nullptr;
		}
		UMaterialInterface* Material = Path.LoadSynchronous();
		if (!Material)
		{
			UE_LOG(LogLiminalPortals, Warning, TEXT("No se pudo cargar el material de %s (%s)."), Label, *Path.ToString());
		}
		return Material;
	}
}

ALiminalPortal::ALiminalPortal()
{
	PrimaryActorTick.bCanEverTick = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* CubeMesh = CubeFinder.Succeeded() ? CubeFinder.Object : nullptr;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	FrameLeft = CreateBox(this, TEXT("FrameLeft"), Root, CubeMesh);
	FrameRight = CreateBox(this, TEXT("FrameRight"), Root, CubeMesh);
	FrameTop = CreateBox(this, TEXT("FrameTop"), Root, CubeMesh);
	BackPlate = CreateBox(this, TEXT("BackPlate"), Root, CubeMesh);

	Surface = CreateBox(this, TEXT("Surface"), Root, CubeMesh);
	MakeVisualOnly(Surface);

	Fallback = CreateBox(this, TEXT("Fallback"), Root, CubeMesh);
	MakeVisualOnly(Fallback);
	Fallback->bVisibleInSceneCaptureOnly = true;

	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
	Capture->SetupAttachment(Root);
	Capture->SetUsingAbsoluteLocation(true);
	Capture->SetUsingAbsoluteRotation(true);
	Capture->SetUsingAbsoluteScale(true);
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->bAlwaysPersistRenderingState = true;
	Capture->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
	Capture->bEnableClipPlane = true;

	ApplyLayout();
}

FTransform ALiminalPortal::GetPortalTransform() const
{
	return FTransform(GetActorQuat(), GetActorLocation());
}

FTransform ALiminalPortal::GetPlaneTransform() const
{
	const FTransform PortalTransform = GetPortalTransform();
	const FVector Center = PortalTransform.TransformPositionNoScale(FVector(0.0, 0.0, PortalSize.Y * 0.5));
	return FTransform(PortalTransform.GetRotation(), Center);
}

FTransform ALiminalPortal::GetTransferToLinked() const
{
	if (!LinkedPortal)
	{
		return FTransform::Identity;
	}
	return LiminalPortalMath::MakeTransfer(GetPortalTransform(), LinkedPortal->GetPortalTransform());
}

UPrimitiveComponent* ALiminalPortal::GetSurfaceComponent() const
{
	return Surface;
}

UPrimitiveComponent* ALiminalPortal::GetFallbackComponent() const
{
	return Fallback;
}

void ALiminalPortal::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyLayout();
}

void ALiminalPortal::BeginPlay()
{
	Super::BeginPlay();
	ApplyLayout();
	ApplyMaterials();
	SetSurfaceHiddenInGame(false);
	// Nombre de la miga de GPU de la captura: Liminal.Bench en modo perfil la separa de la vista principal.
	Capture->ProfilingEventName = FString::Printf(TEXT("LiminalCapture %s"), *GetName());

	if (!LinkedPortal)
	{
		UE_LOG(LogLiminalPortals, Warning, TEXT("%s no tiene LinkedPortal: no capturará ni teletransportará."), *GetName());
	}

	if (ULiminalPortalSubsystem* Subsystem = GetWorld()->GetSubsystem<ULiminalPortalSubsystem>())
	{
		Subsystem->RegisterPortal(this);
	}
}

void ALiminalPortal::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (ULiminalPortalSubsystem* Subsystem = World->GetSubsystem<ULiminalPortalSubsystem>())
		{
			Subsystem->UnregisterPortal(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void ALiminalPortal::ApplyLayout()
{
	const double Width = PortalSize.X;
	const double Height = PortalSize.Y;
	const double Frame = FrameThickness;
	// Empotrado, los montantes cierran los lados hasta la placa trasera: no se puede entrar por
	// detrás del plano. Sin empotrar, el marco es un aro simétrico respecto al plano.
	const double JambBack = bEmbedded ? BackPlateDistance : FrameFrontOverhang;
	const double JambDepth = JambBack + FrameFrontOverhang;
	const double JambCenterX = (FrameFrontOverhang - JambBack) * 0.5;

	const FVector JambSize(JambDepth, Frame, Height + Frame);
	const double JambY = Width * 0.5 + Frame * 0.5;
	PlaceBox(FrameLeft, FVector(JambCenterX, -JambY, (Height + Frame) * 0.5), JambSize);
	PlaceBox(FrameRight, FVector(JambCenterX, JambY, (Height + Frame) * 0.5), JambSize);
	PlaceBox(FrameTop, FVector(JambCenterX, 0.0, Height + Frame * 0.5), FVector(JambDepth, Width + 2.0 * Frame, Frame));
	for (UStaticMeshComponent* Box : { FrameLeft.Get(), FrameRight.Get(), FrameTop.Get() })
	{
		Box->SetVisibility(bShowFrame);
		// Oculto, sigue proyectando sombra: si no, el sol entra por los lados del túnel (entre la pared y la placa).
		Box->SetCastHiddenShadow(true);
	}

	PlaceBox(BackPlate, FVector(-(BackPlateDistance + BackPlateThickness * 0.5), 0.0, (Height + Frame) * 0.5),
		FVector(BackPlateThickness, Width + 2.0 * Frame, Height + Frame));
	BackPlate->SetVisibility(bEmbedded);
	BackPlate->SetCollisionEnabled(bEmbedded ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);

	// El reserva queda medio centímetro detrás del plano para no competir con la superficie.
	PlaceBox(Fallback, FVector(-0.5, 0.0, Height * 0.5), FVector(0.1, Width, Height));

	CurrentSurfaceDepth = -1.f;
	SetSurfaceDepth(SurfaceBaseDepth);
}

void ALiminalPortal::SetSurfaceDepth(float Depth)
{
	const float MaxDepth = FMath::Max(BackPlateDistance - SurfaceToPlateMargin, 0.1f);
	const float SafeDepth = FMath::Clamp(Depth, 0.1f, MaxDepth);
	if (FMath::IsNearlyEqual(SafeDepth, CurrentSurfaceDepth))
	{
		return;
	}
	CurrentSurfaceDepth = SafeDepth;
	// La cara frontal de la caja queda siempre sobre el plano (X = 0); crece hacia detrás.
	PlaceBox(Surface, FVector(-SafeDepth * 0.5, 0.0, PortalSize.Y * 0.5), FVector(SafeDepth, PortalSize.X, PortalSize.Y));
}

void ALiminalPortal::SetSurfaceHiddenInGame(bool bHideSurface)
{
	Surface->SetHiddenInGame(bHideSurface || bExitOnly);
}

void ALiminalPortal::UpdateSurfaceDepth(const FVector& CameraLocation)
{
	const double SignedDistance = LiminalPortalMath::SignedDistanceToPlane(GetPlaneTransform(), CameraLocation);
	SetSurfaceDepth(LiminalPortalMath::ComputeSurfaceDepth(SignedDistance, NearThicknessDistance,
		SurfaceBaseDepth, SurfaceExtendedDepth));
}

void ALiminalPortal::ApplyMaterials()
{
	const ULiminalPortalsSettings* Settings = GetDefault<ULiminalPortalsSettings>();

	if (UMaterialInterface* SurfaceBase = LoadOptionalMaterial(Settings->PortalSurfaceMaterial, TEXT("superficie")))
	{
		SurfaceMaterial = UMaterialInstanceDynamic::Create(SurfaceBase, this);
		Surface->SetMaterial(0, SurfaceMaterial);
		if (RenderTarget)
		{
			SurfaceMaterial->SetTextureParameterValue(Settings->SurfaceTextureParameter, RenderTarget);
		}
	}

	if (UMaterialInterface* FallbackMaterial = LoadOptionalMaterial(Settings->PortalFallbackMaterial, TEXT("reserva")))
	{
		Fallback->SetMaterial(0, FallbackMaterial);
	}
}

bool ALiminalPortal::EnsureRenderTarget(const FIntPoint& Size)
{
	if (Size.X <= 0 || Size.Y <= 0)
	{
		return false;
	}

	if (!RenderTarget)
	{
		RenderTarget = UKismetRenderingLibrary::CreateRenderTarget2D(this, Size.X, Size.Y, RTF_RGBA16f,
			FLinearColor::Black, false);
		if (!RenderTarget)
		{
			UE_LOG(LogLiminalPortals, Error, TEXT("%s: no se pudo crear el render target %dx%d."), *GetName(), Size.X, Size.Y);
			return false;
		}
		Capture->TextureTarget = RenderTarget;
		if (SurfaceMaterial)
		{
			SurfaceMaterial->SetTextureParameterValue(GetDefault<ULiminalPortalsSettings>()->SurfaceTextureParameter, RenderTarget);
		}
		return true;
	}

	if (RenderTarget->SizeX != Size.X || RenderTarget->SizeY != Size.Y)
	{
		RenderTarget->ResizeTarget(Size.X, Size.Y);
	}
	return true;
}

bool ALiminalPortal::CaptureView(const FMinimalViewInfo& View, const FIntPoint& RenderTargetSize,
	TConstArrayView<UPrimitiveComponent*> HiddenComponents)
{
	if (!LinkedPortal || !EnsureRenderTarget(RenderTargetSize))
	{
		return false;
	}

	const FTransform CapturePose = LiminalPortalMath::TransferTransform(GetTransferToLinked(),
		FTransform(View.Rotation.Quaternion(), View.Location));
	Capture->SetWorldLocationAndRotation(CapturePose.GetLocation(), CapturePose.GetRotation());
	Capture->FOVAngle = View.FOV;
	MatchProjectLighting(Capture->PostProcessSettings);
	ApplyCaptureShowFlags();

	const LiminalPortalMath::FClipPlane ClipPlane =
		LiminalPortalMath::ComputeClipPlane(LinkedPortal->GetPlaneTransform(), ClipPlaneOffset);
	const bool bFlipClipPlane = CVarFlipClipPlane.GetValueOnGameThread() != 0;
	Capture->ClipPlaneBase = ClipPlane.Base;
	Capture->ClipPlaneNormal = bFlipClipPlane ? -ClipPlane.Normal : ClipPlane.Normal;

	Capture->ClearHiddenComponents();
	for (UPrimitiveComponent* Component : HiddenComponents)
	{
		Capture->HideComponent(Component);
	}

	Capture->CaptureScene();
	bHasCaptureState = true;
	LastCaptureSeconds = GetWorld()->GetRealTimeSeconds();
	return true;
}

void ALiminalPortal::ApplyCaptureShowFlags()
{
	const FString Spec = CVarCaptureShowFlags.GetValueOnGameThread();
	if (Spec == AppliedShowFlagSpec)
	{
		return;
	}
	for (const TPair<uint32, bool>& Original : AppliedShowFlagOriginals)
	{
		Capture->ShowFlags.SetSingleFlag(Original.Key, Original.Value);
	}
	AppliedShowFlagOriginals.Reset();
	AppliedShowFlagSpec = Spec;

	TArray<FString> Entries;
	Spec.ParseIntoArray(Entries, TEXT("+"), true);
	for (const FString& Entry : Entries)
	{
		FString Name;
		FString Value;
		const int32 Index = Entry.Split(TEXT("="), &Name, &Value) ? FEngineShowFlags::FindIndexByName(*Name.TrimStartAndEnd()) : INDEX_NONE;
		if (Index == INDEX_NONE)
		{
			UE_LOG(LogLiminalPortals, Warning, TEXT("%s: Liminal.Portal.CaptureShowFlags: '%s' no es Nombre=0|1 con un show flag conocido."),
				*GetName(), *Entry);
			continue;
		}
		AppliedShowFlagOriginals.Emplace(Index, Capture->ShowFlags.GetSingleFlag(Index));
		Capture->ShowFlags.SetSingleFlag(Index, Value.TrimStartAndEnd() != TEXT("0"));
	}
}

bool ALiminalPortal::ReleaseCaptureStateIfIdle(double NowSeconds, const FVector& CameraLocation, double IdleSeconds,
	double ReleaseDistance)
{
	if (!bHasCaptureState)
	{
		return false;
	}
	const double Distance = FVector::Dist(GetPlaneTransform().GetLocation(), CameraLocation);
	if (!LiminalPortalMath::ShouldReleaseCaptureState(NowSeconds - LastCaptureSeconds, Distance, IdleSeconds, ReleaseDistance))
	{
		return false;
	}
	// Con bCaptureEveryFrame = false y sin bAlwaysPersistRenderingState, GetViewState destruye el
	// FSceneViewState (y con él el FLumenSceneData de la vista). La siguiente captura lo recrea.
	Capture->bAlwaysPersistRenderingState = false;
	Capture->GetViewState(0);
	Capture->bAlwaysPersistRenderingState = true;
	bHasCaptureState = false;
	UE_LOG(LogLiminalPortals, Verbose, TEXT("%s: estado de captura liberado (%.0f s sin capturar, a %.0f m)"),
		*GetName(), NowSeconds - LastCaptureSeconds, Distance / 100.0);
	return true;
}
