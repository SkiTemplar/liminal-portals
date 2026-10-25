#include "Misc/AutomationTest.h"
#include "World/LiminalHubDressing.h"

// Tests del atrezo del hub (puro): cobertura de la moqueta y del techo, paneles alineados con las luces,
// marcos en las puertas, rodapiés que no invaden los marcos y cartel sobre una puerta. Sin using
// namespace global (unity build, D-22).

namespace LiminalHubDressingTests
{
	constexpr EAutomationTestFlags DressingTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	LiminalWorldLayout::FHubLayout MakeLayout()
	{
		LiminalWorldLayout::FHubLayout Layout;
		Layout.Doors = LiminalWorldLayout::MakeDefaultHubDoors();
		return Layout;
	}

	/** Coordenada a lo largo de la pared de una pieza de pared y su semilongitud (rodapié de 100 cm escalado en Y). */
	FVector2D AlongWall(const FTransform& Piece)
	{
		const FVector Along = Piece.GetRotation().RotateVector(FVector::YAxisVector);
		const double Center = FMath::Abs(Along.X) > 0.5 ? Piece.GetLocation().X : Piece.GetLocation().Y;
		return FVector2D(Center, 0.5 * LiminalHubDressing::BaseboardLength * Piece.GetScale3D().Y);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalHubDressingFloorCeilingTest, "LiminalPortals.World.HubDressing.FloorAndCeiling", LiminalHubDressingTests::DressingTestFlags)
bool FLiminalHubDressingFloorCeilingTest::RunTest(const FString& Parameters)
{
	using namespace LiminalHubDressing;
	using LiminalArtCatalog::EHubPiece;
	const LiminalWorldLayout::FHubLayout Layout = LiminalHubDressingTests::MakeLayout();
	FHubDressing Dressing;
	UTEST_TRUE(TEXT("Atrezo"), BuildHubDressing(Layout, Dressing));

	// Moqueta: 16 × 6 losas de 1 m, con franja de borde en las dos filas junto a las paredes largas.
	const int32 Carpet = Dressing.Get(EHubPiece::Carpet).Num();
	const int32 Border = Dressing.Get(EHubPiece::CarpetBorder).Num();
	TestEqual(TEXT("Moqueta que cubre el suelo"), Carpet + Border, 96);
	TestEqual(TEXT("Franja de borde"), Border, 32);

	// Techo: 28 × 11 módulos de 60 cm; los ocho bajo los paneles los ocupan los cuatro paneles.
	const int32 Tiles = Dressing.Get(EHubPiece::CeilingTile).Num() + Dressing.Get(EHubPiece::CeilingTileStained).Num();
	TestEqual(TEXT("Placas de techo"), Tiles, 28 * 11 - 8);
	TestTrue(TEXT("Algunas placas manchadas"), Dressing.Get(EHubPiece::CeilingTileStained).Num() > 0);
	LiminalWorldLayout::FHubGeometry Geometry;
	UTEST_TRUE(TEXT("Geometría"), LiminalWorldLayout::BuildHubGeometry(Layout, Geometry));
	const TArray<FTransform>& Panels = Dressing.Get(EHubPiece::LightPanel);
	UTEST_EQUAL(TEXT("Un panel por luz"), Panels.Num(), Geometry.LightLocations.Num());
	for (int32 Index = 0; Index < Panels.Num(); ++Index)
	{
		TestTrue(TEXT("Panel alineado con su RectLight"), FVector2D(Panels[Index].GetLocation()).Equals(FVector2D(Geometry.LightLocations[Index]), 1e-6));
		TestNearlyEqual(TEXT("Panel en el plano del techo"), Panels[Index].GetLocation().Z, Layout.InteriorSize.Z, 1e-6);
	}
	// Ninguna placa debajo de un panel y todas en el plano del techo.
	int32 UnderPanel = 0;
	for (const FTransform& Tile : Dressing.Get(EHubPiece::CeilingTile))
	{
		for (const FBox& Panel : Geometry.LightPanels)
		{
			const FVector P = Tile.GetLocation();
			UnderPanel += (P.X > Panel.Min.X && P.X < Panel.Max.X && P.Y > Panel.Min.Y && P.Y < Panel.Max.Y) ? 1 : 0;
		}
	}
	TestEqual(TEXT("Sin placas bajo los paneles"), UnderPanel, 0);

	// Negativo: layout no válido ⇒ sin atrezo.
	LiminalWorldLayout::FHubLayout Invalid = Layout;
	Invalid.Doors.Reset();
	TestFalse(TEXT("Layout sin puertas"), BuildHubDressing(Invalid, Dressing));
	TestEqual(TEXT("Atrezo vacío"), Dressing.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalHubDressingWallsTest, "LiminalPortals.World.HubDressing.WallsAndDoors", LiminalHubDressingTests::DressingTestFlags)
bool FLiminalHubDressingWallsTest::RunTest(const FString& Parameters)
{
	using namespace LiminalHubDressing;
	using LiminalArtCatalog::EHubPiece;
	const LiminalWorldLayout::FHubLayout Layout = LiminalHubDressingTests::MakeLayout();
	FHubDressing Dressing;
	UTEST_TRUE(TEXT("Atrezo"), BuildHubDressing(Layout, Dressing));

	// Marcos: uno por puerta, en el origen del portal.
	const TArray<FTransform>& Frames = Dressing.Get(EHubPiece::DoorFrame);
	UTEST_EQUAL(TEXT("Un marco por puerta"), Frames.Num(), Layout.Doors.Num());
	for (int32 Index = 0; Index < Frames.Num(); ++Index)
	{
		FTransform Door;
		LiminalWorldLayout::GetHubDoorTransform(Layout, Index, Door);
		TestTrue(TEXT("Marco en el transform de la puerta"), Frames[Index].Equals(Door, 1e-6));
	}

	// Rodapiés: cubren el perímetro menos los marcos y ninguno invade un marco.
	double Total = 0.0;
	int32 Invading = 0;
	for (const FTransform& Piece : Dressing.Get(EHubPiece::Baseboard))
	{
		const FVector2D Span = LiminalHubDressingTests::AlongWall(Piece);
		Total += 2.0 * Span.Y;
		const FVector Location = Piece.GetLocation();
		for (const LiminalWorldLayout::FHubDoorSpec& Door : Layout.Doors)
		{
			const bool bSameWall = FMath::IsNearlyEqual(Location.Y, (Door.bPositiveYWall ? 0.5 : -0.5) * Layout.InteriorSize.Y, 1e-6);
			const double Gap = FMath::Abs(Span.X - Door.PositionX) - Span.Y - DoorCasingHalfWidth;
			Invading += (bSameWall && Gap < -1e-6) ? 1 : 0;
		}
	}
	const double Perimeter = 2.0 * (Layout.InteriorSize.X + Layout.InteriorSize.Y);
	TestNearlyEqual(TEXT("Rodapié en todo el perímetro libre"), Total, Perimeter - Layout.Doors.Num() * 2.0 * DoorCasingHalfWidth, 1e-6);
	TestEqual(TEXT("Ningún rodapié invade un marco"), Invading, 0);

	// Cartel de salida sobre la tercera puerta, en su pared y por encima del tapajuntas.
	const TArray<FTransform>& Signs = Dressing.Get(EHubPiece::ExitSign);
	UTEST_EQUAL(TEXT("Un cartel"), Signs.Num(), 1);
	FTransform Exit;
	LiminalWorldLayout::GetHubDoorTransform(Layout, 2, Exit);
	TestNearlyEqual(TEXT("Cartel centrado sobre la puerta"), Signs[0].GetLocation().X, Exit.GetLocation().X, 1e-6);
	TestNearlyEqual(TEXT("Cartel en la pared de la puerta"), Signs[0].GetLocation().Y, Exit.GetLocation().Y, 1e-6);
	TestTrue(TEXT("Cartel por encima del marco y por debajo del techo"), Signs[0].GetLocation().Z > DoorCasingHeight
		&& Signs[0].GetLocation().Z + 15.0 < Layout.InteriorSize.Z);
	TestEqual(TEXT("Una silla"), Dressing.Get(EHubPiece::Chair).Num(), 1);
	TestTrue(TEXT("Enchufes y rejillas"), Dressing.Get(EHubPiece::Outlet).Num() >= 6 && Dressing.Get(EHubPiece::Vent).Num() == 3);
	TestEqual(TEXT("Un interruptor"), Dressing.Get(EHubPiece::Switch).Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalHubDressingIntervalsTest, "LiminalPortals.World.HubDressing.FreeWallIntervals", LiminalHubDressingTests::DressingTestFlags)
bool FLiminalHubDressingIntervalsTest::RunTest(const FString& Parameters)
{
	using namespace LiminalHubDressing;
	const TArray<FVector2D> Free = FreeWallIntervals(-800.0, 800.0, { -500.0, 100.0 }, 93.0);
	UTEST_EQUAL(TEXT("Tres tramos"), Free.Num(), 3);
	TestTrue(TEXT("Primer tramo"), Free[0].Equals(FVector2D(-800.0, -593.0), 1e-9));
	TestTrue(TEXT("Tramo central"), Free[1].Equals(FVector2D(-407.0, 7.0), 1e-9));
	TestTrue(TEXT("Último tramo"), Free[2].Equals(FVector2D(193.0, 800.0), 1e-9));
	// Huecos solapados se fusionan y los que salen de la pared se recortan.
	const TArray<FVector2D> Merged = FreeWallIntervals(0.0, 1000.0, { 100.0, 150.0, 990.0 }, 60.0);
	UTEST_EQUAL(TEXT("Dos tramos tras fusionar"), Merged.Num(), 2);
	TestTrue(TEXT("Antes del primer hueco"), Merged[0].Equals(FVector2D(0.0, 40.0), 1e-9));
	TestTrue(TEXT("Entre huecos"), Merged[1].Equals(FVector2D(210.0, 930.0), 1e-9));
	// Negativo: un hueco que cubre toda la pared no deja tramos.
	TestEqual(TEXT("Sin tramos"), FreeWallIntervals(0.0, 100.0, { 50.0 }, 80.0).Num(), 0);
	return true;
}
