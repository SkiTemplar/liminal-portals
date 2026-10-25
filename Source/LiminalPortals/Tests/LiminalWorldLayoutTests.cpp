#include "Misc/AutomationTest.h"
#include "World/LiminalWorldLayout.h"

// Tests de la geometría pura del hub y de los biomas (E3). Cada test tiene casos positivos y negativos.

namespace LiminalWorldLayoutTests
{
	constexpr EAutomationTestFlags LayoutTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	double Area(const FBox2D& Box)
	{
		const FVector2D Size = Box.GetSize();
		return Size.X * Size.Y;
	}

	bool AnyContains(TConstArrayView<FBox> Boxes, const FVector& Point)
	{
		for (const FBox& Box : Boxes)
		{
			// Estrictamente dentro: las caras compartidas no cuentan.
			if (Point.X > Box.Min.X && Point.X < Box.Max.X && Point.Y > Box.Min.Y && Point.Y < Box.Max.Y
				&& Point.Z > Box.Min.Z && Point.Z < Box.Max.Z)
			{
				return true;
			}
		}
		return false;
	}

	LiminalWorldLayout::FHubLayout MakeDefaultLayout()
	{
		LiminalWorldLayout::FHubLayout Layout;
		Layout.Doors = LiminalWorldLayout::MakeDefaultHubDoors();
		return Layout;
	}
}

using namespace LiminalWorldLayoutTests;
using namespace LiminalWorldLayout;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalSplitWallTest, "LiminalPortals.World.Layout.SplitWallAroundOpenings", LayoutTestFlags)
bool FLiminalSplitWallTest::RunTest(const FString& Parameters)
{
	TArray<FBox2D> Segments;
	TestTrue(TEXT("Pared sin huecos"), SplitWallAroundOpenings(0.0, 1000.0, 300.0, {}, Segments));
	TestEqual(TEXT("Un solo tramo"), Segments.Num(), 1);

	// Dos huecos: tramo, dintel, tramo, dintel, tramo; el área es la de la pared menos los huecos.
	const FWallOpening Openings[] = { { 600.0, 200.0, 250.0 }, { 200.0, 100.0, 200.0 } };
	TestTrue(TEXT("Pared con dos huecos"), SplitWallAroundOpenings(0.0, 1000.0, 300.0, Openings, Segments));
	TestEqual(TEXT("Cinco rectángulos"), Segments.Num(), 5);
	double Total = 0.0;
	for (const FBox2D& Segment : Segments)
	{
		Total += Area(Segment);
		// Ningún rectángulo pisa el interior de un hueco.
		for (const FWallOpening& Opening : Openings)
		{
			const FBox2D Hole(FVector2D(Opening.Center - Opening.Width * 0.5, 0.0), FVector2D(Opening.Center + Opening.Width * 0.5, Opening.Height));
			const FBox2D Shrunk(Hole.Min + FVector2D(1e-3), Hole.Max - FVector2D(1e-3));
			TestFalse(TEXT("Sin solape con el hueco"), Segment.Intersect(Shrunk));
		}
	}
	TestNearlyEqual(TEXT("Área conservada"), Total, 1000.0 * 300.0 - 200.0 * 250.0 - 100.0 * 200.0, 1e-6);

	// Hueco pegado al borde: no hay tramo de anchura cero.
	const FWallOpening AtEdge[] = { { 50.0, 100.0, 200.0 } };
	TestTrue(TEXT("Hueco en el borde"), SplitWallAroundOpenings(0.0, 1000.0, 300.0, AtEdge, Segments));
	TestEqual(TEXT("Dintel y un tramo"), Segments.Num(), 2);

	// Negativos: solape, fuera de la pared, más alto que la pared, pared vacía.
	const FWallOpening Overlap[] = { { 300.0, 200.0, 200.0 }, { 400.0, 200.0, 200.0 } };
	TestFalse(TEXT("Huecos solapados"), SplitWallAroundOpenings(0.0, 1000.0, 300.0, Overlap, Segments));
	TestTrue(TEXT("Sin rectángulos si falla"), Segments.IsEmpty());
	const FWallOpening Outside[] = { { 980.0, 100.0, 200.0 } };
	TestFalse(TEXT("Hueco fuera de la pared"), SplitWallAroundOpenings(0.0, 1000.0, 300.0, Outside, Segments));
	const FWallOpening TooTall[] = { { 500.0, 100.0, 300.0 } };
	TestFalse(TEXT("Hueco tan alto como la pared"), SplitWallAroundOpenings(0.0, 1000.0, 300.0, TooTall, Segments));
	TestFalse(TEXT("Pared de longitud nula"), SplitWallAroundOpenings(10.0, 10.0, 300.0, {}, Segments));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalHubDoorsTest, "LiminalPortals.World.Layout.HubDoors", LayoutTestFlags)
bool FLiminalHubDoorsTest::RunTest(const FString& Parameters)
{
	const FHubLayout Layout = MakeDefaultLayout();
	FString Error;
	TestTrue(TEXT("Layout por defecto válido"), IsValidHubLayout(Layout, &Error));
	TestEqual(TEXT("Cuatro puertas"), Layout.Doors.Num(), 4);
	TestTrue(TEXT("Puerta más ancha que la cápsula (68 cm) con holgura"), Layout.DoorSize.X >= 68.0 + 2.0 * 30.0);

	for (int32 Index = 0; Index < Layout.Doors.Num(); ++Index)
	{
		FTransform Door;
		UTEST_TRUE(TEXT("Transform de puerta"), GetHubDoorTransform(Layout, Index, Door));
		// En la cara interior de una pared larga, a ras de suelo, con el eje X hacia el centro de la sala.
		TestNearlyEqual(TEXT("Sobre la cara interior"), FMath::Abs(Door.GetLocation().Y), Layout.InteriorSize.Y * 0.5, 1e-6);
		TestNearlyEqual(TEXT("A ras de suelo"), Door.GetLocation().Z, 0.0, 1e-6);
		const FVector Inward = FVector(0.0, -Door.GetLocation().Y, 0.0).GetSafeNormal();
		TestTrue(TEXT("Mira hacia dentro"), Door.GetUnitAxis(EAxis::X).Equals(Inward, 1e-6));
	}

	// Negativos: índice fuera de rango, puertas solapadas, puerta en la esquina, sala sin puertas, puerta más alta que la sala.
	FTransform Unused;
	TestFalse(TEXT("Índice 4 no existe"), GetHubDoorTransform(Layout, 4, Unused));
	TestFalse(TEXT("Índice negativo"), GetHubDoorTransform(Layout, -1, Unused));
	FHubLayout Overlap = Layout;
	Overlap.Doors[2].PositionX = Overlap.Doors[0].PositionX + 50.0;
	TestFalse(TEXT("Puertas solapadas"), IsValidHubLayout(Overlap));
	FHubLayout Corner = Layout;
	Corner.Doors[0].PositionX = -Layout.InteriorSize.X * 0.5 + 10.0;
	TestFalse(TEXT("Puerta en la esquina"), IsValidHubLayout(Corner));
	FHubLayout NoDoors = Layout;
	NoDoors.Doors.Reset();
	TestFalse(TEXT("Sin puertas"), IsValidHubLayout(NoDoors));
	FHubLayout Tall = Layout;
	Tall.DoorSize.Y = Layout.InteriorSize.Z;
	TestFalse(TEXT("Puerta más alta que la sala"), IsValidHubLayout(Tall));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalHubGeometryTest, "LiminalPortals.World.Layout.HubGeometry", LayoutTestFlags)
bool FLiminalHubGeometryTest::RunTest(const FString& Parameters)
{
	const FHubLayout Layout = MakeDefaultLayout();
	FHubGeometry Geometry;
	UTEST_TRUE(TEXT("Geometría"), BuildHubGeometry(Layout, Geometry));
	TestEqual(TEXT("Una puerta por especificación"), Geometry.Doors.Num(), Layout.Doors.Num());
	TestEqual(TEXT("Un panel por luz"), Geometry.LightPanels.Num(), Layout.NumCeilingLights);
	TestEqual(TEXT("Una posición por luz"), Geometry.LightLocations.Num(), Layout.NumCeilingLights);

	const double HalfLength = Layout.InteriorSize.X * 0.5;
	const double HalfWidth = Layout.InteriorSize.Y * 0.5;
	const double MidWall = HalfWidth + Layout.WallThickness * 0.5;
	for (const FTransform& Door : Geometry.Doors)
	{
		const double WallY = FMath::Sign(Door.GetLocation().Y) * MidWall;
		// El hueco atraviesa la pared: libre en su centro y junto al marco, macizo al lado.
		const FVector HoleCenter(Door.GetLocation().X, WallY, Layout.DoorSize.Y * 0.5);
		TestFalse(TEXT("Hueco libre en el centro"), AnyContains(Geometry.Walls, HoleCenter));
		const double FrameEdge = Layout.DoorSize.X * 0.5 + Layout.DoorFrameThickness - 1.0;
		TestFalse(TEXT("Hueco libre junto al marco"), AnyContains(Geometry.Walls, HoleCenter + FVector(FrameEdge, 0.0, 0.0)));
		const double Beside = Layout.DoorSize.X * 0.5 + Layout.DoorFrameThickness + 5.0;
		TestTrue(TEXT("Pared maciza al lado del hueco"), AnyContains(Geometry.Walls, HoleCenter + FVector(Beside, 0.0, 0.0)));
		TestTrue(TEXT("Dintel sobre el hueco"), AnyContains(Geometry.Walls, FVector(Door.GetLocation().X, WallY, Layout.InteriorSize.Z - 5.0)));
	}

	// Sala cerrada: paredes en los extremos y esquinas, suelo y techo cubren el exterior de las paredes.
	TestTrue(TEXT("Pared del extremo +X"), AnyContains(Geometry.Walls, FVector(HalfLength + 5.0, 0.0, 100.0)));
	TestTrue(TEXT("Pared del extremo −X"), AnyContains(Geometry.Walls, FVector(-HalfLength - 5.0, 0.0, 100.0)));
	TestTrue(TEXT("Esquina cerrada"), AnyContains(Geometry.Walls, FVector(HalfLength + 5.0, HalfWidth + 5.0, 100.0)));
	TestTrue(TEXT("Suelo con la cara superior en Z = 0"), FMath::IsNearlyZero(Geometry.Floor.Max.Z));
	TestTrue(TEXT("Suelo bajo las paredes"), Geometry.Floor.Min.X <= -HalfLength - Layout.WallThickness && Geometry.Floor.Max.Y >= HalfWidth + Layout.WallThickness);
	TestTrue(TEXT("Techo a la altura de la sala"), FMath::IsNearlyEqual(Geometry.Ceiling.Min.Z, Layout.InteriorSize.Z));
	TestFalse(TEXT("Interior libre"), AnyContains(Geometry.Walls, FVector(0.0, 0.0, 150.0)));
	for (const FBox& Panel : Geometry.LightPanels)
	{
		TestTrue(TEXT("Panel pegado al techo"), FMath::IsNearlyEqual(Panel.Max.Z, Layout.InteriorSize.Z));
	}

	// Negativo: layout no válido ⇒ sin geometría.
	FHubLayout Invalid = Layout;
	Invalid.Doors.Reset();
	TestFalse(TEXT("Layout sin puertas"), BuildHubGeometry(Invalid, Geometry));
	TestTrue(TEXT("Geometría vacía"), Geometry.Walls.IsEmpty() && Geometry.Doors.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalFreestandingWallTest, "LiminalPortals.World.Layout.FreestandingWallAndBiomeOrigin", LayoutTestFlags)
bool FLiminalFreestandingWallTest::RunTest(const FString& Parameters)
{
	const FFreestandingWall Wall;
	TArray<FBox> Boxes;
	UTEST_TRUE(TEXT("Pared exenta"), BuildFreestandingDoorWall(Wall, Boxes));
	TestEqual(TEXT("Dos tramos y un dintel"), Boxes.Num(), 3);
	const double MidX = -Wall.Thickness * 0.5;
	TestFalse(TEXT("Hueco de la puerta libre"), AnyContains(Boxes, FVector(MidX, 0.0, Wall.DoorSize.Y * 0.5)));
	TestTrue(TEXT("Pared al lado"), AnyContains(Boxes, FVector(MidX, Wall.Width * 0.5 - 5.0, 100.0)));
	TestTrue(TEXT("Dintel"), AnyContains(Boxes, FVector(MidX, 0.0, Wall.Height - 5.0)));
	TestFalse(TEXT("Nada por delante del plano"), AnyContains(Boxes, FVector(5.0, Wall.Width * 0.5 - 5.0, 100.0)));

	// Negativo: la puerta no cabe.
	FFreestandingWall Narrow = Wall;
	Narrow.Width = Wall.DoorSize.X;
	TestFalse(TEXT("Pared más estrecha que la puerta con marco"), BuildFreestandingDoorWall(Narrow, Boxes));
	TestTrue(TEXT("Sin cajas si falla"), Boxes.IsEmpty());

	TestTrue(TEXT("Bioma 3 a 60 km"), GetBiomeOrigin(3).Equals(FVector(6'000'000.0, 0.0, 0.0)));
	TestFalse(TEXT("Biomas distintos no coinciden"), GetBiomeOrigin(1).Equals(GetBiomeOrigin(2)));
	return true;
}
