#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "World/LiminalBiomeTypes.h"
#include "World/LiminalTerrainMath.h"

/**
 * Dispersión determinista de los biomas (etapa E4): rejilla con jitter (una muestra por celda,
 * separación mínima garantizada), densidad modulada por ruido y exclusión alrededor de la puerta de
 * vuelta y del camino de salida. Las recetas por tipo de bioma componen las formas básicas del motor
 * (100 cm, centradas) en transforms locales del bioma, agrupados por categoría. Funciones puras.
 */
namespace LiminalScatterMath
{
	/** Presupuesto orientativo de instancias por bioma (plan E4). */
	inline constexpr int32 MaxInstancesPerBiome = 15000;

	/** Zona prohibida: cápsula (segmento Start-End con radio Radius) en el plano XY local. */
	struct FExclusionCapsule
	{
		FVector2D Start = FVector2D::ZeroVector;
		FVector2D End = FVector2D::ZeroVector;
		double Radius = 0.0;
	};

	/** Exclusiones por defecto: círculo de 10 m en la puerta y cápsula de 7 m a lo largo del camino de salida. */
	LIMINALPORTALS_API TArray<FExclusionCapsule> MakeDoorExclusions();

	/** Distancia con signo al borde de la exclusión más cercana (negativa dentro); infinita si no hay. */
	LIMINALPORTALS_API double DistanceToExclusions(TConstArrayView<FExclusionCapsule> Exclusions, const FVector2D& Point);

	/** true si el punto, con un radio de huella Margin, toca alguna exclusión. */
	LIMINALPORTALS_API bool IsExcluded(TConstArrayView<FExclusionCapsule> Exclusions, const FVector2D& Point, double Margin = 0.0);

	/** Parámetros de una capa de rejilla con jitter. */
	struct FJitterGrid
	{
		double CellSize = 500.0;
		/** Fracción de la celda en la que puede caer el punto: separación mínima = (1 − Jitter) · CellSize. */
		double Jitter = 0.8;
		/** Probabilidad de que una celda tenga punto (antes de la modulación por agrupamiento). */
		double Probability = 0.5;
		/** Escala del ruido de agrupamiento en cm; 0 = densidad uniforme. */
		double ClusterScale = 0.0;
		/** Rango del ruido (0-1) que se lleva de densidad nula a completa con smoothstep. */
		double ClusterLow = 0.0;
		double ClusterHigh = 1.0;
		/** Radio de la huella para la exclusión, en cm. */
		double Footprint = 0.0;
	};

	/** Punto de una capa con su hash (de él salen los valores aleatorios con Random). */
	struct FScatterPoint
	{
		FVector2D Location = FVector2D::ZeroVector;
		uint32 Hash = 0;
	};

	/** Valor aleatorio determinista en [0, 1) número Index del punto. */
	LIMINALPORTALS_API double Random(const FScatterPoint& Point, int32 Index);

	/**
	 * Un punto como máximo por celda de la rejilla que cubre Area, dentro de Area y fuera de las
	 * exclusiones (con la huella). Mismo Seed ⇒ mismos puntos en el mismo orden.
	 */
	LIMINALPORTALS_API TArray<FScatterPoint> JitteredGrid(const FJitterGrid& Params, const FBox2D& Area,
		TConstArrayView<FExclusionCapsule> Exclusions, int32 Seed);

	/** Resultado de la dispersión: transforms locales por categoría. */
	struct FBiomeScatter
	{
		TArray<FTransform> Categories[LiminalBiomeTypes::NumScatterCategories];

		TArray<FTransform>& Get(ELiminalScatterCategory Category) { return Categories[static_cast<int32>(Category)]; }
		const TArray<FTransform>& Get(ELiminalScatterCategory Category) const { return Categories[static_cast<int32>(Category)]; }
		LIMINALPORTALS_API int32 Num() const;
	};

	struct FScatterParams
	{
		ELiminalBiomeType Type = ELiminalBiomeType::Jungle;
		int32 Seed = 1;
		/** Zona dispersable (normalmente el terreno menos un margen). */
		FBox2D Area = FBox2D(FVector2D(-19500.0), FVector2D(19500.0));
		TArray<FExclusionCapsule> Exclusions;
		/** Parcelario (solo cultivos): cercas a lo largo de los caminos y pacas en el trigo. */
		LiminalTerrainMath::FFarmGrid Farm;
		/** Semilla del terreno (cultivo de cada parcela en los cultivos). */
		int32 TerrainSeed = 1;
	};

	/**
	 * Parámetros que usa ALiminalBiome: terreno de GroundSize centrado en la puerta menos EdgeMargin,
	 * exclusiones de MakeDoorExclusions y semillas derivadas de la del bioma.
	 */
	LIMINALPORTALS_API FScatterParams MakeScatterParams(ELiminalBiomeType Type, const LiminalTerrainMath::FBiomeSeeds& Seeds,
		const FVector2D& GroundSize, double EdgeMargin = 500.0);

	/** Altura del terreno en (X, Y) local, en cm. */
	using FHeightFunction = TFunctionRef<double(double X, double Y)>;

	/**
	 * Receta del tipo de bioma: árboles, arbustos y sotobosque (selva); rocas erosionadas (desierto);
	 * bloques y placas de hielo y montículos de nieve (Antártida); cercas, pacas y árboles aislados
	 * (cultivos). Ninguna instancia tiene el centro dentro de una exclusión.
	 */
	LIMINALPORTALS_API FBiomeScatter BuildBiomeScatter(const FScatterParams& Params, FHeightFunction HeightAt);

	/** Hash FNV-1a de todos los transforms cuantizados (0,1 mm, 1e-4 en rotación y escala). */
	LIMINALPORTALS_API uint32 HashScatter(const FBiomeScatter& Scatter);

	LIMINALPORTALS_API ELiminalBasicShape GetCategoryShape(ELiminalScatterCategory Category);

	/** Solo troncos, rocas grandes y bloques de hielo bloquean (plan E4: colisión en terreno, troncos y rocas). */
	LIMINALPORTALS_API bool HasCollision(ELiminalScatterCategory Category);

	/** Distancia de desaparición en cm (0 = sin límite) para lo pequeño. */
	LIMINALPORTALS_API double GetCullDistance(ELiminalScatterCategory Category);
}
