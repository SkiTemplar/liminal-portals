#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraTypes.h"
#include "Containers/Ticker.h"
#include "Delegates/IDelegateInstance.h"

class ALiminalPortal;
class APlayerController;
class ULiminalPortalSubsystem;
class UWorld;

/**
 * Ejecución de Liminal.Calibrate (etapa E2 de docs/PLAN-NOCHE.md).
 *
 * Delante del portal de calibración gira la cámara con una secuencia determinista de yaw (10° por
 * fotograma) en dos pasadas idénticas: con la superficie del portal visible y con ella oculta. En
 * cada fotograma guarda la captura del viewport en <Salida>/{visible,hidden}/NNN.png y las esquinas
 * del portal en pantalla (la máscara) en <Salida>/calibration.json. Al acabar pide salir del juego.
 *
 * Orden de fotograma: todo se decide en ULiminalPortalSubsystem::OnPortalsUpdated, que llega con la
 * vista final del fotograma. Ahí se pide la captura de pantalla (se resuelve en el Draw de ese mismo
 * fotograma) y se fija la rotación de control del paso siguiente (la usará la cámara del fotograma
 * siguiente). Los cambios de pasada se aplican un fotograma después de la última captura.
 */
class FLiminalCalibrationRun
{
public:
	struct FConfig
	{
		/** Directorio absoluto de salida. */
		FString OutputDir;
		double StepDegrees = 10.0;
		double AmplitudeDegrees = 20.0;
		int32 FramesPerPass = 32;
		/** Fotogramas quietos antes de cada pasada para que TSR y Lumen converjan igual en ambas. */
		int32 SettleFrames = 45;
		/** Fotogramas en el suelo antes de empezar (el pawn cae al aparecer). */
		int32 GroundedFrames = 15;
		/** Límite de fotogramas de la ejecución completa; si se supera, se aborta con error. */
		int32 MaxTotalFrames = 3000;
	};

	FLiminalCalibrationRun(UWorld* InWorld, const FConfig& InConfig);
	~FLiminalCalibrationRun();

	FLiminalCalibrationRun(const FLiminalCalibrationRun&) = delete;
	FLiminalCalibrationRun& operator=(const FLiminalCalibrationRun&) = delete;

	/** @return false (con el motivo en OutError) si no se pudo empezar. */
	bool Start(FString& OutError);

	bool IsFinished() const { return Phase == EPhase::Finished; }

private:
	enum class EPhase : uint8
	{
		WaitPawn,
		Settle,
		Sweep,
		SwitchPass,
		Finished,
	};

	struct FFrameRecord
	{
		int32 Pass = 0;
		int32 Index = 0;
		double YawOffset = 0.0;
		FRotator Rotation = FRotator::ZeroRotator;
		TArray<FVector2D, TFixedAllocator<4>> Corners;
	};

	void OnPortalsUpdated(const FMinimalViewInfo& View);
	void OnScreenshotCaptured(int32 Width, int32 Height, const TArray<FColor>& Colors);
	bool OnWatchdogTick(float DeltaTime);
	void OnWorldCleanup(UWorld* CleanedWorld, bool bSessionEnded, bool bCleanupResources);

	void UpdateWaitPawn(const FMinimalViewInfo& View);
	void UpdateSweep(const FMinimalViewInfo& View);
	void UpdateSwitchPass();

	void BeginPass(int32 PassIndex);
	void ApplyStepRotation(int32 StepIndex);
	ALiminalPortal* FindCalibrationPortal(const FVector& CameraLocation) const;
	bool RecordFrame(const FMinimalViewInfo& View, FFrameRecord& OutRecord);
	FString GetFramePath(int32 PassIndex, int32 StepIndex) const;

	void Finish(const FString& Error);
	void Unbind();
	bool WriteReport(const FString& Error) const;
	void RestorePlayer();

	FConfig Config;
	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<ULiminalPortalSubsystem> Subsystem;
	TWeakObjectPtr<APlayerController> PlayerController;
	TWeakObjectPtr<ALiminalPortal> Portal;

	FDelegateHandle PortalsUpdatedHandle;
	FDelegateHandle ScreenshotHandle;
	FDelegateHandle WorldCleanupHandle;
	FTSTicker::FDelegateHandle WatchdogHandle;

	TArray<double> YawSweep;
	TArray<FFrameRecord> Frames;
	FRotator BaseRotation = FRotator::ZeroRotator;
	FVector BaseLocation = FVector::ZeroVector;

	EPhase Phase = EPhase::WaitPawn;
	int32 CurrentPass = 0;
	int32 StepIndex = 0;
	int32 PhaseFrames = 0;
	int32 TotalFrames = 0;
	/** Ticks del motor sin noticias del subsistema; detecta que UpdatePortals no se llama. */
	int32 TicksWithoutUpdate = 0;

	/** Ruta de la captura de pantalla pedida en este fotograma; vacía si no hay ninguna pendiente. */
	FString PendingScreenshotPath;
	int32 SavedScreenshots = 0;
	/** Error al guardar una captura (llega en el Draw); se convierte en Finish en el fotograma siguiente. */
	FString ScreenshotError;
	FIntPoint ScreenshotSize = FIntPoint::ZeroValue;
	FIntPoint ViewportSize = FIntPoint::ZeroValue;
	double MaxProjectionErrorPx = 0.0;
	double MaxCameraDriftCm = 0.0;
	FString PortalName;
};
