#include "Misc/AutomationTest.h"
#include "World/LiminalArtCatalog.h"
#include "World/LiminalScatterMath.h"
#include "World/LiminalTerrainMath.h"

// Tests de la dispersión procedural: determinismo, variantes, exclusión alrededor de la puerta y del
// camino, separación mínima, presupuesto de instancias y catálogo de arte. Sin using namespace global (unity build, D-22).

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
		for (const TArray<LiminalScatterMath::FScatterInstance>& Category : Scatter.Categories)
		{
			for (const LiminalScatterMath::FScatterInstance& Instance : Category)
			{
				Inside += LiminalScatterMath::IsExcluded(Exclusions, FVector2D(Instance.Transform.GetLocation())) ? 1 : 0;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalScatterVariantPickTest, "LiminalPortals.World.Scatter.VariantPick", LiminalScatterMathTests::ScatterTestFlags)
bool FLiminalScatterVariantPickTest::RunTest(const FString& Parameters)
{
	using namespace LiminalScatterMath;
	int32 Counts[5] = {};
	int32 OutOfRange = 0;
	for (int32 Index = 0; Index < 1000; ++Index)
	{
		FScatterPoint Point;
		Point.Hash = LiminalTerrainMath::HashInts(Index, 3, 77);
		const int32 Variant = PickVariant(Point, 4, 5);
		OutOfRange += (Variant < 0 || Variant >= 5) ? 1 : 0;
		Counts[FMath::Clamp(Variant, 0, 4)] += 1;
		TestEqual(TEXT("Misma semilla, misma variante"), PickVariant(Point, 4, 5), Variant);
	}
	TestEqual(TEXT("Variantes dentro del rango"), OutOfRange, 0);
	for (int32 Variant = 0; Variant < 5; ++Variant)
	{
		// Reparto casi uniforme: 200 ± 60 de 1000.
		TestTrue(FString::Printf(TEXT("Variante %d con %d puntos"), Variant, Counts[Variant]), Counts[Variant] > 140 && Counts[Variant] < 260);
	}
	// Negativos: con una variante o ninguna, siempre la 0.
	FScatterPoint Probe;
	Probe.Hash = 12345u;
	TestEqual(TEXT("Una variante"), PickVariant(Probe, 0, 1), 0);
	TestEqual(TEXT("Sin variantes"), PickVariant(Probe, 0, 0), 0);
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
	// Negativos del hash: mover una instancia 1 mm o cambiar su variante lo cambia.
	FBiomeScatter Scatter;
	Scatter.Get(ELiminalScatterCategory::JungleBush).Add({ FTransform(FVector(100.0, 0.0, 0.0)), 0 });
	const uint32 Before = HashScatter(Scatter);
	Scatter.Get(ELiminalScatterCategory::JungleBush)[0].Transform.SetLocation(FVector(100.1, 0.0, 0.0));
	const uint32 Moved = HashScatter(Scatter);
	TestNotEqual(TEXT("El hash detecta 1 mm"), Before, Moved);
	Scatter.Get(ELiminalScatterCategory::JungleBush)[0].Variant = 1;
	TestNotEqual(TEXT("El hash detecta la variante"), Moved, HashScatter(Scatter));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalScatterAllVariantsTest, "LiminalPortals.World.Scatter.AllVariantsAppear", LiminalScatterMathTests::ScatterTestFlags)
bool FLiminalScatterAllVariantsTest::RunTest(const FString& Parameters)
{
	using namespace LiminalScatterMath;
	using namespace LiminalScatterMathTests;
	// Con las semillas de L_Hub (1001-1004), cada categoría que aparece en un bioma usa todas sus variantes.
	for (const ELiminalBiomeType Type : ScatterTypes)
	{
		const int32 BiomeSeed = 1001 + static_cast<int32>(Type);
		const FBiomeScatter Scatter = BuildLikeBiome(Type, BiomeSeed);
		for (int32 Index = 0; Index < LiminalBiomeTypes::NumScatterCategories; ++Index)
		{
			const ELiminalScatterCategory Category = static_cast<ELiminalScatterCategory>(Index);
			if (Scatter.Get(Category).IsEmpty())
			{
				continue;
			}
			const TArray<int32> Counts = CountVariants(Scatter, Category);
			FString Line;
			int32 Missing = 0;
			for (int32 Variant = 0; Variant < Counts.Num(); ++Variant)
			{
				Line += FString::Printf(TEXT(" %d"), Counts[Variant]);
				Missing += Counts[Variant] == 0 ? 1 : 0;
			}
			AddInfo(FString::Printf(TEXT("Tipo %s, categoría %d:%s"), *TypeName(Type), Index, *Line));
			TestEqual(FString::Printf(TEXT("Tipo %s, categoría %d: todas las variantes aparecen"), *TypeName(Type), Index), Missing, 0);
		}
	}
	// Negativo: CountVariants no cuenta variantes fuera del catálogo.
	FBiomeScatter Fake;
	Fake.Get(ELiminalScatterCategory::Barn).Add({ FTransform::Identity, 3 });
	const TArray<int32> Counts = CountVariants(Fake, ELiminalScatterCategory::Barn);
	TestTrue(TEXT("Una variante de granero sin instancias válidas"), Counts.Num() == 1 && Counts[0] == 0);
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
	TestNearlyEqual(TEXT("Cercanía 1 en el borde"), EdgeCloseness(Exclusions, FVector2D(0.0, 1000.0), 1500.0), 1.0, 1e-6);
	TestNearlyEqual(TEXT("Cercanía 0 lejos"), EdgeCloseness(Exclusions, FVector2D(0.0, 5000.0), 1500.0), 0.0, 1e-6);
	TestNearlyEqual(TEXT("Cercanía 0 sin exclusiones"), EdgeCloseness({}, FVector2D(0.0, 1000.0), 1500.0), 0.0, 1e-6);

	int32 InsideWithout = 0;
	for (const ELiminalBiomeType Type : ScatterTypes)
	{
		const int32 BiomeSeed = 1001 + static_cast<int32>(Type);
		TestEqual(FString::Printf(TEXT("Tipo %s: ninguna instancia dentro del radio de exclusión"), *TypeName(Type)),
			CountInside(BuildLikeBiome(Type, BiomeSeed), Exclusions), 0);
		// Negativo: sin exclusiones, la receta sí pone instancias en la zona (el filtro es el que actúa). La
		// Antártida es demasiado rala para garantizarlo por tipo: se exige en la suma.
		InsideWithout += CountInside(BuildLikeBiome(Type, BiomeSeed, false), Exclusions);
	}
	TestTrue(FString::Printf(TEXT("Sin exclusiones hay instancias en la zona (%d)"), InsideWithout), InsideWithout > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalScatterBudgetTest, "LiminalPortals.World.Scatter.Budget", LiminalScatterMathTests::ScatterTestFlags)
bool FLiminalScatterBudgetTest::RunTest(const FString& Parameters)
{
	using namespace LiminalScatterMath;
	using namespace LiminalScatterMathTests;
	using ECat = ELiminalScatterCategory;
	// Mínimo por tipo: el bioma no puede quedar vacío; máximo: el presupuesto orientativo del plan.
	const int32 MinInstances[] = { 5000, 500, 500, 1500 };
	const TArray<ECat> Required[] = {
		{ ECat::JungleTree, ECat::JungleBush, ECat::JungleFern, ECat::JungleFlower, ECat::JungleLog },
		{ ECat::DesertMesa, ECat::DesertArch, ECat::DesertRock, ECat::DesertPebble, ECat::Cactus, ECat::DryShrub },
		{ ECat::IceHorizon, ECat::PressureRidge, ECat::IceBlock, ECat::IceShard, ECat::SnowMound, ECat::Outcrop },
		{ ECat::FenceSection, ECat::FencePost, ECat::HayBale, ECat::Scarecrow, ECat::Barn, ECat::FarmTree, ECat::FarmBush, ECat::FarmRock },
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
			for (const ECat Category : Required[TypeIndex])
			{
				TestTrue(FString::Printf(TEXT("Tipo %s, semilla %d: categoría %d presente"), *TypeName(Type), BiomeSeed, static_cast<int32>(Category)),
					Scatter.Get(Category).Num() > 0);
			}
		}
	}
	// Los cultivos de L_Hub: al menos dos de trigo, maíz y filas verdes cerca de la puerta.
	const FBiomeScatter Farm = BuildLikeBiome(ELiminalBiomeType::Farmland, 1004);
	const int32 CropKinds = (Farm.Get(ECat::Wheat).Num() > 0 ? 1 : 0) + (Farm.Get(ECat::Corn).Num() > 0 ? 1 : 0) + (Farm.Get(ECat::CropRow).Num() > 0 ? 1 : 0);
	AddInfo(FString::Printf(TEXT("Cultivos 1004: trigo %d, maíz %d, filas %d"), Farm.Get(ECat::Wheat).Num(), Farm.Get(ECat::Corn).Num(), Farm.Get(ECat::CropRow).Num()));
	TestTrue(TEXT("Dos cultivos instanciados como mínimo"), CropKinds >= 2);
	// Las recetas no mezclan biomas: la selva no tiene hielo ni el desierto árboles.
	TestEqual(TEXT("Selva sin hielo"), BuildLikeBiome(ELiminalBiomeType::Jungle, 1001).Get(ECat::IceBlock).Num(), 0);
	TestEqual(TEXT("Desierto sin árboles"), BuildLikeBiome(ELiminalBiomeType::Desert, 1002).Get(ECat::JungleTree).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalArtCatalogTest, "LiminalPortals.World.Scatter.ArtCatalog", LiminalScatterMathTests::ScatterTestFlags)
bool FLiminalArtCatalogTest::RunTest(const FString& Parameters)
{
	using namespace LiminalArtCatalog;
	using ECat = ELiminalScatterCategory;
	int32 Empty = 0;
	int32 BadPaths = 0;
	TSet<FString> Seen;
	int32 Repeated = 0;
	for (int32 Index = 0; Index < LiminalBiomeTypes::NumScatterCategories; ++Index)
	{
		const TConstArrayView<const TCHAR*> Names = GetVariantNames(static_cast<ECat>(Index));
		Empty += Names.IsEmpty() ? 1 : 0;
		for (const TCHAR* Name : Names)
		{
			BadPaths += MakeMeshObjectPath(Name).IsEmpty() ? 1 : 0;
			Repeated += Seen.Contains(Name) ? 1 : 0;
			Seen.Add(Name);
		}
	}
	TestEqual(TEXT("Toda categoría tiene al menos una variante"), Empty, 0);
	TestEqual(TEXT("Nombres con la convención SM_<Kit>_<Cosa>_<NN>"), BadPaths, 0);
	TestEqual(TEXT("Una malla no está en dos categorías"), Repeated, 0);
	TestEqual(TEXT("Ruta deducida del nombre"), MakeMeshObjectPath(TEXT("SM_Ice_Block_01")), FString(TEXT("/Game/Art/Ice/SM_Ice_Block_01.SM_Ice_Block_01")));
	TestTrue(TEXT("Nombre fuera de la convención"), MakeMeshObjectPath(TEXT("Cube")).IsEmpty() && MakeMeshObjectPath(TEXT("SM_X")).IsEmpty());
	TestEqual(TEXT("Mallas del catálogo (dispersión, hub y paredes)"), GetAllMeshNames().Num(), Seen.Num() + NumHubPieces + 4);
	TestEqual(TEXT("Pared de vuelta del desierto"), FString(GetReturnWallName(ELiminalBiomeType::Desert)), FString(TEXT("SM_ReturnWall_Sand_01")));

	// Colisión solo en troncos, rocas, hielo, hitos y granero; lo pequeño desaparece a distancia.
	TestTrue(TEXT("Los árboles bloquean"), HasCollision(ECat::JungleTree) && HasCollision(ECat::FarmTree));
	TestTrue(TEXT("Rocas, bloques de hielo y granero bloquean"), HasCollision(ECat::DesertRock) && HasCollision(ECat::IceBlock) && HasCollision(ECat::Barn));
	TestFalse(TEXT("El follaje no bloquea"), HasCollision(ECat::JungleFern) || HasCollision(ECat::JungleFlower) || HasCollision(ECat::Wheat) || HasCollision(ECat::JungleBush));
	TestTrue(TEXT("Lo pequeño desaparece a distancia"), GetCullDistance(ECat::JungleFern) > 0.0 && GetCullDistance(ECat::Wheat) > 0.0);
	TestEqual(TEXT("Los hitos no desaparecen"), GetCullDistance(ECat::IceHorizon), 0.0);
	TestEqual(TEXT("Los árboles no desaparecen"), GetCullDistance(ECat::JungleTree), 0.0);
	return true;
}
