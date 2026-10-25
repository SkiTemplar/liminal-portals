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
}
