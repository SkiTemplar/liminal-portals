#include "Debug/LiminalBenchCrossing.h"

#include "Debug/LiminalBenchMath.h"
#include "Debug/LiminalDebugWorld.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Player/LiminalCharacter.h"
#include "Portal/LiminalPortal.h"
#include "Portal/LiminalPortalSubsystem.h"
#include "RenderTimer.h"

// Namespace con nombre propio (D-22): el unity build junta todos los .cpp del módulo.
namespace LiminalBenchCrossingDetail
{
	/** Las puertas del hub están a menos de esto de su centro; los biomas, a 20 km. */
	constexpr double HubRadius = 3000.0;
	/** Fotogramas extra tras la ventana para que lleguen las muestras de GPU del último. */
	constexpr int32 GpuLagFrames = 3;
	/** Muestras de GPU que se guardan (ventana + retraso). */
	constexpr int32 GpuSamples = FLiminalBenchCrossing::WindowBefore + FLiminalBenchCrossing::WindowAfter + GpuLagFrames;

	TArray<TSharedPtr<FJsonValue>> ToJsonNumbers(TConstArrayView<double> Values)
	{
		TArray<TSharedPtr<FJsonValue>> Array;
		for (const double Value : Values)
		{
			Array.Add(MakeShared<FJsonValueNumber>(Value));
		}
		return Array;
	}

	double MaxOf(TConstArrayView<double> Values)
	{
		double Max = 0.0;
		for (const double Value : Values)
		{
			Max = FMath::Max(Max, Value);
		}
		return Max;
	}
}

void FLiminalBenchCrossing::Begin(UWorld* World, const FTransform& HubTransform)
{
	Doors.Reset();
	Results.Reset();
	History.Reset();
	Error.Reset();
	for (TActorIterator<ALiminalPortal> It(World); It; ++It)
	{
		const bool bHubDoor = It->LinkedPortal && !It->bExitOnly
			&& FVector::Dist(It->GetActorLocation(), HubTransform.GetLocation()) < LiminalBenchCrossingDetail::HubRadius;
		if (bHubDoor)
		{
			Doors.Add(*It);
		}
	}
	Doors.Sort([](const TWeakObjectPtr<ALiminalPortal>& A, const TWeakObjectPtr<ALiminalPortal>& B)
	{
		return A->GetName() < B->GetName();
	});
	// Dos pasadas: la primera de cada puerta incluye lo que solo pasa una vez por proceso (PSO, shaders); la
	// segunda, 12 s después y con el estado de render de los portales del bioma liberado otra vez (D-31), lo que se repite.
	const TArray<TWeakObjectPtr<ALiminalPortal>> OnePass = Doors;
	for (int32 Pass = 1; Pass < Passes; ++Pass)
	{
		Doors.Append(OnePass);
	}
	DoorIndex = 0;
	Step = EStep::Place;
	bFinished = Doors.IsEmpty();
	if (bFinished)
	{
		Error = TEXT("no hay puertas del hub para los cruces");
	}
}

void FLiminalBenchCrossing::NextDoor()
{
	History.Reset();
	bRecordingGpu = false;
	Step = EStep::Place;
	bFinished = ++DoorIndex >= Doors.Num();
}

void FLiminalBenchCrossing::AddGpuSample(double Milliseconds)
{
	if (!bRecordingGpu || Results.IsEmpty())
	{
		return;
	}
	TArray<double>& Gpu = Results.Last().GpuMs;
	Gpu.Add(Milliseconds);
	if (Gpu.Num() > LiminalBenchCrossingDetail::GpuSamples)
	{
		Gpu.RemoveAt(0);
	}
}

void FLiminalBenchCrossing::Tick(float DeltaTime, APlayerController* Controller, ALiminalCharacter* Character,
	const ULiminalPortalSubsystem& Subsystem)
{
	ALiminalPortal* Portal = Doors.IsValidIndex(DoorIndex) ? Doors[DoorIndex].Get() : nullptr;
	if (bFinished || !Controller || !Character || !Portal)
	{
		Error = bFinished ? Error : TEXT("el jugador o la puerta ya no existen");
		bFinished = true;
		return;
	}
	const FTransform PortalTransform = Portal->GetPortalTransform();
	const FVector Inward = -PortalTransform.GetUnitAxis(EAxis::X);

	FFrame Frame;
	Frame.FrameMs = static_cast<double>(DeltaTime) * 1000.0;
	Frame.RenderThreadMs = FPlatformTime::ToMilliseconds(GRenderThreadTime);
	Frame.GameThreadMs = FPlatformTime::ToMilliseconds(GGameThreadTime);
	Frame.Captures = Subsystem.GetLastCaptureCount();

	switch (Step)
	{
	case EStep::Place:
	{
		const FVector Feet = PortalTransform.GetLocation() - Inward * StartDistance;
		LiminalDebugWorld::PlacePlayer(Controller, Character, Feet, FRotator(0.0, Inward.Rotation().Yaw, 0.0));
		HoldStart = FPlatformTime::Seconds();
		Step = EStep::Hold;
		break;
	}
	case EStep::Hold:
		History.Add(Frame);
		if (FPlatformTime::Seconds() - HoldStart >= HoldSeconds)
		{
			TeleportsBefore = Subsystem.GetTeleportCount();
			Controller->ResetIgnoreMoveInput();
			Step = EStep::Walk;
			StepFrames = 0;
			FDoor& Door = Results.AddDefaulted_GetRef();
			Door.Portal = Portal->GetName();
			Door.Pass = Doors.Num() > 0 ? 1 + DoorIndex / FMath::Max(Doors.Num() / Passes, 1) : 1;
			bRecordingGpu = true;
		}
		break;
	case EStep::Walk:
		History.Add(Frame);
		Character->AddMovementInput(Inward, 1.f);
		if (Subsystem.GetTeleportCount() != TeleportsBefore)
		{
			FDoor& Door = Results.Last();
			Door.bCrossed = true;
			const int32 First = FMath::Max(History.Num() - WindowBefore - 1, 0);
			Door.Frames.Append(&History[First], History.Num() - First);
			Step = EStep::After;
			StepFrames = 0;
		}
		else if (++StepFrames > MaxWalkFrames)
		{
			Error = FString::Printf(TEXT("%s: sin cruce en %d fotogramas de caminata"), *Portal->GetName(), MaxWalkFrames);
			Controller->SetIgnoreMoveInput(true);
			NextDoor();
		}
		break;
	case EStep::After:
		Character->AddMovementInput(Character->GetActorForwardVector(), 1.f);
		if (StepFrames < WindowAfter)
		{
			Results.Last().Frames.Add(Frame);
		}
		if (++StepFrames >= WindowAfter + LiminalBenchCrossingDetail::GpuLagFrames)
		{
			Controller->SetIgnoreMoveInput(true);
			NextDoor();
		}
		break;
	}
	if (History.Num() > WindowBefore + 1)
	{
		History.RemoveAt(0);
	}
}

TSharedRef<FJsonObject> FLiminalBenchCrossing::ToJson() const
{
	using namespace LiminalBenchCrossingDetail;
	TArray<TSharedPtr<FJsonValue>> DoorValues;
	double WorstFrame = 0.0;
	double WorstGpu = 0.0;
	double WorstRatio = 0.0;
	for (const FDoor& Door : Results)
	{
		TArray<double> FrameMs;
		TArray<double> RenderMs;
		TArray<double> GameMs;
		TArray<double> Captures;
		for (const FFrame& Frame : Door.Frames)
		{
			FrameMs.Add(Frame.FrameMs);
			RenderMs.Add(Frame.RenderThreadMs);
			GameMs.Add(Frame.GameThreadMs);
			Captures.Add(Frame.Captures);
		}
		// El fotograma del cruce es el último de los anteriores (índice WindowBefore si la ventana está completa).
		const int32 CrossIndex = FMath::Max(FrameMs.Num() - FMath::Min(WindowAfter, FrameMs.Num()) - 1, 0);
		const TConstArrayView<double> Before(FrameMs.GetData(), CrossIndex);
		const TConstArrayView<double> After(FrameMs.GetData() + CrossIndex, FrameMs.Num() - CrossIndex);
		const double MedianBefore = LiminalBenchMath::ComputeStats(Before).P50;
		const double MaxAfter = MaxOf(After);
		const double Ratio = MedianBefore > 0.0 ? MaxAfter / MedianBefore : 0.0;
		WorstFrame = FMath::Max(WorstFrame, MaxAfter);
		WorstGpu = FMath::Max(WorstGpu, MaxOf(Door.GpuMs));
		WorstRatio = FMath::Max(WorstRatio, Ratio);

		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("portal"), Door.Portal);
		Entry->SetNumberField(TEXT("pass"), Door.Pass);
		Entry->SetBoolField(TEXT("crossed"), Door.bCrossed);
		Entry->SetNumberField(TEXT("crossIndex"), CrossIndex);
		Entry->SetNumberField(TEXT("frameMsMedianBefore"), MedianBefore);
		Entry->SetNumberField(TEXT("frameMsMaxFromCross"), MaxAfter);
		Entry->SetNumberField(TEXT("spikeRatio"), Ratio);
		Entry->SetNumberField(TEXT("gpuMsMax"), MaxOf(Door.GpuMs));
		Entry->SetNumberField(TEXT("renderThreadMsMax"), MaxOf(RenderMs));
		Entry->SetNumberField(TEXT("gameThreadMsMax"), MaxOf(GameMs));
		Entry->SetArrayField(TEXT("frameMs"), ToJsonNumbers(FrameMs));
		Entry->SetArrayField(TEXT("gpuMs"), ToJsonNumbers(Door.GpuMs));
		Entry->SetArrayField(TEXT("renderThreadMs"), ToJsonNumbers(RenderMs));
		Entry->SetArrayField(TEXT("gameThreadMs"), ToJsonNumbers(GameMs));
		Entry->SetArrayField(TEXT("captures"), ToJsonNumbers(Captures));
		DoorValues.Add(MakeShared<FJsonValueObject>(Entry));
	}
	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetNumberField(TEXT("holdSeconds"), HoldSeconds);
	Object->SetNumberField(TEXT("windowBefore"), WindowBefore);
	Object->SetNumberField(TEXT("windowAfter"), WindowAfter);
	Object->SetNumberField(TEXT("frameMsMaxFromCross"), WorstFrame);
	Object->SetNumberField(TEXT("gpuMsMax"), WorstGpu);
	Object->SetNumberField(TEXT("spikeRatioMax"), WorstRatio);
	Object->SetStringField(TEXT("error"), Error);
	Object->SetArrayField(TEXT("doors"), DoorValues);
	return Object;
}
