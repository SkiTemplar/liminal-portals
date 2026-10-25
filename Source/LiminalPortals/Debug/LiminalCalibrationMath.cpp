#include "Debug/LiminalCalibrationMath.h"

namespace LiminalCalibrationMath
{
	namespace
	{
		/** Distancia mínima delante de la cámara para proyectar (evita dividir por casi cero), en cm. */
		constexpr double MinDepth = 1e-3;
	}

	TArray<double> MakeYawSweep(double StepDegrees, double AmplitudeDegrees, int32 NumFrames)
	{
		TArray<double> Sweep;
		if (StepDegrees <= 0.0 || AmplitudeDegrees < StepDegrees || NumFrames <= 0)
		{
			return Sweep;
		}

		// Onda triangular de periodo 4·Q pasos: sube de 0 a Q, baja hasta -Q y vuelve a 0.
		const int32 Quarter = FMath::FloorToInt32(AmplitudeDegrees / StepDegrees + UE_KINDA_SMALL_NUMBER);
		const int32 Period = 4 * Quarter;
		Sweep.Reserve(NumFrames);
		for (int32 Frame = 0; Frame < NumFrames; ++Frame)
		{
			const int32 Phase = Frame % Period;
			int32 Steps = Phase;
			if (Phase > Quarter && Phase <= 3 * Quarter)
			{
				Steps = 2 * Quarter - Phase;
			}
			else if (Phase > 3 * Quarter)
			{
				Steps = Phase - Period;
			}
			Sweep.Add(Steps * StepDegrees);
		}
		return Sweep;
	}

	bool ProjectToViewport(const FVector& ViewLocation, const FRotator& ViewRotation, float HorizontalFOVDegrees,
		const FIntPoint& ViewportSize, const FVector& WorldPoint, FVector2D& OutPixel)
	{
		const bool bValidView = HorizontalFOVDegrees > 0.f && HorizontalFOVDegrees < 180.f
			&& ViewportSize.X > 0 && ViewportSize.Y > 0;
		if (!bValidView)
		{
			return false;
		}

		// Espacio de vista de UE: X delante, Y derecha, Z arriba.
		const FVector Local = ViewRotation.UnrotateVector(WorldPoint - ViewLocation);
		if (Local.X <= MinDepth)
		{
			return false;
		}

		const double TanHalfFOV = FMath::Tan(FMath::DegreesToRadians(static_cast<double>(HorizontalFOVDegrees)) * 0.5);
		const double Aspect = static_cast<double>(ViewportSize.X) / static_cast<double>(ViewportSize.Y);
		const double NdcX = Local.Y / (Local.X * TanHalfFOV);
		const double NdcY = Local.Z * Aspect / (Local.X * TanHalfFOV);

		OutPixel.X = (NdcX + 1.0) * 0.5 * ViewportSize.X;
		OutPixel.Y = (1.0 - NdcY) * 0.5 * ViewportSize.Y;
		return true;
	}

	TArray<FVector, TFixedAllocator<4>> GetPortalCorners(const FTransform& PlaneTransform, const FVector2D& HalfExtents)
	{
		TArray<FVector, TFixedAllocator<4>> Corners;
		const FVector LocalCorners[4] = {
			FVector(0.0, -HalfExtents.X, -HalfExtents.Y),
			FVector(0.0, HalfExtents.X, -HalfExtents.Y),
			FVector(0.0, HalfExtents.X, HalfExtents.Y),
			FVector(0.0, -HalfExtents.X, HalfExtents.Y),
		};
		for (const FVector& Local : LocalCorners)
		{
			Corners.Add(PlaneTransform.TransformPositionNoScale(Local));
		}
		return Corners;
	}
}
