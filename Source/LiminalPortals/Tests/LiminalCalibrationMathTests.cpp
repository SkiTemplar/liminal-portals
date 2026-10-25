#include "Misc/AutomationTest.h"
#include "Debug/LiminalCalibrationMath.h"
#include "Debug/LiminalDebugCommands.h"

// Tests de las funciones puras de Liminal.Calibrate. Cada test incluye casos positivos y negativos.

namespace LiminalCalibrationMathTests
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	const FIntPoint Viewport(1280, 720);
}

using namespace LiminalCalibrationMathTests;
using namespace LiminalCalibrationMath;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalYawSweepTest, "LiminalPortals.Calibration.YawSweep.TriangleStep", TestFlags)
bool FLiminalYawSweepTest::RunTest(const FString& Parameters)
{
	const TArray<double> Sweep = MakeYawSweep(10.0, 20.0, 17);
	UTEST_EQUAL(TEXT("Número de fotogramas"), Sweep.Num(), 17);
	const TArray<double> Expected = { 0, 10, 20, 10, 0, -10, -20, -10, 0, 10, 20, 10, 0, -10, -20, -10, 0 };
	for (int32 Index = 0; Index < Expected.Num(); ++Index)
	{
		TestNearlyEqual(*FString::Printf(TEXT("Fotograma %d"), Index), Sweep[Index], Expected[Index], 1e-9);
	}
	for (int32 Index = 1; Index < Sweep.Num(); ++Index)
	{
		TestNearlyEqual(TEXT("Cada fotograma gira exactamente un paso"), FMath::Abs(Sweep[Index] - Sweep[Index - 1]), 10.0, 1e-9);
	}

	// La amplitud se redondea hacia abajo al múltiplo del paso.
	const TArray<double> Rounded = MakeYawSweep(10.0, 25.0, 5);
	UTEST_EQUAL(TEXT("Cinco fotogramas"), Rounded.Num(), 5);
	TestNearlyEqual(TEXT("Amplitud 25 ⇒ máximo 20"), Rounded[2], 20.0, 1e-9);
	TestNearlyEqual(TEXT("Amplitud 25 ⇒ baja tras 20"), Rounded[3], 10.0, 1e-9);

	// Negativos: parámetros no válidos dan una secuencia vacía.
	TestTrue(TEXT("Paso nulo"), MakeYawSweep(0.0, 20.0, 10).IsEmpty());
	TestTrue(TEXT("Amplitud menor que el paso"), MakeYawSweep(10.0, 5.0, 10).IsEmpty());
	TestTrue(TEXT("Sin fotogramas"), MakeYawSweep(10.0, 20.0, 0).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalProjectToViewportTest, "LiminalPortals.Calibration.ProjectToViewport.Perspective", TestFlags)
bool FLiminalProjectToViewportTest::RunTest(const FString& Parameters)
{
	const FVector Eye(0.0, 0.0, 100.0);
	const FRotator Forward(0.0, 0.0, 0.0);
	FVector2D Pixel;

	UTEST_TRUE(TEXT("Centro"), ProjectToViewport(Eye, Forward, 90.f, Viewport, Eye + FVector(500.0, 0.0, 0.0), Pixel));
	TestTrue(TEXT("El eje óptico cae en el centro"), Pixel.Equals(FVector2D(640.0, 360.0), 1e-6));

	// FOV horizontal 90°: a 45° a la derecha se llega al borde derecho.
	UTEST_TRUE(TEXT("Borde derecho"), ProjectToViewport(Eye, Forward, 90.f, Viewport, Eye + FVector(500.0, 500.0, 0.0), Pixel));
	TestNearlyEqual(TEXT("X del borde derecho"), Pixel.X, 1280.0, 1e-6);

	// Vertical con aspecto 16:9: el borde superior está en tan = 9/16 y la Y crece hacia abajo.
	UTEST_TRUE(TEXT("Borde superior"), ProjectToViewport(Eye, Forward, 90.f, Viewport, Eye + FVector(1600.0, 0.0, 900.0), Pixel));
	TestNearlyEqual(TEXT("Y del borde superior"), Pixel.Y, 0.0, 1e-6);

	// Con la cámara girada 180° el punto de detrás pasa a estar delante y en el centro.
	UTEST_TRUE(TEXT("Cámara girada"), ProjectToViewport(Eye, FRotator(0.0, 180.0, 0.0), 90.f, Viewport, Eye - FVector(300.0, 0.0, 0.0), Pixel));
	// FMath::SinCos es una aproximación polinómica: tolerancia de una milésima de píxel.
	TestTrue(TEXT("Centro tras girar"), Pixel.Equals(FVector2D(640.0, 360.0), 1e-3));

	// Negativos: detrás de la cámara, FOV o viewport no válidos.
	TestFalse(TEXT("Detrás de la cámara"), ProjectToViewport(Eye, Forward, 90.f, Viewport, Eye - FVector(10.0, 0.0, 0.0), Pixel));
	TestFalse(TEXT("FOV nulo"), ProjectToViewport(Eye, Forward, 0.f, Viewport, Eye + FVector(500.0, 0.0, 0.0), Pixel));
	TestFalse(TEXT("Viewport vacío"), ProjectToViewport(Eye, Forward, 90.f, FIntPoint(0, 720), Eye + FVector(500.0, 0.0, 0.0), Pixel));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalPortalCornersTest, "LiminalPortals.Calibration.PortalCorners.Rectangle", TestFlags)
bool FLiminalPortalCornersTest::RunTest(const FString& Parameters)
{
	const FTransform Plane(FRotator(0.0, 90.0, 0.0), FVector(100.0, 200.0, 150.0));
	const auto Corners = GetPortalCorners(Plane, FVector2D(60.0, 110.0));
	UTEST_EQUAL(TEXT("Cuatro esquinas"), Corners.Num(), 4);

	// Con yaw 90 el eje Y local apunta a -X de mundo; el ancho va en X y el alto en Z.
	TestTrue(TEXT("Primera esquina"), Corners[0].Equals(FVector(160.0, 200.0, 40.0), 1e-6));
	TestTrue(TEXT("Tercera esquina"), Corners[2].Equals(FVector(40.0, 200.0, 260.0), 1e-6));
	for (const FVector& Corner : Corners)
	{
		TestNearlyEqual(TEXT("Todas sobre el plano"), Corner.Y, 200.0, 1e-6);
	}

	// Negativo: las esquinas opuestas no coinciden y los lados contiguos no son diagonales.
	TestFalse(TEXT("Esquinas opuestas distintas"), Corners[0].Equals(Corners[2], 1.0));
	TestNearlyEqual(TEXT("Lado inferior = ancho"), FVector::Dist(Corners[0], Corners[1]), 120.0, 1e-6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalCalibrationTagTest, "LiminalPortals.Calibration.OutputTag.Validation", TestFlags)
bool FLiminalCalibrationTagTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Vacía"), LiminalDebugCommands::IsValidOutputTag(FString()));
	TestTrue(TEXT("lag0"), LiminalDebugCommands::IsValidOutputTag(TEXT("lag0")));
	TestTrue(TEXT("Con guion y subrayado"), LiminalDebugCommands::IsValidOutputTag(TEXT("tsr_lag-1")));

	// Negativos: rutas, espacios y longitud excesiva.
	TestFalse(TEXT("Sube de directorio"), LiminalDebugCommands::IsValidOutputTag(TEXT("../x")));
	TestFalse(TEXT("Con barra"), LiminalDebugCommands::IsValidOutputTag(TEXT("a/b")));
	TestFalse(TEXT("Con espacio"), LiminalDebugCommands::IsValidOutputTag(TEXT("a b")));
	TestFalse(TEXT("Demasiado larga"), LiminalDebugCommands::IsValidOutputTag(FString::ChrN(40, TEXT('a'))));
	return true;
}
