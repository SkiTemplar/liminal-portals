#include "Misc/AutomationTest.h"
#include "World/LiminalScatterMath.h"
#include "World/LiminalTerrainMath.h"

// Tests de la dispersión procedural (E4): determinismo, exclusión alrededor de la puerta y del camino,
// separación mínima y presupuesto de instancias. Sin using namespace global (unity build, D-22).

namespace LiminalScatterMathTests
{
	constexpr EAutomationTestFlags ScatterTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	const ELiminalBiomeType ScatterTypes[] = { ELiminalBiomeType::Jungle, ELiminalBiomeType::Desert, ELiminalBiomeType::Antarctic, ELiminalBiomeType::Farmland };
	const FVector2D BiomeGroundSize(40000.0, 40000.0);

	/** Dispersión con los mismos parámetros que ALiminalBiome para el tipo y la semilla del bioma. */
	LiminalScatterMath::FBiomeScatter BuildLikeBiome(ELiminalBiomeType Type, int32 BiomeSeed, bool bWithExclusions = true)
	{
		FRandomStream Stream(BiomeSeed);
		const LiminalTerrainMath::FBiomeSeeds Seeds = LiminalTerrainMath::DeriveSeeds(Stream);
		LiminalTerrainMath::FTerrainParams Terrain;
		Terrain.Type = Type;
		Terrain.Seed = Seeds.Terrain;
		LiminalScatterMath::FScatterParams Params = LiminalScatterMath::MakeScatterParams(Type, Seeds, BiomeGroundSize);
		if (!bWithExclusions)
		{
			Params.Exclusions.Reset();
		}
		return LiminalScatterMath::BuildBiomeScatter(Params,
			[&Terrain](double X, double Y) { return LiminalTerrainMath::SampleHeight(Terrain, X, Y); });
	}

	int32 CountInside(const LiminalScatterMath::FBiomeScatter& Scatter, TConstArrayView<LiminalScatterMath::FExclusionCapsule> Exclusions)
	{
		int32 Inside = 0;
		for (const TArray<FTransform>& Category : Scatter.Categories)
		{
			for (const FTransform& Transform : Category)
			{
				Inside += LiminalScatterMath::IsExcluded(Exclusions, FVector2D(Transform.GetLocation())) ? 1 : 0;
			}
		}
		return Inside;
	}

	FString TypeName(ELiminalBiomeType Type)
	{
		return FString::FromInt(static_cast<int32>(Type));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalScatterGridTest, "LiminalPortals.World.Scatter.JitteredGrid", LiminalScatterMathTests::ScatterTestFlags)
bool FLiminalScatterGridTest::RunTest(const FString& Parameters)
{
	using namespace LiminalScatterMath;
	const FBox2D Area(FVector2D(0.0, 0.0), FVector2D(5000.0, 4000.0));
	FJitterGrid Grid;
	Grid.CellSize = 250.0;
	Grid.Jitter = 0.8;
	Grid.Probability = 1.0;

	const TArray<FScatterPoint> Full = JitteredGrid(Grid, Area, {}, 11);
	TestEqual(TEXT("Probabilidad 1: un punto por celda"), Full.Num(), 20 * 16);
	double MinDistance = TNumericLimits<double>::Max();
	int32 Outside = 0;
	for (int32 A = 0; A < Full.Num(); ++A)
	{
		Outside += Area.IsInside(Full[A].Location) ? 0 : 1;
		for (int32 B = A + 1; B < Full.Num(); ++B)
		{
			MinDistance = FMath::Min(MinDistance, FVector2D::Distance(Full[A].Location, Full[B].Location));
		}
	}
	TestEqual(TEXT("Todos dentro de la zona"), Outside, 0);
	TestTrue(FString::Printf(TEXT("Separación mínima %.1f >= (1 - jitter) · celda"), MinDistance), MinDistance >= (1.0 - Grid.Jitter) * Grid.CellSize - 1e-6);

	// Determinismo y dependencia de la semilla.
	const TArray<FScatterPoint> Again = JitteredGrid(Grid, Area, {}, 11);
	const TArray<FScatterPoint> Other = JitteredGrid(Grid, Area, {}, 12);
	bool bSame = Again.Num() == Full.Num();
	bool bDifferent = Other.Num() != Full.Num();
	for (int32 Index = 0; Index < FMath::Min(Full.Num(), Other.Num()); ++Index)
	{
		bSame = bSame && Again[Index].Location == Full[Index].Location && Again[Index].Hash == Full[Index].Hash;
		bDifferent = bDifferent || !Other[Index].Location.Equals(Full[Index].Location, 1e-6);
	}
	TestTrue(TEXT("Misma semilla, mismos puntos"), bSame);
	TestTrue(TEXT("Otra semilla, otros puntos"), bDifferent);

	// Exclusión con huella: ningún punto a menos de radio + huella del centro.
	const FExclusionCapsule Hole[] = { { FVector2D(2500.0, 2000.0), FVector2D(2500.0, 2000.0), 800.0 } };
	Grid.Footprint = 100.0;
	for (const FScatterPoint& Point : JitteredGrid(Grid, Area, Hole, 11))
	{
		if (FVector2D::Distance(Point.Location, FVector2D(2500.0, 2000.0)) < 900.0)
		{
			AddError(TEXT("Punto dentro de la exclusión con huella"));
			break;
		}
	}

	// Negativos: probabilidad nula y celda no válida.
	Grid.Probability = 0.0;
	TestEqual(TEXT("Probabilidad 0: sin puntos"), JitteredGrid(Grid, Area, {}, 11).Num(), 0);
	Grid.Probability = 1.0;
	Grid.CellSize = 0.0;
	TestEqual(TEXT("Celda nula: sin puntos"), JitteredGrid(Grid, Area, {}, 11).Num(), 0);

	const FScatterPoint Probe{ FVector2D::ZeroVector, 1234u };
	TestTrue(TEXT("Random en [0, 1)"), Random(Probe, 0) >= 0.0 && Random(Probe, 0) < 1.0);
	TestNotEqual(TEXT("Random depende del índice"), Random(Probe, 0), Random(Probe, 1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalScatterDeterminismTest, "LiminalPortals.World.Scatter.Determinism", LiminalScatterMathTests::ScatterTestFlags)
bool FLiminalScatterDeterminismTest::RunTest(const FString& Parameters)
{
	using namespace LiminalScatterMath;
	using namespace LiminalScatterMathTests;
	for (const ELiminalBiomeType Type : ScatterTypes)
	{
		const int32 BiomeSeed = 1001 + static_cast<int32>(Type);
		const uint32 First = HashScatter(BuildLikeBiome(Type, BiomeSeed));
		const uint32 Second = HashScatter(BuildLikeBiome(Type, BiomeSeed));
		const uint32 Other = HashScatter(BuildLikeBiome(Type, BiomeSeed + 100));
		TestEqual(FString::Printf(TEXT("Tipo %s: misma semilla, mismo hash"), *TypeName(Type)), First, Second);
		TestNotEqual(FString::Printf(TEXT("Tipo %s: semilla distinta, hash distinto"), *TypeName(Type)), First, Other);
	}
	// Negativo del hash: mover una instancia 1 mm lo cambia.
	FBiomeScatter Scatter;
	Scatter.Get(ELiminalScatterCategory::Bush).Add(FTransform(FVector(100.0, 0.0, 0.0)));
	const uint32 Before = HashScatter(Scatter);
	Scatter.Get(ELiminalScatterCategory::Bush)[0].SetLocation(FVector(100.1, 0.0, 0.0));
	TestNotEqual(TEXT("El hash detecta 1 mm"), Before, HashScatter(Scatter));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalScatterExclusionTest, "LiminalPortals.World.Scatter.DoorExclusion", LiminalScatterMathTests::ScatterTestFlags)
bool FLiminalScatterExclusionTest::RunTest(const FString& Parameters)
{
	using namespace LiminalScatterMath;
	using namespace LiminalScatterMathTests;
	const TArray<FExclusionCapsule> Exclusions = MakeDoorExclusions();
	TestTrue(TEXT("La puerta está excluida"), IsExcluded(Exclusions, FVector2D::ZeroVector));
	TestTrue(TEXT("El camino de salida a 30 m está excluido"), IsExcluded(Exclusions, FVector2D(3000.0, 300.0)));
	TestFalse(TEXT("A 20 m de lado no hay exclusión"), IsExcluded(Exclusions, FVector2D(1500.0, 2000.0)));
	TestTrue(TEXT("La huella amplía la exclusión"), IsExcluded(Exclusions, FVector2D(1500.0, 750.0), 100.0) && !IsExcluded(Exclusions, FVector2D(1500.0, 750.0)));

	for (const ELiminalBiomeType Type : ScatterTypes)
	{
		const int32 BiomeSeed = 1001 + static_cast<int32>(Type);
		TestEqual(FString::Printf(TEXT("Tipo %s: ninguna instancia dentro del radio de exclusión"), *TypeName(Type)),
			CountInside(BuildLikeBiome(Type, BiomeSeed), Exclusions), 0);
		// Negativo: sin exclusiones, la receta sí pone instancias en esa zona (el filtro es el que actúa).
		TestTrue(FString::Printf(TEXT("Tipo %s: sin exclusiones hay instancias en la zona"), *TypeName(Type)),
			CountInside(BuildLikeBiome(Type, BiomeSeed, false), Exclusions) > 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalScatterBudgetTest, "LiminalPortals.World.Scatter.Budget", LiminalScatterMathTests::ScatterTestFlags)
bool FLiminalScatterBudgetTest::RunTest(const FString& Parameters)
{
	using namespace LiminalScatterMath;
	using namespace LiminalScatterMathTests;
	// Mínimo por tipo: el bioma no puede quedar vacío; máximo: el presupuesto orientativo del plan.
	const int32 MinInstances[] = { 5000, 500, 500, 1000 };
	const ELiminalScatterCategory Required[][4] = {
		{ ELiminalScatterCategory::TreeTrunk, ELiminalScatterCategory::TreeCrown, ELiminalScatterCategory::Bush, ELiminalScatterCategory::Undergrowth },
		{ ELiminalScatterCategory::RockLarge, ELiminalScatterCategory::RockBlock, ELiminalScatterCategory::Pebble, ELiminalScatterCategory::DryShrub },
		{ ELiminalScatterCategory::IceBlock, ELiminalScatterCategory::IcePlate, ELiminalScatterCategory::SnowMound, ELiminalScatterCategory::SnowMound },
		{ ELiminalScatterCategory::FencePost, ELiminalScatterCategory::FenceRail, ELiminalScatterCategory::HayBale, ELiminalScatterCategory::TreeTrunk },
	};
	for (int32 TypeIndex = 0; TypeIndex < static_cast<int32>(UE_ARRAY_COUNT(ScatterTypes)); ++TypeIndex)
	{
		const ELiminalBiomeType Type = ScatterTypes[TypeIndex];
		for (const int32 BiomeSeed : { 1001, 1002, 1003, 1004, 7, 42 })
		{
			const FBiomeScatter Scatter = BuildLikeBiome(Type, BiomeSeed);
			const int32 Total = Scatter.Num();
			AddInfo(FString::Printf(TEXT("Tipo %s, semilla %d: %d instancias"), *TypeName(Type), BiomeSeed, Total));
			TestTrue(FString::Printf(TEXT("Tipo %s, semilla %d: %d <= %d instancias"), *TypeName(Type), BiomeSeed, Total, MaxInstancesPerBiome),
				Total <= MaxInstancesPerBiome);
			TestTrue(FString::Printf(TEXT("Tipo %s, semilla %d: al menos %d instancias"), *TypeName(Type), BiomeSeed, MinInstances[TypeIndex]),
				Total >= MinInstances[TypeIndex]);
			for (const ELiminalScatterCategory Category : Required[TypeIndex])
			{
				TestTrue(FString::Printf(TEXT("Tipo %s: categoría %d presente"), *TypeName(Type), static_cast<int32>(Category)), Scatter.Get(Category).Num() > 0);
			}
		}
	}
	// Las recetas no mezclan biomas: la selva no tiene hielo ni el desierto árboles.
	TestEqual(TEXT("Selva sin hielo"), BuildLikeBiome(ELiminalBiomeType::Jungle, 1001).Get(ELiminalScatterCategory::IceBlock).Num(), 0);
	TestEqual(TEXT("Desierto sin árboles"), BuildLikeBiome(ELiminalBiomeType::Desert, 1002).Get(ELiminalScatterCategory::TreeTrunk).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalScatterCategoriesTest, "LiminalPortals.World.Scatter.Categories", LiminalScatterMathTests::ScatterTestFlags)
bool FLiminalScatterCategoriesTest::RunTest(const FString& Parameters)
{
	using namespace LiminalScatterMath;
	int32 Colliding = 0;
	for (int32 Index = 0; Index < LiminalBiomeTypes::NumScatterCategories; ++Index)
	{
		Colliding += HasCollision(static_cast<ELiminalScatterCategory>(Index)) ? 1 : 0;
	}
	TestEqual(TEXT("Colisión solo en troncos, rocas grandes, bloques de roca y de hielo"), Colliding, 4);
	TestTrue(TEXT("Los troncos bloquean"), HasCollision(ELiminalScatterCategory::TreeTrunk));
	TestFalse(TEXT("El sotobosque no bloquea"), HasCollision(ELiminalScatterCategory::Undergrowth));
	TestFalse(TEXT("Las cercas no bloquean"), HasCollision(ELiminalScatterCategory::FenceRail));
	TestTrue(TEXT("Tronco cilíndrico"), GetCategoryShape(ELiminalScatterCategory::TreeTrunk) == ELiminalBasicShape::Cylinder);
	TestTrue(TEXT("Copa esférica"), GetCategoryShape(ELiminalScatterCategory::TreeCrown) == ELiminalBasicShape::Sphere);
	TestTrue(TEXT("Bloque de hielo cúbico"), GetCategoryShape(ELiminalScatterCategory::IceBlock) == ELiminalBasicShape::Cube);
	TestTrue(TEXT("Lo pequeño desaparece a distancia"), GetCullDistance(ELiminalScatterCategory::Pebble) > 0.0);
	TestEqual(TEXT("Los árboles no desaparecen"), GetCullDistance(ELiminalScatterCategory::TreeCrown), 0.0);
	return true;
}
