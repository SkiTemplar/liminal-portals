#include "Misc/AutomationTest.h"
#include "Math/RandomStream.h"
#include "Portal/LiminalPortalMath.h"

// Tests de la matemática pura de portales. Cada test incluye al menos un caso positivo y uno negativo.

namespace LiminalPortalMathTests
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr double PositionTolerance = 1e-3;
	constexpr double AngleTolerance = 1e-4;

	FTransform MakePortal(const FVector& Location, double Yaw, double Pitch = 0.0, double Roll = 0.0)
	{
		return FTransform(FRotator(Pitch, Yaw, Roll), Location);
	}

	/** Portal de origen en el hub y destino a 20 km girado 90°. */
	const FTransform PortalA = MakePortal(FVector(0.0, 0.0, 0.0), 0.0);
	const FTransform PortalB = MakePortal(FVector(20000.0, 500.0, 100.0), 90.0);

	bool SameRotation(const FQuat& A, const FQuat& B)
	{
		return A.AngularDistance(B) <= AngleTolerance;
	}

	FTransform RandomPortal(FRandomStream& Stream)
	{
		const FVector Location(Stream.FRandRange(-100000.0, 100000.0), Stream.FRandRange(-100000.0, 100000.0), Stream.FRandRange(-2000.0, 2000.0));
		return MakePortal(Location, Stream.FRandRange(-180.0, 180.0), Stream.FRandRange(-30.0, 30.0), Stream.FRandRange(-30.0, 30.0));
	}
}

using namespace LiminalPortalMathTests;
using namespace LiminalPortalMath;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalTransferCoincidentTest, "LiminalPortals.Math.Transfer.CoincidentOppositeIsIdentity", TestFlags)
bool FLiminalTransferCoincidentTest::RunTest(const FString& Parameters)
{
	// Calibración (E2): A y B en el mismo sitio mirando al lado contrario ⇒ paso identidad.
	const FTransform Source = MakePortal(FVector(300.0, -200.0, 50.0), 30.0);
	const FTransform Target = MakePortal(FVector(300.0, -200.0, 50.0), 210.0);
	const FTransform Transfer = MakeTransfer(Source, Target);
	const FVector Point(120.0, 40.0, 170.0);
	TestTrue(TEXT("Punto sin cambios"), TransferPoint(Transfer, Point).Equals(Point, PositionTolerance));
	TestTrue(TEXT("Rotación sin cambios"), SameRotation(TransferRotation(Transfer, FQuat::Identity), FQuat::Identity));

	// Negativo: mismo sitio y misma orientación no es la identidad (gira 180°).
	const FTransform SameFacing = MakeTransfer(Source, Source);
	TestFalse(TEXT("Misma orientación ⇒ no identidad"), TransferPoint(SameFacing, Point).Equals(Point, 1.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalTransferFrontToFrontTest, "LiminalPortals.Math.Transfer.FrontMapsBehindTarget", TestFlags)
bool FLiminalTransferFrontToFrontTest::RunTest(const FString& Parameters)
{
	const FTransform Transfer = MakeTransfer(PortalA, PortalB);
	// La cámara a 100 cm delante de A aparece 100 cm detrás de B y mira hacia fuera de su cara frontal.
	const FVector InFrontOfA = PortalA.TransformPosition(FVector(100.0, 20.0, 150.0));
	const FVector Mapped = TransferPoint(Transfer, InFrontOfA);
	TestNearlyEqual(TEXT("Distancia a B"), SignedDistanceToPlane(PortalB, Mapped), -100.0, PositionTolerance);

	const FVector LookIntoA = -PortalA.GetUnitAxis(EAxis::X);
	const FVector MappedLook = TransferVector(Transfer, LookIntoA);
	TestTrue(TEXT("Mira hacia fuera de B"), MappedLook.Equals(PortalB.GetUnitAxis(EAxis::X), PositionTolerance));

	// Negativo: un punto detrás de A queda delante de B (por eso solo cuenta el cruce frontal).
	const FVector BehindA = PortalA.TransformPosition(FVector(-50.0, 0.0, 150.0));
	TestTrue(TEXT("Detrás de A ⇒ delante de B"), SignedDistanceToPlane(PortalB, TransferPoint(Transfer, BehindA)) > 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalTransferRoundTripTest, "LiminalPortals.Math.Transfer.RoundTripIsIdentity", TestFlags)
bool FLiminalTransferRoundTripTest::RunTest(const FString& Parameters)
{
	FRandomStream Stream(20261025);
	for (int32 Index = 0; Index < 32; ++Index)
	{
		const FTransform Source = RandomPortal(Stream);
		const FTransform Target = RandomPortal(Stream);
		const FTransform There = MakeTransfer(Source, Target);
		const FTransform Back = MakeTransfer(Target, Source);

		const FVector Point = Source.TransformPosition(FVector(Stream.FRandRange(1.0, 300.0), Stream.FRandRange(-60.0, 60.0), Stream.FRandRange(0.0, 200.0)));
		const FVector Velocity = Stream.GetUnitVector() * 450.0;
		const FQuat Rotation = FRotator(Stream.FRandRange(-80.0, 80.0), Stream.FRandRange(-180.0, 180.0), 0.0).Quaternion();

		TestTrue(TEXT("Punto A→B→A"), TransferPoint(Back, TransferPoint(There, Point)).Equals(Point, PositionTolerance));
		TestTrue(TEXT("Velocidad A→B→A"), TransferVector(Back, TransferVector(There, Velocity)).Equals(Velocity, PositionTolerance));
		TestTrue(TEXT("Rotación A→B→A"), SameRotation(TransferRotation(Back, TransferRotation(There, Rotation)), Rotation));
	}

	// Negativo: aplicar dos veces A→B no devuelve al origen.
	const FTransform There = MakeTransfer(PortalA, PortalB);
	const FVector Point(100.0, 0.0, 150.0);
	TestFalse(TEXT("A→B dos veces ≠ identidad"), TransferPoint(There, TransferPoint(There, Point)).Equals(Point, 1.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalTransferVelocityTest, "LiminalPortals.Math.Transfer.VelocityPreservedAndRotated", TestFlags)
bool FLiminalTransferVelocityTest::RunTest(const FString& Parameters)
{
	const FTransform Transfer = MakeTransfer(PortalA, PortalB);
	// Andar hacia A (−X) sale por la cara frontal de B, que mira a +Y.
	const FVector Velocity(-300.0, 0.0, -50.0);
	const FVector Mapped = TransferVector(Transfer, Velocity);
	TestNearlyEqual(TEXT("Módulo conservado"), Mapped.Size(), Velocity.Size(), PositionTolerance);
	TestTrue(TEXT("Girada con el portal"), Mapped.Equals(FVector(0.0, 300.0, -50.0), PositionTolerance));

	// Negativo: la velocidad no se copia tal cual.
	TestFalse(TEXT("No es la velocidad original"), Mapped.Equals(Velocity, 1.0));
	// Negativo: el desplazamiento del portal no afecta a una dirección.
	TestFalse(TEXT("No incluye la traslación"), Mapped.Equals(TransferPoint(Transfer, Velocity), 1.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalTransferScaleTest, "LiminalPortals.Math.Transfer.IgnoresPortalScale", TestFlags)
bool FLiminalTransferScaleTest::RunTest(const FString& Parameters)
{
	FTransform ScaledA = PortalA;
	ScaledA.SetScale3D(FVector(2.0, 3.0, 0.5));
	FTransform ScaledB = PortalB;
	ScaledB.SetScale3D(FVector(4.0));
	const FVector Point(80.0, -30.0, 120.0);
	const FVector Expected = TransferPoint(MakeTransfer(PortalA, PortalB), Point);
	TestTrue(TEXT("La escala no cambia el paso"), TransferPoint(MakeTransfer(ScaledA, ScaledB), Point).Equals(Expected, PositionTolerance));

	// Negativo: la composición ingenua con escala sí se deforma.
	const FTransform Yaw180(FQuat(FVector::UpVector, UE_DOUBLE_PI));
	const FTransform Naive = ScaledA.Inverse() * Yaw180 * ScaledB;
	TestFalse(TEXT("Con escala la composición ingenua difiere"), Naive.TransformPosition(Point).Equals(Expected, 1.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalTransferTransformTest, "LiminalPortals.Math.Transfer.TransformMatchesParts", TestFlags)
bool FLiminalTransferTransformTest::RunTest(const FString& Parameters)
{
	const FTransform Transfer = MakeTransfer(PortalA, PortalB);
	const FTransform Camera(FRotator(-10.0, 170.0, 0.0), FVector(150.0, 10.0, 160.0), FVector(1.0, 1.0, 1.0));
	const FTransform Mapped = TransferTransform(Transfer, Camera);
	TestTrue(TEXT("Posición"), Mapped.GetLocation().Equals(TransferPoint(Transfer, Camera.GetLocation()), PositionTolerance));
	TestTrue(TEXT("Orientación"), SameRotation(Mapped.GetRotation(), TransferRotation(Transfer, Camera.GetRotation())));
	TestTrue(TEXT("Escala conservada"), Mapped.GetScale3D().Equals(Camera.GetScale3D()));

	// Negativo: la orientación de la cámara sí cambia al pasar (B está girado).
	TestFalse(TEXT("La orientación cambia"), SameRotation(Mapped.GetRotation(), Camera.GetRotation()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalControlRotationTest, "LiminalPortals.Math.ControlRotation.YawPitchKept", TestFlags)
bool FLiminalControlRotationTest::RunTest(const FString& Parameters)
{
	const FTransform Transfer = MakeTransfer(PortalA, PortalB);
	const FRotator Control(20.0, 180.0, 0.0);
	const FRotator Mapped = TransferControlRotation(Transfer, Control);
	// Mirando hacia A (yaw 180) se sale de B mirando a su frontal (yaw 90); el pitch no cambia.
	TestNearlyEqual(TEXT("Pitch"), Mapped.Pitch, 20.0, 1e-3);
	TestNearlyEqual(TEXT("Yaw"), FRotator::NormalizeAxis(Mapped.Yaw), 90.0, 1e-3);
	TestNearlyEqual(TEXT("Roll"), Mapped.Roll, 0.0, 1e-6);

	// Negativo: el yaw de control no se copia.
	TestFalse(TEXT("Yaw distinto del original"), FMath::IsNearlyEqual(FRotator::NormalizeAxis(Mapped.Yaw), 180.0, 1.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalControlRollTest, "LiminalPortals.Math.ControlRotation.RollForcedToZero", TestFlags)
bool FLiminalControlRollTest::RunTest(const FString& Parameters)
{
	// Destino ladeado: la rotación completa tendría roll, la de control no.
	const FTransform Tilted = MakePortal(FVector(5000.0, 0.0, 0.0), 45.0, 0.0, 30.0);
	const FTransform Transfer = MakeTransfer(PortalA, Tilted);
	const FRotator Control(10.0, 170.0, 0.0);
	const FRotator Mapped = TransferControlRotation(Transfer, Control);
	TestNearlyEqual(TEXT("Roll 0"), Mapped.Roll, 0.0, 1e-6);

	const FVector ExpectedForward = TransferVector(Transfer, Control.Vector());
	TestTrue(TEXT("La mirada se conserva"), Mapped.Vector().Equals(ExpectedForward, 1e-4));

	// Negativo: sin forzar, el roll no es 0.
	const FRotator Full = TransferRotation(Transfer, Control.Quaternion()).Rotator();
	TestTrue(TEXT("La rotación completa tiene roll"), FMath::Abs(Full.Roll) > 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalSignedDistanceTest, "LiminalPortals.Math.Plane.SignedDistance", TestFlags)
bool FLiminalSignedDistanceTest::RunTest(const FString& Parameters)
{
	TestNearlyEqual(TEXT("Delante de B"), SignedDistanceToPlane(PortalB, PortalB.TransformPosition(FVector(25.0, 40.0, 10.0))), 25.0, PositionTolerance);
	TestNearlyEqual(TEXT("Sobre el plano"), SignedDistanceToPlane(PortalB, PortalB.TransformPosition(FVector(0.0, 70.0, 10.0))), 0.0, PositionTolerance);
	// Negativo: detrás del plano la distancia es negativa.
	TestTrue(TEXT("Detrás de B"), SignedDistanceToPlane(PortalB, PortalB.TransformPosition(FVector(-5.0, 0.0, 0.0))) < 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalCrossingFrontTest, "LiminalPortals.Math.Crossing.FrontToBackInside", TestFlags)
bool FLiminalCrossingFrontTest::RunTest(const FString& Parameters)
{
	const FVector2D HalfExtents(60.0, 110.0);
	FVector Hit;
	TestTrue(TEXT("Cruce frontal → trasero"), DetectCrossing(PortalB, HalfExtents,
		PortalB.TransformPosition(FVector(10.0, 20.0, 30.0)), PortalB.TransformPosition(FVector(-10.0, 20.0, 30.0)), 1000.0, &Hit));
	TestTrue(TEXT("Punto de cruce"), Hit.Equals(PortalB.TransformPosition(FVector(0.0, 20.0, 30.0)), PositionTolerance));
	TestTrue(TEXT("Empezar sobre el plano cuenta"), DetectCrossing(PortalB, HalfExtents,
		PortalB.TransformPosition(FVector(0.0, 0.0, 0.0)), PortalB.TransformPosition(FVector(-1.0, 0.0, 0.0))));

	// Negativo: quedarse delante o terminar justo sobre el plano no es cruce.
	TestFalse(TEXT("Mismo lado"), DetectCrossing(PortalB, HalfExtents,
		PortalB.TransformPosition(FVector(30.0, 0.0, 0.0)), PortalB.TransformPosition(FVector(5.0, 0.0, 0.0))));
	TestFalse(TEXT("Terminar sobre el plano"), DetectCrossing(PortalB, HalfExtents,
		PortalB.TransformPosition(FVector(5.0, 0.0, 0.0)), PortalB.TransformPosition(FVector(0.0, 0.0, 0.0))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalCrossingBackTest, "LiminalPortals.Math.Crossing.BackToFrontIgnored", TestFlags)
bool FLiminalCrossingBackTest::RunTest(const FString& Parameters)
{
	const FVector2D HalfExtents(60.0, 110.0);
	const FVector Front = PortalA.TransformPosition(FVector(10.0, 0.0, 0.0));
	const FVector Back = PortalA.TransformPosition(FVector(-10.0, 0.0, 0.0));
	TestFalse(TEXT("Trasero → frontal no cuenta"), DetectCrossing(PortalA, HalfExtents, Back, Front));
	// Positivo de control: el mismo segmento al revés sí cuenta.
	TestTrue(TEXT("Frontal → trasero sí cuenta"), DetectCrossing(PortalA, HalfExtents, Front, Back));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalCrossingBoundsTest, "LiminalPortals.Math.Crossing.OutOfBoundsIgnored", TestFlags)
bool FLiminalCrossingBoundsTest::RunTest(const FString& Parameters)
{
	const FVector2D HalfExtents(60.0, 110.0);
	auto Cross = [&](double Y, double Z)
	{
		return DetectCrossing(PortalB, HalfExtents, PortalB.TransformPosition(FVector(10.0, Y, Z)), PortalB.TransformPosition(FVector(-10.0, Y, Z)));
	};
	TestTrue(TEXT("Dentro, junto al borde"), Cross(59.0, 109.0));
	TestFalse(TEXT("Fuera por el ancho"), Cross(61.0, 0.0));
	TestFalse(TEXT("Fuera por el alto"), Cross(0.0, -111.0));

	// Negativo: un salto mayor que MaxStep (aparición del pawn) no cuenta aunque pase por el hueco.
	TestFalse(TEXT("Salto demasiado largo"), DetectCrossing(PortalB, HalfExtents,
		PortalB.TransformPosition(FVector(800.0, 0.0, 0.0)), PortalB.TransformPosition(FVector(-800.0, 0.0, 0.0)), 1000.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalClipPlaneTest, "LiminalPortals.Math.ClipPlane.TargetFront", TestFlags)
bool FLiminalClipPlaneTest::RunTest(const FString& Parameters)
{
	const FClipPlane Plane = ComputeClipPlane(PortalB, -1.0);
	TestTrue(TEXT("Normal = frontal de B"), Plane.Normal.Equals(PortalB.GetUnitAxis(EAxis::X), 1e-6));
	TestTrue(TEXT("Base 1 cm detrás"), Plane.Base.Equals(PortalB.GetLocation() - PortalB.GetUnitAxis(EAxis::X), 1e-6));

	// Lo que hay delante de B se conserva y la placa trasera (60 cm detrás) se recorta.
	const FPlane Clip(Plane.Base, Plane.Normal);
	TestTrue(TEXT("Delante se conserva"), Clip.PlaneDot(PortalB.TransformPosition(FVector(200.0, 0.0, 0.0))) > 0.0);
	TestTrue(TEXT("La placa trasera se recorta"), Clip.PlaneDot(PortalB.TransformPosition(FVector(-60.0, 0.0, 0.0))) < 0.0);

	// Negativo: no es el plano del portal de origen.
	TestFalse(TEXT("No usa la normal del origen"), Plane.Normal.Equals(PortalA.GetUnitAxis(EAxis::X), 1e-3));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalVisibilityTest, "LiminalPortals.Math.Visibility.FrontAndCone", TestFlags)
bool FLiminalVisibilityTest::RunTest(const FString& Parameters)
{
	const FVector2D HalfExtents(60.0, 110.0);
	const FTransform Plane = MakePortal(FVector(0.0, 0.0, 110.0), 0.0);
	const FVector Camera(500.0, 0.0, 160.0);
	TestTrue(TEXT("Mirándolo de frente"), IsPortalPotentiallyVisible(Plane, HalfExtents, Camera, FVector(-1.0, 0.0, 0.0), 90.f, 16.f / 9.f));
	TestTrue(TEXT("Ladeado 40° con FOV 90"), IsPortalPotentiallyVisible(Plane, HalfExtents, Camera, FRotator(0.0, 180.0 + 40.0, 0.0).Vector(), 90.f, 16.f / 9.f));
	TestTrue(TEXT("Dentro del radio"), IsPortalPotentiallyVisible(Plane, HalfExtents, FVector(20.0, 0.0, 110.0), FVector(1.0, 0.0, 0.0), 90.f, 16.f / 9.f));
	TestTrue(TEXT("FOV inválido ⇒ conservador"), IsPortalPotentiallyVisible(Plane, HalfExtents, Camera, FVector(1.0, 0.0, 0.0), 0.f, 16.f / 9.f));

	// Negativos: de espaldas, a 90° de lado o detrás del plano.
	TestFalse(TEXT("De espaldas"), IsPortalPotentiallyVisible(Plane, HalfExtents, Camera, FVector(1.0, 0.0, 0.0), 90.f, 16.f / 9.f));
	TestFalse(TEXT("A 90° de lado"), IsPortalPotentiallyVisible(Plane, HalfExtents, Camera, FVector(0.0, 1.0, 0.0), 90.f, 16.f / 9.f));
	TestFalse(TEXT("Detrás del plano"), IsPortalPotentiallyVisible(Plane, HalfExtents, FVector(-300.0, 0.0, 160.0), FVector(1.0, 0.0, 0.0), 90.f, 16.f / 9.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalSurfaceDepthTest, "LiminalPortals.Math.Surface.DepthNearPlane", TestFlags)
bool FLiminalSurfaceDepthTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Cerca por delante ⇒ extendida"), ComputeSurfaceDepth(10.0, 60.f, 1.f, 40.f), 40.f);
	TestEqual(TEXT("Sobre el plano ⇒ extendida"), ComputeSurfaceDepth(0.0, 60.f, 1.f, 40.f), 40.f);
	// Negativos: lejos, detrás o con extendida menor que la base.
	TestEqual(TEXT("Lejos ⇒ base"), ComputeSurfaceDepth(60.0, 60.f, 1.f, 40.f), 1.f);
	TestEqual(TEXT("Detrás ⇒ base"), ComputeSurfaceDepth(-5.0, 60.f, 1.f, 40.f), 1.f);
	TestEqual(TEXT("Extendida < base ⇒ base"), ComputeSurfaceDepth(10.0, 60.f, 5.f, 2.f), 5.f);
	TestEqual(TEXT("Base negativa ⇒ 0"), ComputeSurfaceDepth(200.0, 60.f, -3.f, 40.f), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalRenderTargetSizeTest, "LiminalPortals.Math.RenderTarget.Size", TestFlags)
bool FLiminalRenderTargetSizeTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Igual al viewport"), ComputeRenderTargetSize(FIntPoint(1920, 1080), 0.f, 1.f, 4096), FIntPoint(1920, 1080));
	TestEqual(TEXT("Escala 0,5"), ComputeRenderTargetSize(FIntPoint(1920, 1080), 0.f, 0.5f, 4096), FIntPoint(960, 540));
	TestEqual(TEXT("Aspecto 4:3 recortado"), ComputeRenderTargetSize(FIntPoint(1920, 1080), 4.f / 3.f, 1.f, 4096), FIntPoint(1440, 1080));
	TestEqual(TEXT("Aspecto más ancho"), ComputeRenderTargetSize(FIntPoint(1920, 1080), 2.f, 1.f, 4096), FIntPoint(1920, 960));
	TestEqual(TEXT("Límite sin deformar"), ComputeRenderTargetSize(FIntPoint(1920, 1080), 0.f, 1.f, 1024), FIntPoint(1024, 576));

	// Negativos: viewport, escala o límite inválidos ⇒ (0, 0) y no se captura.
	TestEqual(TEXT("Viewport vacío"), ComputeRenderTargetSize(FIntPoint(0, 1080), 0.f, 1.f, 4096), FIntPoint::ZeroValue);
	TestEqual(TEXT("Escala negativa"), ComputeRenderTargetSize(FIntPoint(1920, 1080), 0.f, -1.f, 4096), FIntPoint::ZeroValue);
	TestEqual(TEXT("Límite 0"), ComputeRenderTargetSize(FIntPoint(1920, 1080), 0.f, 1.f, 0), FIntPoint::ZeroValue);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalLargeOffsetTest, "LiminalPortals.Math.Transfer.LargeWorldPrecision", TestFlags)
bool FLiminalLargeOffsetTest::RunTest(const FString& Parameters)
{
	// Biomas a k × 20 km: el ida y vuelta tiene que seguir siendo submilimétrico con LWC.
	const FTransform Far = MakePortal(FVector(80000.0 * 100.0, -3000.0, 250.0), -135.0);
	const FTransform There = MakeTransfer(PortalA, Far);
	const FTransform Back = MakeTransfer(Far, PortalA);
	const FVector Point(35.0, -12.0, 164.0);
	const FVector Round = TransferPoint(Back, TransferPoint(There, Point));
	TestTrue(TEXT("Error < 0,01 cm a 80 km"), Round.Equals(Point, 1e-2));

	// Negativo: el punto transferido está realmente lejos del original.
	TestTrue(TEXT("Llega al bioma"), FVector::Dist(TransferPoint(There, Point), Point) > 7000000.0);
	return true;
}
