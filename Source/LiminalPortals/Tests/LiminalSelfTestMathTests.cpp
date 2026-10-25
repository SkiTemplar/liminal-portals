#include "Misc/AutomationTest.h"
#include "Debug/LiminalSelfTestMath.h"
#include "Portal/LiminalPortalMath.h"

// Tests de las comprobaciones puras de Liminal.SelfTest (E3), con casos positivos y negativos.

namespace LiminalSelfTestMathTests
{
	constexpr EAutomationTestFlags SelfTestMathFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** Puerta del hub en la pared de +Y (mira a −Y) y puerta de vuelta en un bioma a 20 km (mira a +X). */
	const FTransform HubDoor(FRotator(0.0, -90.0, 0.0), FVector(-500.0, 300.0, 0.0));
	const FTransform BiomeDoor(FRotator(0.0, 0.0, 0.0), FVector(2'000'000.0, 0.0, 0.0));
}

using namespace LiminalSelfTestMathTests;
using namespace LiminalSelfTestMath;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalSelfTestMirrorTest, "LiminalPortals.Math.SelfTest.MirrorMatchesTransfer", SelfTestMathFlags)
bool FLiminalSelfTestMirrorTest::RunTest(const FString& Parameters)
{
	// La comprobación independiente coincide con el paso del portal (convención Yaw180).
	const FTransform Transfer = LiminalPortalMath::MakeTransfer(HubDoor, BiomeDoor);
	const FVector Point(-480.0, 290.0, 152.0);
	const FVector Velocity(30.0, 398.0, 0.0);
	TestTrue(TEXT("Punto"), MirrorPointThroughPortals(HubDoor, BiomeDoor, Point).Equals(LiminalPortalMath::TransferPoint(Transfer, Point), 1e-3));
	TestTrue(TEXT("Velocidad"), MirrorVectorThroughPortals(HubDoor, BiomeDoor, Velocity).Equals(LiminalPortalMath::TransferVector(Transfer, Velocity), 1e-6));

	// Entrar por la cara frontal de la puerta del hub (hacia +Y) sale alejándose de la de vuelta (+X).
	const FVector Into(0.0, 400.0, 0.0);
	TestTrue(TEXT("Sale hacia fuera de la cara frontal"), MirrorVectorThroughPortals(HubDoor, BiomeDoor, Into).Equals(FVector(400.0, 0.0, 0.0), 1e-6));
	// Negativo: sin el giro de 180° la velocidad apuntaría hacia la pared.
	TestFalse(TEXT("No es el paso sin Yaw180"), MirrorVectorThroughPortals(HubDoor, BiomeDoor, Into).Equals(FVector(-400.0, 0.0, 0.0), 1.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalSelfTestCrossingTest, "LiminalPortals.Math.SelfTest.CheckCrossing", SelfTestMathFlags)
bool FLiminalSelfTestCrossingTest::RunTest(const FString& Parameters)
{
	const FTransform Transfer = LiminalPortalMath::MakeTransfer(HubDoor, BiomeDoor);
	const FVector Before(-500.0, 302.0, 90.0);
	const FVector Velocity(0.0, 400.0, 0.0);
	const FVector After = LiminalPortalMath::TransferPoint(Transfer, Before);
	const FVector VelocityAfter = LiminalPortalMath::TransferVector(Transfer, Velocity);

	const FCrossingCheck Good = CheckCrossing(HubDoor, BiomeDoor, Before, After, Velocity, VelocityAfter);
	TestTrue(TEXT("Cruce correcto"), Good.Passed());
	TestNearlyEqual(TEXT("Sin error de velocidad"), Good.SpeedErrorRatio, 0.0, 1e-9);
	TestNearlyEqual(TEXT("Sin error de dirección"), Good.DirectionErrorDegrees, 0.0, 1e-3);

	// Negativos: velocidad sin girar, 2 % más lenta, posición sin llevar al otro lado, pawn parado.
	const FCrossingCheck NotRotated = CheckCrossing(HubDoor, BiomeDoor, Before, After, Velocity, Velocity);
	TestFalse(TEXT("Velocidad sin girar"), NotRotated.bDirectionOk);
	TestTrue(TEXT("Pero conserva el módulo"), NotRotated.bSpeedOk);
	const FCrossingCheck Slower = CheckCrossing(HubDoor, BiomeDoor, Before, After, Velocity, VelocityAfter * 0.98);
	TestFalse(TEXT("2 % más lenta"), Slower.bSpeedOk);
	TestTrue(TEXT("Dirección correcta aunque frene"), Slower.bDirectionOk);
	const FCrossingCheck WithinTolerance = CheckCrossing(HubDoor, BiomeDoor, Before, After, Velocity, VelocityAfter * 0.995);
	TestTrue(TEXT("0,5 % dentro de la tolerancia"), WithinTolerance.bSpeedOk);
	const FCrossingCheck NotMoved = CheckCrossing(HubDoor, BiomeDoor, Before, Before, Velocity, VelocityAfter);
	TestFalse(TEXT("Posición sin teletransportar"), NotMoved.bPositionOk);
	const FCrossingCheck Stopped = CheckCrossing(HubDoor, BiomeDoor, Before, After, FVector::ZeroVector, FVector::ZeroVector);
	TestFalse(TEXT("Parado no cuenta"), Stopped.Passed());
	const FCrossingCheck Zeroed = CheckCrossing(HubDoor, BiomeDoor, Before, After, Velocity, FVector::ZeroVector);
	TestFalse(TEXT("Velocidad anulada"), Zeroed.bDirectionOk || Zeroed.bSpeedOk);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalSelfTestPortalVolumeTest, "LiminalPortals.Math.SelfTest.InsidePortalVolume", SelfTestMathFlags)
bool FLiminalSelfTestPortalVolumeTest::RunTest(const FString& Parameters)
{
	// Plano del portal: centro del hueco de 140 × 230 a 115 cm de altura.
	const FTransform Plane(FRotator::ZeroRotator, FVector(0.0, 0.0, 115.0));
	const FVector2D Half(70.0, 115.0);
	const double Frame = 12.0;
	const double Depth = 160.0;

	TestTrue(TEXT("En el túnel del marco"), IsInsidePortalVolume(Plane, Half, Frame, Depth, FVector(-30.0, 10.0, 150.0)));
	TestTrue(TEXT("Detrás de la placa"), IsInsidePortalVolume(Plane, Half, Frame, Depth, FVector(-90.0, -75.0, 40.0)));

	TestFalse(TEXT("Delante del plano"), IsInsidePortalVolume(Plane, Half, Frame, Depth, FVector(5.0, 0.0, 115.0)));
	TestFalse(TEXT("Detrás pero a un lado"), IsInsidePortalVolume(Plane, Half, Frame, Depth, FVector(-30.0, 100.0, 115.0)));
	TestFalse(TEXT("Detrás pero por encima del dintel"), IsInsidePortalVolume(Plane, Half, Frame, Depth, FVector(-30.0, 0.0, 300.0)));
	TestFalse(TEXT("Más allá de la profundidad vigilada"), IsInsidePortalVolume(Plane, Half, Frame, Depth, FVector(-200.0, 0.0, 115.0)));
	return true;
}
