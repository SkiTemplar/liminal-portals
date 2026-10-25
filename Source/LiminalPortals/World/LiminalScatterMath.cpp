#include "World/LiminalScatterMath.h"

#include "World/LiminalArtCatalog.h"

// Namespace con nombre propio (D-22): el unity build junta todos los .cpp del módulo.
namespace LiminalScatterMathDetail
{
	using namespace LiminalScatterMath;
	using LiminalTerrainMath::FFarmGrid;
	using ECat = ELiminalScatterCategory;

	constexpr int32 SaltPointHash = static_cast<int32>(0x9E3779B9u);
	constexpr int32 SaltRandom = 0x2545F491;
	constexpr int32 SaltCluster = 0x3C6EF372;
	constexpr int32 SaltFence = 0x5BD1E995;
	constexpr int32 SaltBale = 0x68E31DA4;
	constexpr int32 SaltFarmTree = 0x1B873593;
	constexpr int32 SaltDerived = 0x27D4EB2F;
	constexpr int32 SaltLandmark = 0x165667B1;
	constexpr int32 SaltCrop = 0x61C88647;

	/** Tramo de cerca de SM_Crops_FenceSection_01: 300 cm hacia −Y local tras importar (Unreal invierte Y). */
	constexpr double FenceSectionLength = 300.0;
	/** Cerca de las parcelas, hacia dentro desde el borde del camino. */
	constexpr double FenceInset = 60.0;
	/** Cercas del camino de salida a ambos lados (fuera de la cápsula de exclusión de 7 m). */
	constexpr double RoadFenceOffset = 750.0;
	constexpr double RoadFenceStart = 1100.0;
	/** Los cultivos instanciados empiezan pasada la cerca del camino. */
	constexpr double CropRoadClearance = RoadFenceOffset + 60.0;
	/** Zona con cultivos instanciados (los de más allá son el color del terreno). */
	const FBox2D CropArea(FVector2D(-1500.0, -3000.0), FVector2D(7000.0, 3000.0));
	/** Fila de SM_Crops_CropRow_01: 300 cm a lo largo de X local. */
	constexpr double CropRowLength = 300.0;

	double Lerp(double A, double B, double T)
	{
		return A + (B - A) * T;
	}

	double Symmetric(double Value01, double Amplitude)
	{
		return (Value01 - 0.5) * 2.0 * Amplitude;
	}

	/** Punto hijo con hash propio (para piezas derivadas de un punto de rejilla). */
	FScatterPoint Derive(const FScatterPoint& Parent, int32 Index, const FVector2D& Location)
	{
		FScatterPoint Point;
		Point.Hash = LiminalTerrainMath::HashInts(static_cast<int32>(Parent.Hash), Index, SaltDerived);
		Point.Location = Location;
		return Point;
	}

	/** Punto sembrado a mano (hitos): hash de la semilla y un índice. */
	FScatterPoint Seeded(int32 Seed, int32 Index, const FVector2D& Location)
	{
		FScatterPoint Point;
		Point.Hash = LiminalTerrainMath::HashInts(Index, 0, Seed ^ SaltLandmark);
		Point.Location = Location;
		return Point;
	}

	FVector2D Around(const FScatterPoint& Point, int32 FirstIndex, double MinRadius, double MaxRadius)
	{
		const double Angle = UE_DOUBLE_TWO_PI * Random(Point, FirstIndex);
		const double Radius = Lerp(MinRadius, MaxRadius, Random(Point, FirstIndex + 1));
		return Point.Location + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius;
	}

	/** Malla apoyada en el suelo (pivote en el centro de su base), hundida Sink cm. */
	FTransform Place(const FVector2D& Location, double Ground, const FRotator& Rotation, const FVector& Scale, double Sink = 0.0)
	{
		return FTransform(Rotation.Quaternion(), FVector(Location.X, Location.Y, Ground - Sink), Scale);
	}

	FRotator RandomTilt(const FScatterPoint& Point, int32 FirstIndex, double MaxTilt)
	{
		return FRotator(Symmetric(Random(Point, FirstIndex), MaxTilt), 360.0 * Random(Point, FirstIndex + 1),
			Symmetric(Random(Point, FirstIndex + 2), MaxTilt));
	}

	void Add(FBiomeScatter& Out, ECat Category, const FTransform& Transform, int32 Variant)
	{
		Out.Get(Category).Add({ Transform, Variant });
	}

	int32 VariantOf(const FScatterPoint& Point, int32 Index, ECat Category)
	{
		return PickVariant(Point, Index, LiminalArtCatalog::GetNumVariants(Category));
	}

	FJitterGrid MakeLayer(double CellSize, double Probability, double Footprint, double ClusterScale = 0.0, double Low = 0.0, double High = 1.0)
	{
		FJitterGrid Layer;
		Layer.CellSize = CellSize;
		Layer.Probability = Probability;
		Layer.Footprint = Footprint;
		Layer.ClusterScale = ClusterScale;
		Layer.ClusterLow = Low;
		Layer.ClusterHigh = High;
		return Layer;
	}

	TArray<FScatterPoint> Layer(const FScatterParams& Params, const FJitterGrid& Grid, int32 LayerIndex)
	{
		return JitteredGrid(Grid, Params.Area, Params.Exclusions, Params.Seed + 104729 * (LayerIndex + 1));
	}

	/** Pieza suelta con escala uniforme en [MinScale, MaxScale], giro libre e inclinación de hasta MaxTilt. */
	void AddLoose(FBiomeScatter& Out, ECat Category, const FScatterPoint& Point, FHeightFunction HeightAt,
		double MinScale, double MaxScale, double MaxTilt, double Sink)
	{
		const double Scale = Lerp(MinScale, MaxScale, Random(Point, 20));
		const FVector2D L = Point.Location;
		Add(Out, Category, Place(L, HeightAt(L.X, L.Y), RandomTilt(Point, 21, MaxTilt), FVector(Scale), Sink * Scale),
			VariantOf(Point, 24, Category));
	}

	void BuildJungle(const FScatterParams& Params, FHeightFunction HeightAt, FBiomeScatter& Out)
	{
		// Árboles altos en manchas (copas de 6-18 m), con algo de inclinación.
		for (const FScatterPoint& Point : Layer(Params, MakeLayer(800.0, 0.72, 300.0, 7000.0, 0.2, 0.5), 0))
		{
			AddLoose(Out, ECat::JungleTree, Point, HeightAt, 0.75, 1.2, 2.0, 25.0);
		}
		for (const FScatterPoint& Point : Layer(Params, MakeLayer(450.0, 0.45, 120.0, 5000.0, 0.2, 0.6), 1))
		{
			AddLoose(Out, ECat::JungleBush, Point, HeightAt, 0.8, 1.4, 4.0, 10.0);
		}
		// Arbustos en el borde del claro (sin modulación por manchas): el sotobosque llega hasta el camino.
		for (const FScatterPoint& Point : Layer(Params, MakeLayer(400.0, 0.9, 120.0), 5))
		{
			if (Random(Point, 9) < 0.6 * EdgeCloseness(Params.Exclusions, Point.Location, 800.0))
			{
				AddLoose(Out, ECat::JungleBush, Point, HeightAt, 0.9, 1.5, 4.0, 10.0);
			}
		}
		// Sotobosque: helechos y flores, más densos en el borde del claro de la puerta y del camino.
		for (const FScatterPoint& Point : Layer(Params, MakeLayer(320.0, 0.9, 80.0), 2))
		{
			const double Edge = EdgeCloseness(Params.Exclusions, Point.Location, 1500.0);
			if (Random(Point, 9) < 0.3 + 0.65 * Edge)
			{
				// En el borde del claro, más grandes: enmarcan el camino.
				AddLoose(Out, ECat::JungleFern, Point, HeightAt, 0.7 + 0.4 * Edge, 1.3 + 0.5 * Edge, 6.0, 5.0);
			}
		}
		for (const FScatterPoint& Point : Layer(Params, MakeLayer(450.0, 0.9, 30.0), 3))
		{
			if (Random(Point, 9) < 0.15 + 0.55 * EdgeCloseness(Params.Exclusions, Point.Location, 1200.0))
			{
				AddLoose(Out, ECat::JungleFlower, Point, HeightAt, 0.8, 1.5, 8.0, 2.0);
			}
		}
		// Troncos caídos y raíces, tumbados en cualquier dirección.
		for (const FScatterPoint& Point : Layer(Params, MakeLayer(1800.0, 0.35, 300.0), 4))
		{
			AddLoose(Out, ECat::JungleLog, Point, HeightAt, 0.8, 1.3, 3.0, 15.0);
		}
	}

	/** Roca grande con su corro de piedras y alguna roca mediana alrededor. */
	void AddRockCluster(FBiomeScatter& Out, const FScatterPoint& Point, FHeightFunction HeightAt, double AnchorScale)
	{
		const FVector2D L = Point.Location;
		Add(Out, ECat::DesertRock, Place(L, HeightAt(L.X, L.Y), RandomTilt(Point, 1, 5.0), FVector(AnchorScale), 15.0 * AnchorScale),
			VariantOf(Point, 4, ECat::DesertRock));
		const int32 Satellites = 2 + static_cast<int32>(Random(Point, 5) * 4.0);
		for (int32 Index = 0; Index < Satellites; ++Index)
		{
			const FScatterPoint Child = Derive(Point, Index, FVector2D::ZeroVector);
			const FVector2D C = Around(FScatterPoint{ L, Child.Hash }, 0, 160.0 * AnchorScale, 450.0 * AnchorScale);
			const FScatterPoint Placed{ C, Child.Hash };
			if (Random(Child, 2) < 0.3)
			{
				AddLoose(Out, ECat::DesertRock, Placed, HeightAt, 0.35, 0.7, 8.0, 10.0);
			}
			else
			{
				AddLoose(Out, ECat::DesertPebble, Placed, HeightAt, 0.7, 1.8, 15.0, 8.0);
			}
		}
	}

	/** Valor en [0, 1) para desplazar el hito Index en el eje Axis. */
	double Hash01Of(int32 Seed, int32 Index, int32 Axis)
	{
		return LiminalTerrainMath::Hash01(Index, Axis, Seed ^ SaltLandmark ^ 0x5F356495);
	}

	struct FLandmark
	{
		FVector2D Location;
		FVector Scale;
		double Yaw;
	};

	void BuildDesert(const FScatterParams& Params, FHeightFunction HeightAt, FBiomeScatter& Out)
	{
		// Hitos lejanos dentro del cono de la vista desde el hub (±12° alrededor de +X): una meseta a la
		// izquierda y el arco a la derecha, con otras dos mesetas a los lados para el resto de vistas.
		const FLandmark Mesas[] = {
			{ FVector2D(16500.0, -2900.0), FVector(6.0, 5.5, 3.0), 20.0 },
			{ FVector2D(17500.0, 9500.0), FVector(4.5, 4.5, 2.2), 75.0 },
			{ FVector2D(9000.0, -15500.0), FVector(5.0, 4.5, 2.5), 140.0 },
		};
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Mesas); ++Index)
		{
			const FLandmark& Mesa = Mesas[Index];
			const FScatterPoint Point = Seeded(Params.Seed, Index, Mesa.Location
				+ FVector2D(Symmetric(Hash01Of(Params.Seed, Index, 0), 400.0), Symmetric(Hash01Of(Params.Seed, Index, 1), 400.0)));
			const FVector2D L = Point.Location;
			Add(Out, ECat::DesertMesa, Place(L, HeightAt(L.X, L.Y), FRotator(0.0, Mesa.Yaw + Symmetric(Random(Point, 0), 20.0), 0.0),
				Mesa.Scale, 60.0), 0);
		}
		{
			// El arco mira a la puerta (el hueco atraviesa X local): se ve el cielo a través de él.
			const FScatterPoint Point = Seeded(Params.Seed, 10, FVector2D(12500.0, 2300.0));
			const FVector2D L = Point.Location;
			Add(Out, ECat::DesertArch, Place(L, HeightAt(L.X, L.Y), FRotator(0.0, Symmetric(Random(Point, 0), 12.0), 0.0), FVector(2.6), 60.0), 0);
		}
		// Un corro de rocas a media distancia en la vista desde el hub y el resto repartido en manchas.
		AddRockCluster(Out, Seeded(Params.Seed, 20, FVector2D(7000.0, -1300.0)), HeightAt, 1.3);
		AddRockCluster(Out, Seeded(Params.Seed, 21, FVector2D(4200.0, 1700.0)), HeightAt, 0.9);
		for (const FScatterPoint& Point : Layer(Params, MakeLayer(2500.0, 0.7, 500.0, 9000.0, 0.25, 0.6), 0))
		{
			AddRockCluster(Out, Point, HeightAt, Lerp(0.7, 1.6, Random(Point, 0)));
		}
		for (const FScatterPoint& Point : Layer(Params, MakeLayer(1200.0, 0.4, 150.0), 1))
		{
			AddLoose(Out, ECat::Cactus, Point, HeightAt, 0.8, 1.3, 3.0, 5.0);
			if (Random(Point, 6) < 0.25)
			{
				const FScatterPoint Child = Derive(Point, 0, Around(Point, 7, 120.0, 250.0));
				const FVector2D C = Child.Location;
				Add(Out, ECat::Cactus, Place(C, HeightAt(C.X, C.Y), RandomTilt(Child, 0, 4.0), FVector(Lerp(0.7, 1.0, Random(Child, 3))), 5.0), 2);
			}
		}
		for (const FScatterPoint& Point : Layer(Params, MakeLayer(800.0, 0.3, 60.0), 2))
		{
			AddLoose(Out, ECat::DryShrub, Point, HeightAt, 0.7, 1.3, 5.0, 3.0);
		}
		for (const FScatterPoint& Point : Layer(Params, MakeLayer(1100.0, 0.25, 30.0), 3))
		{
			AddLoose(Out, ECat::DesertPebble, Point, HeightAt, 0.6, 1.4, 15.0, 5.0);
		}
	}

	/** Bloque de hielo con uno o dos menores y un corro de esquirlas. */
	void AddIceCluster(FBiomeScatter& Out, const FScatterPoint& Point, FHeightFunction HeightAt)
	{
		AddLoose(Out, ECat::IceBlock, Point, HeightAt, 0.8, 2.0, 10.0, 10.0);
		const int32 Blocks = static_cast<int32>(Random(Point, 5) * 3.0);
		const int32 Shards = 2 + static_cast<int32>(Random(Point, 6) * 4.0);
		for (int32 Index = 0; Index < Blocks + Shards; ++Index)
		{
			const FScatterPoint Seed = Derive(Point, Index, FVector2D::ZeroVector);
			const FScatterPoint Child{ Around(FScatterPoint{ Point.Location, Seed.Hash }, 0, 100.0, Index < Blocks ? 350.0 : 400.0), Seed.Hash };
			if (Index < Blocks)
			{
				AddLoose(Out, ECat::IceBlock, Child, HeightAt, 0.5, 1.0, 12.0, 8.0);
			}
			else
			{
				AddLoose(Out, ECat::IceShard, Child, HeightAt, 0.7, 1.6, 15.0, 5.0);
			}
		}
	}

	/** Afloramiento de roca oscura (uno a tres) medio enterrado, a veces con un montículo de nieve al pie. */
	void AddOutcrop(FBiomeScatter& Out, const FScatterPoint& Point, FHeightFunction HeightAt, double MinScale, double MaxScale)
	{
		const int32 Rocks = 1 + static_cast<int32>(Random(Point, 5) * 3.0);
		for (int32 Index = 0; Index < Rocks; ++Index)
		{
			const FScatterPoint Seed = Derive(Point, Index, FVector2D::ZeroVector);
			const FScatterPoint Child{ Index == 0 ? Point.Location : Around(FScatterPoint{ Point.Location, Seed.Hash }, 0, 250.0, 600.0), Seed.Hash };
			AddLoose(Out, ECat::Outcrop, Child, HeightAt, MinScale, MaxScale, 8.0, 40.0);
		}
		if (Random(Point, 6) < 0.5)
		{
			const FScatterPoint Child = Derive(Point, 7, Around(Point, 8, 200.0, 400.0));
			AddLoose(Out, ECat::SnowMound, Child, HeightAt, 0.5, 0.9, 0.0, 5.0);
		}
	}

	void BuildAntarctic(const FScatterParams& Params, FHeightFunction HeightAt, FBiomeScatter& Out)
	{
		// Horizonte: icebergs y seracs en un abanico a 155-185 m, más densos en el centro (vista desde el
		// hub, ±12°) y unos pocos detrás de la pared. La variante va por turno: todas aparecen.
		const double Angles[] = { -62.0, -40.0, -22.0, -9.0, 4.0, 14.0, 30.0, 48.0, 66.0, 150.0, 180.0, 210.0 };
		const double Radii[] = { 17500.0, 16500.0, 18000.0, 16000.0, 17000.0, 15500.0, 18000.0, 17000.0, 16500.0, 16000.0, 17000.0, 16000.0 };
		const int32 NumHorizon = LiminalArtCatalog::GetNumVariants(ECat::IceHorizon);
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Angles); ++Index)
		{
			FScatterPoint Point = Seeded(Params.Seed, 100 + Index, FVector2D::ZeroVector);
			const double Angle = FMath::DegreesToRadians(Angles[Index] + Symmetric(Random(Point, 0), 3.0));
			const double Radius = Radii[Index] + Symmetric(Random(Point, 1), 600.0);
			Point.Location = Params.Area.GetClosestPointTo(FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
			const int32 Variant = Index % NumHorizon;
			// La cara ancha hacia la puerta: los icebergs son largos en X local y el serac en Y.
			const double Facing = FMath::RadiansToDegrees(Angle) + (Variant == 2 ? 0.0 : 90.0) + Symmetric(Random(Point, 2), 20.0);
			const double Scale = Lerp(0.85, 1.3, Random(Point, 3));
			const FVector2D L = Point.Location;
			Add(Out, ECat::IceHorizon, Place(L, HeightAt(L.X, L.Y), FRotator(0.0, Facing, 0.0), FVector(Scale), 60.0 * Scale), Variant);
		}
		for (const FScatterPoint& Point : Layer(Params, MakeLayer(6000.0, 0.35, 700.0), 0))
		{
			AddLoose(Out, ECat::PressureRidge, Point, HeightAt, 0.8, 1.4, 2.0, 15.0);
		}
		// Primer plano: una cresta de presión a un lado del camino y esquirlas y montículos en el borde del claro.
		{
			const FScatterPoint Ridge = Seeded(Params.Seed, 32, FVector2D(2600.0, -1700.0));
			const FVector2D L = Ridge.Location;
			Add(Out, ECat::PressureRidge, Place(L, HeightAt(L.X, L.Y), FRotator(0.0, 25.0 + Symmetric(Random(Ridge, 0), 10.0), 0.0), FVector(1.1), 15.0), 0);
		}
		for (const FScatterPoint& Point : Layer(Params, MakeLayer(500.0, 0.9, 60.0), 4))
		{
			const double Edge = EdgeCloseness(Params.Exclusions, Point.Location, 1000.0);
			const double Roll = Random(Point, 9);
			if (Roll < 0.25 * Edge)
			{
				AddLoose(Out, ECat::IceShard, Point, HeightAt, 0.8, 1.8, 15.0, 5.0);
			}
			else if (Roll < 0.4 * Edge)
			{
				AddLoose(Out, ECat::SnowMound, Point, HeightAt, 0.4, 0.8, 0.0, 5.0);
			}
		}
		for (const FScatterPoint& Point : Layer(Params, MakeLayer(2000.0, 0.6, 400.0, 8000.0, 0.25, 0.6), 1))
		{
			AddIceCluster(Out, Point, HeightAt);
		}
		for (const FScatterPoint& Point : Layer(Params, MakeLayer(1500.0, 0.45, 300.0), 2))
		{
			const double Width = Lerp(0.8, 1.6, Random(Point, 0));
			const FVector2D L = Point.Location;
			Add(Out, ECat::SnowMound, Place(L, HeightAt(L.X, L.Y), FRotator(0.0, 360.0 * Random(Point, 1), 0.0),
				FVector(Width, Width * Lerp(0.8, 1.2, Random(Point, 2)), Lerp(0.7, 1.3, Random(Point, 3))), 5.0), VariantOf(Point, 4, ECat::SnowMound));
		}
		// Afloramientos oscuros: dos en la vista desde el hub (contraste con la nieve) y el resto en manchas.
		AddOutcrop(Out, Seeded(Params.Seed, 30, FVector2D(6500.0, 900.0)), HeightAt, 1.0, 1.6);
		AddOutcrop(Out, Seeded(Params.Seed, 31, FVector2D(11000.0, -1800.0)), HeightAt, 1.2, 2.0);
		for (const FScatterPoint& Point : Layer(Params, MakeLayer(4500.0, 0.45, 500.0, 9000.0, 0.3, 0.7), 3))
		{
			AddOutcrop(Out, Point, HeightAt, 0.7, 1.8);
		}
	}

	/** Cerca recta de A a B con tramos de SM_Crops_FenceSection_01 estirados para encajar y un poste al final de cada tramo continuo. */
	void AddFence(const FScatterParams& Params, FHeightFunction HeightAt, const FVector2D& A, const FVector2D& B, FBiomeScatter& Out,
		TArray<FVector2D>* OutSectionStarts = nullptr)
	{
		const double Length = FVector2D::Distance(A, B);
		const int32 NumSections = FMath::FloorToInt32(Length / FenceSectionLength);
		if (NumSections < 1)
		{
			return;
		}
		const double Section = Length / NumSections;
		const FVector2D Direction = (B - A) / Length;
		// El tramo va hacia −Y local: girar −Y hasta Direction es Yaw = rumbo + 90°.
		const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X)) + 90.0;
		auto IsValid = [&Params](const FVector2D& P) { return Params.Area.IsInside(P) && !IsExcluded(Params.Exclusions, P, 20.0); };
		for (int32 Index = 0; Index < NumSections; ++Index)
		{
			const FVector2D Start = A + Direction * (Index * Section);
			const FVector2D End = Start + Direction * Section;
			if (!IsValid(Start) || !IsValid(End))
			{
				continue;
			}
			Add(Out, ECat::FenceSection, Place(Start, HeightAt(Start.X, Start.Y), FRotator(0.0, Yaw, 0.0), FVector(1.0, Section / FenceSectionLength, 1.0), 5.0), 0);
			const FVector2D Next = End + Direction * Section;
			if (Index + 1 == NumSections || !IsValid(Next))
			{
				Add(Out, ECat::FencePost, Place(End, HeightAt(End.X, End.Y), FRotator(0.0, Yaw, 0.0), FVector(1.0), 5.0), 0);
			}
			if (OutSectionStarts)
			{
				OutSectionStarts->Add(Start);
			}
		}
	}

	/** Cultivo de la parcela en filas (trigo, maíz o filas verdes) dentro de Rect; el arado se queda en el color del terreno. */
	void FillParcel(const FScatterParams& Params, FHeightFunction HeightAt, int32 IX, int32 IY, const FBox2D& Rect, FBiomeScatter& Out)
	{
		const int32 Crop = LiminalTerrainMath::GetParcelCrop(IX, IY, Params.TerrainSeed);
		if (Crop == 2 || !Rect.bIsValid)
		{
			return;
		}
		const bool bAlongX = LiminalTerrainMath::IsParcelAlongX(IX, IY, Params.TerrainSeed);
		// Separación entre filas y a lo largo de la fila, en cm.
		const double RowSpacing = Crop == 0 ? 90.0 : (Crop == 1 ? 110.0 : 110.0);
		const double Step = Crop == 0 ? 80.0 : (Crop == 1 ? 50.0 : CropRowLength + 20.0);
		const FVector2D Size = Rect.GetSize();
		const double Across = bAlongX ? Size.Y : Size.X;
		const double Along = bAlongX ? Size.X : Size.Y;
		const int32 NumRows = FMath::FloorToInt32(Across / RowSpacing);
		const int32 NumSteps = FMath::FloorToInt32(Along / Step);
		for (int32 Row = 0; Row < NumRows; ++Row)
		{
			for (int32 Index = 0; Index < NumSteps; ++Index)
			{
				FScatterPoint Point;
				Point.Hash = LiminalTerrainMath::HashInts(Row * 4096 + Index, IX * 64 + IY, Params.Seed ^ SaltCrop);
				const double U = (Index + 0.5) * Step + (Crop == 3 ? 0.0 : Symmetric(Random(Point, 0), 0.18 * Step));
				const double V = (Row + 0.5) * RowSpacing + (Crop == 3 ? 0.0 : Symmetric(Random(Point, 1), 0.15 * RowSpacing));
				Point.Location = Rect.Min + (bAlongX ? FVector2D(U, V) : FVector2D(V, U));
				if (IsExcluded(Params.Exclusions, Point.Location, 40.0))
				{
					continue;
				}
				const FVector2D L = Point.Location;
				const double Ground = HeightAt(L.X, L.Y);
				if (Crop == 0)
				{
					const double Width = Lerp(1.5, 1.9, Random(Point, 2));
					Add(Out, ECat::Wheat, Place(L, Ground, FRotator(0.0, 360.0 * Random(Point, 3), 0.0),
						FVector(Width, Width, Lerp(0.85, 1.15, Random(Point, 4))), 3.0), VariantOf(Point, 5, ECat::Wheat));
				}
				else if (Crop == 1)
				{
					AddLoose(Out, ECat::Corn, Point, HeightAt, 1.0, 1.25, 3.0, 3.0);
				}
				else
				{
					Add(Out, ECat::CropRow, Place(L, Ground, FRotator(0.0, bAlongX ? 0.0 : 90.0, 0.0),
						FVector(1.0, Lerp(0.9, 1.1, Random(Point, 2)), 1.0), 4.0), 0);
				}
			}
		}
	}

	void BuildFarmland(const FScatterParams& Params, FHeightFunction HeightAt, FBiomeScatter& Out)
	{
		const FFarmGrid& Farm = Params.Farm;
		const FVector2D Size = Farm.ParcelSize;
		const int32 MinIX = FMath::FloorToInt32((Params.Area.Min.X - Farm.OriginX) / Size.X);
		const int32 MaxIX = FMath::FloorToInt32((Params.Area.Max.X - Farm.OriginX) / Size.X);
		const int32 MinIY = FMath::FloorToInt32((Params.Area.Min.Y - Farm.OriginY) / Size.Y);
		const int32 MaxIY = FMath::FloorToInt32((Params.Area.Max.Y - Farm.OriginY) / Size.Y);
		const double FenceEdge = Farm.RoadWidth * 0.5 + FenceInset;
		const double CropEdge = Farm.RoadWidth * 0.5 + 2.0 * FenceInset;

		for (int32 IX = MinIX; IX <= MaxIX; ++IX)
		{
			for (int32 IY = MinIY; IY <= MaxIY; ++IY)
			{
				const FVector2D Cell(Farm.OriginX + IX * Size.X, Farm.OriginY + IY * Size.Y);
				const FVector2D Min = Cell + FVector2D(FenceEdge);
				const FVector2D Max = Cell + Size - FVector2D(FenceEdge);
				const FVector2D Corners[4] = { Min, FVector2D(Max.X, Min.Y), Max, FVector2D(Min.X, Max.Y) };
				for (int32 Side = 0; Side < 4; ++Side)
				{
					// Los lados que dan al camino de salida (Y = OriginY) ya tienen la cerca del camino.
					const bool bRoadSide = (Side == 0 && IY == 0) || (Side == 2 && IY == -1);
					if (!bRoadSide && LiminalTerrainMath::Hash01(IX * 4 + Side, IY, Params.Seed ^ SaltFence) < 0.4)
					{
						AddFence(Params, HeightAt, Corners[Side], Corners[(Side + 1) % 4], Out);
					}
				}

				// Cultivos instanciados solo cerca de la puerta y pasada la cerca del camino.
				FBox2D Rect(Cell + FVector2D(CropEdge), Cell + Size - FVector2D(CropEdge));
				Rect.Min = Rect.Min.ComponentMax(CropArea.Min);
				Rect.Max = Rect.Max.ComponentMin(CropArea.Max);
				if (IY == 0)
				{
					Rect.Min.Y = FMath::Max(Rect.Min.Y, CropRoadClearance);
				}
				else if (IY == -1)
				{
					Rect.Max.Y = FMath::Min(Rect.Max.Y, -CropRoadClearance);
				}
				Rect.bIsValid = Rect.Min.X < Rect.Max.X && Rect.Min.Y < Rect.Max.Y;
				FillParcel(Params, HeightAt, IX, IY, Rect, Out);

				// Balas de heno en las parcelas de trigo.
				if (LiminalTerrainMath::GetParcelCrop(IX, IY, Params.TerrainSeed) != 0)
				{
					continue;
				}
				const int32 NumBales = static_cast<int32>(LiminalTerrainMath::Hash01(IX, IY, Params.Seed ^ SaltBale) * 6.0);
				for (int32 Bale = 0; Bale < NumBales; ++Bale)
				{
					FScatterPoint Point;
					Point.Hash = LiminalTerrainMath::HashInts(IX * 16 + Bale, IY, Params.Seed ^ SaltBale);
					Point.Location = FVector2D(Lerp(Min.X + 300.0, Max.X - 300.0, Random(Point, 0)), Lerp(Min.Y + 300.0, Max.Y - 300.0, Random(Point, 1)));
					if (Params.Area.IsInside(Point.Location) && !IsExcluded(Params.Exclusions, Point.Location, 100.0))
					{
						AddLoose(Out, ECat::HayBale, Point, HeightAt, 0.9, 1.1, 0.0, 3.0);
					}
				}
			}
		}

		// Cercas a los dos lados del camino de salida, con matojos y piedras al pie de algunos tramos.
		for (const double Side : { 1.0, -1.0 })
		{
			TArray<FVector2D> Starts;
			AddFence(Params, HeightAt, FVector2D(RoadFenceStart, Side * RoadFenceOffset), FVector2D(Params.Area.Max.X, Side * RoadFenceOffset), Out, &Starts);
			for (int32 Index = 0; Index < Starts.Num(); ++Index)
			{
				FScatterPoint Point;
				Point.Hash = LiminalTerrainMath::HashInts(Index, Side > 0.0 ? 1 : 2, Params.Seed ^ SaltFence);
				Point.Location = Starts[Index] + FVector2D(Lerp(30.0, 270.0, Random(Point, 0)), Side * Lerp(50.0, 110.0, Random(Point, 1)));
				const double Roll = Random(Point, 2);
				if (Roll < 0.15)
				{
					AddLoose(Out, ECat::FarmBush, Point, HeightAt, 0.8, 1.3, 3.0, 5.0);
				}
				else if (Roll < 0.25)
				{
					AddLoose(Out, ECat::FarmRock, Point, HeightAt, 0.8, 1.6, 15.0, 5.0);
				}
			}
		}

		// Espantapájaros en la primera parcela cercana con trigo o filas verdes, mirando a la puerta.
		{
			const FVector2D Candidates[] = { FVector2D(2600.0, 1900.0), FVector2D(2600.0, -1900.0), FVector2D(6200.0, 1700.0), FVector2D(6200.0, -1700.0) };
			FVector2D Chosen = Candidates[0];
			for (const FVector2D& Candidate : Candidates)
			{
				const LiminalTerrainMath::FFarmCell CellAt = LiminalTerrainMath::GetFarmCell(Farm, Candidate);
				const int32 Crop = LiminalTerrainMath::GetParcelCrop(CellAt.IX, CellAt.IY, Params.TerrainSeed);
				if (Crop == 0 || Crop == 3)
				{
					Chosen = Candidate;
					break;
				}
			}
			const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(-Chosen.Y, -Chosen.X));
			Add(Out, ECat::Scarecrow, Place(Chosen, HeightAt(Chosen.X, Chosen.Y), FRotator(0.0, Yaw, 0.0), FVector(1.0), 10.0), 0);
		}

		// Granero en la vista desde el hub, con el portón hacia la puerta, balas al lado y un árbol.
		{
			const FScatterPoint Point = Seeded(Params.Seed, 40, FVector2D(13000.0, -2300.0));
			const FVector2D L = Point.Location;
			Add(Out, ECat::Barn, Place(L, HeightAt(L.X, L.Y), FRotator(0.0, 180.0 + Symmetric(Random(Point, 0), 6.0), 0.0), FVector(1.0), 20.0), 0);
			for (int32 Index = 0; Index < 4; ++Index)
			{
				const FScatterPoint Bale = Derive(Point, Index, L + FVector2D(-650.0 + Symmetric(Random(Point, 10 + Index), 150.0), 550.0 + 170.0 * Index));
				AddLoose(Out, ECat::HayBale, Bale, HeightAt, 0.9, 1.1, 0.0, 3.0);
			}
			const FScatterPoint Tree = Derive(Point, 9, L + FVector2D(300.0, -1150.0));
			AddLoose(Out, ECat::FarmTree, Tree, HeightAt, 1.1, 1.3, 2.0, 15.0);
		}

		// Árboles sueltos en el horizonte, lejos de la puerta y fuera de los caminos; dos en la vista desde el hub.
		AddLoose(Out, ECat::FarmTree, Seeded(Params.Seed, 50, FVector2D(17200.0, 2600.0)), HeightAt, 1.0, 1.3, 2.0, 15.0);
		AddLoose(Out, ECat::FarmTree, Seeded(Params.Seed, 51, FVector2D(15500.0, 1300.0)), HeightAt, 0.9, 1.2, 2.0, 15.0);
		constexpr int32 FarmTreeCandidates = 80;
		constexpr int32 MaxFarmTrees = 18;
		constexpr double MinFarmTreeDistance = 6000.0;
		int32 Trees = 0;
		for (int32 Candidate = 0; Candidate < FarmTreeCandidates && Trees < MaxFarmTrees; ++Candidate)
		{
			FScatterPoint Point;
			Point.Hash = LiminalTerrainMath::HashInts(Candidate, 0, Params.Seed ^ SaltFarmTree);
			Point.Location = FVector2D(Lerp(Params.Area.Min.X, Params.Area.Max.X, Random(Point, 10)), Lerp(Params.Area.Min.Y, Params.Area.Max.Y, Random(Point, 11)));
			const LiminalTerrainMath::FFarmCell CellAt = LiminalTerrainMath::GetFarmCell(Farm, Point.Location);
			if (Point.Location.Size() < MinFarmTreeDistance || CellAt.DistanceToRoad < 400.0 || IsExcluded(Params.Exclusions, Point.Location, 400.0)
				|| CropArea.IsInside(Point.Location))
			{
				continue;
			}
			AddLoose(Out, ECat::FarmTree, Point, HeightAt, 0.8, 1.3, 2.0, 15.0);
			++Trees;
		}
	}

	void HashBytes(uint32& Hash, int64 Value)
	{
		for (int32 Byte = 0; Byte < 8; ++Byte)
		{
			Hash ^= static_cast<uint32>((Value >> (Byte * 8)) & 0xff);
			Hash *= 16777619u;
		}
	}
}

namespace LiminalScatterMath
{
	TArray<FExclusionCapsule> MakeDoorExclusions()
	{
		return {
			{ FVector2D::ZeroVector, FVector2D::ZeroVector, 1000.0 },
			{ FVector2D(0.0, 0.0), FVector2D(3500.0, 0.0), 700.0 },
		};
	}

	FScatterParams MakeScatterParams(ELiminalBiomeType Type, const LiminalTerrainMath::FBiomeSeeds& Seeds,
		const FVector2D& GroundSize, double EdgeMargin)
	{
		FScatterParams Params;
		Params.Type = Type;
		Params.Seed = Seeds.Scatter;
		Params.TerrainSeed = Seeds.Terrain;
		const FVector2D HalfSize = (GroundSize * 0.5 - FVector2D(EdgeMargin)).ComponentMax(FVector2D::ZeroVector);
		Params.Area = FBox2D(-HalfSize, HalfSize);
		Params.Exclusions = MakeDoorExclusions();
		return Params;
	}

	double DistanceToExclusions(TConstArrayView<FExclusionCapsule> Exclusions, const FVector2D& Point)
	{
		double Best = TNumericLimits<double>::Max();
		for (const FExclusionCapsule& Capsule : Exclusions)
		{
			Best = FMath::Min(Best, LiminalTerrainMath::DistanceToSegment(Point, Capsule.Start, Capsule.End) - Capsule.Radius);
		}
		return Best;
	}

	bool IsExcluded(TConstArrayView<FExclusionCapsule> Exclusions, const FVector2D& Point, double Margin)
	{
		return DistanceToExclusions(Exclusions, Point) < Margin;
	}

	double EdgeCloseness(TConstArrayView<FExclusionCapsule> Exclusions, const FVector2D& Point, double Range)
	{
		if (Range <= 0.0 || Exclusions.IsEmpty())
		{
			return 0.0;
		}
		return 1.0 - LiminalTerrainMath::SmoothStep01(DistanceToExclusions(Exclusions, Point) / Range);
	}

	double Random(const FScatterPoint& Point, int32 Index)
	{
		return static_cast<double>(LiminalTerrainMath::HashInts(Index, static_cast<int32>(Point.Hash), LiminalScatterMathDetail::SaltRandom)) / 4294967296.0;
	}

	int32 PickVariant(const FScatterPoint& Point, int32 Index, int32 NumVariants)
	{
		if (NumVariants <= 1)
		{
			return 0;
		}
		return FMath::Clamp(FMath::FloorToInt32(Random(Point, Index) * NumVariants), 0, NumVariants - 1);
	}

	TArray<FScatterPoint> JitteredGrid(const FJitterGrid& Params, const FBox2D& Area, TConstArrayView<FExclusionCapsule> Exclusions, int32 Seed)
	{
		TArray<FScatterPoint> Points;
		if (Params.CellSize <= 0.0 || !Area.bIsValid)
		{
			return Points;
		}
		const FVector2D Size = Area.GetSize();
		const int32 NumX = FMath::CeilToInt32(Size.X / Params.CellSize);
		const int32 NumY = FMath::CeilToInt32(Size.Y / Params.CellSize);
		const double Jitter = FMath::Clamp(Params.Jitter, 0.0, 1.0);
		const double Inset = (1.0 - Jitter) * 0.5;
		const double ClusterSpan = FMath::Max(Params.ClusterHigh - Params.ClusterLow, UE_DOUBLE_SMALL_NUMBER);
		for (int32 IX = 0; IX < NumX; ++IX)
		{
			for (int32 IY = 0; IY < NumY; ++IY)
			{
				FScatterPoint Point;
				Point.Hash = LiminalTerrainMath::HashInts(IX, IY, Seed ^ LiminalScatterMathDetail::SaltPointHash);
				const double Chance = Random(Point, 100);
				Point.Location = Area.Min + FVector2D((IX + Inset + Jitter * Random(Point, 101)) * Params.CellSize,
					(IY + Inset + Jitter * Random(Point, 102)) * Params.CellSize);
				if (!Area.IsInside(Point.Location))
				{
					continue;
				}
				double Probability = Params.Probability;
				if (Params.ClusterScale > 0.0)
				{
					const double Noise = 0.5 + 0.5 * LiminalTerrainMath::Fbm(Point.Location.X / Params.ClusterScale,
						Point.Location.Y / Params.ClusterScale, Seed ^ LiminalScatterMathDetail::SaltCluster, 3);
					Probability *= LiminalTerrainMath::SmoothStep01((Noise - Params.ClusterLow) / ClusterSpan);
				}
				if (Chance >= Probability || IsExcluded(Exclusions, Point.Location, Params.Footprint))
				{
					continue;
				}
				Points.Add(Point);
			}
		}
		return Points;
	}

	int32 FBiomeScatter::Num() const
	{
		int32 Total = 0;
		for (const TArray<FScatterInstance>& Category : Categories)
		{
			Total += Category.Num();
		}
		return Total;
	}

	TArray<int32> CountVariants(const FBiomeScatter& Scatter, ELiminalScatterCategory Category)
	{
		TArray<int32> Counts;
		Counts.SetNumZeroed(LiminalArtCatalog::GetNumVariants(Category));
		for (const FScatterInstance& Instance : Scatter.Get(Category))
		{
			if (Counts.IsValidIndex(Instance.Variant))
			{
				++Counts[Instance.Variant];
			}
		}
		return Counts;
	}

	FBiomeScatter BuildBiomeScatter(const FScatterParams& Params, FHeightFunction HeightAt)
	{
		FBiomeScatter Scatter;
		switch (Params.Type)
		{
		case ELiminalBiomeType::Jungle:
			LiminalScatterMathDetail::BuildJungle(Params, HeightAt, Scatter);
			break;
		case ELiminalBiomeType::Desert:
			LiminalScatterMathDetail::BuildDesert(Params, HeightAt, Scatter);
			break;
		case ELiminalBiomeType::Antarctic:
			LiminalScatterMathDetail::BuildAntarctic(Params, HeightAt, Scatter);
			break;
		case ELiminalBiomeType::Farmland:
			LiminalScatterMathDetail::BuildFarmland(Params, HeightAt, Scatter);
			break;
		default:
			break;
		}
		// Garantía final: las piezas derivadas (satélites de los corros, cercas, hitos) tampoco tienen el
		// centro dentro de una exclusión.
		for (TArray<FScatterInstance>& Category : Scatter.Categories)
		{
			Category.RemoveAll([&Params](const FScatterInstance& Instance)
			{
				return IsExcluded(Params.Exclusions, FVector2D(Instance.Transform.GetLocation()));
			});
		}
		return Scatter;
	}

	uint32 HashScatter(const FBiomeScatter& Scatter)
	{
		uint32 Hash = 2166136261u;
		for (int32 Category = 0; Category < LiminalBiomeTypes::NumScatterCategories; ++Category)
		{
			LiminalScatterMathDetail::HashBytes(Hash, Category);
			LiminalScatterMathDetail::HashBytes(Hash, Scatter.Categories[Category].Num());
			for (const FScatterInstance& Instance : Scatter.Categories[Category])
			{
				const FTransform& Transform = Instance.Transform;
				const FVector Location = Transform.GetLocation();
				const FQuat Rotation = Transform.GetRotation();
				const FVector Scale = Transform.GetScale3D();
				LiminalScatterMathDetail::HashBytes(Hash, Instance.Variant);
				for (const double Value : { Location.X, Location.Y, Location.Z })
				{
					LiminalScatterMathDetail::HashBytes(Hash, FMath::RoundToInt64(Value * 100.0));
				}
				for (const double Value : { Rotation.X, Rotation.Y, Rotation.Z, Rotation.W, Scale.X, Scale.Y, Scale.Z })
				{
					LiminalScatterMathDetail::HashBytes(Hash, FMath::RoundToInt64(Value * 10000.0));
				}
			}
		}
		return Hash;
	}
}
