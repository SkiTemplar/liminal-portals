#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "World/LiminalBiomeTypes.h"

/**
 * Terreno procedural de los biomas (etapa E4): ruido fractal determinista a partir de una semilla,
 * campo de alturas por tipo de bioma, paleta de colores por vértice y malla en rejilla. Funciones
 * puras, sin UWorld ni actores. Espacio local del bioma: origen en la puerta de vuelta (a ras de
 * suelo), eje X hacia el bioma; unidades en cm.
 *
 * La zona de la puerta y el camino de salida quedan exactamente a Z = 0 (el suelo del portal, D-01)
 * y se funden con el relieve en una banda de transición suave (smoothstep).
 */
namespace LiminalTerrainMath
{
	/** Intervalo de alturas posible de un tipo de bioma, en cm. */
	struct FHeightRange
	{
		double Min = 0.0;
		double Max = 0.0;
	};

	/** Zona llana: cápsula (segmento Start-End con radio Radius) y banda de transición de ancho Blend. */
	struct FFlatZone
	{
		FVector2D Start = FVector2D(-800.0, 0.0);
		FVector2D End = FVector2D(3000.0, 0.0);
		double Radius = 600.0;
		double Blend = 1500.0;
	};

	/**
	 * Parcelario de los cultivos: parcelas de ParcelSize separadas por caminos de RoadWidth centrados
	 * en las rectas X = OriginX + i·ParcelSize.X e Y = OriginY + j·ParcelSize.Y. Con OriginY = 0 el
	 * camino de salida de la puerta continúa por un camino de tierra.
	 */
	struct FFarmGrid
	{
		FVector2D ParcelSize = FVector2D(6000.0, 4000.0);
		double OriginX = 4000.0;
		double OriginY = 0.0;
		double RoadWidth = 400.0;
		/** Caballones dentro de cada parcela: periodo y amplitud, en cm. */
		double FurrowPeriod = 1000.0;
		double FurrowAmplitude = 15.0;
	};

	/** Celda del parcelario que contiene un punto. */
	struct FFarmCell
	{
		int32 IX = 0;
		int32 IY = 0;
		/** Distancia al borde del camino más cercano (negativa si el punto está en un camino). */
		double DistanceToRoad = 0.0;
		bool bRoad = false;
	};

	struct FTerrainParams
	{
		ELiminalBiomeType Type = ELiminalBiomeType::Jungle;
		int32 Seed = 1;
		FFlatZone FlatZone;
		FFarmGrid Farm;
	};

	/** Rejilla regular de vértices centrada en el origen: índice = IX · NumY + IY. */
	struct FTerrainGrid
	{
		FVector2D Origin = FVector2D::ZeroVector;
		double Spacing = 200.0;
		int32 NumX = 0;
		int32 NumY = 0;

		int32 Num() const { return NumX * NumY; }
		FVector2D GetPoint(int32 IX, int32 IY) const { return Origin + FVector2D(IX * Spacing, IY * Spacing); }
	};

	/** Malla del terreno lista para UProceduralMeshComponent (colores lineales). */
	struct FTerrainMesh
	{
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector> Tangents;
		TArray<FVector2D> UVs;
		TArray<FLinearColor> Colors;
	};

	/** Semillas derivadas de la del bioma (una por subsistema) a partir de un FRandomStream. */
	struct FBiomeSeeds
	{
		int32 Terrain = 0;
		int32 Scatter = 0;
	};

	/** Consume dos enteros de Stream: mismo estado de partida ⇒ mismas semillas. */
	LIMINALPORTALS_API FBiomeSeeds DeriveSeeds(FRandomStream& Stream);

	/** Hash entero sin estado de (X, Y, Seed) con buena avalancha (lowbias32). */
	LIMINALPORTALS_API uint32 HashInts(int32 X, int32 Y, int32 Seed);

	/** HashInts llevado a [0, 1). */
	LIMINALPORTALS_API double Hash01(int32 X, int32 Y, int32 Seed);

	/** Ruido de valor 2D con interpolación quíntica, en [-1, 1]; periodo de celda 1. */
	LIMINALPORTALS_API double ValueNoise(double X, double Y, int32 Seed);

	/** Suma fractal (fBm) de Octaves octavas de ValueNoise, normalizada a [-1, 1]. */
	LIMINALPORTALS_API double Fbm(double X, double Y, int32 Seed, int32 Octaves, double Lacunarity = 2.0, double Gain = 0.5);

	/** Smoothstep cúbico con T recortado a [0, 1]. */
	LIMINALPORTALS_API double SmoothStep01(double T);

	LIMINALPORTALS_API double DistanceToSegment(const FVector2D& Point, const FVector2D& A, const FVector2D& B);

	/** 0 dentro de la cápsula llana, 1 fuera de la banda de transición y smoothstep entre medias. */
	LIMINALPORTALS_API double FlatZoneWeight(const FFlatZone& Zone, const FVector2D& Point);

	/** Intervalo de alturas de SampleRawHeight para ese tipo (cota garantizada por construcción). */
	LIMINALPORTALS_API FHeightRange GetHeightRange(ELiminalBiomeType Type);

	/** Orientación de las dunas (dirección de avance del perfil) en radianes, en [0, π). */
	LIMINALPORTALS_API double GetDuneAngle(int32 Seed);

	LIMINALPORTALS_API FFarmCell GetFarmCell(const FFarmGrid& Grid, const FVector2D& Point);

	/**
	 * Cultivo de la parcela (0-3). La paridad de IX + IY elige el grupo ({0, 1} o {2, 3}) y la semilla
	 * el cultivo dentro del grupo: dos parcelas vecinas nunca tienen el mismo color.
	 */
	LIMINALPORTALS_API int32 GetParcelCrop(int32 IX, int32 IY, int32 Seed);

	/** Altura del relieve sin la zona llana, en cm (dentro de GetHeightRange). */
	LIMINALPORTALS_API double SampleRawHeight(const FTerrainParams& Params, double X, double Y);

	/** Altura final: el relieve atenuado por FlatZoneWeight (0 en la puerta y el camino de salida). */
	LIMINALPORTALS_API double SampleHeight(const FTerrainParams& Params, double X, double Y);

	/** Color lineal del terreno en (X, Y) con altura Height (paleta por tipo, caminos y parcelas). */
	LIMINALPORTALS_API FLinearColor SampleColor(const FTerrainParams& Params, double X, double Y, double Height);

	/**
	 * Rejilla centrada que cubre Size con separación Spacing (redondeada para que el borde coincida).
	 * @return Rejilla con NumX = NumY = 0 si Size o Spacing no son positivos.
	 */
	LIMINALPORTALS_API FTerrainGrid MakeGrid(const FVector2D& Size, double Spacing);

	/** Alturas de SampleHeight en cada vértice de Grid (índice IX · NumY + IY). */
	LIMINALPORTALS_API TArray<double> SampleHeightField(const FTerrainParams& Params, const FTerrainGrid& Grid);

	/**
	 * Malla de la rejilla: vértices, dos triángulos por celda con la cara frontal hacia +Z según la
	 * convención de UE (normal = (P1 − P2) × (P0 − P2)), normales y tangentes por diferencias
	 * centrales, UV de 10 m y colores de SampleColor.
	 * @return false (con OutMesh vacío) si la rejilla tiene menos de 2 × 2 vértices.
	 */
	LIMINALPORTALS_API bool BuildTerrainMesh(const FTerrainParams& Params, const FTerrainGrid& Grid, FTerrainMesh& OutMesh);

	/** Hash FNV-1a de alturas cuantizadas a 0,01 cm (prueba de determinismo). */
	LIMINALPORTALS_API uint32 HashHeights(TConstArrayView<double> Heights);
}
