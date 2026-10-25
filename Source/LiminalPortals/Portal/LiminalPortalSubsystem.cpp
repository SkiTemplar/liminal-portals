#include "Portal/LiminalPortalSubsystem.h"

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

	constexpr int32 MaxLagFrames = 4;
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
	PushViewHistory(FinalView);
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
		if (!Portal || !Portal->LinkedPortal)
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
		Character->TeleportThroughPortal(Portal->GetTransferToLinked());
		++TeleportCount;
		UE_LOG(LogLiminalPortals, Verbose, TEXT("Cruce %s -> %s"), *Portal->GetName(), *Portal->LinkedPortal->GetName());
		OnPortalTeleport.Broadcast(Portal, Portal->LinkedPortal);
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
	const FIntPoint TargetSize = LiminalPortalMath::ComputeRenderTargetSize(FIntPoint(ViewportX, ViewportY),
		RequestedAspect, Settings->RenderTargetScale, Settings->MaxRenderTargetDimension);
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

		if (!Portal->LinkedPortal)
		{
			continue;
		}
		const bool bVisible = LiminalPortalMath::IsPortalPotentiallyVisible(Portal->GetPlaneTransform(),
			Portal->GetHalfExtents(), View.Location, CameraForward, View.FOV, ViewAspect);
		if (!bVisible)
		{
			continue;
		}

		// El reserva del destino taparía la vista desde su propio plano.
		HiddenComponents.SetNum(NumSurfaces);
		HiddenComponents.Add(Portal->LinkedPortal->GetFallbackComponent());
		if (Portal->CaptureView(CaptureSourceView, TargetSize, HiddenComponents))
		{
			++LastCaptureCount;
		}
	}
}
