#pragma once

#include "CoreMinimal.h"

class ALiminalCharacter;
class ALiminalHub;
class ALiminalPortal;
class APlayerController;
class UWorld;

/** Puerta del hub con su portal y el portal de vuelta del bioma al que lleva. */
struct FLiminalDoorPair
{
	int32 DoorIndex = INDEX_NONE;
	TWeakObjectPtr<ALiminalPortal> HubPortal;
	TWeakObjectPtr<ALiminalPortal> BiomePortal;
	/** Tipo del bioma de destino (p. ej. «Jungle») o «DoorN» si no se encuentra el bioma. */
	FString Label;
};

namespace LiminalDebugWorld
{
	/**
	 * Busca el hub del mundo y, para cada una de sus puertas, el portal colocado en ella (a menos de
	 * 1 cm de GetDoorTransform) y su pareja; exige que el enlace sea recíproco.
	 * @return false (con el motivo en OutError) si falta el hub, un portal o un enlace.
	 */
	bool FindDoorPairs(UWorld* World, TArray<FLiminalDoorPair>& OutPairs, ALiminalHub*& OutHub, FString& OutError);

	/**
	 * Coloca al jugador de pie en FeetLocation mirando con Rotation (sin roll), sin velocidad, y
	 * vacía el historial de cámara del subsistema para que el salto no cuente como cruce. Es una
	 * colocación de preparación de las pruebas, no un paso por portal.
	 */
	void PlacePlayer(APlayerController* Controller, ALiminalCharacter* Character, const FVector& FeetLocation,
		const FRotator& Rotation);

	/** Ojos del jugador de pie con los pies en FeetLocation (cápsula + BaseEyeHeight). */
	FVector GetEyeLocation(const ALiminalCharacter* Character, const FVector& FeetLocation);
}
