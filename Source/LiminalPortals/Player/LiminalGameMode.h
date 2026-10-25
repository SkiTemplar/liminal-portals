#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "LiminalGameMode.generated.h"

/** GameMode por defecto: personaje en primera persona y controlador con la cámara de portales. */
UCLASS()
class LIMINALPORTALS_API ALiminalGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ALiminalGameMode();
};
