#include "Misc/AutomationTest.h"
#include "Debug/LiminalBenchMath.h"
#include "Debug/LiminalCalibrationMath.h"
#include "World/LiminalWorldLayout.h"

// Tests de las funciones puras de Liminal.Bench (E5). Cada test incluye casos positivos y negativos.

namespace LiminalBenchMathTests
{
	constexpr EAutomationTestFlags BenchMathTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** Portal de las puertas del hub (D-16): 140 × 230 cm; ojos a 152 cm del suelo. */
	const FVector2D BenchHalfExtents(70.0, 115.0);
	constexpr double BenchEyeHeight = 152.0;
	constexpr float BenchFOV = 90.f;

	/** Centros de los rectángulos de las cuatro puertas del hub por defecto (como GetPlaneTransform). */
	TArray<FTransform> MakeDefaultHubPlanes()
	{
		LiminalWorldLayout::FHubLayout Layout;
		Layout.Doors = LiminalWorldLayout::MakeDefaultHubDoors();
		TArray<FTransform> Planes;
		for (int32 Index = 0; Index < Layout.Doors.Num(); ++Index)
		{
			FTransform Door;
			LiminalWorldLayout::GetHubDoorTransform(Layout, Index, Door);
			Planes.Add(FTransform(Door.GetRotation(), Door.TransformPositionNoScale(FVector(0.0, 0.0, BenchHalfExtents.Y))));
		}
		return Planes;
	}

	/** Portales con el centro o alguna esquina dentro del viewport (visibles de verdad, no solo por el cono). */
	int32 CountOnScreen(TConstArrayView<FTransform> Planes, const FVector& Eye, const FRotator& View, const FIntPoint& Viewport)
	{
		int32 Count = 0;
		for (const FTransform& Plane : Planes)
		{
			TArray<FVector, TInlineAllocator<5>> Points(LiminalCalibrationMath::GetPortalCorners(Plane, BenchHalfExtents));
			Points.Add(Plane.GetLocation());
			for (const FVector& Point : Points)
			{
				FVector2D Pixel;
				const bool bInside = LiminalCalibrationMath::ProjectToViewport(Eye, View, BenchFOV, Viewport, Point, Pixel)
					&& Pixel.X >= 0.0 && Pixel.X <= Viewport.X && Pixel.Y >= 0.0 && Pixel.Y <= Viewport.Y;
				if (bInside)
				{
					++Count;
					break;
				}
			}
		}
		return Count;
	}
}

using namespace LiminalBenchMathTests;
using namespace LiminalBenchMath;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalBenchPercentileTest, "LiminalPortals.Bench.Percentile.LinearInterpolation", BenchMathTestFlags)
bool FLiminalBenchPercentileTest::RunTest(const FString& Parameters)
{
	const TArray<double> Sorted = { 1.0, 2.0, 3.0, 4.0, 5.0 };
	TestNearlyEqual(TEXT("p50 de 1..5"), Percentile(Sorted, 0.5), 3.0, 1e-12);
	TestNearlyEqual(TEXT("p0 = mínimo"), Percentile(Sorted, 0.0), 1.0, 1e-12);
	TestNearlyEqual(TEXT("p100 = máximo"), Percentile(Sorted, 1.0), 5.0, 1e-12);
	// numpy.percentile([1..5], 95) = 4.8 (rango 3,8 entre 4 y 5).
	TestNearlyEqual(TEXT("p95 interpolado"), Percentile(Sorted, 0.95), 4.8, 1e-12);
	TestNearlyEqual(TEXT("p50 con número par"), Percentile(TArray<double>{ 10.0, 20.0 }, 0.5), 15.0, 1e-12);

	// Negativos: serie vacía y fracciones fuera de rango.
	TestNearlyEqual(TEXT("Vacía ⇒ 0"), Percentile(TArray<double>(), 0.5), 0.0, 1e-12);
	TestNearlyEqual(TEXT("Fracción > 1 se recorta"), Percentile(Sorted, 7.0), 5.0, 1e-12);
	TestNearlyEqual(TEXT("Fracción < 0 se recorta"), Percentile(Sorted, -1.0), 1.0, 1e-12);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalBenchStatsTest, "LiminalPortals.Bench.Stats.MeanAndPercentiles", BenchMathTestFlags)
bool FLiminalBenchStatsTest::RunTest(const FString& Parameters)
{
	// Desordenadas a propósito: ComputeStats ordena una copia.
	const TArray<double> Samples = { 5.0, 1.0, 4.0, 2.0, 3.0, 100.0 };
	const FSampleStats Stats = ComputeStats(Samples);
	UTEST_EQUAL(TEXT("Número de muestras"), Stats.Count, 6);
	TestNearlyEqual(TEXT("Media"), Stats.Mean, 115.0 / 6.0, 1e-12);
	TestNearlyEqual(TEXT("p50 (entre 3 y 4)"), Stats.P50, 3.5, 1e-12);
	// Rango 4,75 entre 5 y 100: un pico aislado sube el p95 pero no el p50.
	TestNearlyEqual(TEXT("p95"), Stats.P95, 5.0 + 0.75 * 95.0, 1e-9);
	TestNearlyEqual(TEXT("Mínimo"), Stats.Min, 1.0, 1e-12);
	TestNearlyEqual(TEXT("Máximo"), Stats.Max, 100.0, 1e-12);
	TestNearlyEqual(TEXT("La entrada no se modifica"), Samples[0], 5.0, 1e-12);

	// Negativo: sin muestras no hay estadísticos.
	const FSampleStats Empty = ComputeStats(TArray<double>());
	TestEqual(TEXT("Vacía ⇒ Count 0"), Empty.Count, 0);
	TestNearlyEqual(TEXT("Vacía ⇒ media 0"), Empty.Mean, 0.0, 1e-12);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalBenchFitLineTest, "LiminalPortals.Bench.FitLine.LeastSquares", BenchMathTestFlags)
bool FLiminalBenchFitLineTest::RunTest(const FString& Parameters)
{
	double Slope = 0.0;
	double Intercept = 0.0;
	// Recta exacta: 10 ms de base y 2,5 ms por portal en 0, 1, 2 y 4 portales.
	const TArray<double> Portals = { 0.0, 1.0, 2.0, 4.0 };
	UTEST_TRUE(TEXT("Recta exacta"), FitLine(Portals, TArray<double>{ 10.0, 12.5, 15.0, 20.0 }, Slope, Intercept));
	TestNearlyEqual(TEXT("Pendiente exacta"), Slope, 2.5, 1e-12);
	TestNearlyEqual(TEXT("Ordenada exacta"), Intercept, 10.0, 1e-12);

	// Residuos (+1, −1, −1, +1) sobre X = (0, 1, 2, 4): media de X 1,75, Σ(ΔX)² = 8,75 y
	// Σ ΔX · residuo = 1, así que la pendiente sube 1 / 8,75 respecto a la de la recta.
	UTEST_TRUE(TEXT("Con ruido"), FitLine(Portals, TArray<double>{ 11.0, 11.5, 14.0, 21.0 }, Slope, Intercept));
	TestNearlyEqual(TEXT("Pendiente con ruido"), Slope, 2.5 + 1.0 / 8.75, 1e-12);

	// Negativos: tamaños distintos, un solo punto, todas las X iguales.
	TestFalse(TEXT("Tamaños distintos"), FitLine(Portals, TArray<double>{ 1.0, 2.0 }, Slope, Intercept));
	TestFalse(TEXT("Un solo punto"), FitLine(TArray<double>{ 1.0 }, TArray<double>{ 1.0 }, Slope, Intercept));
	TestFalse(TEXT("X constantes"), FitLine(TArray<double>{ 2.0, 2.0, 2.0 }, TArray<double>{ 1.0, 2.0, 3.0 }, Slope, Intercept));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalBenchCostPerPortalTest, "LiminalPortals.Bench.CostPerPortal.Paired", BenchMathTestFlags)
bool FLiminalBenchCostPerPortalTest::RunTest(const FString& Parameters)
{
	double Cost = 0.0;
	UTEST_TRUE(TEXT("Cuatro portales"), ComputeCostPerPortal(22.0, 10.0, 4, Cost));
	TestNearlyEqual(TEXT("(22 − 10) / 4"), Cost, 3.0, 1e-12);
	UTEST_TRUE(TEXT("Más barato con portales (ruido)"), ComputeCostPerPortal(9.0, 10.0, 2, Cost));
	TestNearlyEqual(TEXT("Coste negativo admitido"), Cost, -0.5, 1e-12);

	// Negativos: sin portales no hay coste por portal (y la salida no se toca).
	Cost = 123.0;
	TestFalse(TEXT("Cero portales"), ComputeCostPerPortal(22.0, 10.0, 0, Cost));
	TestFalse(TEXT("Portales negativos"), ComputeCostPerPortal(22.0, 10.0, -1, Cost));
	TestNearlyEqual(TEXT("Salida intacta"), Cost, 123.0, 1e-12);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalBenchPosesTest, "LiminalPortals.Bench.Poses.ExpectedVisibility", BenchMathTestFlags)
bool FLiminalBenchPosesTest::RunTest(const FString& Parameters)
{
	const TArray<FTransform> Planes = MakeDefaultHubPlanes();
	UTEST_EQUAL(TEXT("Cuatro puertas"), Planes.Num(), 4);
	const TArray<FBenchPose> Poses = MakeHubBenchPoses();
	UTEST_EQUAL(TEXT("Cinco casos"), Poses.Num(), 5);

	const TArray<int32> ExpectedCaptures = { 0, 1, 2, 4, 0 };
	const TArray<FIntPoint> Viewports = { FIntPoint(1920, 1080), FIntPoint(1280, 720), FIntPoint(1600, 1200), FIntPoint(2560, 1080) };
	for (int32 Index = 0; Index < Poses.Num(); ++Index)
	{
		const FBenchPose& Pose = Poses[Index];
		TestEqual(*FString::Printf(TEXT("%s: capturas esperadas"), *Pose.Name), Pose.ExpectedCaptures, ExpectedCaptures[Index]);
		const FVector Eye(Pose.Feet.X, Pose.Feet.Y, BenchEyeHeight);
		const FRotator View(0.0, Pose.Yaw, 0.0);
		// Sala interior de 16 × 6 m con cápsula de 34 cm de radio: los pies caben dentro.
		TestTrue(*FString::Printf(TEXT("%s: dentro de la sala"), *Pose.Name),
			FMath::Abs(Pose.Feet.X) <= 800.0 - 34.0 && FMath::Abs(Pose.Feet.Y) <= 300.0 - 34.0);

		for (const FIntPoint& Viewport : Viewports)
		{
			const float Aspect = static_cast<float>(Viewport.X) / static_cast<float>(Viewport.Y);
			// Con los portales apagados el subsistema no captura, pero la vista es la del caso de 4.
			const int32 GeometricExpected = Pose.bPortalsEnabled ? Pose.ExpectedCaptures : 4;
			TestEqual(*FString::Printf(TEXT("%s a %dx%d: portales que pasan el test del subsistema"), *Pose.Name, Viewport.X, Viewport.Y),
				CountPotentiallyVisiblePortals(Planes, BenchHalfExtents, Eye, View.Vector(), BenchFOV, Aspect), GeometricExpected);
			TestEqual(*FString::Printf(TEXT("%s a %dx%d: portales en pantalla"), *Pose.Name, Viewport.X, Viewport.Y),
				CountOnScreen(Planes, Eye, View, Viewport), GeometricExpected);
		}
	}

	// Negativos: desde detrás de las puertas de +Y (fuera de la sala) no pasa ninguna de esa pared, y
	// mirando al techo con un FOV estrecho tampoco las de la pared opuesta.
	const FVector Outside(-200.0, 400.0, BenchEyeHeight);
	TestEqual(TEXT("Por detrás de la pared de +Y"),
		CountPotentiallyVisiblePortals(TArray<FTransform>{ Planes[0], Planes[2] }, BenchHalfExtents, Outside, FVector(0.0, -1.0, 0.0), BenchFOV, 16.f / 9.f), 0);
	TestEqual(TEXT("Mirando al techo"),
		CountPotentiallyVisiblePortals(Planes, BenchHalfExtents, FVector(0.0, 0.0, BenchEyeHeight), FVector::UpVector, 20.f, 16.f / 9.f), 0);
	TestEqual(TEXT("Sin portales"), CountPotentiallyVisiblePortals(TArray<FTransform>(), BenchHalfExtents, Outside, FVector::ForwardVector, BenchFOV, 1.f), 0);
	return true;
}
