#include "Portal/LiminalPortalSubsystem.h"

#include "ContentStreaming.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "LiminalPortals.h"
#include "LiminalPortalsSettings.h"
#include "Player/LiminalCharacter.h"
#include "Portal/LiminalPortal.h"
#include "Portal/LiminalPortalMath.h"

namespace
{
	TAutoConsoleVariable<int32> CVarPortalsEnable(
		TEXT("Liminal.Portals.Enable"),
		1,
		TEXT("Activa los portales: cruces, teletransporte y capturas (0 = apagados)."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarPortalLagFrames(
		TEXT("Liminal.Portal.LagFrames"),
		0,
		TEXT("Control negativo de calibración: captura con la pose de cámara de N fotogramas atrás (0 = sin retraso, máximo 4)."),
		ECVF_Default);

	TAutoConsoleVariable<float> CVarDebugVelocityScale(
		TEXT("Liminal.Portal.DebugVelocityScale"),
		1.f,
		TEXT("Control negativo de Liminal.SelfTest: escala la velocidad del pawn tras cada paso (1 = sin cambios)."),
		ECVF_Default);

	TAutoConsoleVariable<float> CVarCaptureScale(
		TEXT("Liminal.Portal.CaptureScale"),
		0.f,
		TEXT("Experimento de coste (E5, D-30): escala del render target de las capturas respecto al viewport ")
		TEXT("(0 = la de ULiminalPortalsSettings::RenderTargetScale; si no, entre 0,1 y 2)."),
		ECVF_Default);

	TAutoConsoleVariable<float> CVarReleaseIdleSeconds(
		TEXT("Liminal.Portal.ReleaseIdleSeconds"),
		10.f,
		TEXT("Memoria (D-31): segundos sin capturar tras los que un portal lejano libera su estado de render ")
		TEXT("y su escena de Lumen propia (0 = no liberar nunca)."),
		ECVF_Default);

	TAutoConsoleVariable<float> CVarReleaseDistance(
		TEXT("Liminal.Portal.ReleaseDistance"),
		10000.f,
		TEXT("Memoria (D-31): distancia mínima en cm de la cámara al portal para liberar su estado de render ")
		TEXT("(100 m: más que el hub, mucho menos que los 20 km entre mundos)."),
		ECVF_Default);

	TAutoConsoleVariable<float> CVarPrewarmDistance(
		TEXT("Liminal.Portal.PrewarmDistance"),
		400.f,
		TEXT("Tirón al cruzar (D-37): distancia en cm de la cámara al centro de una puerta, por delante de ella, a la ")
		TEXT("que la escena de Lumen de la vista principal empieza a preparar el destino (0 = sin precalentar)."),
		ECVF_Default);

	constexpr int32 MaxLagFrames = 4;
	/** Distancia por delante del portal de destino del origen de precalentamiento (cámara tras cruzar andando). */
	constexpr double PrewarmExitDepth = 150.0;
	/**
	 * Vida del origen en el streaming. Con duración > 0 entra en LastingViewInfos, que el streaming pone antes
	 * que la vista del jugador: Lumen solo admite LUMEN_MAX_VIEWS = 2 orígenes (la vista y uno más).
	 */
	constexpr float PrewarmOriginSeconds = 0.5f;
}

void ULiminalPortalSubsystem::RegisterPortal(ALiminalPortal* Portal)
{
	if (IsValid(Portal))
	{
		Portals.AddUnique(Portal);
	}
}

void ULiminalPortalSubsystem::UnregisterPortal(ALiminalPortal* Portal)
{
	Portals.RemoveAll([Portal](const TWeakObjectPtr<ALiminalPortal>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == Portal;
	});
}

void ULiminalPortalSubsystem::ResetCameraHistory()
{
	PreviousCameraLocation.Reset();
	ViewHistory.Reset();
}

bool ULiminalPortalSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void ULiminalPortalSubsystem::UpdatePortals(APlayerController* PlayerController, const FMinimalViewInfo& View,
	TFunctionRef<FMinimalViewInfo()> RecomputeView)
{
	LastCaptureCount = 0;
	if (!PlayerController)
	{
		return;
	}
	if (CVarPortalsEnable.GetValueOnGameThread() == 0)
	{
		ResetCameraHistory();
		return;
	}

	Portals.RemoveAll([](const TWeakObjectPtr<ALiminalPortal>& Entry) { return !Entry.IsValid(); });

	FMinimalViewInfo FinalView = View;
	if (ProcessCrossings(PlayerController, View.Location))
	{
		FinalView = RecomputeView();
		// La pose anterior pertenece al otro lado del portal: no sirve para el control con retraso.
		ViewHistory.Reset();
	}
	PreviousCameraLocation = FinalView.Location;

	CaptureVisiblePortals(PlayerController, FinalView);
	ReleaseIdleCaptureStates(FinalView.Location);
	PrewarmNearestDestination(FinalView.Location);
	PushViewHistory(FinalView);
	OnPortalsUpdated.Broadcast(FinalView);
}

bool ULiminalPortalSubsystem::ProcessCrossings(APlayerController* PlayerController, const FVector& CameraLocation)
{
	if (!PreviousCameraLocation.IsSet())
	{
		return false;
	}

	ALiminalCharacter* Character = Cast<ALiminalCharacter>(PlayerController->GetPawn());
	if (!Character)
	{
		return false;
	}

	for (const TWeakObjectPtr<ALiminalPortal>& Entry : Portals)
	{
		ALiminalPortal* Portal = Entry.Get();
		if (!Portal || !Portal->LinkedPortal || Portal->bExitOnly)
		{
			continue;
		}

		const bool bCrossed = LiminalPortalMath::DetectCrossing(Portal->GetPlaneTransform(), Portal->GetHalfExtents(),
			PreviousCameraLocation.GetValue(), CameraLocation);
		if (!bCrossed)
		{
			continue;
		}

		// Un solo cruce por fotograma: tras el paso la cámara queda delante del destino.
		FLiminalPortalTeleportEvent Event;
		Event.Source = Portal;
		Event.Target = Portal->LinkedPortal;
		Event.Transfer = Portal->GetTransferToLinked();
		Event.LocationBefore = Character->GetActorLocation();
		Event.VelocityBefore = Character->GetCharacterMovement()->Velocity;
		Event.CameraBefore = CameraLocation;
		Character->TeleportThroughPortal(Event.Transfer);
		const float DebugVelocityScale = CVarDebugVelocityScale.GetValueOnGameThread();
		if (DebugVelocityScale != 1.f)
		{
			Character->GetCharacterMovement()->Velocity *= DebugVelocityScale;
		}
		Event.LocationAfter = Character->GetActorLocation();
		Event.VelocityAfter = Character->GetCharacterMovement()->Velocity;
		++TeleportCount;
		UE_LOG(LogLiminalPortals, Verbose, TEXT("Cruce %s -> %s"), *Portal->GetName(), *Portal->LinkedPortal->GetName());
		OnPortalTeleport.Broadcast(Event);
		return true;
	}
	return false;
}

const FMinimalViewInfo& ULiminalPortalSubsystem::SelectCaptureView(const FMinimalViewInfo& View)
{
	const int32 LagFrames = FMath::Clamp(CVarPortalLagFrames.GetValueOnGameThread(), 0, MaxLagFrames);
	if (LagFrames == 0 || ViewHistory.Num() < LagFrames)
	{
		return View;
	}
	return ViewHistory[LagFrames - 1];
}

void ULiminalPortalSubsystem::PushViewHistory(const FMinimalViewInfo& View)
{
	ViewHistory.Insert(View, 0);
	if (ViewHistory.Num() > MaxLagFrames)
	{
		ViewHistory.SetNum(MaxLagFrames);
	}
}

void ULiminalPortalSubsystem::CaptureVisiblePortals(APlayerController* PlayerController, const FMinimalViewInfo& View)
{
	int32 ViewportX = 0;
	int32 ViewportY = 0;
	PlayerController->GetViewportSize(ViewportX, ViewportY);

	const ULiminalPortalsSettings* Settings = GetDefault<ULiminalPortalsSettings>();
	const float RequestedAspect = View.bConstrainAspectRatio ? View.AspectRatio : 0.f;
	const float ScaleOverride = CVarCaptureScale.GetValueOnGameThread();
	const float CaptureScale = ScaleOverride > 0.f ? FMath::Clamp(ScaleOverride, 0.1f, 2.f) : Settings->RenderTargetScale;
	const FIntPoint TargetSize = LiminalPortalMath::ComputeRenderTargetSize(FIntPoint(ViewportX, ViewportY),
		RequestedAspect, CaptureScale, Settings->MaxRenderTargetDimension);
	if (TargetSize.X <= 0 || TargetSize.Y <= 0)
	{
		return;
	}
	const float ViewAspect = static_cast<float>(TargetSize.X) / static_cast<float>(TargetSize.Y);

	// Recursión cortada (decisión 6): ninguna captura ve superficies de portal.
	TArray<UPrimitiveComponent*, TInlineAllocator<16>> HiddenComponents;
	for (const TWeakObjectPtr<ALiminalPortal>& Entry : Portals)
	{
		if (ALiminalPortal* Portal = Entry.Get())
		{
			HiddenComponents.Add(Portal->GetSurfaceComponent());
		}
	}
	const int32 NumSurfaces = HiddenComponents.Num();

	const FMinimalViewInfo& CaptureSourceView = SelectCaptureView(View);
	const FVector CameraForward = View.Rotation.Vector();

	for (const TWeakObjectPtr<ALiminalPortal>& Entry : Portals)
	{
		ALiminalPortal* Portal = Entry.Get();
		if (!Portal)
		{
			continue;
		}
		Portal->UpdateSurfaceDepth(View.Location);

		if (!Portal->LinkedPortal || Portal->bExitOnly)
		{
			continue;
		}
		const bool bVisible = LiminalPortalMath::IsPortalPotentiallyVisible(Portal->GetPlaneTransform(),
			Portal->GetHalfExtents(), View.Location, CameraForward, View.FOV, ViewAspect);
		if (!bVisible)
		{
			continue;
		}

		// El reserva del destino taparía la vista desde su propio plano. El del origen solo entra en
		// su propia captura si el origen es visible desde el destino (en calibración coinciden).
		HiddenComponents.SetNum(NumSurfaces);
		HiddenComponents.Add(Portal->LinkedPortal->GetFallbackComponent());
		HiddenComponents.Add(Portal->GetFallbackComponent());
		if (Portal->CaptureView(CaptureSourceView, TargetSize, HiddenComponents))
		{
			++LastCaptureCount;
			LastCaptureSize = TargetSize;
		}
	}
}

void ULiminalPortalSubsystem::ReleaseIdleCaptureStates(const FVector& CameraLocation)
{
	const double IdleSeconds = CVarReleaseIdleSeconds.GetValueOnGameThread();
	if (IdleSeconds <= 0.0)
	{
		return;
	}
	const double ReleaseDistance = CVarReleaseDistance.GetValueOnGameThread();
	const double Now = GetWorld()->GetRealTimeSeconds();
	for (const TWeakObjectPtr<ALiminalPortal>& Entry : Portals)
	{
		if (ALiminalPortal* Portal = Entry.Get())
		{
			Portal->ReleaseCaptureStateIfIdle(Now, CameraLocation, IdleSeconds, ReleaseDistance);
		}
	}
}

void ULiminalPortalSubsystem::PrewarmNearestDestination(const FVector& CameraLocation)
{
	LastPrewarmPortal.Reset();
	const double PrewarmDistance = CVarPrewarmDistance.GetValueOnGameThread();
	const ALiminalPortal* Nearest = nullptr;
	double NearestDistance = TNumericLimits<double>::Max();
	for (const TWeakObjectPtr<ALiminalPortal>& Entry : Portals)
	{
		const ALiminalPortal* Portal = Entry.Get();
		if (!Portal || !Portal->LinkedPortal || Portal->bExitOnly)
		{
			continue;
		}
		const FTransform Plane = Portal->GetPlaneTransform();
		const double Distance = FVector::Dist(CameraLocation, Plane.GetLocation());
		const double SignedDistance = LiminalPortalMath::SignedDistanceToPlane(Plane, CameraLocation);
		if (LiminalPortalMath::ShouldPrewarmDestination(SignedDistance, Distance, PrewarmDistance) && Distance < NearestDistance)
		{
			Nearest = Portal;
			NearestDistance = Distance;
		}
	}
	if (!Nearest)
	{
		return;
	}

	// La escena de Lumen de la vista principal prioriza las tarjetas de la caché de superficies por la distancia
	// al origen más cercano, incluidos los orígenes de streaming (AddLumenStreamingViewOrigins): con el destino
	// como segundo origen, las tarjetas del bioma se capturan durante el acercamiento y no todas al cruzar.
	const FVector Origin = LiminalPortalMath::ComputePrewarmOrigin(Nearest->LinkedPortal->GetPlaneTransform(), PrewarmExitDepth);
	IStreamingManager::Get().AddViewLocation(Origin, 1.f, false, PrewarmOriginSeconds);
	LastPrewarmPortal = Nearest;
}
