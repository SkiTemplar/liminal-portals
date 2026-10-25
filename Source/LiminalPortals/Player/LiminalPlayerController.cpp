#include "Player/LiminalPlayerController.h"

#include "Player/LiminalPlayerCameraManager.h"

ALiminalPlayerController::ALiminalPlayerController()
{
	PlayerCameraManagerClass = ALiminalPlayerCameraManager::StaticClass();
}

void ALiminalPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (IsLocalController())
	{
		SetInputMode(FInputModeGameOnly());
		SetShowMouseCursor(false);
	}
}
