#include "World/LiminalWorldLayout.h"

namespace
{
	/** Tolerancia geométrica: huecos que se tocan no se consideran solapados. */
	constexpr double Epsilon = 1e-6;

	/** Caja 3D de una pared paralela a X a partir de un rectángulo (U = X, V = Z) y su franja en Y. */
	FBox WallBoxAlongX(const FBox2D& Segment, double MinY, double MaxY)
	{
		return FBox(FVector(Segment.Min.X, MinY, Segment.Min.Y), FVector(Segment.Max.X, MaxY, Segment.Max.Y));
	}

	/** Caja 3D de una pared paralela a Y a partir de un rectángulo (U = Y, V = Z) y su franja en X. */
	FBox WallBoxAlongY(const FBox2D& Segment, double MinX, double MaxX)
	{
		return FBox(FVector(MinX, Segment.Min.X, Segment.Min.Y), FVector(MaxX, Segment.Max.X, Segment.Max.Y));
	}

	TArray<LiminalWorldLayout::FWallOpening> CollectOpenings(const LiminalWorldLayout::FHubLayout& Layout, bool bPositiveYWall)
	{
		TArray<LiminalWorldLayout::FWallOpening> Openings;
		for (const LiminalWorldLayout::FHubDoorSpec& Door : Layout.Doors)
		{
			if (Door.bPositiveYWall == bPositiveYWall)
			{
				Openings.Add(LiminalWorldLayout::MakeDoorOpening(Door.PositionX, Layout.DoorSize, Layout.DoorFrameThickness));
			}
		}
		return Openings;
	}
}

namespace LiminalWorldLayout
{
	FWallOpening MakeDoorOpening(double Center, const FVector2D& DoorSize, double FrameThickness)
	{
		FWallOpening Opening;
		Opening.Center = Center;
		Opening.Width = DoorSize.X + 2.0 * FrameThickness;
		Opening.Height = DoorSize.Y + FrameThickness;
		return Opening;
	}

	bool SplitWallAroundOpenings(double Start, double End, double Height, TConstArrayView<FWallOpening> Openings,
		TArray<FBox2D>& OutSegments)
	{
		OutSegments.Reset();
		if (End - Start <= Epsilon || Height <= Epsilon)
		{
			return false;
		}

		TArray<FWallOpening> Sorted(Openings.GetData(), Openings.Num());
		Sorted.Sort([](const FWallOpening& A, const FWallOpening& B) { return A.Center < B.Center; });

		TArray<FBox2D> Segments;
		double Cursor = Start;
		for (const FWallOpening& Opening : Sorted)
		{
			const double Left = Opening.Center - Opening.Width * 0.5;
			const double Right = Opening.Center + Opening.Width * 0.5;
			const bool bValid = Opening.Width > Epsilon && Opening.Height > Epsilon && Opening.Height < Height - Epsilon
				&& Left >= Cursor - Epsilon && Right <= End + Epsilon;
			if (!bValid)
			{
				return false;
			}
			if (Left > Cursor + Epsilon)
			{
				Segments.Emplace(FVector2D(Cursor, 0.0), FVector2D(Left, Height));
			}
			Segments.Emplace(FVector2D(Left, Opening.Height), FVector2D(Right, Height));
			Cursor = Right;
		}
		if (End > Cursor + Epsilon)
		{
			Segments.Emplace(FVector2D(Cursor, 0.0), FVector2D(End, Height));
		}

		OutSegments = MoveTemp(Segments);
		return true;
	}

	TArray<FHubDoorSpec> MakeDefaultHubDoors()
	{
		return {
			{ -500.0, true },
			{ -100.0, false },
			{ 100.0, true },
			{ 500.0, false },
		};
	}

	bool IsValidHubLayout(const FHubLayout& Layout, FString* OutError)
	{
		auto Fail = [OutError](const TCHAR* Message)
		{
			if (OutError)
			{
				*OutError = Message;
			}
			return false;
		};

		const FVector& Size = Layout.InteriorSize;
		if (Size.X <= Epsilon || Size.Y <= Epsilon || Size.Z <= Epsilon || Layout.WallThickness <= Epsilon
			|| Layout.SlabThickness <= Epsilon || Layout.CeilingPlenum < 0.0)
		{
			return Fail(TEXT("medidas de la sala no positivas"));
		}
		if (Layout.DoorSize.X <= Epsilon || Layout.DoorSize.Y <= Epsilon || Layout.DoorFrameThickness < 0.0)
		{
			return Fail(TEXT("medidas de puerta no válidas"));
		}
		if (Layout.Doors.IsEmpty())
		{
			return Fail(TEXT("el hub no tiene puertas"));
		}
		if (Layout.NumCeilingLights < 0 || Layout.LightPanelSize.X <= Epsilon || Layout.LightPanelSize.Y <= Epsilon
			|| Layout.LightPanelThickness <= Epsilon || Layout.LightPanelThickness >= Size.Z)
		{
			return Fail(TEXT("paneles de luz no válidos"));
		}

		// Cada pared larga solo admite huecos dentro del interior: no pueden cortar las paredes de los extremos.
		const double HalfLength = Size.X * 0.5;
		for (const bool bPositive : { true, false })
		{
			TArray<FBox2D> Segments;
			if (!SplitWallAroundOpenings(-HalfLength, HalfLength, Size.Z, CollectOpenings(Layout, bPositive), Segments))
			{
				return Fail(TEXT("puertas solapadas, fuera de la pared o más altas que la sala"));
			}
		}
		return true;
	}

	bool GetHubDoorTransform(const FHubLayout& Layout, int32 Index, FTransform& OutTransform)
	{
		if (!Layout.Doors.IsValidIndex(Index))
		{
			return false;
		}
		const FHubDoorSpec& Door = Layout.Doors[Index];
		const double HalfWidth = Layout.InteriorSize.Y * 0.5;
		// El plano del portal queda en la cara interior de la pared y su eje X apunta hacia dentro.
		const double Y = Door.bPositiveYWall ? HalfWidth : -HalfWidth;
		const double Yaw = Door.bPositiveYWall ? -90.0 : 90.0;
		OutTransform = FTransform(FRotator(0.0, Yaw, 0.0), FVector(Door.PositionX, Y, 0.0));
		return true;
	}

	bool BuildHubGeometry(const FHubLayout& Layout, FHubGeometry& OutGeometry)
	{
		OutGeometry = FHubGeometry();
		if (!IsValidHubLayout(Layout))
		{
			return false;
		}

		FHubGeometry Geometry;
		const double HalfLength = Layout.InteriorSize.X * 0.5;
		const double HalfWidth = Layout.InteriorSize.Y * 0.5;
		const double Height = Layout.InteriorSize.Z;
		const double Wall = Layout.WallThickness;
		// Las paredes cierran también la cámara del techo de placas.
		const double WallHeight = Height + FMath::Max(Layout.CeilingPlenum, 0.0);

		// Paredes largas: llegan hasta el exterior de las de los extremos para cerrar las esquinas.
		for (const bool bPositive : { true, false })
		{
			TArray<FBox2D> Segments;
			SplitWallAroundOpenings(-HalfLength - Wall, HalfLength + Wall, WallHeight, CollectOpenings(Layout, bPositive), Segments);
			const double MinY = bPositive ? HalfWidth : -HalfWidth - Wall;
			for (const FBox2D& Segment : Segments)
			{
				Geometry.Walls.Add(WallBoxAlongX(Segment, MinY, MinY + Wall));
			}
		}
		const FBox2D EndWall(FVector2D(-HalfWidth, 0.0), FVector2D(HalfWidth, WallHeight));
		Geometry.Walls.Add(WallBoxAlongY(EndWall, HalfLength, HalfLength + Wall));
		Geometry.Walls.Add(WallBoxAlongY(EndWall, -HalfLength - Wall, -HalfLength));

		const FVector OuterMin(-HalfLength - Wall, -HalfWidth - Wall, 0.0);
		const FVector OuterMax(HalfLength + Wall, HalfWidth + Wall, 0.0);
		Geometry.Floor = FBox(OuterMin - FVector(0.0, 0.0, Layout.SlabThickness), OuterMax);
		Geometry.Ceiling = FBox(OuterMin + FVector(0.0, 0.0, WallHeight), OuterMax + FVector(0.0, 0.0, WallHeight + Layout.SlabThickness));

		const FVector PanelHalf(Layout.LightPanelSize.X * 0.5, Layout.LightPanelSize.Y * 0.5, 0.0);
		for (int32 Index = 0; Index < Layout.NumCeilingLights; ++Index)
		{
			const double X = -HalfLength + (Index + 0.5) * Layout.InteriorSize.X / Layout.NumCeilingLights;
			const FVector EmitterCenter(X, 0.0, Height - Layout.LightPanelThickness);
			Geometry.LightPanels.Add(FBox(EmitterCenter - PanelHalf, EmitterCenter + PanelHalf + FVector(0.0, 0.0, Layout.LightPanelThickness)));
			Geometry.LightLocations.Add(EmitterCenter);
		}

		for (int32 Index = 0; Index < Layout.Doors.Num(); ++Index)
		{
			FTransform Door;
			GetHubDoorTransform(Layout, Index, Door);
			Geometry.Doors.Add(Door);
		}

		OutGeometry = MoveTemp(Geometry);
		return true;
	}

	bool BuildFreestandingDoorWall(const FFreestandingWall& Wall, TArray<FBox>& OutBoxes)
	{
		OutBoxes.Reset();
		if (Wall.Thickness <= Epsilon)
		{
			return false;
		}
		const FWallOpening Opening = MakeDoorOpening(0.0, Wall.DoorSize, Wall.DoorFrameThickness);
		TArray<FBox2D> Segments;
		const double HalfWidth = Wall.Width * 0.5;
		if (!SplitWallAroundOpenings(-HalfWidth, HalfWidth, Wall.Height, MakeArrayView(&Opening, 1), Segments))
		{
			return false;
		}
		for (const FBox2D& Segment : Segments)
		{
			OutBoxes.Add(WallBoxAlongY(Segment, -Wall.Thickness, 0.0));
		}
		return true;
	}

	FVector GetBiomeOrigin(int32 BiomeNumber, double Spacing)
	{
		return FVector(BiomeNumber * Spacing, 0.0, 0.0);
	}
}
