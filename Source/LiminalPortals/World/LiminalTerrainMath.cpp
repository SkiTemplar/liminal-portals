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
	constexpr int32 SaltFace = 0x13579BDF;

	/** Variación de brillo por cara (facetado): ±FaceJitter. */
	constexpr double FaceJitter = 0.04;

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
		const double Coordinate = LiminalTerrainMath::IsParcelAlongX(Cell.IX, Cell.IY, Params.Seed) ? Y : X;
		const double Furrow = Farm.FurrowAmplitude * FMath::Sin(UE_DOUBLE_TWO_PI * Coordinate / Farm.FurrowPeriod);
		const double FurrowFade = LiminalTerrainMath::SmoothStep01(Cell.DistanceToRoad / 150.0);
		const double Roadness = 1.0 - LiminalTerrainMath::SmoothStep01((Cell.DistanceToRoad + 50.0) / 100.0);
		return Base + Furrow * FurrowFade - 5.0 * Roadness;
	}

	FLinearColor FarmlandColor(const LiminalTerrainMath::FTerrainParams& Params, double X, double Y)
	{
		// Paleta de docs/ARTE.md en lineal: trigo #C9A54A, cultivo verde #5E8C3A, tierra #7A5434 y #8DB45A.
		static const FLinearColor Crops[4] = {
			FLinearColor(0.584f, 0.376f, 0.070f), // trigo
			FLinearColor(0.111f, 0.262f, 0.042f), // verde (maíz)
			FLinearColor(0.195f, 0.089f, 0.034f), // arado
			FLinearColor(0.266f, 0.456f, 0.102f), // verde claro (filas)
		};
		const FLinearColor Road(0.24f, 0.16f, 0.08f);
		const LiminalTerrainMath::FFarmGrid& Farm = Params.Farm;
		const LiminalTerrainMath::FFarmCell Cell = LiminalTerrainMath::GetFarmCell(Farm, FVector2D(X, Y));
		if (Cell.bRoad)
		{
			return Road;
		}
		const double Coordinate = LiminalTerrainMath::IsParcelAlongX(Cell.IX, Cell.IY, Params.Seed) ? Y : X;
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

	bool IsParcelAlongX(int32 IX, int32 IY, int32 Seed)
	{
		return Hash01(IX, IY, Seed ^ LiminalTerrainMathDetail::SaltFurrow) < 0.5;
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
			const FLinearColor Moss = LiminalTerrainMathDetail::Mix(FLinearColor(0.10f, 0.32f, 0.08f), FLinearColor(0.09f, 0.18f, 0.05f), Variation);
			return LiminalTerrainMathDetail::Mix(Moss, FLinearColor(0.26f, 0.17f, 0.09f), LiminalTerrainMathDetail::PathMix(Params.FlatZone, Point));
		}
		case ELiminalBiomeType::Desert:
		{
			// Arena de docs/ARTE.md en lineal: #C48A4E en las hondonadas, #D9A866 a media altura y #E8C48A en las crestas.
			const double T = FMath::Clamp(Height / 700.0, 0.0, 1.0);
			const FLinearColor Low(0.552f, 0.254f, 0.076f);
			const FLinearColor Mid(0.693f, 0.392f, 0.135f);
			const FLinearColor High(0.807f, 0.552f, 0.254f);
			const FLinearColor Sand = T < 0.5 ? LiminalTerrainMathDetail::Mix(Low, Mid, T * 2.0) : LiminalTerrainMathDetail::Mix(Mid, High, T * 2.0 - 1.0);
			return Sand * static_cast<float>(0.94 + 0.06 * Variation);
		}
		case ELiminalBiomeType::Antarctic:
		{
			// Nieve #F4F7FA y #DDE7EF alternando por ruido y placas de hielo #A9D6EA (lineal, docs/ARTE.md).
			const double Ice = SmoothStep01((Fbm(X / 5000.0, Y / 5000.0, Params.Seed ^ LiminalTerrainMathDetail::SaltColor, 3) - 0.15) / 0.2);
			// Con el sol compartido la nieve más clara se satura: domina #DDE7EF y el hielo llega a #6FB3D6 en el centro de las placas.
			const FLinearColor Drift = LiminalTerrainMathDetail::Mix(FLinearColor(0.723f, 0.799f, 0.863f), FLinearColor(0.905f, 0.930f, 0.955f), Variation * Variation);
			const FLinearColor Plate = LiminalTerrainMathDetail::Mix(FLinearColor(0.397f, 0.672f, 0.823f), FLinearColor(0.159f, 0.451f, 0.672f), Ice * Ice);
			const FLinearColor Snow = LiminalTerrainMathDetail::Mix(Drift, Plate, Ice);
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

	double SampleSurfaceHeight(const FTerrainParams& Params, const FTerrainGrid& Grid, double X, double Y)
	{
		if (Grid.NumX < 2 || Grid.NumY < 2 || Grid.Spacing <= 0.0)
		{
			return SampleHeight(Params, X, Y);
		}
		const double U = FMath::Clamp((X - Grid.Origin.X) / Grid.Spacing, 0.0, static_cast<double>(Grid.NumX - 1));
		const double V = FMath::Clamp((Y - Grid.Origin.Y) / Grid.Spacing, 0.0, static_cast<double>(Grid.NumY - 1));
		const int32 IX = FMath::Min(FMath::FloorToInt32(U), Grid.NumX - 2);
		const int32 IY = FMath::Min(FMath::FloorToInt32(V), Grid.NumY - 2);
		const double FX = U - IX;
		const double FY = V - IY;
		auto CornerHeight = [&Params, &Grid](int32 CX, int32 CY)
		{
			const FVector2D Point = Grid.GetPoint(CX, CY);
			return SampleHeight(Params, Point.X, Point.Y);
		};
		const double H0 = CornerHeight(IX, IY);
		const double H2 = CornerHeight(IX + 1, IY + 1);
		// Triángulos de BuildTerrainMesh: (I0, I3, I2) con FY >= FX e (I0, I2, I1) con FX >= FY.
		if (FX >= FY)
		{
			const double H1 = CornerHeight(IX + 1, IY);
			return H0 + FX * (H1 - H0) + FY * (H2 - H1);
		}
		const double H3 = CornerHeight(IX, IY + 1);
		return H0 + FY * (H3 - H0) + FX * (H2 - H3);
	}

	bool BuildTerrainMesh(const FTerrainParams& Params, const FTerrainGrid& Grid, FTerrainMesh& OutMesh)
	{
		OutMesh = FTerrainMesh();
		if (Grid.NumX < 2 || Grid.NumY < 2 || Grid.Spacing <= 0.0)
		{
			return false;
		}
		const TArray<double> Heights = SampleHeightField(Params, Grid);
		auto Corner = [&Heights, &Grid](int32 IX, int32 IY)
		{
			const FVector2D Point = Grid.GetPoint(IX, IY);
			return FVector(Point.X, Point.Y, Heights[IX * Grid.NumY + IY]);
		};

		const int32 NumCorners = (Grid.NumX - 1) * (Grid.NumY - 1) * 6;
		OutMesh.Vertices.Reserve(NumCorners);
		OutMesh.Normals.Reserve(NumCorners);
		OutMesh.Tangents.Reserve(NumCorners);
		OutMesh.UVs.Reserve(NumCorners);
		OutMesh.Colors.Reserve(NumCorners);
		OutMesh.Triangles.Reserve(NumCorners);

		auto AddFace = [&OutMesh, &Params](const FVector& P0, const FVector& P1, const FVector& P2, uint32 FaceHash)
		{
			// (P1 − P2) × (P0 − P2) apunta a +Z con este orden (convención de UE para la cara frontal).
			const FVector Normal = FVector::CrossProduct(P1 - P2, P0 - P2).GetSafeNormal();
			const FVector Tangent = FVector::VectorPlaneProject(FVector::XAxisVector, Normal).GetSafeNormal();
			const FVector Center = (P0 + P1 + P2) / 3.0;
			const double Jitter = 1.0 + LiminalTerrainMathDetail::FaceJitter * (2.0 * (static_cast<double>(FaceHash) / 4294967296.0) - 1.0);
			const FLinearColor Color = SampleColor(Params, Center.X, Center.Y, Center.Z) * static_cast<float>(Jitter);
			for (const FVector& P : { P0, P1, P2 })
			{
				OutMesh.Triangles.Add(OutMesh.Vertices.Num());
				OutMesh.Vertices.Add(P);
				OutMesh.Normals.Add(Normal);
				OutMesh.Tangents.Add(Tangent);
				OutMesh.UVs.Add(FVector2D(P.X, P.Y) / 1000.0);
				OutMesh.Colors.Add(Color);
			}
		};

		for (int32 IX = 0; IX + 1 < Grid.NumX; ++IX)
		{
			for (int32 IY = 0; IY + 1 < Grid.NumY; ++IY)
			{
				const FVector P0 = Corner(IX, IY);
				const FVector P1 = Corner(IX + 1, IY);
				const FVector P2 = Corner(IX + 1, IY + 1);
				const FVector P3 = Corner(IX, IY + 1);
				AddFace(P0, P3, P2, HashInts(IX * 2, IY, Params.Seed ^ LiminalTerrainMathDetail::SaltFace));
				AddFace(P0, P2, P1, HashInts(IX * 2 + 1, IY, Params.Seed ^ LiminalTerrainMathDetail::SaltFace));
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
