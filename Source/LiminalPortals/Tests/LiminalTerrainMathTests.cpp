#include "Misc/AutomationTest.h"
#include "World/LiminalTerrainMath.h"

// Tests del terreno procedural (E4): ruido determinista, alturas acotadas por tipo, zona llana de la
// puerta y malla. Cada test tiene casos positivos y negativos. Sin using namespace global (unity build, D-22).

namespace LiminalTerrainMathTests
{
	constexpr EAutomationTestFlags TerrainTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	const ELiminalBiomeType AllTypes[] = { ELiminalBiomeType::Jungle, ELiminalBiomeType::Desert, ELiminalBiomeType::Antarctic, ELiminalBiomeType::Farmland };

	/** Terreno del tamaño real (400 × 400 m, 2 m): el mismo que genera ALiminalBiome. */
	const FVector2D FullSize(40000.0, 40000.0);
	constexpr double FullSpacing = 200.0;

	LiminalTerrainMath::FTerrainParams MakeParams(ELiminalBiomeType Type, int32 Seed)
	{
		LiminalTerrainMath::FTerrainParams Params;
		Params.Type = Type;
		Params.Seed = Seed;
		return Params;
	}

	FString TypeLabel(ELiminalBiomeType Type)
	{
		return FString::FromInt(static_cast<int32>(Type));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalTerrainNoiseTest, "LiminalPortals.World.Terrain.Noise", LiminalTerrainMathTests::TerrainTestFlags)
bool FLiminalTerrainNoiseTest::RunTest(const FString& Parameters)
{
	using namespace LiminalTerrainMath;
	int32 OutOfRange = 0;
	int32 Repeated = 0;
	int32 SameAcrossSeeds = 0;
	constexpr int32 Samples = 2000;
	for (int32 Index = 0; Index < Samples; ++Index)
	{
		const double X = Index * 0.137 - 50.0;
		const double Y = Index * 0.291 + 13.0;
		const double Value = ValueNoise(X, Y, 7);
		const double Fractal = Fbm(X, Y, 7, 5);
		OutOfRange += (Value < -1.0 || Value > 1.0 || Fractal < -1.0 || Fractal > 1.0) ? 1 : 0;
		Repeated += (Value == ValueNoise(X, Y, 7) && Fractal == Fbm(X, Y, 7, 5)) ? 1 : 0;
		SameAcrossSeeds += FMath::IsNearlyEqual(Fractal, Fbm(X, Y, 8, 5), 1e-9) ? 1 : 0;
	}
	TestEqual(TEXT("Ruido de valor y fBm en [-1, 1]"), OutOfRange, 0);
	TestEqual(TEXT("Misma entrada, mismo valor"), Repeated, Samples);
	// Negativo: otra semilla da otro campo (alguna coincidencia casual es admisible).
	TestTrue(TEXT("Otra semilla cambia el ruido en casi todos los puntos"), SameAcrossSeeds < Samples / 100);
	TestNotEqual(TEXT("El hash depende de la semilla"), HashInts(3, 4, 1), HashInts(3, 4, 2));
	TestNotEqual(TEXT("El hash depende de X e Y por separado"), HashInts(3, 4, 1), HashInts(4, 3, 1));
	TestTrue(TEXT("Hash01 en [0, 1)"), Hash01(-5, 9, 3) >= 0.0 && Hash01(-5, 9, 3) < 1.0);

	FRandomStream A(1001);
	FRandomStream B(1001);
	FRandomStream C(1002);
	const FBiomeSeeds SeedsA = DeriveSeeds(A);
	const FBiomeSeeds SeedsB = DeriveSeeds(B);
	const FBiomeSeeds SeedsC = DeriveSeeds(C);
	TestTrue(TEXT("Mismo stream, mismas semillas"), SeedsA.Terrain == SeedsB.Terrain && SeedsA.Scatter == SeedsB.Scatter);
	TestTrue(TEXT("Otra semilla del bioma, otras semillas derivadas"), SeedsA.Terrain != SeedsC.Terrain && SeedsA.Scatter != SeedsC.Scatter);
	TestNotEqual(TEXT("Terreno y dispersión con semillas distintas"), SeedsA.Terrain, SeedsA.Scatter);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalTerrainDeterminismTest, "LiminalPortals.World.Terrain.HeightFieldDeterminism", LiminalTerrainMathTests::TerrainTestFlags)
bool FLiminalTerrainDeterminismTest::RunTest(const FString& Parameters)
{
	using namespace LiminalTerrainMath;
	using namespace LiminalTerrainMathTests;
	const FTerrainGrid Grid = MakeGrid(FullSize, FullSpacing);
	TestEqual(TEXT("Rejilla de 201 × 201"), Grid.Num(), 201 * 201);
	for (const ELiminalBiomeType Type : AllTypes)
	{
		const uint32 First = HashHeights(SampleHeightField(MakeParams(Type, 1001), Grid));
		const uint32 Second = HashHeights(SampleHeightField(MakeParams(Type, 1001), Grid));
		const uint32 Other = HashHeights(SampleHeightField(MakeParams(Type, 2002), Grid));
		TestEqual(FString::Printf(TEXT("Tipo %s: misma semilla, mismo hash"), *TypeLabel(Type)), First, Second);
		TestNotEqual(FString::Printf(TEXT("Tipo %s: semilla distinta, hash distinto"), *TypeLabel(Type)), First, Other);
	}
	// Negativo del hash: un solo cambio de 1 mm en una altura cambia el resultado.
	TArray<double> Heights = { 0.0, 10.0, 20.0 };
	const uint32 Before = HashHeights(Heights);
	Heights[1] += 0.1;
	TestNotEqual(TEXT("El hash detecta un cambio de 1 mm"), Before, HashHeights(Heights));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalTerrainBoundsTest, "LiminalPortals.World.Terrain.HeightBounds", LiminalTerrainMathTests::TerrainTestFlags)
bool FLiminalTerrainBoundsTest::RunTest(const FString& Parameters)
{
	using namespace LiminalTerrainMath;
	using namespace LiminalTerrainMathTests;
	// Relieve mínimo esperado por tipo (max − min, en cm): ninguno es plano.
	const double MinSpan[] = { 400.0, 400.0, 150.0, 40.0 };
	const FTerrainGrid Grid = MakeGrid(FullSize, FullSpacing);
	for (int32 TypeIndex = 0; TypeIndex < static_cast<int32>(UE_ARRAY_COUNT(AllTypes)); ++TypeIndex)
	{
		const ELiminalBiomeType Type = AllTypes[TypeIndex];
		const FHeightRange Range = GetHeightRange(Type);
		for (const int32 Seed : { 1001, 1002, 1003, 1004, 77 })
		{
			const FTerrainParams Params = MakeParams(Type, Seed);
			double Min = TNumericLimits<double>::Max();
			double Max = TNumericLimits<double>::Lowest();
			for (int32 Index = 0; Index < Grid.Num(); ++Index)
			{
				const FVector2D Point = Grid.GetPoint(Index / Grid.NumY, Index % Grid.NumY);
				const double Raw = SampleRawHeight(Params, Point.X, Point.Y);
				const double Final = SampleHeight(Params, Point.X, Point.Y);
				Min = FMath::Min(Min, FMath::Min(Raw, Final));
				Max = FMath::Max(Max, FMath::Max(Raw, Final));
			}
			const FString Label = FString::Printf(TEXT("Tipo %s, semilla %d"), *TypeLabel(Type), Seed);
			TestTrue(Label + TEXT(": alturas dentro del intervalo del tipo"), Min >= Range.Min - 1e-6 && Max <= Range.Max + 1e-6);
			TestTrue(Label + FString::Printf(TEXT(": relieve de al menos %.0f cm (%.0f)"), MinSpan[TypeIndex], Max - Min), Max - Min >= MinSpan[TypeIndex]);
		}
	}
	TestTrue(TEXT("Las dunas son el relieve más alto"), GetHeightRange(ELiminalBiomeType::Desert).Max > GetHeightRange(ELiminalBiomeType::Antarctic).Max);
	TestTrue(TEXT("Los cultivos son casi llanos"), GetHeightRange(ELiminalBiomeType::Farmland).Max < 100.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalTerrainDunesTest, "LiminalPortals.World.Terrain.DunesOriented", LiminalTerrainMathTests::TerrainTestFlags)
bool FLiminalTerrainDunesTest::RunTest(const FString& Parameters)
{
	using namespace LiminalTerrainMath;
	using namespace LiminalTerrainMathTests;
	// Dunas orientadas: la altura cambia mucho más al avanzar perpendicular a las crestas que a lo largo de ellas.
	for (const int32 Seed : { 1002, 5, 6 })
	{
		const FTerrainParams Desert = MakeParams(ELiminalBiomeType::Desert, Seed);
		const double Angle = GetDuneAngle(Seed);
		const FVector2D Across(FMath::Cos(Angle), FMath::Sin(Angle));
		const FVector2D Along(-Across.Y, Across.X);
		double SumAcross = 0.0;
		double SumAlong = 0.0;
		for (int32 Index = 0; Index < 400; ++Index)
		{
			const FVector2D P(-15000.0 + 75.0 * Index, 12000.0 - 61.0 * Index);
			const double H = SampleRawHeight(Desert, P.X, P.Y);
			SumAcross += FMath::Abs(SampleRawHeight(Desert, P.X + Across.X * 100.0, P.Y + Across.Y * 100.0) - H);
			SumAlong += FMath::Abs(SampleRawHeight(Desert, P.X + Along.X * 100.0, P.Y + Along.Y * 100.0) - H);
		}
		TestTrue(FString::Printf(TEXT("Semilla %d: pendiente transversal %.0f > 2 × longitudinal %.0f"), Seed, SumAcross, SumAlong), SumAcross > 2.0 * SumAlong);
		TestTrue(TEXT("Ángulo en [0, π)"), Angle >= 0.0 && Angle < UE_DOUBLE_PI);
	}
	// Negativo: otra semilla, otra orientación.
	TestNotEqual(TEXT("Orientación distinta con otra semilla"), GetDuneAngle(1), GetDuneAngle(2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalTerrainFlatZoneTest, "LiminalPortals.World.Terrain.FlatDoorZone", LiminalTerrainMathTests::TerrainTestFlags)
bool FLiminalTerrainFlatZoneTest::RunTest(const FString& Parameters)
{
	using namespace LiminalTerrainMath;
	using namespace LiminalTerrainMathTests;
	const FFlatZone Zone;
	TestEqual(TEXT("Peso nulo en la puerta"), FlatZoneWeight(Zone, FVector2D::ZeroVector), 0.0);
	TestEqual(TEXT("Peso nulo a 30 m por el camino"), FlatZoneWeight(Zone, FVector2D(3000.0, Zone.Radius - 1.0)), 0.0);
	TestEqual(TEXT("Peso completo fuera de la banda"), FlatZoneWeight(Zone, FVector2D(1500.0, Zone.Radius + Zone.Blend + 1.0)), 1.0);
	const double Mid = FlatZoneWeight(Zone, FVector2D(1500.0, Zone.Radius + Zone.Blend * 0.5));
	TestTrue(TEXT("Transición intermedia"), Mid > 0.3 && Mid < 0.7);

	for (const ELiminalBiomeType Type : AllTypes)
	{
		const FTerrainParams Params = MakeParams(Type, 1001 + static_cast<int32>(Type));
		// La zona de la puerta y el camino de salida (30 m) están exactamente a ras del portal.
		int32 NotFlat = 0;
		for (double X = -500.0; X <= 3000.0; X += 50.0)
		{
			for (double Y = -Zone.Radius + 1.0; Y <= Zone.Radius - 1.0; Y += 50.0)
			{
				NotFlat += SampleHeight(Params, X, Y) != 0.0 ? 1 : 0;
			}
		}
		TestEqual(FString::Printf(TEXT("Tipo %s: puerta y camino llanos"), *TypeLabel(Type)), NotFlat, 0);

		// Transición continua: sin escalones entre muestras a 10 cm al salir de la zona llana (20 cm
		// cada 10 cm acota la pendiente más fuerte de las dunas; un corte sin banda daría el relieve entero).
		double MaxStep = 0.0;
		double Previous = SampleHeight(Params, 1500.0, 0.0);
		for (double Y = 10.0; Y <= Zone.Radius + Zone.Blend + 500.0; Y += 10.0)
		{
			const double Height = SampleHeight(Params, 1500.0, Y);
			MaxStep = FMath::Max(MaxStep, FMath::Abs(Height - Previous));
			Previous = Height;
		}
		TestTrue(FString::Printf(TEXT("Tipo %s: escalón máximo %.2f cm cada 10 cm"), *TypeLabel(Type), MaxStep), MaxStep <= 20.0);

		// Negativo: fuera de la zona llana el relieve existe.
		double MaxOutside = 0.0;
		for (double X = -15000.0; X <= 15000.0; X += 997.0)
		{
			MaxOutside = FMath::Max(MaxOutside, FMath::Abs(SampleHeight(Params, X, 9000.0)));
		}
		TestTrue(FString::Printf(TEXT("Tipo %s: relieve fuera de la zona llana (%.1f cm)"), *TypeLabel(Type), MaxOutside), MaxOutside > 10.0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalTerrainMeshTest, "LiminalPortals.World.Terrain.Mesh", LiminalTerrainMathTests::TerrainTestFlags)
bool FLiminalTerrainMeshTest::RunTest(const FString& Parameters)
{
	using namespace LiminalTerrainMath;
	using namespace LiminalTerrainMathTests;
	const FTerrainGrid Grid = MakeGrid(FVector2D(4000.0, 3000.0), 200.0);
	TestEqual(TEXT("21 columnas"), Grid.NumX, 21);
	TestEqual(TEXT("16 filas"), Grid.NumY, 16);
	TestTrue(TEXT("Rejilla centrada"), Grid.Origin.Equals(FVector2D(-2000.0, -1500.0), 1e-9));

	FTerrainMesh Mesh;
	UTEST_TRUE(TEXT("Malla de selva"), BuildTerrainMesh(MakeParams(ELiminalBiomeType::Jungle, 3), Grid, Mesh));
	const int32 NumCorners = (Grid.NumX - 1) * (Grid.NumY - 1) * 6;
	TestEqual(TEXT("Dos triángulos por celda"), Mesh.Triangles.Num(), NumCorners);
	TestEqual(TEXT("Facetado: tres vértices propios por triángulo"), Mesh.Vertices.Num(), NumCorners);
	TestTrue(TEXT("Normales, tangentes, UV y colores por vértice"), Mesh.Normals.Num() == NumCorners && Mesh.Tangents.Num() == NumCorners
		&& Mesh.UVs.Num() == NumCorners && Mesh.Colors.Num() == NumCorners);

	int32 BadNormals = 0;
	for (int32 Index = 0; Index < Mesh.Normals.Num(); ++Index)
	{
		const FVector& N = Mesh.Normals[Index];
		BadNormals += (!FMath::IsNearlyEqual(N.Size(), 1.0, 1e-6) || N.Z <= 0.0 || FMath::Abs(FVector::DotProduct(N, Mesh.Tangents[Index])) > 1e-6) ? 1 : 0;
	}
	TestEqual(TEXT("Normales unitarias hacia arriba y perpendiculares a la tangente"), BadNormals, 0);

	// Cara frontal hacia arriba con la convención de UE (KismetProceduralMeshLibrary): (P1 − P2) × (P0 − P2),
	// y normal y color de la cara compartidos por sus tres vértices.
	int32 Flipped = 0;
	int32 NotFlat = 0;
	for (int32 Index = 0; Index + 2 < Mesh.Triangles.Num(); Index += 3)
	{
		const int32 I0 = Mesh.Triangles[Index];
		const int32 I1 = Mesh.Triangles[Index + 1];
		const int32 I2 = Mesh.Triangles[Index + 2];
		const FVector Face = FVector::CrossProduct(Mesh.Vertices[I1] - Mesh.Vertices[I2], Mesh.Vertices[I0] - Mesh.Vertices[I2]);
		Flipped += Face.Z <= 0.0 ? 1 : 0;
		NotFlat += (!Mesh.Normals[I0].Equals(Face.GetSafeNormal(), 1e-6) || Mesh.Normals[I0] != Mesh.Normals[I1] || Mesh.Normals[I0] != Mesh.Normals[I2]
			|| Mesh.Colors[I0] != Mesh.Colors[I1] || Mesh.Colors[I0] != Mesh.Colors[I2]) ? 1 : 0;
	}
	TestEqual(TEXT("Ningún triángulo mira hacia abajo"), Flipped, 0);
	TestEqual(TEXT("Normal y color por cara"), NotFlat, 0);
	// Negativo: caras vecinas con relieve tienen normales distintas (no es un suavizado).
	int32 DistinctNeighbours = 0;
	for (int32 Index = 0; Index + 5 < Mesh.Vertices.Num(); Index += 6)
	{
		DistinctNeighbours += Mesh.Normals[Index].Equals(Mesh.Normals[Index + 3], 1e-9) ? 0 : 1;
	}
	TestTrue(TEXT("Facetas visibles"), DistinctNeighbours > (Grid.NumX - 1) * (Grid.NumY - 1) / 2);

	// Negativos: rejillas inválidas.
	TestEqual(TEXT("Separación nula, rejilla vacía"), MakeGrid(FVector2D(100.0, 100.0), 0.0).Num(), 0);
	FTerrainGrid Tiny;
	Tiny.NumX = 1;
	Tiny.NumY = 5;
	TestFalse(TEXT("Rejilla de una columna"), BuildTerrainMesh(MakeParams(ELiminalBiomeType::Desert, 1), Tiny, Mesh));
	TestTrue(TEXT("Malla vacía si falla"), Mesh.Vertices.IsEmpty() && Mesh.Triangles.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalTerrainFarmTest, "LiminalPortals.World.Terrain.FarmParcels", LiminalTerrainMathTests::TerrainTestFlags)
bool FLiminalTerrainFarmTest::RunTest(const FString& Parameters)
{
	using namespace LiminalTerrainMath;
	const FFarmGrid Farm;
	// Parcelas de color alterno: las vecinas en X y en Y nunca comparten cultivo.
	int32 SameNeighbours = 0;
	for (int32 IX = -5; IX <= 5; ++IX)
	{
		for (int32 IY = -5; IY <= 5; ++IY)
		{
			const int32 Crop = GetParcelCrop(IX, IY, 1004);
			TestTrue(TEXT("Cultivo 0-3"), Crop >= 0 && Crop <= 3);
			SameNeighbours += (Crop == GetParcelCrop(IX + 1, IY, 1004) || Crop == GetParcelCrop(IX, IY + 1, 1004)) ? 1 : 0;
		}
	}
	TestEqual(TEXT("Vecinas con distinto cultivo"), SameNeighbours, 0);

	// El camino de salida (Y = 0) sigue por un camino de tierra; el centro de una parcela no es camino.
	TestTrue(TEXT("Camino de tierra en Y = 0"), GetFarmCell(Farm, FVector2D(8000.0, 50.0)).bRoad);
	const FFarmCell Center = GetFarmCell(Farm, FVector2D(Farm.OriginX + Farm.ParcelSize.X * 0.5, Farm.ParcelSize.Y * 0.5));
	TestFalse(TEXT("Centro de parcela"), Center.bRoad);
	TestTrue(TEXT("Distancia al camino en el centro"), FMath::IsNearlyEqual(Center.DistanceToRoad, Farm.ParcelSize.Y * 0.5 - Farm.RoadWidth * 0.5, 1e-6));
	TestEqual(TEXT("Índices de celda negativos al otro lado"), GetFarmCell(Farm, FVector2D(Farm.OriginX - 1.0, -1.0)).IX, -1);

	// Negativo: otro color de camino que de parcela.
	FTerrainParams Params;
	Params.Type = ELiminalBiomeType::Farmland;
	Params.Seed = 1004;
	const FLinearColor Road = SampleColor(Params, 8000.0, 50.0, 0.0);
	const FLinearColor Field = SampleColor(Params, Farm.OriginX + Farm.ParcelSize.X * 0.5, Farm.ParcelSize.Y * 0.5, 0.0);
	TestFalse(TEXT("Camino y parcela con colores distintos"), Road.Equals(Field, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalTerrainSurfaceHeightTest, "LiminalPortals.World.Terrain.SurfaceHeight", LiminalTerrainMathTests::TerrainTestFlags)
bool FLiminalTerrainSurfaceHeightTest::RunTest(const FString& Parameters)
{
	using namespace LiminalTerrainMath;
	using namespace LiminalTerrainMathTests;
	const FTerrainParams Params = MakeParams(ELiminalBiomeType::Desert, 11);
	const FTerrainGrid Grid = MakeGrid(FVector2D(20000.0, 20000.0), 250.0);
	FTerrainMesh Mesh;
	UTEST_TRUE(TEXT("Malla"), BuildTerrainMesh(Params, Grid, Mesh));

	// En los vértices coincide con el campo de alturas y en el baricentro de cada cara con la media de sus vértices.
	double MaxVertexError = 0.0;
	double MaxFaceError = 0.0;
	for (int32 Index = 0; Index + 2 < Mesh.Vertices.Num(); Index += 3 * 37)
	{
		const FVector& P0 = Mesh.Vertices[Index];
		const FVector& P1 = Mesh.Vertices[Index + 1];
		const FVector& P2 = Mesh.Vertices[Index + 2];
		MaxVertexError = FMath::Max(MaxVertexError, FMath::Abs(SampleSurfaceHeight(Params, Grid, P0.X, P0.Y) - P0.Z));
		const FVector Center = (P0 + P1 + P2) / 3.0;
		MaxFaceError = FMath::Max(MaxFaceError, FMath::Abs(SampleSurfaceHeight(Params, Grid, Center.X, Center.Y) - Center.Z));
	}
	TestTrue(FString::Printf(TEXT("Vértices sobre la superficie (%.2e cm)"), MaxVertexError), MaxVertexError < 1e-6);
	TestTrue(FString::Printf(TEXT("Caras planas (%.2e cm)"), MaxFaceError), MaxFaceError < 1e-6);

	// Negativo: entre vértices, en las dunas, la superficie facetada no es el campo continuo.
	double MaxGap = 0.0;
	for (double X = 3000.0; X < 9000.0; X += 173.0)
	{
		MaxGap = FMath::Max(MaxGap, FMath::Abs(SampleSurfaceHeight(Params, Grid, X, 4321.0) - SampleHeight(Params, X, 4321.0)));
	}
	TestTrue(FString::Printf(TEXT("Facetas distintas del campo continuo (%.2f cm)"), MaxGap), MaxGap > 0.1);
	// Fuera de la rejilla se recorta al borde.
	TestNearlyEqual(TEXT("Recorte al borde"), SampleSurfaceHeight(Params, Grid, 50000.0, 0.0), SampleSurfaceHeight(Params, Grid, 10000.0, 0.0), 1e-9);
	return true;
}
