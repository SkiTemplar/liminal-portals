#include "World/LiminalScatterMath.h"

// Namespace con nombre propio (D-22): el unity build junta todos los .cpp del módulo.
namespace LiminalScatterMathDetail
{
	using namespace LiminalScatterMath;
	using LiminalTerrainMath::FFarmGrid;

	constexpr int32 SaltPointHash = static_cast<int32>(0x9E3779B9u);
	constexpr int32 SaltRandom = 0x2545F491;
	constexpr int32 SaltCluster = 0x3C6EF372;
	constexpr int32 SaltFence = 0x5BD1E995;
	constexpr int32 SaltBale = 0x68E31DA4;
	constexpr int32 SaltFarmTree = 0x1B873593;

	/** Separación de los postes de las cercas y alturas de los dos travesaños, en cm. */
	constexpr double FencePostSpacing = 400.0;
	constexpr double FenceInset = 60.0;
	constexpr double FencePostHeight = 130.0;
	constexpr double FenceRailHeights[2] = { 50.0, 100.0 };

	double Lerp(double A, double B, double T)
	{
		return A + (B - A) * T;
	}

	double Symmetric(double Value01, double Amplitude)
	{
		return (Value01 - 0.5) * 2.0 * Amplitude;
	}

	/** Forma básica de 100 cm escalada a SizeCm, centrada en Center. */
	FTransform MakeShape(const FVector& Center, const FRotator& Rotation, const FVector& SizeCm)
	{
		return FTransform(Rotation.Quaternion(), Center, SizeCm / 100.0);
	}

	void Add(FBiomeScatter& Out, ELiminalScatterCategory Category, const FTransform& Transform)
	{
		Out.Get(Category).Add(Transform);
	}

	/** Árbol: tronco cilíndrico ligeramente inclinado y una o dos copas esféricas achatadas. */
	void AddTree(FBiomeScatter& Out, const FScatterPoint& Point, double Ground, const FVector2D& HeightRange, const FVector2D& CrownRange)
	{
		const double Height = Lerp(HeightRange.X, HeightRange.Y, Random(Point, 0));
		const double Diameter = Lerp(35.0, 70.0, Random(Point, 1)) * FMath::Clamp(Height / 1200.0, 0.7, 1.3);
		const double Yaw = 360.0 * Random(Point, 2);
		const FRotator Lean(Symmetric(Random(Point, 3), 3.0), Yaw, Symmetric(Random(Point, 4), 3.0));
		constexpr double Buried = 30.0;
		const FVector TrunkCenter(Point.Location.X, Point.Location.Y, Ground + Height * 0.5 - Buried);
		Add(Out, ELiminalScatterCategory::TreeTrunk, MakeShape(TrunkCenter, Lean, FVector(Diameter, Diameter, Height)));

		const FVector Top = TrunkCenter + Lean.RotateVector(FVector(0.0, 0.0, Height * 0.5));
		const double Crown = Lerp(CrownRange.X, CrownRange.Y, Random(Point, 5));
		const double Squash = Lerp(0.55, 0.8, Random(Point, 6));
		Add(Out, ELiminalScatterCategory::TreeCrown, MakeShape(Top + FVector(0.0, 0.0, Crown * Squash * 0.2), FRotator(0.0, Yaw, 0.0),
			FVector(Crown, Crown * Lerp(0.85, 1.0, Random(Point, 7)), Crown * Squash)));

		if (Random(Point, 8) < 0.5)
		{
			const double Second = Crown * 0.65;
			const FVector Side = FRotator(0.0, Yaw + 90.0, 0.0).Vector() * (0.3 * Crown);
			Add(Out, ELiminalScatterCategory::TreeCrown, MakeShape(Top - FVector(0.0, 0.0, 0.22 * Height) + Side, FRotator(0.0, Yaw, 0.0),
				FVector(Second, Second, Second * Squash)));
		}
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

	void BuildJungle(const FScatterParams& Params, FHeightFunction HeightAt, FBiomeScatter& Out)
	{
		for (const FScatterPoint& Point : Layer(Params, MakeLayer(650.0, 0.62, 300.0, 7000.0, 0.25, 0.55), 0))
		{
			AddTree(Out, Point, HeightAt(Point.Location.X, Point.Location.Y), FVector2D(900.0, 1600.0), FVector2D(420.0, 760.0));
		}
		for (const FScatterPoint& Point : Layer(Params, MakeLayer(500.0, 0.45, 120.0, 5000.0, 0.2, 0.6), 1))
		{
			const double Width = Lerp(120.0, 260.0, Random(Point, 0));
			const double Height = Width * Lerp(0.55, 0.85, Random(Point, 1));
			const FVector Center(Point.Location.X, Point.Location.Y, HeightAt(Point.Location.X, Point.Location.Y) + Height * 0.3);
			Add(Out, ELiminalScatterCategory::Bush, MakeShape(Center, FRotator(0.0, 360.0 * Random(Point, 2), 0.0),
				FVector(Width, Width * Lerp(0.8, 1.0, Random(Point, 3)), Height)));
		}
		for (const FScatterPoint& Point : Layer(Params, MakeLayer(300.0, 0.32, 60.0), 2))
		{
			const double Height = Lerp(60.0, 150.0, Random(Point, 0));
			const double Width = Lerp(50.0, 120.0, Random(Point, 1));
			const FVector Center(Point.Location.X, Point.Location.Y, HeightAt(Point.Location.X, Point.Location.Y) + Height * 0.5 - 5.0);
			const FRotator Tilt(Symmetric(Random(Point, 2), 10.0), 360.0 * Random(Point, 3), Symmetric(Random(Point, 4), 10.0));
			Add(Out, ELiminalScatterCategory::Undergrowth, MakeShape(Center, Tilt, FVector(Width, Width, Height)));
		}
	}

	FRotator RandomTilt(const FScatterPoint& Point, int32 FirstIndex, double MaxTilt)
	{
		return FRotator(Symmetric(Random(Point, FirstIndex), MaxTilt), 360.0 * Random(Point, FirstIndex + 1),
			Symmetric(Random(Point, FirstIndex + 2), MaxTilt));
	}

	void BuildDesert(const FScatterParams& Params, FHeightFunction HeightAt, FBiomeScatter& Out)
	{
		// Rocas erosionadas: esferas aplastadas medio enterradas, a veces con una compañera más pequeña.
		for (const FScatterPoint& Point : Layer(Params, MakeLayer(1800.0, 0.5, 400.0, 9000.0, 0.3, 0.65), 0))
		{
			const double Width = Lerp(250.0, 700.0, Random(Point, 0));
			const double Height = Lerp(150.0, 400.0, Random(Point, 1));
			const FVector2D L = Point.Location;
			const FVector Center(L.X, L.Y, HeightAt(L.X, L.Y) + Height * 0.25);
			Add(Out, ELiminalScatterCategory::RockLarge, MakeShape(Center, RandomTilt(Point, 2, 12.0),
				FVector(Width, Width * Lerp(0.6, 1.0, Random(Point, 5)), Height)));
			if (Random(Point, 6) < 0.4)
			{
				const FVector2D Offset = FVector2D(FRotator(0.0, 360.0 * Random(Point, 7), 0.0).Vector()) * (0.6 * Width);
				const FVector2D C = L + Offset;
				Add(Out, ELiminalScatterCategory::RockLarge, MakeShape(FVector(C.X, C.Y, HeightAt(C.X, C.Y) + Height * 0.12),
					RandomTilt(Point, 8, 15.0), FVector(Width * 0.5, Width * 0.4, Height * 0.5)));
			}
		}
		// Bloques: cubos aplastados y girados, la mitad con un casquete esférico gastado encima.
		for (const FScatterPoint& Point : Layer(Params, MakeLayer(2200.0, 0.4, 350.0, 9000.0, 0.3, 0.65), 1))
		{
			const FVector Size(Lerp(150.0, 500.0, Random(Point, 0)), Lerp(100.0, 400.0, Random(Point, 1)), Lerp(80.0, 300.0, Random(Point, 2)));
			const FVector2D L = Point.Location;
			const FRotator Rotation = RandomTilt(Point, 3, 18.0);
			const FVector Center(L.X, L.Y, HeightAt(L.X, L.Y) + Size.Z * 0.3);
			Add(Out, ELiminalScatterCategory::RockBlock, MakeShape(Center, Rotation, Size));
			if (Random(Point, 6) < 0.5)
			{
				const FVector Cap = Center + Rotation.RotateVector(FVector(0.0, 0.0, Size.Z * 0.5));
				Add(Out, ELiminalScatterCategory::RockLarge, MakeShape(Cap, Rotation, FVector(Size.X * 0.9, Size.Y * 0.9, Size.Z * 0.35)));
			}
		}
		for (const FScatterPoint& Point : Layer(Params, MakeLayer(600.0, 0.25, 30.0), 2))
		{
			const double Size = Lerp(20.0, 80.0, Random(Point, 0));
			const FVector Center(Point.Location.X, Point.Location.Y, HeightAt(Point.Location.X, Point.Location.Y) + Size * 0.12);
			Add(Out, ELiminalScatterCategory::Pebble, MakeShape(Center, RandomTilt(Point, 1, 20.0), FVector(Size, Size * 0.8, Size * 0.5)));
		}
		for (const FScatterPoint& Point : Layer(Params, MakeLayer(1200.0, 0.25, 80.0), 3))
		{
			const double Width = Lerp(60.0, 140.0, Random(Point, 0));
			const FVector Center(Point.Location.X, Point.Location.Y, HeightAt(Point.Location.X, Point.Location.Y) + Width * 0.2);
			Add(Out, ELiminalScatterCategory::DryShrub, MakeShape(Center, FRotator(0.0, 360.0 * Random(Point, 1), 0.0),
				FVector(Width, Width * 0.9, Width * 0.6)));
		}
	}

	void BuildAntarctic(const FScatterParams& Params, FHeightFunction HeightAt, FBiomeScatter& Out)
	{
		for (const FScatterPoint& Point : Layer(Params, MakeLayer(1400.0, 0.4, 300.0, 8000.0, 0.3, 0.65), 0))
		{
			const double Width = Lerp(100.0, 450.0, Random(Point, 0));
			const FVector Size(Width, Width * Lerp(0.6, 1.0, Random(Point, 1)), Width * Lerp(0.5, 1.1, Random(Point, 2)));
			const FVector Center(Point.Location.X, Point.Location.Y, HeightAt(Point.Location.X, Point.Location.Y) + Size.Z * 0.3);
			Add(Out, ELiminalScatterCategory::IceBlock, MakeShape(Center, RandomTilt(Point, 3, 25.0), Size));
		}
		for (const FScatterPoint& Point : Layer(Params, MakeLayer(1300.0, 0.3, 450.0, 8000.0, 0.3, 0.65), 1))
		{
			const FVector Size(Lerp(300.0, 900.0, Random(Point, 0)), Lerp(200.0, 600.0, Random(Point, 1)), Lerp(25.0, 60.0, Random(Point, 2)));
			const FVector Center(Point.Location.X, Point.Location.Y, HeightAt(Point.Location.X, Point.Location.Y) + 5.0);
			Add(Out, ELiminalScatterCategory::IcePlate, MakeShape(Center, RandomTilt(Point, 3, 12.0), Size));
		}
		for (const FScatterPoint& Point : Layer(Params, MakeLayer(900.0, 0.35, 300.0), 2))
		{
			const double Width = Lerp(200.0, 700.0, Random(Point, 0));
			const double Height = Width * Lerp(0.25, 0.4, Random(Point, 1));
			const FVector Center(Point.Location.X, Point.Location.Y, HeightAt(Point.Location.X, Point.Location.Y) - Height * 0.1);
			Add(Out, ELiminalScatterCategory::SnowMound, MakeShape(Center, FRotator(0.0, 360.0 * Random(Point, 2), 0.0),
				FVector(Width, Width * Lerp(0.7, 1.0, Random(Point, 3)), Height)));
		}
	}

	/** Cerca recta de A a B: postes cada FencePostSpacing y dos travesaños entre postes consecutivos válidos. */
	void AddFence(const FScatterParams& Params, FHeightFunction HeightAt, const FVector2D& A, const FVector2D& B, FBiomeScatter& Out)
	{
		const double Length = FVector2D::Distance(A, B);
		if (Length < FencePostSpacing)
		{
			return;
		}
		const int32 NumPosts = FMath::FloorToInt32(Length / FencePostSpacing) + 1;
		const FVector2D Direction = (B - A) / Length;
		const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));
		bool bPreviousValid = false;
		FVector Previous = FVector::ZeroVector;
		for (int32 Index = 0; Index < NumPosts; ++Index)
		{
			const FVector2D P = A + Direction * (Index * FencePostSpacing);
			const bool bValid = Params.Area.IsInside(P) && !IsExcluded(Params.Exclusions, P, 20.0);
			const FVector Base(P.X, P.Y, HeightAt(P.X, P.Y));
			if (bValid)
			{
				Add(Out, ELiminalScatterCategory::FencePost, MakeShape(Base + FVector(0.0, 0.0, FencePostHeight * 0.5 - 10.0),
					FRotator(0.0, Yaw, 0.0), FVector(12.0, 12.0, FencePostHeight)));
			}
			if (bValid && bPreviousValid)
			{
				const FVector Span = Base - Previous;
				const double SpanLength = Span.Size();
				const double Pitch = FMath::RadiansToDegrees(FMath::Atan2(Span.Z, FVector2D(Span).Size()));
				for (const double RailHeight : FenceRailHeights)
				{
					const FVector Center = (Base + Previous) * 0.5 + FVector(0.0, 0.0, RailHeight);
					Add(Out, ELiminalScatterCategory::FenceRail, MakeShape(Center, FRotator(Pitch, Yaw, 0.0), FVector(SpanLength, 5.0, 8.0)));
				}
			}
			bPreviousValid = bValid;
			Previous = Base;
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
		const double Inset = Farm.RoadWidth * 0.5 + FenceInset;

		for (int32 IX = MinIX; IX <= MaxIX; ++IX)
		{
			for (int32 IY = MinIY; IY <= MaxIY; ++IY)
			{
				const FVector2D Min(Farm.OriginX + IX * Size.X + Inset, Farm.OriginY + IY * Size.Y + Inset);
				const FVector2D Max(Farm.OriginX + (IX + 1) * Size.X - Inset, Farm.OriginY + (IY + 1) * Size.Y - Inset);
				const FVector2D Corners[4] = { Min, FVector2D(Max.X, Min.Y), Max, FVector2D(Min.X, Max.Y) };
				for (int32 Side = 0; Side < 4; ++Side)
				{
					if (LiminalTerrainMath::Hash01(IX * 4 + Side, IY, Params.Seed ^ SaltFence) < 0.4)
					{
						AddFence(Params, HeightAt, Corners[Side], Corners[(Side + 1) % 4], Out);
					}
				}

				// Pacas de heno (cilindros tumbados) en las parcelas de trigo.
				if (LiminalTerrainMath::GetParcelCrop(IX, IY, Params.TerrainSeed) != 0)
				{
					continue;
				}
				const int32 NumBales = static_cast<int32>(LiminalTerrainMath::Hash01(IX, IY, Params.Seed ^ SaltBale) * 7.0);
				for (int32 Bale = 0; Bale < NumBales; ++Bale)
				{
					FScatterPoint Point;
					Point.Hash = LiminalTerrainMath::HashInts(IX * 16 + Bale, IY, Params.Seed ^ SaltBale);
					Point.Location = FVector2D(Lerp(Min.X + 300.0, Max.X - 300.0, Random(Point, 0)), Lerp(Min.Y + 300.0, Max.Y - 300.0, Random(Point, 1)));
					if (!Params.Area.IsInside(Point.Location) || IsExcluded(Params.Exclusions, Point.Location, 100.0))
					{
						continue;
					}
					const FVector Center(Point.Location.X, Point.Location.Y, HeightAt(Point.Location.X, Point.Location.Y) + 70.0);
					Add(Out, ELiminalScatterCategory::HayBale, MakeShape(Center, FRotator(0.0, 360.0 * Random(Point, 2), 90.0), FVector(150.0, 150.0, 120.0)));
				}
			}
		}

		// Árboles aislados en el horizonte, lejos de la puerta y fuera de los caminos.
		constexpr int32 FarmTreeCandidates = 60;
		constexpr int32 MaxFarmTrees = 14;
		constexpr double MinFarmTreeDistance = 9000.0;
		int32 Trees = 0;
		for (int32 Candidate = 0; Candidate < FarmTreeCandidates && Trees < MaxFarmTrees; ++Candidate)
		{
			FScatterPoint Point;
			Point.Hash = LiminalTerrainMath::HashInts(Candidate, 0, Params.Seed ^ SaltFarmTree);
			Point.Location = FVector2D(Lerp(Params.Area.Min.X, Params.Area.Max.X, Random(Point, 10)), Lerp(Params.Area.Min.Y, Params.Area.Max.Y, Random(Point, 11)));
			const LiminalTerrainMath::FFarmCell Cell = LiminalTerrainMath::GetFarmCell(Farm, Point.Location);
			if (Point.Location.Size() < MinFarmTreeDistance || Cell.DistanceToRoad < 400.0 || IsExcluded(Params.Exclusions, Point.Location, 400.0))
			{
				continue;
			}
			AddTree(Out, Point, HeightAt(Point.Location.X, Point.Location.Y), FVector2D(700.0, 1100.0), FVector2D(500.0, 800.0));
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

	double Random(const FScatterPoint& Point, int32 Index)
	{
		return static_cast<double>(LiminalTerrainMath::HashInts(Index, static_cast<int32>(Point.Hash), LiminalScatterMathDetail::SaltRandom)) / 4294967296.0;
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
		for (const TArray<FTransform>& Category : Categories)
		{
			Total += Category.Num();
		}
		return Total;
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
		// Garantía final: las piezas derivadas (segunda copa, compañeras, travesaños) tampoco tienen el
		// centro dentro de una exclusión.
		for (TArray<FTransform>& Category : Scatter.Categories)
		{
			Category.RemoveAll([&Params](const FTransform& Transform)
			{
				return IsExcluded(Params.Exclusions, FVector2D(Transform.GetLocation()));
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
			for (const FTransform& Transform : Scatter.Categories[Category])
			{
				const FVector Location = Transform.GetLocation();
				const FQuat Rotation = Transform.GetRotation();
				const FVector Scale = Transform.GetScale3D();
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

	ELiminalBasicShape GetCategoryShape(ELiminalScatterCategory Category)
	{
		switch (Category)
		{
		case ELiminalScatterCategory::TreeTrunk:
		case ELiminalScatterCategory::HayBale:
			return ELiminalBasicShape::Cylinder;
		case ELiminalScatterCategory::Undergrowth:
			return ELiminalBasicShape::Cone;
		case ELiminalScatterCategory::RockBlock:
		case ELiminalScatterCategory::IceBlock:
		case ELiminalScatterCategory::IcePlate:
		case ELiminalScatterCategory::FencePost:
		case ELiminalScatterCategory::FenceRail:
			return ELiminalBasicShape::Cube;
		default:
			return ELiminalBasicShape::Sphere;
		}
	}

	bool HasCollision(ELiminalScatterCategory Category)
	{
		return Category == ELiminalScatterCategory::TreeTrunk || Category == ELiminalScatterCategory::RockLarge
			|| Category == ELiminalScatterCategory::RockBlock || Category == ELiminalScatterCategory::IceBlock;
	}

	double GetCullDistance(ELiminalScatterCategory Category)
	{
		switch (Category)
		{
		case ELiminalScatterCategory::Undergrowth:
			return 7000.0;
		case ELiminalScatterCategory::Pebble:
			return 5000.0;
		case ELiminalScatterCategory::DryShrub:
			return 9000.0;
		case ELiminalScatterCategory::Bush:
			return 15000.0;
		case ELiminalScatterCategory::FencePost:
		case ELiminalScatterCategory::FenceRail:
			return 20000.0;
		default:
			return 0.0;
		}
	}
}
