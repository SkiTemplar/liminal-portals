#include "Debug/LiminalBenchMath.h"

#include "Portal/LiminalPortalMath.h"

namespace LiminalBenchMath
{
	namespace
	{
		/** Varianza mínima de X para ajustar una recta (X son números de portales: enteros). */
		constexpr double MinVarianceX = 1e-12;
	}

	double Percentile(TConstArrayView<double> SortedSamples, double Fraction)
	{
		const int32 Count = SortedSamples.Num();
		if (Count == 0)
		{
			return 0.0;
		}
		const double Rank = FMath::Clamp(Fraction, 0.0, 1.0) * static_cast<double>(Count - 1);
		const int32 Lower = FMath::FloorToInt32(Rank);
		const int32 Upper = FMath::Min(Lower + 1, Count - 1);
		const double Weight = Rank - static_cast<double>(Lower);
		return SortedSamples[Lower] + (SortedSamples[Upper] - SortedSamples[Lower]) * Weight;
	}

	FSampleStats ComputeStats(TConstArrayView<double> Samples)
	{
		FSampleStats Stats;
		if (Samples.IsEmpty())
		{
			return Stats;
		}
		TArray<double> Sorted(Samples.GetData(), Samples.Num());
		Sorted.Sort();

		double Sum = 0.0;
		for (const double Sample : Sorted)
		{
			Sum += Sample;
		}
		Stats.Count = Sorted.Num();
		Stats.Mean = Sum / static_cast<double>(Stats.Count);
		Stats.P50 = Percentile(Sorted, 0.5);
		Stats.P95 = Percentile(Sorted, 0.95);
		Stats.Min = Sorted[0];
		Stats.Max = Sorted.Last();
		return Stats;
	}

	bool FitLine(TConstArrayView<double> X, TConstArrayView<double> Y, double& OutSlope, double& OutIntercept)
	{
		const int32 Count = X.Num();
		if (Count != Y.Num() || Count < 2)
		{
			return false;
		}
		double MeanX = 0.0;
		double MeanY = 0.0;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			MeanX += X[Index];
			MeanY += Y[Index];
		}
		MeanX /= static_cast<double>(Count);
		MeanY /= static_cast<double>(Count);

		double CovarianceXY = 0.0;
		double VarianceX = 0.0;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const double DeltaX = X[Index] - MeanX;
			CovarianceXY += DeltaX * (Y[Index] - MeanY);
			VarianceX += DeltaX * DeltaX;
		}
		if (VarianceX <= MinVarianceX)
		{
			return false;
		}
		OutSlope = CovarianceXY / VarianceX;
		OutIntercept = MeanY - OutSlope * MeanX;
		return true;
	}

	bool ComputeCostPerPortal(double WithPortals, double WithoutPortals, int32 NumPortals, double& OutCostPerPortal)
	{
		if (NumPortals <= 0)
		{
			return false;
		}
		OutCostPerPortal = (WithPortals - WithoutPortals) / static_cast<double>(NumPortals);
		return true;
	}

	TArray<FBenchPose> MakeHubBenchPoses()
	{
		// Puertas por defecto: X = −500 y +100 en la pared de +Y (miran a −Y), −100 y +500 en la de −Y.
		// Ojos a 152 cm (cápsula de 88 + 64): las poses quedan lejos de cualquier plano de portal.
		return {
			// De cara a la pared del fondo (−X): todas las puertas quedan detrás o fuera del cono.
			{ TEXT("portals0"), FVector2D(-650.0, 0.0), 180.0, 0, true },
			// A 1,5 m de la puerta de X = +100, mirándola de frente.
			{ TEXT("portals1"), FVector2D(100.0, 150.0), 90.0, 1, true },
			// Desde el lado de −Y mirando a la pared de +Y: sus dos puertas, a ±31° del centro.
			{ TEXT("portals2"), FVector2D(-200.0, -200.0), 90.0, 2, true },
			// Desde el fondo, pegado a la pared de −Y y girado 15° hacia +Y: las cuatro en pantalla.
			{ TEXT("portals4"), FVector2D(-760.0, -150.0), 15.0, 4, true },
			// Misma vista con los portales apagados: referencia del coste de las cuatro capturas.
			{ TEXT("portals4_disabled"), FVector2D(-760.0, -150.0), 15.0, 0, false },
		};
	}

	int32 CountPotentiallyVisiblePortals(TConstArrayView<FTransform> PlaneTransforms, const FVector2D& HalfExtents,
		const FVector& CameraLocation, const FVector& CameraForward, float HorizontalFOVDegrees, float AspectRatio)
	{
		int32 Count = 0;
		for (const FTransform& Plane : PlaneTransforms)
		{
			if (LiminalPortalMath::IsPortalPotentiallyVisible(Plane, HalfExtents, CameraLocation, CameraForward,
				HorizontalFOVDegrees, AspectRatio))
			{
				++Count;
			}
		}
		return Count;
	}

	TArray<FBenchPose> MakeHubWalkPoses()
	{
		constexpr double FirstX = -650.0;
		constexpr double StepX = 250.0;
		constexpr int32 NumX = 6;
		constexpr double Ys[] = { -150.0, 0.0, 150.0 };
		constexpr int32 NumYaws = 12;
		TArray<FBenchPose> Poses;
		Poses.Reserve(NumX * UE_ARRAY_COUNT(Ys) * NumYaws);
		for (int32 IndexX = 0; IndexX < NumX; ++IndexX)
		{
			for (const double Y : Ys)
			{
				for (int32 IndexYaw = 0; IndexYaw < NumYaws; ++IndexYaw)
				{
					FBenchPose& Pose = Poses.AddDefaulted_GetRef();
					const double X = FirstX + StepX * IndexX;
					const double Yaw = 360.0 * IndexYaw / NumYaws;
					Pose.Name = FString::Printf(TEXT("walk_%.0f_%.0f_%.0f"), X, Y, Yaw);
					Pose.Feet = FVector2D(X, Y);
					Pose.Yaw = Yaw;
					Pose.ExpectedCaptures = -1;
				}
			}
		}
		return Poses;
	}

	double ComputeScreenRectFraction(TConstArrayView<FVector> Corners, const FVector& ViewLocation,
		const FRotator& ViewRotation, float HorizontalFOVDegrees, float AspectRatio, double NearPlane)
	{
		const bool bValidView = HorizontalFOVDegrees > 0.f && HorizontalFOVDegrees < 180.f && AspectRatio > UE_KINDA_SMALL_NUMBER
			&& NearPlane > 0.0;
		if (!bValidView || Corners.Num() < 3)
		{
			return 0.0;
		}

		// Espacio de cámara de UE: X adelante, Y derecha, Z arriba.
		TArray<FVector, TInlineAllocator<8>> Local;
		for (const FVector& Corner : Corners)
		{
			Local.Add(ViewRotation.UnrotateVector(Corner - ViewLocation));
		}

		// Sutherland-Hodgman contra el plano X = NearPlane.
		TArray<FVector, TInlineAllocator<8>> Clipped;
		for (int32 Index = 0; Index < Local.Num(); ++Index)
		{
			const FVector& Current = Local[Index];
			const FVector& Next = Local[(Index + 1) % Local.Num()];
			const bool bCurrentIn = Current.X >= NearPlane;
			const bool bNextIn = Next.X >= NearPlane;
			if (bCurrentIn)
			{
				Clipped.Add(Current);
			}
			if (bCurrentIn != bNextIn)
			{
				const double T = (NearPlane - Current.X) / (Next.X - Current.X);
				Clipped.Add(FMath::Lerp(Current, Next, T));
			}
		}
		if (Clipped.Num() < 3)
		{
			return 0.0;
		}

		const double TanHalfHorizontal = FMath::Tan(FMath::DegreesToRadians(static_cast<double>(HorizontalFOVDegrees)) * 0.5);
		const double TanHalfVertical = TanHalfHorizontal / AspectRatio;
		FBox2D Rect(ForceInit);
		for (const FVector& Point : Clipped)
		{
			Rect += FVector2D(Point.Y / (Point.X * TanHalfHorizontal), Point.Z / (Point.X * TanHalfVertical));
		}
		const double Width = FMath::Max(FMath::Min(Rect.Max.X, 1.0) - FMath::Max(Rect.Min.X, -1.0), 0.0);
		const double Height = FMath::Max(FMath::Min(Rect.Max.Y, 1.0) - FMath::Max(Rect.Min.Y, -1.0), 0.0);
		return Width * Height / 4.0;
	}

	double ComputeTurnYaw(double StartYaw, double TotalDegrees, int32 Frame, int32 TurnFrames)
	{
		if (TurnFrames <= 0)
		{
			return StartYaw + TotalDegrees;
		}
		const double Alpha = FMath::Clamp(static_cast<double>(Frame) / TurnFrames, 0.0, 1.0);
		return StartYaw + TotalDegrees * Alpha * Alpha * (3.0 - 2.0 * Alpha);
	}

	FSpike FindSpike(TConstArrayView<double> Values)
	{
		FSpike Spike;
		for (int32 Index = 0; Index < Values.Num(); ++Index)
		{
			if (Spike.Index == INDEX_NONE || Values[Index] > Spike.MaxMs)
			{
				Spike.Index = Index;
				Spike.MaxMs = Values[Index];
			}
		}
		if (Spike.Index == INDEX_NONE)
		{
			return Spike;
		}
		Spike.MedianMs = ComputeStats(Values).P50;
		Spike.Ratio = Spike.MedianMs > 0.0 ? Spike.MaxMs / Spike.MedianMs : 0.0;
		return Spike;
	}
}
