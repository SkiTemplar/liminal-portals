#pragma once

#include "CoreMinimal.h"

/**
 * Matemática pura de los portales: sin UWorld ni actores, para poder testearla aislada.
 *
 * Convención (decisión 1 de docs/PLAN-NOCHE.md): el eje X local del portal apunta hacia fuera de
 * la cara visible; Y es el ancho y Z el alto. Entrar por la cara frontal de A sale por la cara
 * frontal de B, de modo que el paso es Transfer = Target · Yaw180 · Source⁻¹ (en notación
 * matemática, aplicada de derecha a izquierda). Los transforms de portal se tratan como rígidos:
 * la escala se ignora.
 */
namespace LiminalPortalMath
{
	/** Plano de recorte de una captura: se conserva el semiespacio hacia el que apunta Normal. */
	struct FClipPlane
	{
		FVector Base = FVector::ZeroVector;
		FVector Normal = FVector::ForwardVector;
	};

	/** Transformación rígida que lleva el espacio de delante de Source al de detrás de Target. */
	LIMINALPORTALS_API FTransform MakeTransfer(const FTransform& Source, const FTransform& Target);

	/** Aplica el paso a un punto en espacio de mundo. */
	LIMINALPORTALS_API FVector TransferPoint(const FTransform& Transfer, const FVector& Point);

	/** Aplica el paso a una dirección o velocidad (solo rotación: el módulo se conserva). */
	LIMINALPORTALS_API FVector TransferVector(const FTransform& Transfer, const FVector& Vector);

	/** Aplica el paso a una orientación en espacio de mundo. */
	LIMINALPORTALS_API FQuat TransferRotation(const FTransform& Transfer, const FQuat& Rotation);

	/** Aplica el paso a un transform completo (posición y orientación; la escala se conserva). */
	LIMINALPORTALS_API FTransform TransferTransform(const FTransform& Transfer, const FTransform& Transform);

	/**
	 * Rotación de control tras el paso: conserva la dirección de la mirada (yaw y pitch) y fuerza
	 * el roll a 0 para que el jugador no quede ladeado si los portales no son verticales.
	 */
	LIMINALPORTALS_API FRotator TransferControlRotation(const FTransform& Transfer, const FRotator& ControlRotation);

	/** Distancia con signo de Point al plano del portal (positiva por delante). */
	LIMINALPORTALS_API double SignedDistanceToPlane(const FTransform& Portal, const FVector& Point);

	/**
	 * Cruce del segmento PreviousPoint → CurrentPoint por el rectángulo del portal.
	 * Solo cuenta de la cara frontal a la trasera (decisión 2) y con la intersección dentro de
	 * HalfExtents (X = medio ancho en Y local, Y = medio alto en Z local). Un salto más largo que
	 * MaxStep no cuenta (teletransportes ajenos, aparición del pawn).
	 * @param OutHitPoint Si no es nulo y hay cruce, recibe el punto de intersección.
	 */
	LIMINALPORTALS_API bool DetectCrossing(const FTransform& Portal, const FVector2D& HalfExtents,
		const FVector& PreviousPoint, const FVector& CurrentPoint, double MaxStep = 1000.0,
		FVector* OutHitPoint = nullptr);

	/**
	 * Plano de recorte de la captura en el portal de destino: normal hacia su cara frontal y base
	 * desplazada Offset a lo largo de esa normal (negativo = detrás del plano).
	 */
	LIMINALPORTALS_API FClipPlane ComputeClipPlane(const FTransform& TargetPortal, double Offset = 0.0);

	/**
	 * Visibilidad aproximada y conservadora: la cámara está delante del portal y la esfera que
	 * envuelve el rectángulo corta el cono que envuelve el frustum. Con FOV o aspecto inválidos
	 * devuelve true (mejor capturar de más que dejar el portal congelado).
	 * @param HorizontalFOVDegrees FOV horizontal en grados, como FMinimalViewInfo::FOV.
	 * @param AspectRatio Ancho / alto de la vista.
	 */
	LIMINALPORTALS_API bool IsPortalPotentiallyVisible(const FTransform& Portal, const FVector2D& HalfExtents,
		const FVector& CameraLocation, const FVector& CameraForward, float HorizontalFOVDegrees, float AspectRatio);

	/**
	 * Profundidad de la caja de la superficie detrás del plano (decisión 7): ExtendedDepth si la
	 * cámara está por delante a menos de NearThicknessDistance; BaseDepth en cualquier otro caso.
	 */
	LIMINALPORTALS_API float ComputeSurfaceDepth(double SignedDistance, float NearThicknessDistance,
		float BaseDepth, float ExtendedDepth);

	/**
	 * Tamaño del render target: el del viewport, recortado al aspecto pedido (AspectRatio <= 0 usa
	 * el del viewport), escalado por Scale y limitado a MaxDimension en el lado mayor sin deformar.
	 * Devuelve (0, 0) si el viewport no es válido; el llamador no debe capturar.
	 */
	LIMINALPORTALS_API FIntPoint ComputeRenderTargetSize(const FIntPoint& ViewportSize, float AspectRatio,
		float Scale, int32 MaxDimension);

	/**
	 * Si una captura debe liberar su estado de render persistente (FSceneViewState con su escena de
	 * Lumen propia, D-31): lleva al menos IdleSeconds sin capturar y la cámara está a ReleaseDistance o
	 * más del plano del portal (otro mundo desplazado). IdleSeconds <= 0 desactiva la liberación; un
	 * tiempo o una distancia no finitos o negativos no liberan.
	 */
	LIMINALPORTALS_API bool ShouldReleaseCaptureState(double SecondsSinceLastCapture, double DistanceToCamera,
		double IdleSeconds, double ReleaseDistance);

	/**
	 * Precalentamiento de Lumen antes de un cruce (D-37): la cámara está por delante del plano del portal
	 * (SignedDistance > 0) y a PrewarmDistance o menos de su centro. PrewarmDistance <= 0 lo desactiva; las
	 * entradas no finitas no precalientan.
	 */
	LIMINALPORTALS_API bool ShouldPrewarmDestination(double SignedDistance, double DistanceToCenter, double PrewarmDistance);

	/**
	 * Origen extra de la escena de Lumen de la vista principal para precalentar un cruce (D-37): ExitDepth
	 * por delante del centro del portal de destino, donde queda la cámara al cruzar. Es fijo por portal para
	 * que el streaming lo reconozca como la misma vista de un fotograma a otro.
	 */
	LIMINALPORTALS_API FVector ComputePrewarmOrigin(const FTransform& TargetPortal, double ExitDepth);
}
