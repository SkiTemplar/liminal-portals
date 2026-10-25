#include "Debug/LiminalDebugWorld.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Player/LiminalCharacter.h"
#include "Portal/LiminalPortalSubsystem.h"
#include "Portal/LiminalPortal.h"
#include "World/LiminalBiome.h"
#include "World/LiminalHub.h"

namespace
{
	constexpr double MatchToleranceCm = 1.0;

	ALiminalPortal* FindPortalAt(UWorld* World, const FVector& Location)
	{
		for (TActorIterator<ALiminalPortal> It(World); It; ++It)
		{
			if (FVector::Dist(It->GetActorLocation(), Location) <= MatchToleranceCm)
			{
				return *It;
			}
		}
		return nullptr;
	}

	FString FindBiomeLabel(UWorld* World, const ALiminalPortal* BiomePortal, int32 DoorIndex)
	{
		for (TActorIterator<ALiminalBiome> It(World); It; ++It)
		{
			if (FVector::Dist(It->GetReturnDoorTransform().GetLocation(), BiomePortal->GetActorLocation()) <= MatchToleranceCm)
			{
				return ALiminalBiome::GetBiomeTypeName(It->BiomeType);
			}
		}
		return FString::Printf(TEXT("Door%d"), DoorIndex);
	}
}

namespace LiminalDebugWorld
{
	bool FindDoorPairs(UWorld* World, TArray<FLiminalDoorPair>& OutPairs, ALiminalHub*& OutHub, FString& OutError)
	{
		OutPairs.Reset();
		OutHub = nullptr;
		if (!World)
		{
			OutError = TEXT("no hay mundo");
			return false;
		}

		TActorIterator<ALiminalHub> HubIt(World);
		if (!HubIt)
		{
			OutError = TEXT("el mapa no tiene ALiminalHub");
			return false;
		}
		ALiminalHub* Hub = *HubIt;

		TArray<FLiminalDoorPair> Pairs;
		for (int32 Index = 0; Index < Hub->GetNumDoors(); ++Index)
		{
			ALiminalPortal* HubPortal = FindPortalAt(World, Hub->GetDoorTransform(Index).GetLocation());
			if (!HubPortal)
			{
				OutError = FString::Printf(TEXT("no hay portal en la puerta %d del hub"), Index);
				return false;
			}
			ALiminalPortal* BiomePortal = HubPortal->LinkedPortal;
			if (!BiomePortal || BiomePortal->LinkedPortal != HubPortal || BiomePortal->bExitOnly || HubPortal->bExitOnly)
			{
				OutError = FString::Printf(TEXT("el portal %s de la puerta %d no tiene una pareja recíproca de entrada y salida"),
					*HubPortal->GetName(), Index);
				return false;
			}

			FLiminalDoorPair& Pair = Pairs.AddDefaulted_GetRef();
			Pair.DoorIndex = Index;
			Pair.HubPortal = HubPortal;
			Pair.BiomePortal = BiomePortal;
			Pair.Label = FindBiomeLabel(World, BiomePortal, Index);
		}

		OutPairs = MoveTemp(Pairs);
		OutHub = Hub;
		return true;
	}

	FVector GetEyeLocation(const ALiminalCharacter* Character, const FVector& FeetLocation)
	{
		const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		return FeetLocation + FVector(0.0, 0.0, HalfHeight + Character->BaseEyeHeight);
	}

	void PlacePlayer(APlayerController* Controller, ALiminalCharacter* Character, const FVector& FeetLocation,
		const FRotator& Rotation)
	{
		if (!Controller || !Character)
		{
			return;
		}
		// 2 cm de holgura sobre el suelo: el personaje cae y se apoya en el fotograma siguiente.
		const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const FVector Location = FeetLocation + FVector(0.0, 0.0, HalfHeight + 2.0);
		const FRotator Control(Rotation.Pitch, Rotation.Yaw, 0.0);
		Character->SetActorLocationAndRotation(Location, FRotator(0.0, Rotation.Yaw, 0.0), false, nullptr, ETeleportType::TeleportPhysics);
		Character->GetCharacterMovement()->StopMovementImmediately();
		Controller->SetControlRotation(Control);
		if (APlayerCameraManager* CameraManager = Controller->PlayerCameraManager)
		{
			CameraManager->SetGameCameraCutThisFrame();
		}
		if (ULiminalPortalSubsystem* Subsystem = Controller->GetWorld()->GetSubsystem<ULiminalPortalSubsystem>())
		{
			Subsystem->ResetCameraHistory();
		}
	}
}
