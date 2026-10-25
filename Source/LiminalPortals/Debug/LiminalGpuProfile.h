#pragma once

#include "CoreMinimal.h"

/**
 * Desglose de GPU por pase para Liminal.Bench en modo perfil (D-36).
 *
 * Un sumidero del perfilador de GPU de UE 5.6 (UE::RHI::GPUProfiler::FEventSink, el mismo flujo de
 * eventos que alimenta ProfileGPU y stat gpu) recibe las migas (breadcrumbs) de RDG con sus marcas de
 * tiempo de GPU y acumula, por cola y por ruta de migas, el tiempo ocupado de la GPU. La ruta separa la
 * vista principal ("…/Scene/…") de cada captura ("…/LiminalCapture <portal>/…/Scene/…").
 *
 * Solo graba entre BeginRecording y EndRecording; fuera de esa ventana mantiene la pila de migas y no
 * acumula nada. Sin el perfilador nuevo o sin migas (Shipping), IsAvailable devuelve false.
 */
namespace LiminalGpuProfile
{
	/** Tiempo acumulado de una ruta de migas durante la grabación. */
	struct FPathTime
	{
		/** Cola y migas separadas por '/', p. ej. "Graphics0/Scene/Lumen/…". */
		FString Path;
		/** Tiempo ocupado de la GPU dentro de la miga y sus hijas, sumado sobre todos los fotogramas. */
		double InclusiveMs = 0.0;
		/** Veces que se cerró la miga durante la grabación. */
		int32 Count = 0;
	};

	struct FSnapshot
	{
		/** Fronteras de fotograma de la cola gráfica vistas durante la grabación. */
		int32 Frames = 0;
		/** Tiempo ocupado total de cada cola ("Graphics0", "Compute0"…), sumado sobre los fotogramas. */
		TMap<FString, double> QueueBusyMs;
		TArray<FPathTime> Paths;
	};

	/** Profundidad máxima de migas que se distinguen; lo más profundo se suma a su antecesor de ese nivel. */
	constexpr int32 MaxDepth = 12;

	/**
	 * Nombre de miga para la ruta: "Frame <n>" (la raíz de cada fotograma) pasa a "Frame" para que los
	 * fotogramas se sumen en la misma ruta, y '/' a '|' porque separa los niveles. El resto no cambia.
	 */
	LIMINALPORTALS_API FString NormalizeBreadcrumbName(const FString& Name);

	LIMINALPORTALS_API bool IsAvailable();

	/** Vacía lo acumulado y empieza a grabar. */
	LIMINALPORTALS_API void BeginRecording();

	/** Deja de grabar y devuelve lo acumulado desde BeginRecording. */
	LIMINALPORTALS_API FSnapshot EndRecording();
}
