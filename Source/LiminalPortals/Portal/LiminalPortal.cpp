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

		Settings.bOverride_DynamicGlobalIlluminationMethod = true;
		Settings.DynamicGlobalIlluminationMethod = static_cast<EDynamicGlobalIlluminationMethod::Type>(
			GIMethod ? GIMethod->GetValueOnGameThread() : EDynamicGlobalIlluminationMethod::Lumen);
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
	return true;
}
