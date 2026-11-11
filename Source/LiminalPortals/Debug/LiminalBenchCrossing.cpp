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
	/** Muestras de GPU que se guardan del giro. */
	constexpr int32 TurnGpuSamples = FLiminalBenchCrossing::TurnWindow + GpuLagFrames;

	bool HasReturnState(const ALiminalPortal& Portal)
	{
		return Portal.LinkedPortal && Portal.LinkedPortal->HasCaptureState();
	}

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
	bRecordingTurnGpu = false;
	Step = EStep::Place;
	bFinished = ++DoorIndex >= Doors.Num();
}

void FLiminalBenchCrossing::AddGpuSample(double Milliseconds)
{
	if (!bRecordingGpu || Results.IsEmpty())
	{
		return;
	}
	TArray<double>& Gpu = bRecordingTurnGpu ? Results.Last().TurnGpuMs : Results.Last().GpuMs;
	Gpu.Add(Milliseconds);
	if (Gpu.Num() > (bRecordingTurnGpu ? LiminalBenchCrossingDetail::TurnGpuSamples : LiminalBenchCrossingDetail::GpuSamples))
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
	const bool bPrewarming = Subsystem.GetLastPrewarmPortal() == Portal;

	switch (Step)
	{
	case EStep::Place:
	{
		const FVector Feet = PortalTransform.GetLocation() - Inward * StartDistance;
		LiminalDebugWorld::PlacePlayer(Controller, Character, Feet, FRotator(0.0, Inward.Rotation().Yaw, 0.0));
		HoldStart = FPlatformTime::Seconds();
		PrewarmFrames = 0;
		Step = EStep::Hold;
		break;
	}
	case EStep::Hold:
		History.Add(Frame);
		PrewarmFrames += bPrewarming ? 1 : 0;
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
		PrewarmFrames += bPrewarming ? 1 : 0;
		Character->AddMovementInput(Inward, 1.f);
		if (Subsystem.GetTeleportCount() != TeleportsBefore)
		{
			FDoor& Door = Results.Last();
			Door.bCrossed = true;
			Door.ReturnPortal = Portal->LinkedPortal ? Portal->LinkedPortal->GetName() : FString();
			Door.bReturnHadStateAtCross = LiminalBenchCrossingDetail::HasReturnState(*Portal);
			Door.PrewarmFrames = PrewarmFrames;
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
			BeginTurn(*Controller, *Portal);
		}
		break;
	case EStep::Turn:
		TickTurn(Frame, *Controller, *Portal);
		break;
	}
	if (History.Num() > WindowBefore + 1)
	{
		History.RemoveAt(0);
	}
}

void FLiminalBenchCrossing::BeginTurn(APlayerController& Controller, const ALiminalPortal& Portal)
{
	FDoor& Door = Results.Last();
	Door.bReturnHadStateBeforeTurn = LiminalBenchCrossingDetail::HasReturnState(Portal);
	TurnStartYaw = Controller.GetControlRotation().Yaw;
	bRecordingTurnGpu = true;
	Step = EStep::Turn;
	StepFrames = 0;
	// Primer paso del giro ya en este fotograma: el siguiente Tick mide el fotograma que lo dibuja.
	Controller.SetControlRotation(FRotator(0.0, LiminalBenchMath::ComputeTurnYaw(TurnStartYaw, TurnDegrees, 1, TurnFrames), 0.0));
}

void FLiminalBenchCrossing::TickTurn(const FFrame& Frame, APlayerController& Controller, const ALiminalPortal& Portal)
{
	FDoor& Door = Results.Last();
	if (StepFrames < TurnWindow)
	{
		Door.TurnFrames.Add(Frame);
	}
	if (Door.ReturnStateFrame == INDEX_NONE && !Door.bReturnHadStateBeforeTurn && LiminalBenchCrossingDetail::HasReturnState(Portal))
	{
		Door.ReturnStateFrame = StepFrames;
	}
	++StepFrames;
	if (StepFrames < TurnFrames)
	{
		Controller.SetControlRotation(FRotator(0.0,
			LiminalBenchMath::ComputeTurnYaw(TurnStartYaw, TurnDegrees, StepFrames + 1, TurnFrames), 0.0));
	}
	if (StepFrames >= TurnWindow + LiminalBenchCrossingDetail::GpuLagFrames)
	{
		NextDoor();
	}
}

TSharedRef<FJsonObject> FLiminalBenchCrossing::TurnToJson(const FDoor& Door)
{
	using namespace LiminalBenchCrossingDetail;
	TArray<double> FrameMs;
	TArray<double> RenderMs;
	TArray<double> GameMs;
	TArray<double> Captures;
	for (const FFrame& Frame : Door.TurnFrames)
	{
		FrameMs.Add(Frame.FrameMs);
		RenderMs.Add(Frame.RenderThreadMs);
		GameMs.Add(Frame.GameThreadMs);
		Captures.Add(Frame.Captures);
	}
	const LiminalBenchMath::FSpike Spike = LiminalBenchMath::FindSpike(FrameMs);
	const TSharedRef<FJsonObject> Turn = MakeShared<FJsonObject>();
	Turn->SetStringField(TEXT("returnPortal"), Door.ReturnPortal);
	Turn->SetBoolField(TEXT("returnHadStateAtCross"), Door.bReturnHadStateAtCross);
	Turn->SetBoolField(TEXT("returnHadStateBeforeTurn"), Door.bReturnHadStateBeforeTurn);
	Turn->SetNumberField(TEXT("returnStateFrame"), Door.ReturnStateFrame);
	Turn->SetNumberField(TEXT("frames"), FrameMs.Num());
	Turn->SetNumberField(TEXT("frameMsMedian"), Spike.MedianMs);
	Turn->SetNumberField(TEXT("frameMsMax"), Spike.MaxMs);
	Turn->SetNumberField(TEXT("frameMsMaxIndex"), Spike.Index);
	Turn->SetNumberField(TEXT("spikeRatio"), Spike.Ratio);
	Turn->SetNumberField(TEXT("gpuMsMax"), MaxOf(Door.TurnGpuMs));
	Turn->SetNumberField(TEXT("renderThreadMsMax"), MaxOf(RenderMs));
	Turn->SetNumberField(TEXT("gameThreadMsMax"), MaxOf(GameMs));
	Turn->SetArrayField(TEXT("frameMs"), ToJsonNumbers(FrameMs));
	Turn->SetArrayField(TEXT("gpuMs"), ToJsonNumbers(Door.TurnGpuMs));
	Turn->SetArrayField(TEXT("renderThreadMs"), ToJsonNumbers(RenderMs));
	Turn->SetArrayField(TEXT("gameThreadMs"), ToJsonNumbers(GameMs));
	Turn->SetArrayField(TEXT("captures"), ToJsonNumbers(Captures));
	return Turn;
}

TSharedRef<FJsonObject> FLiminalBenchCrossing::ToJson() const
{
	using namespace LiminalBenchCrossingDetail;
	TArray<TSharedPtr<FJsonValue>> DoorValues;
	double WorstFrame = 0.0;
	double WorstGpu = 0.0;
	double WorstRatio = 0.0;
	double WorstTurnFrame = 0.0;
	double WorstTurnRatio = 0.0;
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
		Entry->SetNumberField(TEXT("prewarmFrames"), Door.PrewarmFrames);
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
		const TSharedRef<FJsonObject> Turn = TurnToJson(Door);
		WorstTurnFrame = FMath::Max(WorstTurnFrame, Turn->GetNumberField(TEXT("frameMsMax")));
		WorstTurnRatio = FMath::Max(WorstTurnRatio, Turn->GetNumberField(TEXT("spikeRatio")));
		Entry->SetObjectField(TEXT("turn"), Turn);
		DoorValues.Add(MakeShared<FJsonValueObject>(Entry));
	}
	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetNumberField(TEXT("holdSeconds"), HoldSeconds);
	Object->SetNumberField(TEXT("windowBefore"), WindowBefore);
	Object->SetNumberField(TEXT("windowAfter"), WindowAfter);
	Object->SetNumberField(TEXT("frameMsMaxFromCross"), WorstFrame);
	Object->SetNumberField(TEXT("gpuMsMax"), WorstGpu);
	Object->SetNumberField(TEXT("spikeRatioMax"), WorstRatio);
	Object->SetNumberField(TEXT("turnDegrees"), TurnDegrees);
	Object->SetNumberField(TEXT("turnFrames"), TurnFrames);
	Object->SetNumberField(TEXT("turnWindow"), TurnWindow);
	Object->SetNumberField(TEXT("turnFrameMsMax"), WorstTurnFrame);
	Object->SetNumberField(TEXT("turnSpikeRatioMax"), WorstTurnRatio);
	Object->SetStringField(TEXT("error"), Error);
	Object->SetArrayField(TEXT("doors"), DoorValues);
	return Object;
}
