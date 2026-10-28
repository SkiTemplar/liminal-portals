#pragma once

#include "CoreMinimal.h"

/**
 * Funciones puras de Liminal.Bench (etapa E5): estadísticos de las muestras de tiempo, coste
 * marginal por portal y poses fijas del hub. Sin UWorld ni actores, para poder testearlas.
 */
namespace LiminalBenchMath
{
	/** Estadísticos de una serie de tiempos en ms. Count = 0 si la serie está vacía. */
	struct FSampleStats
	{
		int32 Count = 0;
		double Mean = 0.0;
		double P50 = 0.0;
		double P95 = 0.0;
		double Min = 0.0;
		double Max = 0.0;
	};

	/**
	 * Percentil por interpolación lineal entre rangos vecinos (método 7 de Hyndman y Fan, el de
	 * numpy.percentile por defecto). Fraction se recorta a [0, 1].
	 * @param SortedSamples Muestras ordenadas de menor a mayor.
	 * @return 0 si no hay muestras.
	 */
	LIMINALPORTALS_API double Percentile(TConstArrayView<double> SortedSamples, double Fraction);

	/** Media, p50, p95, mínimo y máximo de Samples (no hace falta que estén ordenadas). */
	LIMINALPORTALS_API FSampleStats ComputeStats(TConstArrayView<double> Samples);

	/**
	 * Recta de mínimos cuadrados Y = Slope · X + Intercept.
	 * @return false si X e Y tienen tamaños distintos, hay menos de dos puntos o todas las X son iguales.
	 */
	LIMINALPORTALS_API bool FitLine(TConstArrayView<double> X, TConstArrayView<double> Y, double& OutSlope,
		double& OutIntercept);

	/**
	 * Coste por portal entre dos medidas desde la misma vista: (WithPortals − WithoutPortals) / NumPortals.
	 * @return false si NumPortals <= 0.
	 */
	LIMINALPORTALS_API bool ComputeCostPerPortal(double WithPortals, double WithoutPortals, int32 NumPortals,
		double& OutCostPerPortal);

	/** Pose fija de medida en espacio local del hub (pies en el suelo, Z = 0). */
	struct FBenchPose
	{
		FString Name;
		FVector2D Feet = FVector2D::ZeroVector;
		/** Yaw de la mirada respecto al hub, en grados (pitch y roll a 0). */
		double Yaw = 0.0;
		/** Capturas por fotograma que se esperan con esta pose. */
		int32 ExpectedCaptures = 0;
		/** false: misma vista con Liminal.Portals.Enable 0 (referencia sin portales). */
		bool bPortalsEnabled = true;
	};

	/**
	 * Cinco poses para el hub por defecto (LiminalWorldLayout::MakeDefaultHubDoors, sala de 16 × 6 m):
	 * 0, 1, 2 y 4 portales visibles y la vista del caso de 4 con los portales apagados. Las poses
	 * dejan al menos 12° de margen respecto al límite del test de visibilidad para aspectos de 4:3 a 21:9.
	 */
	LIMINALPORTALS_API TArray<FBenchPose> MakeHubBenchPoses();

	/**
	 * Cuántos portales pasan LiminalPortalMath::IsPortalPotentiallyVisible desde la cámara dada
	 * (lo mismo que decide ULiminalPortalSubsystem antes de capturar).
	 * @param PlaneTransforms Centro de cada rectángulo de portal (GetPlaneTransform).
	 */
	LIMINALPORTALS_API int32 CountPotentiallyVisiblePortals(TConstArrayView<FTransform> PlaneTransforms,
		const FVector2D& HalfExtents, const FVector& CameraLocation, const FVector& CameraForward,
		float HorizontalFOVDegrees, float AspectRatio);

	/**
	 * Recorrido del modo perfil (D-36): rejilla de 6 × 3 posiciones por la sala del hub por defecto
	 * (X de −650 a +600 cada 250 cm, Y = −150, 0 y +150) y 12 orientaciones cada 30°. Ninguna pose queda a
	 * menos de 150 cm de un plano de portal. ExpectedCaptures = −1: no se fija.
	 */
	LIMINALPORTALS_API TArray<FBenchPose> MakeHubWalkPoses();

	/**
	 * Fracción del viewport que ocupa el rectángulo de pantalla del polígono Corners (la caja envolvente de
	 * su proyección, recortada al viewport): lo que renderizaría una captura recortada con proyección fuera
	 * de eje. El polígono se recorta antes contra el plano cercano (NearPlane cm delante de la cámara).
	 * FOV horizontal, como FMinimalViewInfo con AspectRatio >= 1.
	 * @return 0 si el polígono queda detrás de la cámara o fuera del viewport; 1 si lo cubre entero.
	 */
	LIMINALPORTALS_API double ComputeScreenRectFraction(TConstArrayView<FVector> Corners, const FVector& ViewLocation,
		const FRotator& ViewRotation, float HorizontalFOVDegrees, float AspectRatio, double NearPlane = 10.0);

	/**
	 * Giro de los cruces del modo perfil (D-36): yaw tras Frame fotogramas de un giro suave (smoothstep) de
	 * TotalDegrees grados repartido en TurnFrames fotogramas, como un jugador que se da la vuelta.
	 * Frame se recorta a [0, TurnFrames]; con TurnFrames <= 0 el giro es instantáneo (StartYaw + TotalDegrees).
	 */
	LIMINALPORTALS_API double ComputeTurnYaw(double StartYaw, double TotalDegrees, int32 Frame, int32 TurnFrames);

	/** Pico de una ventana de tiempos de fotograma. */
	struct FSpike
	{
		/** Índice del máximo en la ventana; INDEX_NONE si está vacía. */
		int32 Index = INDEX_NONE;
		double MaxMs = 0.0;
		/** Mediana de la ventana: la referencia del pico. */
		double MedianMs = 0.0;
		/** MaxMs / MedianMs; 0 si la ventana está vacía o la mediana es 0. */
		double Ratio = 0.0;
	};

	/** Máximo de Values, su índice (el primero si se repite) y su cociente frente a la mediana de la propia ventana. */
	LIMINALPORTALS_API FSpike FindSpike(TConstArrayView<double> Values);
}
