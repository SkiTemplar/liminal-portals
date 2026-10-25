#include "Player/LiminalGameMode.h"

#include "Player/LiminalCharacter.h"
#include "Player/LiminalPlayerController.h"

ALiminalGameMode::ALiminalGameMode()
{
	DefaultPawnClass = ALiminalCharacter::StaticClass();
	PlayerControllerClass = ALiminalPlayerController::StaticClass();
}
