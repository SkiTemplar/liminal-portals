#include "Portal/LiminalPortalMath.h"

namespace LiminalPortalMath
{
	namespace
	{
		FTransform MakeRigid(const FTransform& Transform)
		{
			return FTransform(Transform.GetRotation().GetNormalized(), Transform.GetLocation());
		}
	}

	FTransform MakeTransfer(const FTransform& Source, const FTransform& Target)
	{
		// FTransform compone de izquierda a derecha (A * B aplica A y después B), así que
		// Target · Yaw180 · Source⁻¹ se escribe en orden inverso.
		const FTransform Yaw180(FQuat(FVector::UpVector, UE_DOUBLE_PI));
		return MakeRigid(Source).Inverse() * Yaw180 * MakeRigid(Target);
	}

	FVector TransferPoint(const FTransform& Transfer, const FVector& Point)
	{
		return Transfer.TransformPositionNoScale(Point);
	}

	FVector TransferVector(const FTransform& Transfer, const FVector& Vector)
	{
		return Transfer.TransformVectorNoScale(Vector);
	}

	FQuat TransferRotation(const FTransform& Transfer, const FQuat& Rotation)
	{
		return (Transfer.GetRotation() * Rotation).GetNormalized();
	}

	FTransform TransferTransform(const FTransform& Transfer, const FTransform& Transform)
	{
		return FTransform(
			TransferRotation(Transfer, Transform.GetRotation()),
			TransferPoint(Transfer, Transform.GetLocation()),
			Transform.GetScale3D());
	}

	FRotator TransferControlRotation(const FTransform& Transfer, const FRotator& ControlRotation)
	{
		// El roll no cambia la dirección de la mirada: anularlo conserva yaw y pitch efectivos.
		FRotator Result = TransferRotation(Transfer, ControlRotation.Quaternion()).Rotator();
		Result.Roll = 0.0;
		return Result.GetNormalized();
	}

	double SignedDistanceToPlane(const FTransform& Portal, const FVector& Point)
	{
		const FVector Normal = Portal.GetRotation().GetForwardVector();
		return FVector::DotProduct(Point - Portal.GetLocation(), Normal);
	}

	bool DetectCrossing(const FTransform& Portal, const FVector2D& HalfExtents,
		const FVector& PreviousPoint, const FVector& CurrentPoint, double MaxStep, FVector* OutHitPoint)
	{
		if (FVector::DistSquared(PreviousPoint, CurrentPoint) > FMath::Square(MaxStep))
		{
			return false;
		}

		const double PreviousDistance = SignedDistanceToPlane(Portal, PreviousPoint);
		const double CurrentDistance = SignedDistanceToPlane(Portal, CurrentPoint);
		if (PreviousDistance < 0.0 || CurrentDistance >= 0.0)
		{
			return false;
		}

		const double Alpha = PreviousDistance / (PreviousDistance - CurrentDistance);
		const FVector HitPoint = FMath::Lerp(PreviousPoint, CurrentPoint, Alpha);
		const FVector Local = MakeRigid(Portal).InverseTransformPositionNoScale(HitPoint);
		if (FMath::Abs(Local.Y) > HalfExtents.X || FMath::Abs(Local.Z) > HalfExtents.Y)
		{
			return false;
		}

		if (OutHitPoint)
		{
			*OutHitPoint = HitPoint;
		}
		return true;
	}

	FClipPlane ComputeClipPlane(const FTransform& TargetPortal, double Offset)
	{
		FClipPlane Plane;
		Plane.Normal = TargetPortal.GetRotation().GetForwardVector();
		Plane.Base = TargetPortal.GetLocation() + Plane.Normal * Offset;
		return Plane;
	}

	bool IsPortalPotentiallyVisible(const FTransform& Portal, const FVector2D& HalfExtents,
		const FVector& CameraLocation, const FVector& CameraForward, float HorizontalFOVDegrees, float AspectRatio)
	{
		if (SignedDistanceToPlane(Portal, CameraLocation) <= 0.0)
		{
			return false;
		}

		const bool bValidView = HorizontalFOVDegrees > 0.f && HorizontalFOVDegrees < 180.f
			&& AspectRatio > UE_KINDA_SMALL_NUMBER && !CameraForward.IsNearlyZero();
		if (!bValidView)
		{
			return true;
		}

		const FVector ToPortal = Portal.GetLocation() - CameraLocation;
		const double Distance = ToPortal.Size();
		const double Radius = HalfExtents.Size();
		if (Distance <= Radius)
		{
			return true;
		}

		const double TanHalfHorizontal = FMath::Tan(FMath::DegreesToRadians(HorizontalFOVDegrees) * 0.5);
		const double TanHalfVertical = TanHalfHorizontal / AspectRatio;
		const double ConeHalfAngle = FMath::Atan(FMath::Sqrt(FMath::Square(TanHalfHorizontal) + FMath::Square(TanHalfVertical)));
		const double SphereHalfAngle = FMath::Asin(FMath::Clamp(Radius / Distance, 0.0, 1.0));

		const double CosAngle = FMath::Clamp(FVector::DotProduct(CameraForward.GetSafeNormal(), ToPortal / Distance), -1.0, 1.0);
		return FMath::Acos(CosAngle) <= ConeHalfAngle + SphereHalfAngle;
	}

	float ComputeSurfaceDepth(double SignedDistance, float NearThicknessDistance, float BaseDepth, float ExtendedDepth)
	{
		const float SafeBase = FMath::Max(BaseDepth, 0.f);
		const bool bNearFront = SignedDistance >= 0.0 && SignedDistance < NearThicknessDistance;
		return bNearFront ? FMath::Max(ExtendedDepth, SafeBase) : SafeBase;
	}

	FIntPoint ComputeRenderTargetSize(const FIntPoint& ViewportSize, float AspectRatio, float Scale, int32 MaxDimension)
	{
		if (ViewportSize.X <= 0 || ViewportSize.Y <= 0 || Scale <= 0.f || MaxDimension <= 0)
		{
			return FIntPoint::ZeroValue;
		}

		double Width = ViewportSize.X;
		double Height = ViewportSize.Y;
		if (AspectRatio > UE_KINDA_SMALL_NUMBER)
		{
			// Recorte tipo letterbox: el mayor rectángulo del aspecto pedido que cabe en el viewport.
			if (Width / Height > AspectRatio)
			{
				Width = Height * AspectRatio;
			}
			else
			{
				Height = Width / AspectRatio;
			}
		}

		Width *= Scale;
		Height *= Scale;
		const double LongestSide = FMath::Max(Width, Height);
		if (LongestSide > MaxDimension)
		{
			const double Fit = MaxDimension / LongestSide;
			Width *= Fit;
			Height *= Fit;
		}

		return FIntPoint(
			FMath::Max(1, FMath::RoundToInt32(Width)),
			FMath::Max(1, FMath::RoundToInt32(Height)));
	}

	bool ShouldReleaseCaptureState(double SecondsSinceLastCapture, double DistanceToCamera,
		double IdleSeconds, double ReleaseDistance)
	{
		const bool bValidInput = FMath::IsFinite(SecondsSinceLastCapture) && FMath::IsFinite(DistanceToCamera)
			&& SecondsSinceLastCapture >= 0.0 && DistanceToCamera >= 0.0;
		if (!bValidInput || !(IdleSeconds > 0.0))
		{
			return false;
		}
		return SecondsSinceLastCapture >= IdleSeconds && DistanceToCamera >= FMath::Max(ReleaseDistance, 0.0);
	}
}
