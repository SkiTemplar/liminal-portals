#pragma once

#include "CoreMinimal.h"

class UWorld;

/**
 * Órdenes de consola de depuración y verificación (Liminal.*). Las órdenes se registran solas al
 * cargar el módulo; estas funciones permiten lanzarlas desde código.
 */
namespace LiminalDebugCommands
{
	/**
	 * Liminal.Calibrate [Etiqueta]: salida en Saved/Calibration[/Etiqueta]. La etiqueta solo admite
	 * letras, dígitos, '_' y '-'.
	 * @return false (con el motivo en OutError) si no se pudo empezar o ya hay una en curso.
	 */
	LIMINALPORTALS_API bool StartCalibration(UWorld* World, const FString& Tag, FString& OutError);

	/**
	 * Liminal.SelfTest [Ciclos]: cruces hub → bioma → hub por cada puerta; informe en Saved/SelfTest.json.
	 * @return false (con el motivo en OutError) si no se pudo empezar o ya hay una en curso.
	 */
	LIMINALPORTALS_API bool StartSelfTest(UWorld* World, int32 CyclesPerDoor, FString& OutError);

	/**
	 * Liminal.Bench [Medidos] [Calentamiento] [profile]: tiempos de GPU y de CPU por caso en Saved/Bench.json;
	 * con bProfile (D-36), además el desglose de GPU por pase y el recorrido del hub en Saved/BenchProfile.json.
	 * @return false (con el motivo en OutError) si los fotogramas están fuera de rango (medidos >= 120,
	 *         calentamiento >= 60), no hay mundo de juego o ya hay una medida en curso.
	 */
	LIMINALPORTALS_API bool StartBench(UWorld* World, int32 MeasureFrames, int32 WarmupFrames, bool bProfile, FString& OutError);

	/** Liminal.Tour: capturas del hub y de cada puerta en Saved/Tour/. */
	LIMINALPORTALS_API bool StartTour(UWorld* World, FString& OutError);

	/** Comprueba la etiqueta de salida de Liminal.Calibrate (vacía = válida). */
	LIMINALPORTALS_API bool IsValidOutputTag(const FString& Tag);
}
