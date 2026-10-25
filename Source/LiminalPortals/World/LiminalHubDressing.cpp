#include "World/LiminalHubDressing.h"

#include "World/LiminalTerrainMath.h"

// Namespace con nombre propio (D-22): el unity build junta todos los .cpp del módulo.
namespace LiminalHubDressingDetail
{
	using LiminalArtCatalog::EHubPiece;
	using LiminalWorldLayout::FHubLayout;

	constexpr int32 SaltStain = 0x2F6B1C35;
	/** Proporción de placas de techo con mancha de humedad. */
	constexpr double StainedFraction = 0.05;
	/** Alturas de las piezas de pared (base), en cm. */
	constexpr double VentZ = 262.0;
	constexpr double OutletZ = 25.0;
	constexpr double SwitchZ = 105.0;
	constexpr double ExitSignGap = 6.0;
	/** Separación del interruptor respecto al tapajuntas. */
	constexpr double SwitchBesideCasing = 15.0;
	/** Huelgo de los enchufes respecto a los extremos de un tramo libre. */
	constexpr double OutletMargin = 30.0;

	/** Pared interior: 0 = +Y, 1 = −Y, 2 = +X, 3 = −X. U recorre la pared (X en las largas, Y en las de los extremos). */
	struct FWall
	{
		int32 Index = 0;
		double Start = 0.0;
		double End = 0.0;
	};

	FVector WallPoint(const FHubLayout& Layout, int32 Wall, double U, double Z)
	{
		const double HalfX = Layout.InteriorSize.X * 0.5;
		const double HalfY = Layout.InteriorSize.Y * 0.5;
		switch (Wall)
		{
		case 0: return FVector(U, HalfY, Z);
		case 1: return FVector(U, -HalfY, Z);
		case 2: return FVector(HalfX, U, Z);
		default: return FVector(-HalfX, U, Z);
		}
	}

	/** Giro que lleva el +X local de la pieza a la normal interior de la pared. */
	double WallYaw(int32 Wall)
	{
		static constexpr double Yaws[4] = { -90.0, 90.0, 180.0, 0.0 };
		return Yaws[FMath::Clamp(Wall, 0, 3)];
	}

	FTransform OnWall(const FHubLayout& Layout, int32 Wall, double U, double Z, const FVector& Scale = FVector::OneVector)
	{
		return FTransform(FRotator(0.0, WallYaw(Wall), 0.0), WallPoint(Layout, Wall, U, Z), Scale);
	}

	TArray<FWall> MakeWalls(const FHubLayout& Layout)
	{
		const double HalfX = Layout.InteriorSize.X * 0.5;
		const double HalfY = Layout.InteriorSize.Y * 0.5;
		return { { 0, -HalfX, HalfX }, { 1, -HalfX, HalfX }, { 2, -HalfY, HalfY }, { 3, -HalfY, HalfY } };
	}

	TArray<double> DoorCenters(const FHubLayout& Layout, int32 Wall)
	{
		TArray<double> Centers;
		if (Wall > 1)
		{
			return Centers;
		}
		for (const LiminalWorldLayout::FHubDoorSpec& Door : Layout.Doors)
		{
			if (Door.bPositiveYWall == (Wall == 0))
			{
				Centers.Add(Door.PositionX);
			}
		}
		return Centers;
	}

	void AddCarpet(const FHubLayout& Layout, LiminalHubDressing::FHubDressing& Out)
	{
		const int32 NumX = FMath::Max(1, FMath::RoundToInt32(Layout.InteriorSize.X / LiminalHubDressing::CarpetTileSize));
		const int32 NumY = FMath::Max(1, FMath::RoundToInt32(Layout.InteriorSize.Y / LiminalHubDressing::CarpetTileSize));
		const FVector2D Tile(Layout.InteriorSize.X / NumX, Layout.InteriorSize.Y / NumY);
		const FVector Scale(Tile.X / LiminalHubDressing::CarpetTileSize, Tile.Y / LiminalHubDressing::CarpetTileSize, 1.0);
		for (int32 IX = 0; IX < NumX; ++IX)
		{
			for (int32 IY = 0; IY < NumY; ++IY)
			{
				const FVector Center(-Layout.InteriorSize.X * 0.5 + (IX + 0.5) * Tile.X, -Layout.InteriorSize.Y * 0.5 + (IY + 0.5) * Tile.Y, 0.0);
				// Franja de borde junto a las paredes largas y rombos en el resto.
				const bool bBorder = IY == 0 || IY == NumY - 1;
				// La franja gira 90° para que sus rayas corran a lo largo de la pared.
				const FQuat Rotation = bBorder ? FRotator(0.0, 90.0, 0.0).Quaternion() : FQuat::Identity;
				Out.Get(bBorder ? EHubPiece::CarpetBorder : EHubPiece::Carpet).Emplace(Rotation, Center, bBorder ? FVector(Scale.Y, Scale.X, 1.0) : Scale);
			}
		}
	}

	bool InsideAnyPanel(TConstArrayView<FBox> Panels, const FVector2D& Point)
	{
		for (const FBox& Panel : Panels)
		{
			if (Point.X > Panel.Min.X && Point.X < Panel.Max.X && Point.Y > Panel.Min.Y && Point.Y < Panel.Max.Y)
			{
				return true;
			}
		}
		return false;
	}

	/** Placas de 60 cm con las juntas en múltiplos de 60 en X y centradas en Y = 0 (como los paneles). */
	void AddCeiling(const FHubLayout& Layout, const LiminalWorldLayout::FHubGeometry& Geometry, LiminalHubDressing::FHubDressing& Out)
	{
		constexpr double Tile = LiminalHubDressing::CeilingTileSize;
		const double Height = Layout.InteriorSize.Z;
		const int32 HalfCountX = FMath::CeilToInt32(Layout.InteriorSize.X * 0.5 / Tile);
		const int32 HalfCountY = FMath::CeilToInt32((Layout.InteriorSize.Y * 0.5 - Tile * 0.5) / Tile);
		for (int32 KX = -HalfCountX; KX < HalfCountX; ++KX)
		{
			for (int32 KY = -HalfCountY; KY <= HalfCountY; ++KY)
			{
				const FVector2D Center((KX + 0.5) * Tile, KY * Tile);
				if (InsideAnyPanel(Geometry.LightPanels, Center))
				{
					continue;
				}
				const bool bStained = LiminalTerrainMath::Hash01(KX, KY, SaltStain) < StainedFraction;
				const double Yaw = bStained ? 90.0 * FMath::FloorToDouble(LiminalTerrainMath::Hash01(KX, KY, SaltStain ^ 0x7F) * 4.0) : 0.0;
				Out.Get(bStained ? EHubPiece::CeilingTileStained : EHubPiece::CeilingTile)
					.Emplace(FRotator(0.0, Yaw, 0.0).Quaternion(), FVector(Center.X, Center.Y, Height));
			}
		}
		for (const FBox& Panel : Geometry.LightPanels)
		{
			const FVector Center = Panel.GetCenter();
			Out.Get(EHubPiece::LightPanel).Emplace(FQuat::Identity, FVector(Center.X, Center.Y, Height));
		}
	}

	void AddBaseboards(const FHubLayout& Layout, LiminalHubDressing::FHubDressing& Out)
	{
		for (const FWall& Wall : MakeWalls(Layout))
		{
			const TArray<double> Doors = DoorCenters(Layout, Wall.Index);
			for (const FVector2D& Interval : LiminalHubDressing::FreeWallIntervals(Wall.Start, Wall.End, Doors, LiminalHubDressing::DoorCasingHalfWidth))
			{
				const double Length = Interval.Y - Interval.X;
				const int32 Count = FMath::Max(1, FMath::CeilToInt32(Length / LiminalHubDressing::BaseboardLength - 1e-6));
				const double Piece = Length / Count;
				for (int32 Index = 0; Index < Count; ++Index)
				{
					Out.Get(EHubPiece::Baseboard).Add(OnWall(Layout, Wall.Index, Interval.X + (Index + 0.5) * Piece, 0.0,
						FVector(1.0, Piece / LiminalHubDressing::BaseboardLength, 1.0)));
				}
			}
		}
	}

	void AddWallFixtures(const FHubLayout& Layout, LiminalHubDressing::FHubDressing& Out)
	{
		const double HalfX = Layout.InteriorSize.X * 0.5;
		const double HalfY = Layout.InteriorSize.Y * 0.5;
		// Rejillas: una en cada pared de los extremos y otra en el tramo libre más largo de la pared +Y.
		Out.Get(EHubPiece::Vent).Add(OnWall(Layout, 2, HalfY * 0.5, VentZ));
		Out.Get(EHubPiece::Vent).Add(OnWall(Layout, 3, -HalfY * 0.5, VentZ));
		const TArray<FVector2D> Free = LiminalHubDressing::FreeWallIntervals(-HalfX, HalfX, DoorCenters(Layout, 0), LiminalHubDressing::DoorCasingHalfWidth);
		const FVector2D* Longest = nullptr;
		for (const FVector2D& Interval : Free)
		{
			Longest = (!Longest || Interval.Y - Interval.X > Longest->Y - Longest->X) ? &Interval : Longest;
		}
		if (Longest)
		{
			Out.Get(EHubPiece::Vent).Add(OnWall(Layout, 0, (Longest->X + Longest->Y) * 0.5, VentZ));
		}

		// Enchufes a intervalos regulares en las paredes largas (fuera de las puertas) y uno por extremo.
		for (const int32 Wall : { 0, 1 })
		{
			const TArray<FVector2D> Intervals = LiminalHubDressing::FreeWallIntervals(-HalfX, HalfX, DoorCenters(Layout, Wall), LiminalHubDressing::DoorCasingHalfWidth);
			for (const double Fraction : { -0.875, -0.4375, 0.0, 0.4375, 0.875 })
			{
				const double U = Fraction * HalfX;
				for (const FVector2D& Interval : Intervals)
				{
					if (U > Interval.X + OutletMargin && U < Interval.Y - OutletMargin)
					{
						Out.Get(EHubPiece::Outlet).Add(OnWall(Layout, Wall, U, OutletZ));
					}
				}
			}
		}
		Out.Get(EHubPiece::Outlet).Add(OnWall(Layout, 2, 0.0, OutletZ));
		Out.Get(EHubPiece::Outlet).Add(OnWall(Layout, 3, 0.0, OutletZ));

		// Interruptor junto a la primera puerta, del lado del centro de la sala; cartel de salida sobre la tercera.
		if (Layout.Doors.Num() > 0)
		{
			const LiminalWorldLayout::FHubDoorSpec& Door = Layout.Doors[0];
			const double Side = Door.PositionX > 0.0 ? -1.0 : 1.0;
			Out.Get(EHubPiece::Switch).Add(OnWall(Layout, Door.bPositiveYWall ? 0 : 1,
				Door.PositionX + Side * (LiminalHubDressing::DoorCasingHalfWidth + SwitchBesideCasing), SwitchZ));
			const LiminalWorldLayout::FHubDoorSpec& Exit = Layout.Doors[FMath::Min(2, Layout.Doors.Num() - 1)];
			Out.Get(EHubPiece::ExitSign).Add(OnWall(Layout, Exit.bPositiveYWall ? 0 : 1, Exit.PositionX,
				LiminalHubDressing::DoorCasingHeight + ExitSignGap));
		}

		// Una silla sola en un rincón, girada hacia la esquina.
		Out.Get(EHubPiece::Chair).Emplace(FRotator(0.0, 35.0, 0.0).Quaternion(), FVector(HalfX - 80.0, HalfY - 90.0, 0.0));
	}
}

namespace LiminalHubDressing
{
	int32 FHubDressing::Num() const
	{
		int32 Total = 0;
		for (const TArray<FTransform>& Piece : Pieces)
		{
			Total += Piece.Num();
		}
		return Total;
	}

	TArray<FVector2D> FreeWallIntervals(double Start, double End, TConstArrayView<double> Centers, double HalfWidth)
	{
		TArray<FVector2D> Blocked;
		for (const double Center : Centers)
		{
			const double Low = FMath::Max(Center - HalfWidth, Start);
			const double High = FMath::Min(Center + HalfWidth, End);
			if (High > Low)
			{
				Blocked.Emplace(Low, High);
			}
		}
		Blocked.Sort([](const FVector2D& A, const FVector2D& B) { return A.X < B.X; });
		TArray<FVector2D> Free;
		double Cursor = Start;
		for (const FVector2D& Interval : Blocked)
		{
			if (Interval.X > Cursor)
			{
				Free.Emplace(Cursor, Interval.X);
			}
			Cursor = FMath::Max(Cursor, Interval.Y);
		}
		if (End > Cursor)
		{
			Free.Emplace(Cursor, End);
		}
		return Free;
	}

	bool BuildHubDressing(const LiminalWorldLayout::FHubLayout& Layout, FHubDressing& OutDressing)
	{
		OutDressing = FHubDressing();
		LiminalWorldLayout::FHubGeometry Geometry;
		if (!LiminalWorldLayout::BuildHubGeometry(Layout, Geometry))
		{
			return false;
		}
		FHubDressing Dressing;
		Dressing.Get(LiminalArtCatalog::EHubPiece::DoorFrame) = Geometry.Doors;
		LiminalHubDressingDetail::AddCarpet(Layout, Dressing);
		LiminalHubDressingDetail::AddCeiling(Layout, Geometry, Dressing);
		LiminalHubDressingDetail::AddBaseboards(Layout, Dressing);
		LiminalHubDressingDetail::AddWallFixtures(Layout, Dressing);
		OutDressing = MoveTemp(Dressing);
		return true;
	}
}
