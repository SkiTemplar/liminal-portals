#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Debug/LiminalBenchMath.h"
#include "GPUProfiler.h"

class ALiminalCharacter;
class APlayerController;
class FJsonObject;
class ULiminalPortalSubsystem;
class UWorld;

/**
 * Liminal.Bench (etapa E5): coloca la cámara en las poses fijas del hub
 * (LiminalBenchMath::MakeHubBenchPoses), calienta, mide por fotograma el tiempo de GPU
 * (FRHIGPUFrameTimeHistory, la serie de la que sale RHIGetGPUFrameCycles), de render thread y de
 * game thread, comprueba con ULiminalPortalSubsystem::GetLastCaptureCount las capturas de cada
 * fotograma medido y escribe el informe JSON; después pide salir.
 *
 * Lo mueve el ticker del núcleo (después de UEngine::Tick) y no OnPortalsUpdated, porque con
 * Liminal.Portals.Enable 0 el subsistema no emite ese evento.
 */
class FLiminalBenchRun
{
public:
	struct FConfig
	{
		/** Ruta absoluta del informe. */
		FString OutputPath;
		/** Fotogramas quietos antes de medir cada caso (mínimo 60). */
		int32 WarmupFrames = 120;
		/** Calentamiento extra del primer caso (carga de shaders, Lumen, generación de los biomas). */
		int32 FirstCaseExtraWarmup = 240;
		/** Fotogramas medidos por caso (mínimo 120), tanto de CPU como de GPU. */
		int32 MeasureFrames = 240;
		/** Tope de fotogramas de la ejecución completa. */
		int32 MaxTotalFrames = 20000;
	};

	FLiminalBenchRun(UWorld* InWorld, const FConfig& InConfig);
	~FLiminalBenchRun();

	FLiminalBenchRun(const FLiminalBenchRun&) = delete;
	FLiminalBenchRun& operator=(const FLiminalBenchRun&) = delete;

	bool Start(FString& OutError);
	bool IsFinished() const { return Phase == EPhase::Finished; }

private:
	enum class EPhase : uint8
	{
		WaitPawn,
		Warmup,
		Measure,
		Finished,
	};

	struct FCase
	{
		LiminalBenchMath::FBenchPose Pose;
		FVector Feet = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		/** Portales que pasan el test de visibilidad del subsistema desde la vista real (informativo). */
		int32 PredictedVisible = INDEX_NONE;
		int32 MinCaptures = MAX_int32;
		int32 MaxCaptures = MIN_int32;
		int32 GpuDisjoint = 0;
		TArray<double> GpuMs;
		TArray<double> RenderThreadMs;
		TArray<double> GameThreadMs;
		TArray<double> FrameMs;
		LiminalBenchMath::FSampleStats Gpu;
		LiminalBenchMath::FSampleStats RenderThread;
		LiminalBenchMath::FSampleStats GameThread;
		LiminalBenchMath::FSampleStats Frame;
		bool bMeasured = false;

		bool CapturesMatch() const { return bMeasured && MinCaptures == Pose.ExpectedCaptures && MaxCaptures == Pose.ExpectedCaptures; }
	};

	bool OnTick(float DeltaTime);
	void OnWorldCleanup(UWorld* CleanedWorld, bool bSessionEnded, bool bCleanupResources);

	void UpdateWaitPawn();
	bool BuildCases(FString& OutError);
	void BeginCase(int32 Index);
	void BeginMeasure();
	void SampleFrame(float DeltaTime);
	void FinishCase();
	int32 PredictVisible(const FCase& Case) const;

	void SetPortalsEnabled(bool bEnabled) const;
	void Finish(const FString& Error);
	void Unbind();
	static TSharedRef<FJsonObject> CaseToJson(const FCase& Case);
	TSharedRef<FJsonObject> PerPortalToJson() const;
	bool WriteReport(const FString& Error) const;

	FConfig Config;
	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<ULiminalPortalSubsystem> Subsystem;
	TWeakObjectPtr<APlayerController> PlayerController;
	TWeakObjectPtr<ALiminalCharacter> Character;

	FTSTicker::FDelegateHandle TickHandle;
	FDelegateHandle WorldCleanupHandle;
	FRHIGPUFrameTimeHistory::FState GpuHistory;

	TArray<FCase> Cases;
	EPhase Phase = EPhase::WaitPawn;
	int32 CurrentCase = 0;
	int32 PhaseFrames = 0;
	int32 TotalFrames = 0;
	int32 TeleportsAtStart = 0;
	int32 OriginalPortalsEnable = 1;
	FIntPoint Viewport = FIntPoint::ZeroValue;
	/** Render target de las capturas medidas (el del último fotograma con capturas). */
	FIntPoint CaptureSize = FIntPoint::ZeroValue;
	float ViewFOV = 0.f;
};
