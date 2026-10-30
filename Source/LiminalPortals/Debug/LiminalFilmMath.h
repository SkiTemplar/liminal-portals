#pragma once

#include "CoreMinimal.h"

/**
 * Matemática pura de Liminal.Film: planos de cámara definidos por claves en el tiempo, su
 * interpolación y el paso de coordenadas locales de puerta a mundo. Sin UWorld ni actores.
 *
 * Coordenadas de puerta «desplegadas»: el sistema local del portal de la puerta en el hub (X hacia
 * fuera de la cara visible, es decir, hacia el hub; origen a ras de suelo en el centro del hueco).
 * Los puntos con X < 0 están al otro lado de la puerta, en el bioma: el paso del portal los lleva a
 * (−X, −Y, Z) en el sistema del portal de vuelta. La cámara se sigue en este espacio continuo y el
 * paso real lo hace ULiminalPortalSubsystem al cruzar el plano.
 */
namespace LiminalFilmMath
{
	/** Pose de cámara (posición de los ojos y orientación sin roll). */
	struct FFilmPose
	{
		FVector Location = FVector::ZeroVector;
		double Yaw = 0.0;
		double Pitch = 0.0;
	};

	/** Clave de un plano: pose en el instante Time (s desde el inicio del plano). */
	struct FFilmKey
	{
		double Time = 0.0;
		FFilmPose Pose;
	};

	enum class EFilmAnchor : uint8
	{
		/** Claves en el sistema del hub (sin cruces). */
		Hub,
		/** Claves en coordenadas desplegadas de la puerta DoorIndex. */
		Door,
	};

	struct FFilmShot
	{
		FString Name;
		EFilmAnchor Anchor = EFilmAnchor::Hub;
		int32 DoorIndex = INDEX_NONE;
		TArray<FFilmKey> Keys;

		double GetDuration() const { return Keys.IsEmpty() ? 0.0 : Keys.Last().Time; }
	};

	/**
	 * Comprueba un plano: nombre de letras, dígitos y '_', al menos dos claves, la primera en t = 0 y
	 * tiempos estrictamente crecientes, puerta en [0, NumDoors) si el ancla es una puerta.
	 */
	LIMINALPORTALS_API bool IsValidShot(const FFilmShot& Shot, int32 NumDoors, FString* OutError = nullptr);

	/**
	 * Pose del plano en Time con Hermite cúbico por tramos: tangentes de Catmull-Rom en las claves
	 * interiores y nulas en la primera y la última (arranque y parada suaves). Fuera de [0, duración]
	 * devuelve la clave del extremo. Yaw y Pitch se interpolan sin envolver (las claves van seguidas).
	 */
	LIMINALPORTALS_API FFilmPose EvaluateShot(TConstArrayView<FFilmKey> Keys, double Time);

	/** Pose local de Frame (pose de un plano) a mundo; el yaw local se suma al del sistema. */
	LIMINALPORTALS_API FFilmPose ToWorld(const FTransform& Frame, const FFilmPose& Local);

	/**
	 * Pasos por el hueco de la puerta del recorrido muestreado a SampleRate Hz: cambios de signo de X
	 * entre muestras seguidas cuyo punto de corte cae dentro del hueco (|Y| < HalfExtents.X y
	 * 0 < Z < 2·HalfExtents.Y). Rodear la pared por fuera del hueco no cuenta.
	 */
	LIMINALPORTALS_API int32 CountDoorCrossings(TConstArrayView<FFilmKey> Keys, double SampleRate, const FVector2D& HalfExtents);

	/** Fotogramas de simulación del plano a SampleRate Hz, incluidos el primero y el último. */
	LIMINALPORTALS_API int32 GetShotFrameCount(const FFilmShot& Shot, double SampleRate);

	/** Planos del vídeo del proyecto: recorridos por el hub y los cuatro biomas y tres acercamientos lentos a las puertas. */
	LIMINALPORTALS_API TArray<FFilmShot> MakeDefaultFilmShots();
}
