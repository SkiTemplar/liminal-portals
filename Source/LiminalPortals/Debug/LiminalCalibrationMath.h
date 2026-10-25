#pragma once

#include "CoreMinimal.h"

/**
 * Funciones puras de la calibración del portal (Liminal.Calibrate, etapa E2): secuencia de giros y
 * proyección de puntos a píxeles del viewport. Sin UWorld ni actores, para poder testearlas.
 */
namespace LiminalCalibrationMath
{
	/**
	 * Barrido de yaw en onda triangular que empieza en 0 y sube: 0, s, 2s, …, A, …, 0, …, -A, …
	 * Cada fotograma difiere del anterior exactamente StepDegrees. La amplitud se redondea hacia
	 * abajo a un múltiplo del paso.
	 * @return Desplazamientos de yaw en grados; vacío si algún parámetro no es válido
	 *         (paso <= 0, amplitud < paso o NumFrames <= 0).
	 */
	LIMINALPORTALS_API TArray<double> MakeYawSweep(double StepDegrees, double AmplitudeDegrees, int32 NumFrames);

	/**
	 * Proyecta un punto de mundo a píxeles del viewport con la proyección en perspectiva de UE
	 * (FOV horizontal fijo, AspectRatio_MaintainXFOV, sin restricción de aspecto). El origen del
	 * píxel es la esquina superior izquierda; X crece a la derecha e Y hacia abajo.
	 * @return false si el punto no está delante de la cámara o la vista no es válida.
	 */
	LIMINALPORTALS_API bool ProjectToViewport(const FVector& ViewLocation, const FRotator& ViewRotation,
		float HorizontalFOVDegrees, const FIntPoint& ViewportSize, const FVector& WorldPoint, FVector2D& OutPixel);

	/**
	 * Esquinas del rectángulo del portal en espacio de mundo, recorridas en bucle (polígono simple):
	 * locales (-Y, -Z), (+Y, -Z), (+Y, +Z), (-Y, +Z).
	 * @param PlaneTransform Centro del rectángulo; X = normal, Y = ancho, Z = alto.
	 * @param HalfExtents X = medio ancho, Y = medio alto.
	 */
	LIMINALPORTALS_API TArray<FVector, TFixedAllocator<4>> GetPortalCorners(const FTransform& PlaneTransform,
		const FVector2D& HalfExtents);
}
