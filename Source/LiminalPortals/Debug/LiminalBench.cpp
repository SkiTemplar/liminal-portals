#include "Debug/LiminalBench.h"

#include "Debug/LiminalDebugShared.h"
#include "Debug/LiminalDebugWorld.h"
#include "Camera/PlayerCameraManager.h"
#include "Dom/JsonObject.h"
#include "DynamicRHI.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "LiminalPortals.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Player/LiminalCharacter.h"
#include "Portal/LiminalPortal.h"
#include "Portal/LiminalPortalSubsystem.h"
#include "RenderTimer.h"
#include "RHIGlobals.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "World/LiminalHub.h"

// Namespace con nombre propio (D-22): el unity build junta todos los .cpp del módulo.
namespace LiminalBenchDetail
{
	const TCHAR* const PortalsEnableCVar = TEXT("Liminal.Portals.Enable");
	/** Fotogramas de espera al pawn antes de abortar. */
	constexpr int32 MaxWaitPawnFrames = 600;
	/** Fotogramas medidos de más que se toleran esperando a las muestras de GPU. */
	constexpr int32 MaxExtraMeasureFrames = 600;

	double CyclesToMs(uint32 Cycles)
	{
		return FPlatformTime::ToMilliseconds(Cycles);
	}

	IConsoleVariable* FindCVar(const TCHAR* Name)
	{
		return IConsoleManager::Get().FindConsoleVariable(Name);
	}

	TSharedRef<FJsonObject> StatsToJson(const LiminalBenchMath::FSampleStats& Stats)
	{
		const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("count"), Stats.Count);
		Object->SetNumberField(TEXT("mean"), Stats.Mean);
		Object->SetNumberField(TEXT("p50"), Stats.P50);
		Object->SetNumberField(TEXT("p95"), Stats.P95);
		Object->SetNumberField(TEXT("min"), Stats.Min);
		Object->SetNumberField(TEXT("max"), Stats.Max);
		return Object;
	}

	/** Valor de una CVar como texto para el informe ("" si no existe). */
	FString CVarString(const TCHAR* Name)
	{
		const IConsoleVariable* Variable = FindCVar(Name);
		return Variable ? Variable->GetString() : FString();
	}

	/** Pases de menos de esto por fotograma no entran en BenchProfile.json. */
	constexpr double MinProfileMsPerFrame = 0.005;

	/** CVars que cambian lo que se mide (las de las palancas de D-30 y D-36 incluidas). */
	TSharedRef<FJsonObject> CVarsToJson()
	{
		const TSharedRef<FJsonObject> CVars = MakeShared<FJsonObject>();
		for (const TCHAR* Name : { TEXT("r.VSync"), TEXT("t.MaxFPS"), TEXT("r.ScreenPercentage"), TEXT("r.AntiAliasingMethod"),
			TEXT("r.DynamicGlobalIlluminationMethod"), TEXT("r.ReflectionMethod"), TEXT("r.Shadow.Virtual.Enable"),
			TEXT("r.DistanceFieldAO"), TEXT("Liminal.Portal.CaptureScale"), TEXT("Liminal.Portal.CaptureReflectionMethod"),
			TEXT("Liminal.Portal.CaptureGIMethod"), TEXT("Liminal.Portal.CaptureShowFlags"), TEXT("Liminal.Scatter.CullScale") })
		{
			CVars->SetStringField(Name, CVarString(Name));
		}
		return CVars;
	}

	bool SaveJson(const TSharedRef<FJsonObject>& Root, const FString& Path)
	{
		FString Json;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
		if (!FJsonSerializer::Serialize(Root, Writer))
		{
			return false;
		}
		return FFileHelper::SaveStringToFile(Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
}

FLiminalBenchRun::FLiminalBenchRun(UWorld* InWorld, const FConfig& InConfig)
	: Config(InConfig)
	, World(InWorld)
{
}

FLiminalBenchRun::~FLiminalBenchRun()
{
	if (Phase != EPhase::Finished)
	{
		Unbind();
	}
}

bool FLiminalBenchRun::Start(FString& OutError)
{
	UWorld* GameWorld = World.Get();
	ULiminalPortalSubsystem* PortalSubsystem = GameWorld ? GameWorld->GetSubsystem<ULiminalPortalSubsystem>() : nullptr;
	if (!PortalSubsystem)
	{
		OutError = TEXT("no hay mundo de juego con ULiminalPortalSubsystem");
		return false;
	}
	const IConsoleVariable* EnableVariable = LiminalBenchDetail::FindCVar(LiminalBenchDetail::PortalsEnableCVar);
	if (!EnableVariable)
	{
		OutError = TEXT("no existe la CVar Liminal.Portals.Enable");
		return false;
	}
	OriginalPortalsEnable = EnableVariable->GetInt();
	if (Config.bProfile && !LiminalGpuProfile::IsAvailable())
	{
		OutError = TEXT("el modo perfil necesita el perfilador de GPU nuevo con migas (no en Shipping)");
		return false;
	}

	Subsystem = PortalSubsystem;
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FLiminalBenchRun::OnTick));
	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddRaw(this, &FLiminalBenchRun::OnWorldCleanup);
	UE_LOG(LogLiminalPortals, Display, TEXT("Liminal.Bench: %d fotogramas de calentamiento (+%d el primer caso) y %d medidos por caso; informe %s"),
		Config.WarmupFrames, Config.FirstCaseExtraWarmup, Config.MeasureFrames, *Config.OutputPath);
	return true;
}

void FLiminalBenchRun::OnWorldCleanup(UWorld* CleanedWorld, bool bSessionEnded, bool bCleanupResources)
{
	if (CleanedWorld == World.Get() && Phase != EPhase::Finished)
	{
		Finish(TEXT("el mundo se destruyó durante la medida"));
	}
}

bool FLiminalBenchRun::OnTick(float DeltaTime)
{
	if (Phase == EPhase::Finished)
	{
		return false;
	}
	if (!World.IsValid() || !Subsystem.IsValid())
	{
		Finish(TEXT("el mundo o el subsistema de portales ya no existen"));
		return false;
	}
	if (++TotalFrames > Config.MaxTotalFrames)
	{
		Finish(FString::Printf(TEXT("se superaron %d fotogramas"), Config.MaxTotalFrames));
		return false;
	}

	switch (Phase)
	{
	case EPhase::WaitPawn:
		UpdateWaitPawn();
		break;
	case EPhase::Warmup:
	{
		const int32 Warmup = Config.WarmupFrames + (CurrentCase == 0 ? Config.FirstCaseExtraWarmup : 0);
		if (++PhaseFrames >= Warmup)
		{
			BeginMeasure();
		}
		break;
	}
	case EPhase::Measure:
		SampleFrame(DeltaTime);
		break;
	case EPhase::Walk:
		UpdateWalk();
		break;
	case EPhase::Cross:
		UpdateCrossing(DeltaTime);
		break;
	default:
		break;
	}
	return Phase != EPhase::Finished;
}

void FLiminalBenchRun::UpdateWaitPawn()
{
	UWorld* GameWorld = World.Get();
	APlayerController* Controller = GameWorld->GetFirstPlayerController();
	ALiminalCharacter* Pawn = Controller ? Cast<ALiminalCharacter>(Controller->GetPawn()) : nullptr;
	if (!Pawn)
	{
		if (TotalFrames > LiminalBenchDetail::MaxWaitPawnFrames)
		{
			Finish(TEXT("no apareció un ALiminalCharacter controlado por el jugador"));
		}
		return;
	}
	PlayerController = Controller;
	Character = Pawn;

	FString Error;
	if (!BuildCases(Error))
	{
		Finish(Error);
		return;
	}
	Controller->SetIgnoreLookInput(true);
	Controller->SetIgnoreMoveInput(true);
	TeleportsAtStart = Subsystem->GetTeleportCount();
	BeginCase(0);
}

bool FLiminalBenchRun::BuildCases(FString& OutError)
{
	UWorld* GameWorld = World.Get();
	TActorIterator<ALiminalHub> HubIt(GameWorld);
	if (!HubIt)
	{
		OutError = TEXT("el mapa no tiene ALiminalHub (Liminal.Bench se lanza en /Game/Maps/L_Hub)");
		return false;
	}
	const ALiminalHub* Hub = *HubIt;
	HubTransform = FTransform(Hub->GetActorQuat(), Hub->GetActorLocation());

	for (const LiminalBenchMath::FBenchPose& Pose : LiminalBenchMath::MakeHubBenchPoses())
	{
		FCase& Case = Cases.AddDefaulted_GetRef();
		Case.Pose = Pose;
		Case.Feet = HubTransform.TransformPositionNoScale(FVector(Pose.Feet.X, Pose.Feet.Y, 0.0));
		Case.Rotation = FRotator(0.0, HubTransform.Rotator().Yaw + Pose.Yaw, 0.0);
	}
	return true;
}

void FLiminalBenchRun::SetPortalsEnabled(bool bEnabled) const
{
	if (IConsoleVariable* Variable = LiminalBenchDetail::FindCVar(LiminalBenchDetail::PortalsEnableCVar))
	{
		Variable->Set(bEnabled ? 1 : 0, ECVF_SetByConsole);
	}
}

void FLiminalBenchRun::BeginCase(int32 Index)
{
	CurrentCase = Index;
	PhaseFrames = 0;
	Phase = EPhase::Warmup;
	const FCase& Case = Cases[Index];
	SetPortalsEnabled(Case.Pose.bPortalsEnabled);
	LiminalDebugWorld::PlacePlayer(PlayerController.Get(), Character.Get(), Case.Feet, Case.Rotation);
	UE_LOG(LogLiminalPortals, Display, TEXT("Liminal.Bench: caso %s (%d capturas esperadas, portales %s)"),
		*Case.Pose.Name, Case.Pose.ExpectedCaptures, Case.Pose.bPortalsEnabled ? TEXT("activos") : TEXT("apagados"));
}

void FLiminalBenchRun::BeginMeasure()
{
	Phase = EPhase::Measure;
	PhaseFrames = 0;
	// Descarta las muestras de GPU acumuladas: solo cuentan las que lleguen desde ahora.
	uint64 Discarded = 0;
	while (GpuHistory.PopFrameCycles(Discarded) != FRHIGPUFrameTimeHistory::EResult::Empty)
	{
	}
	FCase& Case = Cases[CurrentCase];
	Case.PredictedVisible = PredictVisible(Case);
	if (Config.bProfile)
	{
		LiminalGpuProfile::BeginRecording();
	}
}

int32 FLiminalBenchRun::PredictVisible(const FCase& Case) const
{
	const APlayerController* Controller = PlayerController.Get();
	const ULiminalPortalSubsystem* PortalSubsystem = Subsystem.Get();
	if (!Controller || !Controller->PlayerCameraManager || !PortalSubsystem)
	{
		return INDEX_NONE;
	}
	int32 SizeX = 0;
	int32 SizeY = 0;
	Controller->GetViewportSize(SizeX, SizeY);
	if (SizeX <= 0 || SizeY <= 0)
	{
		return INDEX_NONE;
	}

	TArray<FTransform> Planes;
	FVector2D HalfExtents = FVector2D::ZeroVector;
	for (TActorIterator<ALiminalPortal> It(World.Get()); It; ++It)
	{
		if (It->LinkedPortal && !It->bExitOnly)
		{
			Planes.Add(It->GetPlaneTransform());
			HalfExtents = It->GetHalfExtents();
		}
	}
	const FMinimalViewInfo& View = Controller->PlayerCameraManager->GetCameraCacheView();
	return LiminalBenchMath::CountPotentiallyVisiblePortals(Planes, HalfExtents, View.Location, View.Rotation.Vector(),
		View.FOV, static_cast<float>(SizeX) / static_cast<float>(SizeY));
}

void FLiminalBenchRun::SampleFrame(float DeltaTime)
{
	FCase& Case = Cases[CurrentCase];
	++PhaseFrames;

	const int32 Captures = Subsystem->GetLastCaptureCount();
	if (Captures > 0)
	{
		CaptureSize = Subsystem->GetLastCaptureSize();
	}
	Case.MinCaptures = FMath::Min(Case.MinCaptures, Captures);
	Case.MaxCaptures = FMath::Max(Case.MaxCaptures, Captures);

	if (Case.RenderThreadMs.Num() < Config.MeasureFrames)
	{
		Case.RenderThreadMs.Add(LiminalBenchDetail::CyclesToMs(GRenderThreadTime));
		Case.GameThreadMs.Add(LiminalBenchDetail::CyclesToMs(GGameThreadTime));
		Case.FrameMs.Add(static_cast<double>(DeltaTime) * 1000.0);
		Case.RhiThreadMs.Add(LiminalBenchDetail::CyclesToMs(GRHIThreadTime));
		Case.RenderThreadWaitMs.Add(LiminalBenchDetail::CyclesToMs(GRenderThreadWaitTime));
		Case.GameThreadWaitMs.Add(LiminalBenchDetail::CyclesToMs(GGameThreadWaitTime));
	}

	uint64 GpuCycles64 = 0;
	FRHIGPUFrameTimeHistory::EResult Result;
	while ((Result = GpuHistory.PopFrameCycles(GpuCycles64)) != FRHIGPUFrameTimeHistory::EResult::Empty)
	{
		Case.GpuDisjoint += Result == FRHIGPUFrameTimeHistory::EResult::Disjoint ? 1 : 0;
		if (Case.GpuMs.Num() < Config.MeasureFrames)
		{
			Case.GpuMs.Add(FPlatformTime::ToMilliseconds64(GpuCycles64));
		}
	}

	const bool bCpuDone = Case.RenderThreadMs.Num() >= Config.MeasureFrames;
	const bool bGpuDone = Case.GpuMs.Num() >= Config.MeasureFrames;
	if (bCpuDone && bGpuDone)
	{
		FinishCase();
	}
	else if (PhaseFrames > Config.MeasureFrames + LiminalBenchDetail::MaxExtraMeasureFrames)
	{
		Finish(FString::Printf(TEXT("caso %s: solo %d muestras de GPU en %d fotogramas (¿RHI sin tiempos de GPU?)"),
			*Case.Pose.Name, Case.GpuMs.Num(), PhaseFrames));
	}
}

void FLiminalBenchRun::FinishCase()
{
	FCase& Case = Cases[CurrentCase];
	Case.bMeasured = true;
	Case.Gpu = LiminalBenchMath::ComputeStats(Case.GpuMs);
	Case.RenderThread = LiminalBenchMath::ComputeStats(Case.RenderThreadMs);
	Case.GameThread = LiminalBenchMath::ComputeStats(Case.GameThreadMs);
	Case.Frame = LiminalBenchMath::ComputeStats(Case.FrameMs);
	Case.RhiThread = LiminalBenchMath::ComputeStats(Case.RhiThreadMs);
	Case.RenderThreadWait = LiminalBenchMath::ComputeStats(Case.RenderThreadWaitMs);
	Case.GameThreadWait = LiminalBenchMath::ComputeStats(Case.GameThreadWaitMs);
	if (Config.bProfile)
	{
		Case.Profile = LiminalGpuProfile::EndRecording();
	}
	UE_LOG(LogLiminalPortals, Display, TEXT("Liminal.Bench: %s GPU %.3f ms (p50 %.3f, p95 %.3f), RT %.3f ms, GT %.3f ms, capturas %d-%d (esperadas %d)"),
		*Case.Pose.Name, Case.Gpu.Mean, Case.Gpu.P50, Case.Gpu.P95, Case.RenderThread.Mean, Case.GameThread.Mean,
		Case.MinCaptures, Case.MaxCaptures, Case.Pose.ExpectedCaptures);

	if (!Case.CapturesMatch())
	{
		Finish(FString::Printf(TEXT("caso %s: %d-%d capturas por fotograma y se esperaban %d"),
			*Case.Pose.Name, Case.MinCaptures, Case.MaxCaptures, Case.Pose.ExpectedCaptures));
		return;
	}
	if (Subsystem->GetTeleportCount() != TeleportsAtStart)
	{
		Finish(TEXT("hubo un teletransporte durante la medida (pose demasiado cerca de un portal)"));
		return;
	}

	if (CurrentCase + 1 < Cases.Num())
	{
		BeginCase(CurrentCase + 1);
	}
	else if (Config.bProfile)
	{
		BeginWalk();
	}
	else
	{
		Finish(FString());
	}
}

void FLiminalBenchRun::BeginWalk()
{
	Phase = EPhase::Walk;
	PhaseFrames = 0;
	SetPortalsEnabled(true);
	Walk.Begin(HubTransform);
	uint64 Discarded = 0;
	while (GpuHistory.PopFrameCycles(Discarded) != FRHIGPUFrameTimeHistory::EResult::Empty)
	{
	}
	UE_LOG(LogLiminalPortals, Display, TEXT("Liminal.Bench: recorrido del hub (%d fotogramas por pose)"), FLiminalBenchWalk::HoldFrames);
}

void FLiminalBenchRun::UpdateWalk()
{
	Walk.Tick(World.Get(), PlayerController.Get(), Character.Get(), *Subsystem);
	uint64 GpuCycles64 = 0;
	while (GpuHistory.PopFrameCycles(GpuCycles64) != FRHIGPUFrameTimeHistory::EResult::Empty)
	{
		Walk.AddGpuSample(FPlatformTime::ToMilliseconds64(GpuCycles64));
	}
	if (!Walk.IsFinished())
	{
		return;
	}
	if (Subsystem->GetTeleportCount() != TeleportsAtStart)
	{
		Finish(TEXT("hubo un teletransporte durante el recorrido"));
		return;
	}
	Phase = EPhase::Cross;
	Crossing.Begin(World.Get(), HubTransform);
	UE_LOG(LogLiminalPortals, Display, TEXT("Liminal.Bench: cruces por las puertas del hub (%.0f s de espera antes de cada uno)"),
		FLiminalBenchCrossing::HoldSeconds);
}

void FLiminalBenchRun::UpdateCrossing(float DeltaTime)
{
	Crossing.Tick(DeltaTime, PlayerController.Get(), Character.Get(), *Subsystem);
	uint64 GpuCycles64 = 0;
	while (GpuHistory.PopFrameCycles(GpuCycles64) != FRHIGPUFrameTimeHistory::EResult::Empty)
	{
		Crossing.AddGpuSample(FPlatformTime::ToMilliseconds64(GpuCycles64));
	}
	if (Crossing.IsFinished())
	{
		Finish(Crossing.GetError());
	}
}

void FLiminalBenchRun::Finish(const FString& Error)
{
	if (Phase == EPhase::Finished)
	{
		return;
	}
	Phase = EPhase::Finished;
	Unbind();
	if (APlayerController* Controller = PlayerController.Get())
	{
		int32 SizeX = 0;
		int32 SizeY = 0;
		Controller->GetViewportSize(SizeX, SizeY);
		Viewport = FIntPoint(SizeX, SizeY);
		ViewFOV = Controller->PlayerCameraManager ? Controller->PlayerCameraManager->GetFOVAngle() : 0.f;
		Controller->ResetIgnoreLookInput();
		Controller->ResetIgnoreMoveInput();
	}
	if (IConsoleVariable* Variable = LiminalBenchDetail::FindCVar(LiminalBenchDetail::PortalsEnableCVar))
	{
		Variable->Set(OriginalPortalsEnable, ECVF_SetByConsole);
	}

	bool bWritten = WriteReport(Error);
	if (Config.bProfile)
	{
		bWritten &= WriteProfileReport(Error);
	}
	if (Error.IsEmpty() && bWritten)
	{
		UE_LOG(LogLiminalPortals, Display, TEXT("Liminal.Bench: %d casos en %s"), Cases.Num(), *Config.OutputPath);
	}
	else
	{
		UE_LOG(LogLiminalPortals, Error, TEXT("Liminal.Bench: %s"), Error.IsEmpty() ? TEXT("no se pudo escribir el informe") : *Error);
	}
	if (!GIsEditor)
	{
		FPlatformMisc::RequestExit(false, TEXT("Liminal.Bench"));
	}
}

void FLiminalBenchRun::Unbind()
{
	FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
	if (TickHandle.IsValid())
	{
		FTSTicker::RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
}

TSharedRef<FJsonObject> FLiminalBenchRun::CaseToJson(const FCase& Case)
{
	using namespace LiminalBenchDetail;
	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("name"), Case.Pose.Name);
	Object->SetBoolField(TEXT("portalsEnabled"), Case.Pose.bPortalsEnabled);
	Object->SetNumberField(TEXT("expectedCaptures"), Case.Pose.ExpectedCaptures);
	Object->SetNumberField(TEXT("minCaptures"), Case.bMeasured ? Case.MinCaptures : -1);
	Object->SetNumberField(TEXT("maxCaptures"), Case.bMeasured ? Case.MaxCaptures : -1);
	Object->SetBoolField(TEXT("capturesOk"), Case.CapturesMatch());
	Object->SetNumberField(TEXT("predictedVisible"), Case.PredictedVisible);
	Object->SetArrayField(TEXT("hubFeet"), LiminalDebugShared::ToJsonArray({ Case.Pose.Feet.X, Case.Pose.Feet.Y }));
	Object->SetNumberField(TEXT("hubYaw"), Case.Pose.Yaw);
	Object->SetNumberField(TEXT("gpuDisjoint"), Case.GpuDisjoint);
	Object->SetObjectField(TEXT("gpuMs"), StatsToJson(Case.Gpu));
	Object->SetObjectField(TEXT("renderThreadMs"), StatsToJson(Case.RenderThread));
	Object->SetObjectField(TEXT("gameThreadMs"), StatsToJson(Case.GameThread));
	Object->SetObjectField(TEXT("frameMs"), StatsToJson(Case.Frame));
	Object->SetObjectField(TEXT("rhiThreadMs"), StatsToJson(Case.RhiThread));
	Object->SetObjectField(TEXT("renderThreadWaitMs"), StatsToJson(Case.RenderThreadWait));
	Object->SetObjectField(TEXT("gameThreadWaitMs"), StatsToJson(Case.GameThreadWait));
	return Object;
}

TSharedRef<FJsonObject> FLiminalBenchRun::PerPortalToJson() const
{
	const FCase* Baseline = nullptr;
	const FCase* Disabled = nullptr;
	const FCase* Four = nullptr;
	TArray<double> Portals;
	TArray<double> GpuMean;
	TArray<double> GpuP50;
	TArray<double> RenderThreadMean;
	for (const FCase& Case : Cases)
	{
		if (!Case.bMeasured)
		{
			continue;
		}
		if (!Case.Pose.bPortalsEnabled)
		{
			Disabled = &Case;
			continue;
		}
		Baseline = Case.Pose.ExpectedCaptures == 0 ? &Case : Baseline;
		Four = Case.Pose.ExpectedCaptures == 4 ? &Case : Four;
		Portals.Add(Case.Pose.ExpectedCaptures);
		GpuMean.Add(Case.Gpu.Mean);
		GpuP50.Add(Case.Gpu.P50);
		RenderThreadMean.Add(Case.RenderThread.Mean);
	}

	// Principal: caso de 4 frente a la misma vista con los portales apagados (la vista no cambia).
	// Secundario: pendiente de mínimos cuadrados sobre 0, 1, 2 y 4 portales (cada caso mira otra cosa).
	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	double Value = 0.0;
	double Intercept = 0.0;
	if (Four && Disabled)
	{
		struct FPairedField
		{
			const TCHAR* Field;
			double WithPortals;
			double WithoutPortals;
		};
		const FPairedField Paired[] = {
			{ TEXT("gpuMsPaired"), Four->Gpu.Mean, Disabled->Gpu.Mean },
			{ TEXT("gpuMsPairedP50"), Four->Gpu.P50, Disabled->Gpu.P50 },
			{ TEXT("renderThreadMsPaired"), Four->RenderThread.Mean, Disabled->RenderThread.Mean },
			{ TEXT("gameThreadMsPaired"), Four->GameThread.Mean, Disabled->GameThread.Mean },
		};
		for (const FPairedField& Entry : Paired)
		{
			if (LiminalBenchMath::ComputeCostPerPortal(Entry.WithPortals, Entry.WithoutPortals, Four->Pose.ExpectedCaptures, Value))
			{
				Object->SetNumberField(Entry.Field, Value);
			}
		}
	}
	if (LiminalBenchMath::FitLine(Portals, GpuMean, Value, Intercept))
	{
		Object->SetNumberField(TEXT("gpuMsSlope"), Value);
		Object->SetNumberField(TEXT("gpuMsSlopeIntercept"), Intercept);
	}
	if (LiminalBenchMath::FitLine(Portals, GpuP50, Value, Intercept))
	{
		Object->SetNumberField(TEXT("gpuMsSlopeP50"), Value);
	}
	if (LiminalBenchMath::FitLine(Portals, RenderThreadMean, Value, Intercept))
	{
		Object->SetNumberField(TEXT("renderThreadMsSlope"), Value);
	}

	TArray<TSharedPtr<FJsonValue>> PerCase;
	for (const FCase& Case : Cases)
	{
		const bool bComparable = Baseline && Case.bMeasured && Case.Pose.bPortalsEnabled && Case.Pose.ExpectedCaptures > 0;
		if (bComparable && LiminalBenchMath::ComputeCostPerPortal(Case.Gpu.Mean, Baseline->Gpu.Mean, Case.Pose.ExpectedCaptures, Value))
		{
			const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), Case.Pose.Name);
			Entry->SetNumberField(TEXT("gpuMsVsPortals0"), Value);
			PerCase.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}
	Object->SetArrayField(TEXT("perCase"), PerCase);
	return Object;
}

bool FLiminalBenchRun::WriteReport(const FString& Error) const
{
	using namespace LiminalBenchDetail;

	bool bAllMeasured = !Cases.IsEmpty();
	TArray<TSharedPtr<FJsonValue>> CaseValues;
	for (const FCase& Case : Cases)
	{
		bAllMeasured &= Case.CapturesMatch();
		CaseValues.Add(MakeShared<FJsonValueObject>(CaseToJson(Case)));
	}

	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("complete"), Error.IsEmpty() && bAllMeasured);
	Root->SetStringField(TEXT("error"), Error);
	Root->SetStringField(TEXT("gpu"), GRHIAdapterName);
	Root->SetStringField(TEXT("rhi"), GDynamicRHI ? FString(GDynamicRHI->GetName()) : FString());
	Root->SetStringField(TEXT("gpuTimeSource"), TEXT("FRHIGPUFrameTimeHistory (RHIGetGPUFrameCycles)"));
	Root->SetArrayField(TEXT("viewport"), LiminalDebugShared::ToJsonArray({ static_cast<double>(Viewport.X), static_cast<double>(Viewport.Y) }));
	Root->SetArrayField(TEXT("renderTarget"), LiminalDebugShared::ToJsonArray({ static_cast<double>(CaptureSize.X), static_cast<double>(CaptureSize.Y) }));
	Root->SetNumberField(TEXT("fov"), ViewFOV);
	Root->SetNumberField(TEXT("warmupFrames"), Config.WarmupFrames);
	Root->SetNumberField(TEXT("firstCaseExtraWarmup"), Config.FirstCaseExtraWarmup);
	Root->SetNumberField(TEXT("measureFrames"), Config.MeasureFrames);
	Root->SetObjectField(TEXT("cvars"), CVarsToJson());
	Root->SetArrayField(TEXT("cases"), CaseValues);
	Root->SetObjectField(TEXT("perPortal"), PerPortalToJson());
	return SaveJson(Root, Config.OutputPath);
}

bool FLiminalBenchRun::WriteProfileReport(const FString& Error) const
{
	using namespace LiminalBenchDetail;

	TArray<TSharedPtr<FJsonValue>> CaseValues;
	for (const FCase& Case : Cases)
	{
		const TSharedRef<FJsonObject> Object = LiminalBenchProfileJson::SnapshotToJson(Case.Profile, MinProfileMsPerFrame);
		Object->SetStringField(TEXT("name"), Case.Pose.Name);
		Object->SetBoolField(TEXT("measured"), Case.bMeasured);
		Object->SetNumberField(TEXT("captures"), Case.bMeasured ? Case.MaxCaptures : -1);
		Object->SetNumberField(TEXT("gpuMsMean"), Case.Gpu.Mean);
		Object->SetNumberField(TEXT("frameMsMean"), Case.Frame.Mean);
		Object->SetNumberField(TEXT("fpsMean"), Case.Frame.Mean > 0.0 ? 1000.0 / Case.Frame.Mean : 0.0);
		Object->SetNumberField(TEXT("gameThreadMsMean"), Case.GameThread.Mean);
		Object->SetNumberField(TEXT("renderThreadMsMean"), Case.RenderThread.Mean);
		Object->SetNumberField(TEXT("renderThreadWaitMsMean"), Case.RenderThreadWait.Mean);
		Object->SetNumberField(TEXT("rhiThreadMsMean"), Case.RhiThread.Mean);
		Object->SetNumberField(TEXT("gameThreadWaitMsMean"), Case.GameThreadWait.Mean);
		CaseValues.Add(MakeShared<FJsonValueObject>(Object));
	}

	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("complete"), Error.IsEmpty() && Walk.IsFinished() && Crossing.IsFinished());
	Root->SetStringField(TEXT("error"), Error);
	Root->SetStringField(TEXT("gpu"), GRHIAdapterName);
	Root->SetStringField(TEXT("source"), TEXT("UE::RHI::GPUProfiler::FEventSink (migas de RDG, tiempo ocupado por cola)"));
	Root->SetNumberField(TEXT("maxDepth"), LiminalGpuProfile::MaxDepth);
	Root->SetNumberField(TEXT("minMsPerFrame"), MinProfileMsPerFrame);
	Root->SetArrayField(TEXT("viewport"), LiminalDebugShared::ToJsonArray({ static_cast<double>(Viewport.X), static_cast<double>(Viewport.Y) }));
	Root->SetArrayField(TEXT("renderTarget"), LiminalDebugShared::ToJsonArray({ static_cast<double>(CaptureSize.X), static_cast<double>(CaptureSize.Y) }));
	Root->SetObjectField(TEXT("cvars"), CVarsToJson());
	Root->SetArrayField(TEXT("cases"), CaseValues);
	Root->SetObjectField(TEXT("walk"), Walk.ToJson());
	Root->SetObjectField(TEXT("crossings"), Crossing.ToJson());
	return SaveJson(Root, Config.ProfileOutputPath);
}
