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

	/** Comprueba la etiqueta de salida de Liminal.Calibrate (vacía = válida). */
	LIMINALPORTALS_API bool IsValidOutputTag(const FString& Tag);
}
