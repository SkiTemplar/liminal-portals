#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

#include <initializer_list>

/**
 * Constantes y utilidades comunes a las ejecuciones de depuración (Calibrate, SelfTest, Tour).
 * Viven aquí, en un namespace con nombre, porque el unity build junta todos los .cpp del módulo en
 * una sola unidad de traducción: dos namespaces anónimos con el mismo símbolo chocan (C2374/C2084).
 */
namespace LiminalDebugShared
{
	/** Ticks del motor sin UpdatePortals tras los que se aborta (el subsistema no corre). */
	inline constexpr int32 MaxTicksWithoutUpdate = 600;

	/** Array JSON de números (vectores, rotaciones y tamaños de los informes). */
	inline TArray<TSharedPtr<FJsonValue>> ToJsonArray(std::initializer_list<double> Values)
	{
		TArray<TSharedPtr<FJsonValue>> Array;
		Array.Reserve(static_cast<int32>(Values.size()));
		for (const double Value : Values)
		{
			Array.Add(MakeShared<FJsonValueNumber>(Value));
		}
		return Array;
	}
}
