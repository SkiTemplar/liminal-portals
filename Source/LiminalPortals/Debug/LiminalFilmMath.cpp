#include "Debug/LiminalFilmMath.h"

// Namespace con nombre propio (D-22): el unity build junta todos los .cpp del módulo.
namespace LiminalFilmMathDetail
{
	/** Ojos de pie (cápsula de 88 cm + 64 cm, LiminalCharacter). */
	constexpr double EyeHeight = 152.0;

	LiminalFilmMath::FFilmKey Key(double Time, double X, double Y, double Z, double Yaw, double Pitch = 0.0)
	{
		LiminalFilmMath::FFilmKey Result;
		Result.Time = Time;
		Result.Pose.Location = FVector(X, Y, Z);
		Result.Pose.Yaw = Yaw;
		Result.Pose.Pitch = Pitch;
		return Result;
	}

	/** Pose como vector de cinco componentes para interpolar todas igual. */
	struct FPoseVector
	{
		double V[5] = {};

		static FPoseVector From(const LiminalFilmMath::FFilmPose& Pose)
		{
			return { { Pose.Location.X, Pose.Location.Y, Pose.Location.Z, Pose.Yaw, Pose.Pitch } };
		}

		LiminalFilmMath::FFilmPose ToPose() const
		{
			LiminalFilmMath::FFilmPose Pose;
			Pose.Location = FVector(V[0], V[1], V[2]);
			Pose.Yaw = V[3];
			Pose.Pitch = V[4];
			return Pose;
		}
	};

	/** Tangente (por segundo) en la clave Index: Catmull-Rom en las interiores, nula en los extremos. */
	FPoseVector GetTangent(TConstArrayView<LiminalFilmMath::FFilmKey> Keys, int32 Index)
	{
		FPoseVector Tangent;
		if (Index <= 0 || Index >= Keys.Num() - 1)
		{
			return Tangent;
		}
		const FPoseVector Previous = FPoseVector::From(Keys[Index - 1].Pose);
		const FPoseVector Next = FPoseVector::From(Keys[Index + 1].Pose);
		const double Span = Keys[Index + 1].Time - Keys[Index - 1].Time;
		for (int32 Component = 0; Component < 5; ++Component)
		{
			Tangent.V[Component] = (Next.V[Component] - Previous.V[Component]) / Span;
		}
		return Tangent;
	}

	bool IsValidName(const FString& Name)
	{
		if (Name.IsEmpty())
		{
			return false;
		}
		for (const TCHAR Char : Name)
		{
			if (!FChar::IsAlnum(Char) && Char != TEXT('_'))
			{
				return false;
			}
		}
		return true;
	}
}

namespace LiminalFilmMath
{
	bool IsValidShot(const FFilmShot& Shot, int32 NumDoors, FString* OutError)
	{
		auto Fail = [OutError](const FString& Message)
		{
			if (OutError)
			{
				*OutError = Message;
			}
			return false;
		};

		if (!LiminalFilmMathDetail::IsValidName(Shot.Name))
		{
			return Fail(FString::Printf(TEXT("nombre de plano no válido: '%s'"), *Shot.Name));
		}
		if (Shot.Keys.Num() < 2)
		{
			return Fail(FString::Printf(TEXT("%s: hacen falta al menos dos claves"), *Shot.Name));
		}
		if (!FMath::IsNearlyZero(Shot.Keys[0].Time))
		{
			return Fail(FString::Printf(TEXT("%s: la primera clave no está en t = 0"), *Shot.Name));
		}
		for (int32 Index = 1; Index < Shot.Keys.Num(); ++Index)
		{
			if (!(Shot.Keys[Index].Time > Shot.Keys[Index - 1].Time))
			{
				return Fail(FString::Printf(TEXT("%s: la clave %d no avanza en el tiempo"), *Shot.Name, Index));
			}
		}
		if (Shot.Anchor == EFilmAnchor::Door && (Shot.DoorIndex < 0 || Shot.DoorIndex >= NumDoors))
		{
			return Fail(FString::Printf(TEXT("%s: puerta %d fuera de [0, %d)"), *Shot.Name, Shot.DoorIndex, NumDoors));
		}
		return true;
	}

	FFilmPose EvaluateShot(TConstArrayView<FFilmKey> Keys, double Time)
	{
		using LiminalFilmMathDetail::FPoseVector;
		if (Keys.IsEmpty())
		{
			return FFilmPose();
		}
		if (Keys.Num() == 1 || Time <= Keys[0].Time)
		{
			return Keys[0].Pose;
		}
		if (Time >= Keys.Last().Time)
		{
			return Keys.Last().Pose;
		}

		int32 Segment = 0;
		while (Segment + 1 < Keys.Num() - 1 && Time >= Keys[Segment + 1].Time)
		{
			++Segment;
		}
		const double Span = Keys[Segment + 1].Time - Keys[Segment].Time;
		const double S = (Time - Keys[Segment].Time) / Span;
		const double S2 = S * S;
		const double S3 = S2 * S;
		const double H00 = 2.0 * S3 - 3.0 * S2 + 1.0;
		const double H10 = S3 - 2.0 * S2 + S;
		const double H01 = -2.0 * S3 + 3.0 * S2;
		const double H11 = S3 - S2;

		const FPoseVector P0 = FPoseVector::From(Keys[Segment].Pose);
		const FPoseVector P1 = FPoseVector::From(Keys[Segment + 1].Pose);
		const FPoseVector M0 = LiminalFilmMathDetail::GetTangent(Keys, Segment);
		const FPoseVector M1 = LiminalFilmMathDetail::GetTangent(Keys, Segment + 1);
		FPoseVector Result;
		for (int32 Component = 0; Component < 5; ++Component)
		{
			Result.V[Component] = H00 * P0.V[Component] + H10 * Span * M0.V[Component]
				+ H01 * P1.V[Component] + H11 * Span * M1.V[Component];
		}
		return Result.ToPose();
	}

	FFilmPose ToWorld(const FTransform& Frame, const FFilmPose& Local)
	{
		FFilmPose World;
		World.Location = Frame.TransformPositionNoScale(Local.Location);
		World.Yaw = Frame.Rotator().Yaw + Local.Yaw;
		World.Pitch = Local.Pitch;
		return World;
	}

	int32 GetShotFrameCount(const FFilmShot& Shot, double SampleRate)
	{
		if (SampleRate <= 0.0 || Shot.Keys.IsEmpty())
		{
			return 0;
		}
		return FMath::FloorToInt32(Shot.GetDuration() * SampleRate + 1e-6) + 1;
	}

	int32 CountDoorCrossings(TConstArrayView<FFilmKey> Keys, double SampleRate, const FVector2D& HalfExtents)
	{
		if (Keys.Num() < 2 || SampleRate <= 0.0)
		{
			return 0;
		}
		const int32 Frames = FMath::FloorToInt32(Keys.Last().Time * SampleRate + 1e-6) + 1;
		int32 Crossings = 0;
		FVector Previous = EvaluateShot(Keys, 0.0).Location;
		for (int32 Frame = 1; Frame < Frames; ++Frame)
		{
			const FVector Current = EvaluateShot(Keys, Frame / SampleRate).Location;
			if ((Previous.X > 0.0) != (Current.X > 0.0))
			{
				const double Alpha = Previous.X / (Previous.X - Current.X);
				const FVector Hit = FMath::Lerp(Previous, Current, Alpha);
				if (FMath::Abs(Hit.Y) < HalfExtents.X && Hit.Z > 0.0 && Hit.Z < 2.0 * HalfExtents.Y)
				{
					++Crossings;
				}
			}
			Previous = Current;
		}
		return Crossings;
	}

	TArray<FFilmShot> MakeDefaultFilmShots()
	{
		using LiminalFilmMathDetail::Key;
		constexpr double Eye = LiminalFilmMathDetail::EyeHeight;
		TArray<FFilmShot> Shots;

		// Hub: travelling por el pasillo; las cuatro puertas muestran sus biomas a la vez.
		FFilmShot& Hub = Shots.AddDefaulted_GetRef();
		Hub.Name = TEXT("01_hub");
		Hub.Anchor = EFilmAnchor::Hub;
		Hub.Keys = {
			Key(0.0, -760.0, 0.0, 165.0, -6.0, -4.0),
			Key(4.0, -260.0, 40.0, 160.0, 10.0, -3.0),
			Key(8.0, 260.0, 0.0, 158.0, -4.0, -3.0),
		};

		// Selva: de frente hacia la puerta, se cruza y se gira para ver el pasillo desde la selva.
		FFilmShot& Jungle = Shots.AddDefaulted_GetRef();
		Jungle.Name = TEXT("02_jungle");
		Jungle.Anchor = EFilmAnchor::Door;
		Jungle.DoorIndex = 0;
		Jungle.Keys = {
			Key(0.0, 520.0, 40.0, Eye, 184.0, -2.0),
			Key(2.5, 240.0, 10.0, Eye, 181.0, -1.0),
			Key(5.0, -40.0, 0.0, Eye, 180.0, 0.0),
			Key(7.0, -330.0, 0.0, Eye, 182.0, -1.0),
			Key(9.5, -560.0, -40.0, Eye, 270.0, -2.0),
			Key(12.0, -640.0, -60.0, Eye + 8.0, 360.0, -2.0),
		};

		// Desierto: rodeo de la pared de vuelta; el pasillo se ve por el hueco y detrás solo hay arena.
		// El arco acaba a unos 78°: más allá se ve de canto la placa trasera del portal.
		FFilmShot& Desert = Shots.AddDefaulted_GetRef();
		Desert.Name = TEXT("03_desert");
		Desert.Anchor = EFilmAnchor::Door;
		Desert.DoorIndex = 1;
		Desert.Keys = {
			Key(0.0, -620.0, 0.0, 165.0, 360.0, -2.0),
			Key(3.0, -560.0, 200.0, 165.0, 340.0, -2.0),
			Key(6.0, -300.0, 500.0, 168.0, 300.0, -3.0),
			Key(9.0, -120.0, 580.0, 170.0, 282.0, -3.0),
		};

		// Antártida: del bioma al hub por la puerta de vuelta y giro para ver el hielo desde el pasillo.
		FFilmShot& Antarctic = Shots.AddDefaulted_GetRef();
		Antarctic.Name = TEXT("04_antarctic");
		Antarctic.Anchor = EFilmAnchor::Door;
		Antarctic.DoorIndex = 2;
		Antarctic.Keys = {
			Key(0.0, -900.0, 30.0, Eye, 0.0, -1.0),
			Key(3.5, -420.0, 10.0, Eye, 0.0, 0.0),
			Key(6.0, 40.0, 0.0, Eye, 0.0, 0.0),
			Key(8.0, 250.0, 0.0, Eye, 60.0, -1.0),
			Key(10.5, 300.0, -20.0, Eye, 180.0, -3.0),
		};

		// Cultivos: travelling lateral delante de la puerta (paralaje) y entrada hasta el camino.
		FFilmShot& Farmland = Shots.AddDefaulted_GetRef();
		Farmland.Name = TEXT("05_farmland");
		Farmland.Anchor = EFilmAnchor::Door;
		Farmland.DoorIndex = 3;
		Farmland.Keys = {
			Key(0.0, 300.0, -220.0, Eye, 143.7, -2.0),
			Key(4.0, 300.0, 220.0, Eye, 216.3, -2.0),
			Key(7.0, 120.0, 0.0, Eye, 180.0, -1.0),
			Key(9.5, -250.0, 0.0, Eye, 180.0, -1.0),
			Key(12.0, -650.0, 0.0, Eye + 10.0, 180.0, -3.0),
		};

		// Acercamientos lentos (~40 cm/s) para el montaje: la puerta del desierto desde el pasillo, sin cruzar.
		FFilmShot& PushDesert = Shots.AddDefaulted_GetRef();
		PushDesert.Name = TEXT("06_push_desert");
		PushDesert.Anchor = EFilmAnchor::Door;
		PushDesert.DoorIndex = 1;
		PushDesert.Keys = {
			Key(0.0, 480.0, 0.0, Eye, 180.0, 0.0),
			Key(12.0, 30.0, 0.0, Eye, 180.0, 0.0),
		};

		// La puerta de vuelta, sola en la selva, con el pasillo dentro; acercamiento sin cruzar.
		FFilmShot& PushJungle = Shots.AddDefaulted_GetRef();
		PushJungle.Name = TEXT("07_push_jungle");
		PushJungle.Anchor = EFilmAnchor::Door;
		PushJungle.DoorIndex = 0;
		PushJungle.Keys = {
			Key(0.0, -1100.0, 0.0, Eye, 0.0, 1.0),
			Key(12.0, -180.0, 0.0, Eye, 0.0, 0.0),
		};

		// De la nieve al pasillo, despacio, por la puerta de vuelta de la Antártida.
		FFilmShot& PushAntarctic = Shots.AddDefaulted_GetRef();
		PushAntarctic.Name = TEXT("08_push_antarctic");
		PushAntarctic.Anchor = EFilmAnchor::Door;
		PushAntarctic.DoorIndex = 2;
		PushAntarctic.Keys = {
			Key(0.0, -520.0, 0.0, Eye, 0.0, 0.0),
			Key(14.0, 140.0, 0.0, Eye, 0.0, 0.0),
		};
		return Shots;
	}
}
