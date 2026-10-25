#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "LiminalPlayerController.generated.h"

/** Controlador del jugador: usa ALiminalPlayerCameraManager y entrada solo de juego. */
UCLASS()
class LIMINALPORTALS_API ALiminalPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ALiminalPlayerController();

protected:
	virtual void BeginPlay() override;
};
