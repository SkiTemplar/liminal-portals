#include "Player/LiminalPlayerCameraManager.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Portal/LiminalPortalSubsystem.h"

void ALiminalPlayerCameraManager::UpdateCamera(float DeltaTime)
{
	Super::UpdateCamera(DeltaTime);
	if (bIsRecomputingCamera)
	{
		return;
	}

	APlayerController* PlayerController = GetOwningPlayerController();
	UWorld* World = GetWorld();
	if (!PlayerController || !PlayerController->IsLocalController() || !World)
	{
		return;
	}

	if (ULiminalPortalSubsystem* Subsystem = World->GetSubsystem<ULiminalPortalSubsystem>())
	{
		Subsystem->UpdatePortals(PlayerController, GetCameraCacheView(),
			[this]() { return RecomputeAfterTeleport(); });
	}
}

FMinimalViewInfo ALiminalPlayerCameraManager::RecomputeAfterTeleport()
{
	TGuardValue<bool> Guard(bIsRecomputingCamera, true);
	if (bCameraCutOnTeleport)
	{
		SetGameCameraCutThisFrame();
	}
	// DeltaTime 0: la cámara se recoloca con el pawn ya movido sin avanzar interpolaciones ni sacudidas.
	Super::UpdateCamera(0.f);
	return GetCameraCacheView();
}
