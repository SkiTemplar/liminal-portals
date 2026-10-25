#pragma once

#include "CoreMinimal.h"

/**
 * Comprobaciones puras de Liminal.SelfTest (etapa E3). Son independientes de
 * LiminalPortalMath::MakeTransfer: el paso se recalcula con coordenadas locales (lo que sale por
 * delante de A en (x, y, z) entra por B en (−x, −y, z)), de modo que un error en el paso no se
 * valida a sí mismo.
 */
namespace LiminalSelfTestMath
{
	struct FCrossingTolerances
	{
		/** Error relativo máximo del módulo de la velocidad (0,01 = 1 %). */
		double SpeedRatio = 0.01;
		double DirectionDegrees = 0.5;
		double PositionCm = 1.0;
		/** Por debajo de esta velocidad no se puede medir la dirección y el cruce no cuenta. */
		double MinSpeed = 10.0;
	};

	struct FCrossingCheck
	{
		double SpeedBefore = 0.0;
		double SpeedAfter = 0.0;
		/** |v₁ − v₀| / v₀ en módulo. */
		double SpeedErrorRatio = 0.0;
		/** Ángulo entre la velocidad de salida y la esperada (la de entrada girada con el portal). */
		double DirectionErrorDegrees = 0.0;
		/** Distancia entre la posición de salida y la esperada. */
		double PositionErrorCm = 0.0;
		bool bMoving = false;
		bool bSpeedOk = false;
		bool bDirectionOk = false;
		bool bPositionOk = false;

		bool Passed() const { return bMoving && bSpeedOk && bDirectionOk && bPositionOk; }
	};

	/** Punto que corresponde, al otro lado de Target, a Point visto desde Source. */
	LIMINALPORTALS_API FVector MirrorPointThroughPortals(const FTransform& Source, const FTransform& Target, const FVector& Point);

	/** Dirección o velocidad equivalente al otro lado (solo giro: el módulo se conserva). */
	LIMINALPORTALS_API FVector MirrorVectorThroughPortals(const FTransform& Source, const FTransform& Target, const FVector& Vector);

	/**
	 * Compara el estado del pawn antes y después de un paso Source → Target (transforms de origen de
	 * portal, sin escala) con lo que exige la convención: módulo de la velocidad conservado, dirección
	 * girada con el portal y posición llevada al otro lado.
	 */
	LIMINALPORTALS_API FCrossingCheck CheckCrossing(const FTransform& Source, const FTransform& Target,
		const FVector& LocationBefore, const FVector& LocationAfter, const FVector& VelocityBefore,
		const FVector& VelocityAfter, const FCrossingTolerances& Tolerances = FCrossingTolerances());

	/**
	 * La cámara está detrás del plano del portal (entre el plano y Depth cm por detrás) y dentro del
	 * rectángulo del hueco ampliado con el marco: es decir, en el túnel del marco o al otro lado de la
	 * placa trasera, donde no debe acabar nunca un fotograma.
	 * @param PortalPlane Transform del centro del rectángulo (ALiminalPortal::GetPlaneTransform).
	 * @param HalfExtents Medio ancho (X) y medio alto (Y) del hueco.
	 */
	LIMINALPORTALS_API bool IsInsidePortalVolume(const FTransform& PortalPlane, const FVector2D& HalfExtents,
		double FrameThickness, double Depth, const FVector& Point);
}
