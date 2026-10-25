#include "Debug/LiminalBenchWalk.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/PrimitiveComponent.h"
#include "Debug/LiminalCalibrationMath.h"
#include "Debug/LiminalDebugWorld.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Player/LiminalCharacter.h"
#include "Portal/LiminalPortal.h"
#include "Portal/LiminalPortalMath.h"
#include "Portal/LiminalPortalSubsystem.h"

// Namespace con nombre propio (D-22): el unity build junta todos los .cpp del módulo.
namespace LiminalBenchWalkDetail
{
	double Mean(TConstArrayView<double> Values)
	{
		double Sum = 0.0;
		for (const double Value : Values)
		{
			Sum += Value;
		}
		return Values.IsEmpty() ? 0.0 : Sum / Values.Num();
	}

	TArray<TSharedPtr<FJsonValue>> NumbersToJson(TConstArrayView<double> Values)
	{
		TArray<TSharedPtr<FJsonValue>> Array;
		Array.Reserve(Values.Num());
		for (const double Value : Values)
		{
			Array.Add(MakeShared<FJsonValueNumber>(Value));
		}
		return Array;
	}
}

void FLiminalBenchWalk::Begin(const FTransform& InHubTransform)
{
	HubTransform = InHubTransform;
	Poses = LiminalBenchMath::MakeHubWalkPoses();
	Samples.Reset();
	GpuMs.Reset();
	PoseIndex = 0;
	PoseFrames = 0;
	bFinished = Poses.IsEmpty();
}

void FLiminalBenchWalk::Tick(UWorld* World, APlayerController* Controller, ALiminalCharacter* Character,
	const ULiminalPortalSubsystem& Subsystem)
{
	if (bFinished || !World || !Controller || !Character)
	{
		bFinished = true;
		return;
	}
	if (PoseFrames == 0)
	{
		const LiminalBenchMath::FBenchPose& Pose = Poses[PoseIndex];
		const FVector Feet = HubTransform.TransformPositionNoScale(FVector(Pose.Feet.X, Pose.Feet.Y, 0.0));
		const FRotator Rotation(0.0, HubTransform.Rotator().Yaw + Pose.Yaw, 0.0);
		LiminalDebugWorld::PlacePlayer(Controller, Character, Feet, Rotation);
	}
	if (++PoseFrames < HoldFrames)
	{
		return;
	}
	Sample(World, Controller, Subsystem);
	PoseFrames = 0;
	bFinished = ++PoseIndex >= Poses.Num();
}

void FLiminalBenchWalk::Sample(UWorld* World, APlayerController* Controller, const ULiminalPortalSubsystem& Subsystem)
{
	FSample& Sample = Samples.AddDefaulted_GetRef();
	Sample.Name = Poses[PoseIndex].Name;
	Sample.Captures = Subsystem.GetLastCaptureCount();
	if (!Controller->PlayerCameraManager)
	{
		return;
	}
	int32 SizeX = 0;
	int32 SizeY = 0;
	Controller->GetViewportSize(SizeX, SizeY);
	if (SizeX <= 0 || SizeY <= 0)
	{
		return;
	}
	const float Aspect = static_cast<float>(SizeX) / static_cast<float>(SizeY);
	const FMinimalViewInfo& View = Controller->PlayerCameraManager->GetCameraCacheView();
	const double Now = World->GetTimeSeconds();
	for (TActorIterator<ALiminalPortal> It(World); It; ++It)
	{
		const ALiminalPortal* Portal = *It;
		if (!Portal->LinkedPortal || Portal->bExitOnly)
		{
			continue;
		}
		const FTransform Plane = Portal->GetPlaneTransform();
		if (!LiminalPortalMath::IsPortalPotentiallyVisible(Plane, Portal->GetHalfExtents(), View.Location,
			View.Rotation.Vector(), View.FOV, Aspect))
		{
			continue;
		}
		++Sample.Potential;
		const TArray<FVector, TFixedAllocator<4>> Corners = LiminalCalibrationMath::GetPortalCorners(Plane, Portal->GetHalfExtents());
		const double Area = LiminalBenchMath::ComputeScreenRectFraction(Corners, View.Location, View.Rotation, View.FOV, Aspect);
		Sample.Areas.Add(Area);
		Sample.AreaSum += Area;
		const UPrimitiveComponent* Surface = Portal->GetSurfaceComponent();
		const bool bRendered = Surface && Now - Surface->GetLastRenderTimeOnScreen() <= RenderedToleranceSeconds;
		if (bRendered)
		{
			++Sample.Rendered;
			Sample.RenderedAreaSum += Area;
		}
	}
}

TSharedRef<FJsonObject> FLiminalBenchWalk::ToJson() const
{
	using namespace LiminalBenchWalkDetail;
	TArray<double> Captures;
	TArray<double> Potential;
	TArray<double> Rendered;
	TArray<double> AreaSums;
	TArray<double> RenderedAreaSums;
	TArray<double> AreasPerPortal;
	TArray<double> Histogram;
	Histogram.SetNumZeroed(9);
	int32 Mismatches = 0;
	TArray<TSharedPtr<FJsonValue>> PoseValues;
	for (const FSample& Sample : Samples)
	{
		Captures.Add(Sample.Captures);
		Potential.Add(Sample.Potential);
		Rendered.Add(Sample.Rendered);
		AreaSums.Add(Sample.AreaSum);
		RenderedAreaSums.Add(Sample.RenderedAreaSum);
		AreasPerPortal.Append(Sample.Areas);
		Histogram[FMath::Clamp(Sample.Captures, 0, Histogram.Num() - 1)] += 1.0;
		Mismatches += Sample.Captures != Sample.Potential ? 1 : 0;

		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Sample.Name);
		Entry->SetNumberField(TEXT("captures"), Sample.Captures);
		Entry->SetNumberField(TEXT("rendered"), Sample.Rendered);
		Entry->SetArrayField(TEXT("areas"), NumbersToJson(Sample.Areas));
		PoseValues.Add(MakeShared<FJsonValueObject>(Entry));
	}

	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetNumberField(TEXT("poses"), Samples.Num());
	Object->SetNumberField(TEXT("holdFrames"), HoldFrames);
	Object->SetNumberField(TEXT("renderedToleranceSeconds"), RenderedToleranceSeconds);
	Object->SetNumberField(TEXT("capturesMean"), Mean(Captures));
	Object->SetNumberField(TEXT("potentialMean"), Mean(Potential));
	Object->SetNumberField(TEXT("capturesPotentialMismatches"), Mismatches);
	Object->SetNumberField(TEXT("renderedMean"), Mean(Rendered));
	Object->SetNumberField(TEXT("areaSumMean"), Mean(AreaSums));
	Object->SetNumberField(TEXT("renderedAreaSumMean"), Mean(RenderedAreaSums));
	Object->SetNumberField(TEXT("areaPerCapturedPortalMean"), Mean(AreasPerPortal));
	Object->SetArrayField(TEXT("capturesHistogram"), NumbersToJson(Histogram));
	const LiminalBenchMath::FSampleStats Gpu = LiminalBenchMath::ComputeStats(GpuMs);
	const TSharedRef<FJsonObject> GpuObject = MakeShared<FJsonObject>();
	GpuObject->SetNumberField(TEXT("count"), Gpu.Count);
	GpuObject->SetNumberField(TEXT("mean"), Gpu.Mean);
	GpuObject->SetNumberField(TEXT("p50"), Gpu.P50);
	GpuObject->SetNumberField(TEXT("p95"), Gpu.P95);
	Object->SetObjectField(TEXT("gpuMs"), GpuObject);
	Object->SetArrayField(TEXT("samples"), PoseValues);
	return Object;
}

namespace LiminalBenchProfileJson
{
	TSharedRef<FJsonObject> SnapshotToJson(const LiminalGpuProfile::FSnapshot& Snapshot, double MinMsPerFrame)
	{
		const double Frames = FMath::Max(Snapshot.Frames, 1);
		const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("frames"), Snapshot.Frames);

		const TSharedRef<FJsonObject> Queues = MakeShared<FJsonObject>();
		for (const TPair<FString, double>& Entry : Snapshot.QueueBusyMs)
		{
			Queues->SetNumberField(Entry.Key, Entry.Value / Frames);
		}
		Object->SetObjectField(TEXT("queueBusyMsPerFrame"), Queues);

		TArray<TSharedPtr<FJsonValue>> Paths;
		for (const LiminalGpuProfile::FPathTime& Path : Snapshot.Paths)
		{
			const double PerFrame = Path.InclusiveMs / Frames;
			if (PerFrame < MinMsPerFrame)
			{
				continue;
			}
			const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("path"), Path.Path);
			Entry->SetNumberField(TEXT("ms"), PerFrame);
			Entry->SetNumberField(TEXT("countPerFrame"), Path.Count / Frames);
			Paths.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Object->SetArrayField(TEXT("paths"), Paths);
		return Object;
	}
}
