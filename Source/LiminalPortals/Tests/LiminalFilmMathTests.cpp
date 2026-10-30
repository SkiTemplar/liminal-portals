#include "Misc/AutomationTest.h"
#include "Debug/LiminalFilmMath.h"
#include "Portal/LiminalPortalMath.h"

// Tests de las funciones puras de Liminal.Film. Cada test incluye casos positivos y negativos.

namespace LiminalFilmMathTests
{
	constexpr EAutomationTestFlags FilmMathFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** Hueco de las puertas del hub (D-16): 140 × 230 cm. */
	const FVector2D DoorHalfExtents(70.0, 115.0);

	LiminalFilmMath::FFilmKey MakeKey(double Time, const FVector& Location, double Yaw = 0.0, double Pitch = 0.0)
	{
		LiminalFilmMath::FFilmKey Key;
		Key.Time = Time;
		Key.Pose.Location = Location;
		Key.Pose.Yaw = Yaw;
		Key.Pose.Pitch = Pitch;
		return Key;
	}
}

using namespace LiminalFilmMathTests;
using namespace LiminalFilmMath;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalFilmEvaluateTest, "LiminalPortals.Math.Film.EvaluateShot", FilmMathFlags)
bool FLiminalFilmEvaluateTest::RunTest(const FString& Parameters)
{
	const TArray<FFilmKey> Keys = {
		MakeKey(0.0, FVector(0.0, 0.0, 150.0), 0.0, 0.0),
		MakeKey(2.0, FVector(200.0, 100.0, 150.0), 90.0, -10.0),
		MakeKey(5.0, FVector(500.0, 100.0, 160.0), 180.0, 0.0),
	};
	// Pasa por las claves y se queda en los extremos fuera del intervalo.
	TestTrue(TEXT("Primera clave"), EvaluateShot(Keys, 0.0).Location.Equals(Keys[0].Pose.Location, 1e-9));
	TestTrue(TEXT("Clave interior"), EvaluateShot(Keys, 2.0).Location.Equals(Keys[1].Pose.Location, 1e-9));
	TestNearlyEqual(TEXT("Yaw de la clave interior"), EvaluateShot(Keys, 2.0).Yaw, 90.0, 1e-9);
	TestTrue(TEXT("Antes del inicio"), EvaluateShot(Keys, -1.0).Location.Equals(Keys[0].Pose.Location, 1e-9));
	TestTrue(TEXT("Tras el final"), EvaluateShot(Keys, 9.0).Location.Equals(Keys[2].Pose.Location, 1e-9));
	TestNearlyEqual(TEXT("Yaw final"), EvaluateShot(Keys, 9.0).Yaw, 180.0, 1e-9);

	// Arranque y parada suaves: velocidad nula en los extremos (diferencia finita casi cero).
	const double Epsilon = 1e-3;
	const double StartSpeed = FVector::Dist(EvaluateShot(Keys, Epsilon).Location, EvaluateShot(Keys, 0.0).Location) / Epsilon;
	const double EndSpeed = FVector::Dist(EvaluateShot(Keys, 5.0).Location, EvaluateShot(Keys, 5.0 - Epsilon).Location) / Epsilon;
	TestTrue(TEXT("Arranca parado"), StartSpeed < 1.0);
	TestTrue(TEXT("Para al final"), EndSpeed < 1.0);
	// En la clave interior la velocidad es la de Catmull-Rom: (P2 − P0) / (t2 − t0) = (100, 20, 2) cm/s.
	// La aceleración salta en la clave: con la diferencia central el error es del orden de Step · salto.
	const double Step = 1e-5;
	const FVector MidVelocity = (EvaluateShot(Keys, 2.0 + Step).Location - EvaluateShot(Keys, 2.0 - Step).Location) / (2.0 * Step);
	TestTrue(TEXT("Tangente de Catmull-Rom"), MidVelocity.Equals(FVector(100.0, 20.0, 2.0), 0.05));
	// Negativo: no es una interpolación lineal (a mitad del primer tramo no está en el punto medio).
	TestFalse(TEXT("No es lineal"), EvaluateShot(Keys, 1.0).Location.Equals(FVector(100.0, 50.0, 150.0), 1.0));

	// Dos claves con tangentes nulas: a mitad de tiempo, el punto medio exacto (Hermite simétrico).
	const TArray<FFilmKey> Two = { MakeKey(0.0, FVector::ZeroVector, 0.0), MakeKey(4.0, FVector(400.0, 0.0, 0.0), 40.0) };
	TestTrue(TEXT("Punto medio con dos claves"), EvaluateShot(Two, 2.0).Location.Equals(FVector(200.0, 0.0, 0.0), 1e-9));
	TestNearlyEqual(TEXT("Yaw medio"), EvaluateShot(Two, 2.0).Yaw, 20.0, 1e-9);
	// El yaw no se envuelve: de 180 a 360 pasa por 270, no por 0.
	const TArray<FFilmKey> Turn = { MakeKey(0.0, FVector::ZeroVector, 180.0), MakeKey(2.0, FVector::ZeroVector, 360.0) };
	TestNearlyEqual(TEXT("Giro sin envolver"), EvaluateShot(Turn, 1.0).Yaw, 270.0, 1e-9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalFilmValidateTest, "LiminalPortals.Math.Film.IsValidShot", FilmMathFlags)
bool FLiminalFilmValidateTest::RunTest(const FString& Parameters)
{
	FFilmShot Shot;
	Shot.Name = TEXT("02_jungle");
	Shot.Anchor = EFilmAnchor::Door;
	Shot.DoorIndex = 0;
	Shot.Keys = { MakeKey(0.0, FVector(300.0, 0.0, 152.0)), MakeKey(3.0, FVector(-300.0, 0.0, 152.0)) };
	FString Error;
	TestTrue(TEXT("Plano válido"), IsValidShot(Shot, 4, &Error));

	FFilmShot OneKey = Shot;
	OneKey.Keys.SetNum(1);
	TestFalse(TEXT("Una sola clave"), IsValidShot(OneKey, 4, &Error));
	FFilmShot LateStart = Shot;
	LateStart.Keys[0].Time = 0.5;
	TestFalse(TEXT("No empieza en 0"), IsValidShot(LateStart, 4, &Error));
	FFilmShot Backwards = Shot;
	Backwards.Keys[1].Time = 0.0;
	TestFalse(TEXT("Tiempos que no avanzan"), IsValidShot(Backwards, 4, &Error));
	FFilmShot BadDoor = Shot;
	BadDoor.DoorIndex = 4;
	TestFalse(TEXT("Puerta inexistente"), IsValidShot(BadDoor, 4, &Error));
	FFilmShot BadName = Shot;
	BadName.Name = TEXT("../x");
	TestFalse(TEXT("Nombre con separadores de ruta"), IsValidShot(BadName, 4, &Error));
	FFilmShot HubShot = Shot;
	HubShot.Anchor = EFilmAnchor::Hub;
	HubShot.DoorIndex = INDEX_NONE;
	TestTrue(TEXT("El hub no necesita puerta"), IsValidShot(HubShot, 4, &Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalFilmCrossingsTest, "LiminalPortals.Math.Film.CountDoorCrossings", FilmMathFlags)
bool FLiminalFilmCrossingsTest::RunTest(const FString& Parameters)
{
	const TArray<FFilmKey> Through = { MakeKey(0.0, FVector(400.0, 0.0, 152.0)), MakeKey(4.0, FVector(-400.0, 0.0, 152.0)) };
	TestEqual(TEXT("De frente por el hueco"), CountDoorCrossings(Through, 60.0, DoorHalfExtents), 1);
	const TArray<FFilmKey> Back = {
		MakeKey(0.0, FVector(400.0, 0.0, 152.0)), MakeKey(3.0, FVector(-300.0, 20.0, 152.0)), MakeKey(6.0, FVector(300.0, 0.0, 152.0)),
	};
	TestEqual(TEXT("Ida y vuelta"), CountDoorCrossings(Back, 60.0, DoorHalfExtents), 2);
	// Negativos: rodear la pared por fuera del hueco, pasar por encima del dintel o no llegar al plano.
	const TArray<FFilmKey> Around = { MakeKey(0.0, FVector(-600.0, 0.0, 160.0)), MakeKey(4.0, FVector(50.0, 580.0, 160.0)) };
	TestEqual(TEXT("Rodeo por el lado"), CountDoorCrossings(Around, 60.0, DoorHalfExtents), 0);
	const TArray<FFilmKey> Above = { MakeKey(0.0, FVector(300.0, 0.0, 260.0)), MakeKey(2.0, FVector(-300.0, 0.0, 260.0)) };
	TestEqual(TEXT("Por encima del hueco"), CountDoorCrossings(Above, 60.0, DoorHalfExtents), 0);
	const TArray<FFilmKey> Short = { MakeKey(0.0, FVector(400.0, 0.0, 152.0)), MakeKey(2.0, FVector(10.0, 0.0, 152.0)) };
	TestEqual(TEXT("Se queda delante"), CountDoorCrossings(Short, 60.0, DoorHalfExtents), 0);
	TestEqual(TEXT("Frecuencia no válida"), CountDoorCrossings(Through, 0.0, DoorHalfExtents), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalFilmWorldTest, "LiminalPortals.Math.Film.ToWorldAndTransfer", FilmMathFlags)
bool FLiminalFilmWorldTest::RunTest(const FString& Parameters)
{
	// Puerta del hub en la pared de +Y (mira a −Y) y puerta de vuelta en un bioma a 20 km (mira a +X).
	const FTransform HubDoor(FRotator(0.0, -90.0, 0.0), FVector(-500.0, 300.0, 0.0));
	const FTransform BiomeDoor(FRotator(0.0, 0.0, 0.0), FVector(2'000'000.0, 0.0, 0.0));
	FFilmPose Local;
	Local.Location = FVector(200.0, 30.0, 152.0);
	Local.Yaw = 180.0;
	Local.Pitch = -5.0;
	const FFilmPose World = ToWorld(HubDoor, Local);
	TestTrue(TEXT("Posición en el hub"), World.Location.Equals(FVector(-470.0, 100.0, 152.0), 1e-6));
	TestNearlyEqual(TEXT("Mira hacia la puerta (+Y)"), FRotator::NormalizeAxis(World.Yaw), 90.0, 1e-9);
	TestNearlyEqual(TEXT("Pitch sin cambios"), World.Pitch, -5.0, 1e-9);

	// Del lado del bioma, la pose desplegada (X < 0) pasada por el portal cae delante de la puerta de vuelta.
	const FTransform Transfer = LiminalPortalMath::MakeTransfer(HubDoor, BiomeDoor);
	Local.Location = FVector(-300.0, 30.0, 152.0);
	const FVector Beyond = LiminalPortalMath::TransferPoint(Transfer, ToWorld(HubDoor, Local).Location);
	TestTrue(TEXT("Delante de la puerta de vuelta"), Beyond.Equals(BiomeDoor.TransformPositionNoScale(FVector(300.0, -30.0, 152.0)), 1e-3));
	TestTrue(TEXT("Por delante del plano"), LiminalPortalMath::SignedDistanceToPlane(BiomeDoor, Beyond) > 0.0);
	// Negativo: sin el paso, la misma pose queda detrás de la pared del hub.
	TestTrue(TEXT("Sin paso, detrás de la puerta del hub"), LiminalPortalMath::SignedDistanceToPlane(HubDoor, ToWorld(HubDoor, Local).Location) < 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalFilmDefaultShotsTest, "LiminalPortals.Math.Film.DefaultShots", FilmMathFlags)
bool FLiminalFilmDefaultShotsTest::RunTest(const FString& Parameters)
{
	const TArray<FFilmShot> Shots = MakeDefaultFilmShots();
	TestEqual(TEXT("Ocho planos"), Shots.Num(), 8);
	double Total = 0.0;
	for (const FFilmShot& Shot : Shots)
	{
		FString Error;
		TestTrue(*FString::Printf(TEXT("%s válido (%s)"), *Shot.Name, *Error), IsValidShot(Shot, 4, &Error));
		Total += Shot.GetDuration();
		// Clips de galería de 6 a 15 s.
		TestTrue(*FString::Printf(TEXT("%s dura entre 6 y 15 s"), *Shot.Name), Shot.GetDuration() >= 6.0 && Shot.GetDuration() <= 15.0);
	}
	TestTrue(TEXT("Material de 60 a 100 s"), Total >= 60.0 && Total <= 100.0);
	if (Shots.Num() != 8)
	{
		return false;
	}
	// Cruces previstos: uno en la selva, la Antártida y los cultivos; ninguno en el rodeo del desierto.
	TestEqual(TEXT("Selva"), CountDoorCrossings(Shots[1].Keys, 60.0, DoorHalfExtents), 1);
	TestEqual(TEXT("Desierto"), CountDoorCrossings(Shots[2].Keys, 60.0, DoorHalfExtents), 0);
	TestEqual(TEXT("Antártida"), CountDoorCrossings(Shots[3].Keys, 60.0, DoorHalfExtents), 1);
	TestEqual(TEXT("Cultivos"), CountDoorCrossings(Shots[4].Keys, 60.0, DoorHalfExtents), 1);
	// Acercamientos: los dos primeros se paran antes del plano; el de la Antártida cruza al pasillo.
	TestEqual(TEXT("Acercamiento al desierto"), CountDoorCrossings(Shots[5].Keys, 60.0, DoorHalfExtents), 0);
	TestEqual(TEXT("Acercamiento en la selva"), CountDoorCrossings(Shots[6].Keys, 60.0, DoorHalfExtents), 0);
	TestEqual(TEXT("Acercamiento en la Antártida"), CountDoorCrossings(Shots[7].Keys, 60.0, DoorHalfExtents), 1);
	TestEqual(TEXT("Fotogramas a 60 Hz de un plano de 12 s"), GetShotFrameCount(Shots[1], 60.0), 721);
	return true;
}
