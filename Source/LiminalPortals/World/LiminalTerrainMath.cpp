#include "World/LiminalTerrainMath.h"

// Namespace con nombre propio (D-22): el unity build junta todos los .cpp del módulo.
namespace LiminalTerrainMathDetail
{
	/** Sales para separar los ruidos que usan la misma semilla. */
	constexpr int32 SaltRidge = 0x1F3D5B79;
	constexpr int32 SaltWarp = 0x2C6B1A43;
	constexpr int32 SaltAmplitude = 0x3A9E2F17;
	constexpr int32 SaltDetail = 0x4B8D7C25;
	constexpr int32 SaltColor = 0x5D2F9E61;
	constexpr int32 SaltFurrow = 0x6E1A4B83;
	constexpr int32 SaltCrop = 0x7F4C2D95;
	constexpr int32 SaltDune = 0x0E5EED11;

	/** Semiancho del camino de tierra pintado sobre la zona llana, en cm. */
	constexpr double PathHalfWidth = 250.0;
	constexpr double PathEdge = 200.0;

	double Quintic(double T)
	{
		return T * T * T * (T * (T * 6.0 - 15.0) + 10.0);
	}

	double Signed(double Value01)
	{
		return Value01 * 2.0 - 1.0;
	}

	FLinearColor Mix(const FLinearColor& A, const FLinearColor& B, double T)
	{
		return A + (B - A) * static_cast<float>(FMath::Clamp(T, 0.0, 1.0));
	}

	/** Mezcla (0-1) del color del camino de salida: 1 sobre el camino y 0 a más de PathHalfWidth + PathEdge. */
	double PathMix(const LiminalTerrainMath::FFlatZone& Zone, const FVector2D& Point)
	{
		const double Distance = LiminalTerrainMath::DistanceToSegment(Point, Zone.Start, Zone.End);
		return 1.0 - LiminalTerrainMath::SmoothStep01((Distance - PathHalfWidth) / PathEdge);
	}

	double JungleHeight(int32 Seed, double X, double Y)
	{
		const double Base = LiminalTerrainMath::Fbm(X / 9000.0, Y / 9000.0, Seed, 5);
		const double Ridge = 1.0 - FMath::Abs(LiminalTerrainMath::Fbm(X / 3500.0 + 17.3, Y / 3500.0 - 4.1, Seed ^ SaltRidge, 3));
		return 450.0 * Base + 300.0 * Ridge - 100.0;
	}

	/** Perfil asimétrico de duna en [0, 1]: barlovento suave hasta T = 0,72 y sotavento empinado. */
	double DuneProfile(double Phase)
	{
		const double T = Phase - FMath::FloorToDouble(Phase);
		constexpr double Crest = 0.72;
		return T < Crest ? LiminalTerrainMath::SmoothStep01(T / Crest) : 1.0 - LiminalTerrainMath::SmoothStep01((T - Crest) / (1.0 - Crest));
	}

	double DesertHeight(int32 Seed, double X, double Y)
	{
		const double Angle = LiminalTerrainMath::GetDuneAngle(Seed);
		const double C = FMath::Cos(Angle);
		const double S = FMath::Sin(Angle);
		const double U = X * C + Y * S;
		const double V = -X * S + Y * C;
		const double Warp = 1800.0 * LiminalTerrainMath::Fbm(V / 15000.0, U / 30000.0, Seed ^ SaltWarp, 3);
		constexpr double Wavelength = 5500.0;
		const double Profile = DuneProfile((U + Warp) / Wavelength);
		const double Amplitude = 300.0 + 450.0 * (0.5 + 0.5 * LiminalTerrainMath::Fbm(V / 12000.0, U / 12000.0, Seed ^ SaltAmplitude, 2));
		return Amplitude * Profile + 60.0 * LiminalTerrainMath::Fbm(X / 1500.0, Y / 1500.0, Seed ^ SaltDetail, 2);
	}

	double AntarcticHeight(int32 Seed, double X, double Y)
	{
		// Ondulación amplia y sastrugi alargados (el viento los estira en X).
		const double Swell = 250.0 * LiminalTerrainMath::Fbm(X / 16000.0, Y / 16000.0, Seed, 4);
		const double Sastrugi = 40.0 * LiminalTerrainMath::Fbm(X / 2500.0, Y / 700.0, Seed ^ SaltDetail, 2);
		return Swell + Sastrugi;
	}

	double FarmlandHeight(const LiminalTerrainMath::FTerrainParams& Params, double X, double Y)
	{
		const LiminalTerrainMath::FFarmGrid& Farm = Params.Farm;
		const double Base = 70.0 * LiminalTerrainMath::Fbm(X / 25000.0, Y / 25000.0, Params.Seed, 3);
		const LiminalTerrainMath::FFarmCell Cell = LiminalTerrainMath::GetFarmCell(Farm, FVector2D(X, Y));
		const bool bAlongX = LiminalTerrainMath::Hash01(Cell.IX, Cell.IY, Params.Seed ^ SaltFurrow) < 0.5;
		const double Coordinate = bAlongX ? Y : X;
		const double Furrow = Farm.FurrowAmplitude * FMath::Sin(UE_DOUBLE_TWO_PI * Coordinate / Farm.FurrowPeriod);
		const double FurrowFade = LiminalTerrainMath::SmoothStep01(Cell.DistanceToRoad / 150.0);
		const double Roadness = 1.0 - LiminalTerrainMath::SmoothStep01((Cell.DistanceToRoad + 50.0) / 100.0);
		return Base + Furrow * FurrowFade - 5.0 * Roadness;
	}

	FLinearColor FarmlandColor(const LiminalTerrainMath::FTerrainParams& Params, double X, double Y)
	{
		static const FLinearColor Crops[4] = {
			FLinearColor(0.50f, 0.36f, 0.08f),  // trigo
			FLinearColor(0.10f, 0.25f, 0.04f),  // verde
			FLinearColor(0.13f, 0.07f, 0.035f), // arado
			FLinearColor(0.25f, 0.38f, 0.08f),  // verde claro
		};
		const FLinearColor Road(0.24f, 0.16f, 0.08f);
		const LiminalTerrainMath::FFarmGrid& Farm = Params.Farm;
		const LiminalTerrainMath::FFarmCell Cell = LiminalTerrainMath::GetFarmCell(Farm, FVector2D(X, Y));
		if (Cell.bRoad)
		{
			return Road;
		}
		const bool bAlongX = LiminalTerrainMath::Hash01(Cell.IX, Cell.IY, Params.Seed ^ SaltFurrow) < 0.5;
		const double Coordinate = bAlongX ? Y : X;
		const double Stripe = 0.88 + 0.12 * (0.5 + 0.5 * FMath::Sin(UE_DOUBLE_TWO_PI * Coordinate / Farm.FurrowPeriod));
		const FLinearColor Crop = Crops[LiminalTerrainMath::GetParcelCrop(Cell.IX, Cell.IY, Params.Seed)] * static_cast<float>(Stripe);
		return Mix(Road, Crop, Cell.DistanceToRoad / 60.0);
	}

	int32 QuantizedIndex(double Coordinate, double Origin, double Size)
	{
		return FMath::FloorToInt32((Coordinate - Origin) / Size);
	}
}

namespace LiminalTerrainMath
{
	FBiomeSeeds DeriveSeeds(FRandomStream& Stream)
	{
		FBiomeSeeds Seeds;
		Seeds.Terrain = static_cast<int32>(Stream.GetUnsignedInt() & 0x7fffffffu);
		Seeds.Scatter = static_cast<int32>(Stream.GetUnsignedInt() & 0x7fffffffu);
		return Seeds;
	}

	uint32 HashInts(int32 X, int32 Y, int32 Seed)
	{
		uint32 H = static_cast<uint32>(X) * 0x8da6b343u;
		H ^= static_cast<uint32>(Y) * 0xd8163841u;
		H ^= static_cast<uint32>(Seed) * 0xcb1ab31fu;
		H ^= H >> 16;
		H *= 0x7feb352du;
		H ^= H >> 15;
		H *= 0x846ca68bu;
		H ^= H >> 16;
		return H;
	}

	double Hash01(int32 X, int32 Y, int32 Seed)
	{
		return static_cast<double>(HashInts(X, Y, Seed)) / 4294967296.0;
	}

	double ValueNoise(double X, double Y, int32 Seed)
	{
		const double FX = FMath::FloorToDouble(X);
		const double FY = FMath::FloorToDouble(Y);
		const int32 IX = static_cast<int32>(FX);
		const int32 IY = static_cast<int32>(FY);
		const double U = LiminalTerrainMathDetail::Quintic(X - FX);
		const double V = LiminalTerrainMathDetail::Quintic(Y - FY);
		const double V00 = LiminalTerrainMathDetail::Signed(Hash01(IX, IY, Seed));
		const double V10 = LiminalTerrainMathDetail::Signed(Hash01(IX + 1, IY, Seed));
		const double V01 = LiminalTerrainMathDetail::Signed(Hash01(IX, IY + 1, Seed));
		const double V11 = LiminalTerrainMathDetail::Signed(Hash01(IX + 1, IY + 1, Seed));
		const double Bottom = V00 + (V10 - V00) * U;
		const double Top = V01 + (V11 - V01) * U;
		return Bottom + (Top - Bottom) * V;
	}

	double Fbm(double X, double Y, int32 Seed, int32 Octaves, double Lacunarity, double Gain)
	{
		double Sum = 0.0;
		double Norm = 0.0;
		double Amplitude = 1.0;
		double Frequency = 1.0;
		for (int32 Octave = 0; Octave < FMath::Max(Octaves, 1); ++Octave)
		{
			// Desplazamiento por octava: las rejillas de las octavas no coinciden en el origen.
			const double Offset = 17.31 * Octave;
			Sum += Amplitude * ValueNoise(X * Frequency + Offset, Y * Frequency - Offset, Seed + 7919 * Octave);
			Norm += Amplitude;
			Amplitude *= Gain;
			Frequency *= Lacunarity;
		}
		return Norm > 0.0 ? Sum / Norm : 0.0;
	}

	double SmoothStep01(double T)
	{
		const double C = FMath::Clamp(T, 0.0, 1.0);
		return C * C * (3.0 - 2.0 * C);
	}

	double DistanceToSegment(const FVector2D& Point, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D AB = B - A;
		const double LengthSquared = AB.SizeSquared();
		const double T = LengthSquared > UE_DOUBLE_SMALL_NUMBER ? FMath::Clamp(FVector2D::DotProduct(Point - A, AB) / LengthSquared, 0.0, 1.0) : 0.0;
		return FVector2D::Distance(Point, A + AB * T);
	}

	double FlatZoneWeight(const FFlatZone& Zone, const FVector2D& Point)
	{
		const double Distance = DistanceToSegment(Point, Zone.Start, Zone.End);
		if (Zone.Blend <= 0.0)
		{
			return Distance <= Zone.Radius ? 0.0 : 1.0;
		}
		return SmoothStep01((Distance - Zone.Radius) / Zone.Blend);
	}

	FHeightRange GetHeightRange(ELiminalBiomeType Type)
	{
		switch (Type)
		{
		case ELiminalBiomeType::Jungle:
			return { -550.0, 650.0 };
		case ELiminalBiomeType::Desert:
			return { -60.0, 810.0 };
		case ELiminalBiomeType::Antarctic:
			return { -290.0, 290.0 };
		case ELiminalBiomeType::Farmland:
			return { -90.0, 85.0 };
		default:
			return { 0.0, 0.0 };
		}
	}

	double GetDuneAngle(int32 Seed)
	{
		return Hash01(0, 0, Seed ^ LiminalTerrainMathDetail::SaltDune) * UE_DOUBLE_PI;
	}

	FFarmCell GetFarmCell(const FFarmGrid& Grid, const FVector2D& Point)
	{
		FFarmCell Cell;
		const double SizeX = FMath::Max(Grid.ParcelSize.X, 1.0);
		const double SizeY = FMath::Max(Grid.ParcelSize.Y, 1.0);
		Cell.IX = LiminalTerrainMathDetail::QuantizedIndex(Point.X, Grid.OriginX, SizeX);
		Cell.IY = LiminalTerrainMathDetail::QuantizedIndex(Point.Y, Grid.OriginY, SizeY);
		const double FX = Point.X - Grid.OriginX - Cell.IX * SizeX;
		const double FY = Point.Y - Grid.OriginY - Cell.IY * SizeY;
		const double HalfRoad = Grid.RoadWidth * 0.5;
		const double ToRoadX = FMath::Min(FX, SizeX - FX) - HalfRoad;
		const double ToRoadY = FMath::Min(FY, SizeY - FY) - HalfRoad;
		Cell.DistanceToRoad = FMath::Min(ToRoadX, ToRoadY);
		Cell.bRoad = Cell.DistanceToRoad < 0.0;
		return Cell;
	}

	int32 GetParcelCrop(int32 IX, int32 IY, int32 Seed)
	{
		const int32 Group = ((IX + IY) & 1) * 2;
		return Group + (Hash01(IX, IY, Seed ^ LiminalTerrainMathDetail::SaltCrop) < 0.5 ? 0 : 1);
	}

	double SampleRawHeight(const FTerrainParams& Params, double X, double Y)
	{
		switch (Params.Type)
		{
		case ELiminalBiomeType::Jungle:
			return LiminalTerrainMathDetail::JungleHeight(Params.Seed, X, Y);
		case ELiminalBiomeType::Desert:
			return LiminalTerrainMathDetail::DesertHeight(Params.Seed, X, Y);
		case ELiminalBiomeType::Antarctic:
			return LiminalTerrainMathDetail::AntarcticHeight(Params.Seed, X, Y);
		case ELiminalBiomeType::Farmland:
			return LiminalTerrainMathDetail::FarmlandHeight(Params, X, Y);
		default:
			return 0.0;
		}
	}

	double SampleHeight(const FTerrainParams& Params, double X, double Y)
	{
		const double Weight = FlatZoneWeight(Params.FlatZone, FVector2D(X, Y));
		return Weight > 0.0 ? Weight * SampleRawHeight(Params, X, Y) : 0.0;
	}

	FLinearColor SampleColor(const FTerrainParams& Params, double X, double Y, double Height)
	{
		const FVector2D Point(X, Y);
		const double Variation = 0.5 + 0.5 * Fbm(X / 2500.0, Y / 2500.0, Params.Seed ^ LiminalTerrainMathDetail::SaltColor, 3);
		switch (Params.Type)
		{
		case ELiminalBiomeType::Jungle:
		{
			const FLinearColor Moss = LiminalTerrainMathDetail::Mix(FLinearColor(0.05f, 0.22f, 0.06f), FLinearColor(0.04f, 0.08f, 0.02f), Variation);
			return LiminalTerrainMathDetail::Mix(Moss, FLinearColor(0.12f, 0.075f, 0.04f), LiminalTerrainMathDetail::PathMix(Params.FlatZone, Point));
		}
		case ELiminalBiomeType::Desert:
		{
			const FLinearColor Sand = LiminalTerrainMathDetail::Mix(FLinearColor(0.45f, 0.30f, 0.15f), FLinearColor(0.70f, 0.52f, 0.30f), Height / 800.0);
			return Sand * static_cast<float>(0.94 + 0.06 * Variation);
		}
		case ELiminalBiomeType::Antarctic:
		{
			const double Ice = SmoothStep01((Fbm(X / 5000.0, Y / 5000.0, Params.Seed ^ LiminalTerrainMathDetail::SaltColor, 3) - 0.3) / 0.2);
			const FLinearColor Snow = LiminalTerrainMathDetail::Mix(FLinearColor(0.80f, 0.85f, 0.90f), FLinearColor(0.42f, 0.60f, 0.75f), Ice);
			return LiminalTerrainMathDetail::Mix(Snow, FLinearColor(0.62f, 0.66f, 0.71f), LiminalTerrainMathDetail::PathMix(Params.FlatZone, Point));
		}
		case ELiminalBiomeType::Farmland:
			return LiminalTerrainMathDetail::FarmlandColor(Params, X, Y);
		default:
			return FLinearColor(0.5f, 0.5f, 0.5f);
		}
	}

	FTerrainGrid MakeGrid(const FVector2D& Size, double Spacing)
	{
		FTerrainGrid Grid;
		if (Size.X <= 0.0 || Size.Y <= 0.0 || Spacing <= 0.0)
		{
			return Grid;
		}
		Grid.Spacing = Spacing;
		Grid.NumX = FMath::FloorToInt32(Size.X / Spacing + 0.5) + 1;
		Grid.NumY = FMath::FloorToInt32(Size.Y / Spacing + 0.5) + 1;
		Grid.Origin = -0.5 * FVector2D((Grid.NumX - 1) * Spacing, (Grid.NumY - 1) * Spacing);
		return Grid;
	}

	TArray<double> SampleHeightField(const FTerrainParams& Params, const FTerrainGrid& Grid)
	{
		TArray<double> Heights;
		Heights.SetNumUninitialized(Grid.Num());
		for (int32 IX = 0; IX < Grid.NumX; ++IX)
		{
			for (int32 IY = 0; IY < Grid.NumY; ++IY)
			{
				const FVector2D Point = Grid.GetPoint(IX, IY);
				Heights[IX * Grid.NumY + IY] = SampleHeight(Params, Point.X, Point.Y);
			}
		}
		return Heights;
	}

	bool BuildTerrainMesh(const FTerrainParams& Params, const FTerrainGrid& Grid, FTerrainMesh& OutMesh)
	{
		OutMesh = FTerrainMesh();
		if (Grid.NumX < 2 || Grid.NumY < 2 || Grid.Spacing <= 0.0)
		{
			return false;
		}
		const TArray<double> Heights = SampleHeightField(Params, Grid);
		auto HeightAt = [&Heights, &Grid](int32 IX, int32 IY)
		{
			return Heights[FMath::Clamp(IX, 0, Grid.NumX - 1) * Grid.NumY + FMath::Clamp(IY, 0, Grid.NumY - 1)];
		};

		const int32 NumVertices = Grid.Num();
		OutMesh.Vertices.Reserve(NumVertices);
		OutMesh.Normals.Reserve(NumVertices);
		OutMesh.Tangents.Reserve(NumVertices);
		OutMesh.UVs.Reserve(NumVertices);
		OutMesh.Colors.Reserve(NumVertices);
		for (int32 IX = 0; IX < Grid.NumX; ++IX)
		{
			for (int32 IY = 0; IY < Grid.NumY; ++IY)
			{
				const FVector2D Point = Grid.GetPoint(IX, IY);
				const double Height = HeightAt(IX, IY);
				// Diferencias centrales (laterales en el borde): gradiente del campo de alturas.
				const int32 X0 = FMath::Max(IX - 1, 0);
				const int32 X1 = FMath::Min(IX + 1, Grid.NumX - 1);
				const int32 Y0 = FMath::Max(IY - 1, 0);
				const int32 Y1 = FMath::Min(IY + 1, Grid.NumY - 1);
				const double DX = (HeightAt(X1, IY) - HeightAt(X0, IY)) / ((X1 - X0) * Grid.Spacing);
				const double DY = (HeightAt(IX, Y1) - HeightAt(IX, Y0)) / ((Y1 - Y0) * Grid.Spacing);
				OutMesh.Vertices.Add(FVector(Point.X, Point.Y, Height));
				OutMesh.Normals.Add(FVector(-DX, -DY, 1.0).GetSafeNormal());
				OutMesh.Tangents.Add(FVector(1.0, 0.0, DX).GetSafeNormal());
				OutMesh.UVs.Add(Point / 1000.0);
				OutMesh.Colors.Add(SampleColor(Params, Point.X, Point.Y, Height));
			}
		}

		OutMesh.Triangles.Reserve((Grid.NumX - 1) * (Grid.NumY - 1) * 6);
		for (int32 IX = 0; IX + 1 < Grid.NumX; ++IX)
		{
			for (int32 IY = 0; IY + 1 < Grid.NumY; ++IY)
			{
				const int32 I0 = IX * Grid.NumY + IY;
				const int32 I1 = (IX + 1) * Grid.NumY + IY;
				const int32 I2 = (IX + 1) * Grid.NumY + IY + 1;
				const int32 I3 = IX * Grid.NumY + IY + 1;
				// (P1 − P2) × (P0 − P2) apunta a +Z con este orden (convención de UE para la cara frontal).
				OutMesh.Triangles.Append({ I0, I3, I2, I0, I2, I1 });
			}
		}
		return true;
	}

	uint32 HashHeights(TConstArrayView<double> Heights)
	{
		uint32 Hash = 2166136261u;
		for (const double Height : Heights)
		{
			const int64 Quantized = FMath::RoundToInt64(Height * 100.0);
			for (int32 Byte = 0; Byte < 8; ++Byte)
			{
				Hash ^= static_cast<uint32>((Quantized >> (Byte * 8)) & 0xff);
				Hash *= 16777619u;
			}
		}
		return Hash;
	}
}
