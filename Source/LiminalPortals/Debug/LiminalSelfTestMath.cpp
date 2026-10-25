#include "Debug/LiminalSelfTestMath.h"

namespace
{
	FVector MirrorLocal(const FVector& Local)
	{
		return FVector(-Local.X, -Local.Y, Local.Z);
	}
}

namespace LiminalSelfTestMath
{
	FVector MirrorPointThroughPortals(const FTransform& Source, const FTransform& Target, const FVector& Point)
	{
		return Target.TransformPositionNoScale(MirrorLocal(Source.InverseTransformPositionNoScale(Point)));
	}

	FVector MirrorVectorThroughPortals(const FTransform& Source, const FTransform& Target, const FVector& Vector)
	{
		return Target.TransformVectorNoScale(MirrorLocal(Source.InverseTransformVectorNoScale(Vector)));
	}

	FCrossingCheck CheckCrossing(const FTransform& Source, const FTransform& Target, const FVector& LocationBefore,
		const FVector& LocationAfter, const FVector& VelocityBefore, const FVector& VelocityAfter,
		const FCrossingTolerances& Tolerances)
	{
		FCrossingCheck Check;
		Check.SpeedBefore = VelocityBefore.Size();
		Check.SpeedAfter = VelocityAfter.Size();
		Check.bMoving = Check.SpeedBefore >= Tolerances.MinSpeed;

		if (Check.bMoving)
		{
			Check.SpeedErrorRatio = FMath::Abs(Check.SpeedAfter - Check.SpeedBefore) / Check.SpeedBefore;
			const FVector Expected = MirrorVectorThroughPortals(Source, Target, VelocityBefore).GetSafeNormal();
			const FVector Actual = VelocityAfter.GetSafeNormal();
			const double Cosine = FMath::Clamp(FVector::DotProduct(Expected, Actual), -1.0, 1.0);
			// Una velocidad de salida nula no tiene dirección: cuenta como 180°.
			Check.DirectionErrorDegrees = Actual.IsZero() ? 180.0 : FMath::RadiansToDegrees(FMath::Acos(Cosine));
		}
		Check.PositionErrorCm = FVector::Dist(MirrorPointThroughPortals(Source, Target, LocationBefore), LocationAfter);

		Check.bSpeedOk = Check.bMoving && Check.SpeedErrorRatio <= Tolerances.SpeedRatio;
		Check.bDirectionOk = Check.bMoving && Check.DirectionErrorDegrees <= Tolerances.DirectionDegrees;
		Check.bPositionOk = Check.PositionErrorCm <= Tolerances.PositionCm;
		return Check;
	}

	bool IsInsidePortalVolume(const FTransform& PortalPlane, const FVector2D& HalfExtents, double FrameThickness,
		double Depth, const FVector& Point)
	{
		const FVector Local = PortalPlane.InverseTransformPositionNoScale(Point);
		const bool bBehind = Local.X < 0.0 && Local.X >= -Depth;
		const bool bWithinWidth = FMath::Abs(Local.Y) <= HalfExtents.X + FrameThickness;
		const bool bWithinHeight = FMath::Abs(Local.Z) <= HalfExtents.Y + FrameThickness;
		return bBehind && bWithinWidth && bWithinHeight;
	}
}
