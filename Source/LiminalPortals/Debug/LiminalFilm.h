#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraTypes.h"
#include "Containers/Ticker.h"
#include "Debug/LiminalFilmMath.h"
#include "Delegates/IDelegateInstance.h"

class ALiminalCharacter;
class ALiminalPortal;
class APlayerController;
class ULiminalPortalSubsystem;
class UWorld;
struct FLiminalPortalTeleportEvent;

/**
 * Ejecución de Liminal.Film: rueda los planos de LiminalFilmMath::MakeDefaultFilmShots fotograma a
 * fotograma y guarda el viewport en <Salida>/<Plano>/frame_NNNN.png con un índice film.json.
 *
 * La pose de cada fotograma depende solo de su índice (t = fotograma / SampleRate), así que el
 * resultado no depende del tiempo real; con -UseFixedTimeStep -FPS=SampleRate también Lumen y TSR
 * avanzan igual en cada ejecución. El pawn se coloca en cada fotograma sin corte de cámara y los
 * cruces los hace ULiminalPortalSubsystem al pasar la cámara por el hueco, como con un jugador: la
 * ejecución sigue en qué lado está por OnPortalTeleport y comprueba que la vista de cada fotograma
 * coincide con la pose pedida (MaxDeviationCm) y que los cruces son los del recorrido.
 *
 * Igual que Liminal.Tour, todo se decide en ULiminalPortalSubsystem::OnPortalsUpdated; al acabar
 * pide salir.
 */
class FLiminalFilmRun
{
public:
	struct FConfig
	{
		/** Directorio absoluto de salida (se vacía al empezar). */
		FString OutputDir;
		/** Fotogramas de simulación por segundo de recorrido. */
		double SampleRate = 60.0;
		/** Se guarda un fotograma de cada CaptureEvery (60 / 2 = 30 fps de vídeo). */
		int32 CaptureEvery = 2;
		/** Fotogramas quietos en la primera pose de cada plano (convergencia de TSR y Lumen). */
		int32 PrerollFrames = 120;
		/** Solo los planos cuyo nombre contiene este texto (vacío = todos). */
		FString ShotFilter;
		/** Desviación máxima admitida entre la vista y la pose pedida, en cm. */
		double MaxDeviationCm = 1.0;
		int32 MaxTotalFrames = 20000;
	};

	FLiminalFilmRun(UWorld* InWorld, const FConfig& InConfig);
	~FLiminalFilmRun();

	FLiminalFilmRun(const FLiminalFilmRun&) = delete;
	FLiminalFilmRun& operator=(const FLiminalFilmRun&) = delete;

	bool Start(FString& OutError);
	bool IsFinished() const { return Phase == EPhase::Finished; }

private:
	enum class EPhase : uint8
	{
		WaitPawn,
		Preroll,
		Play,
		/** Último fotograma del plano: se espera a que llegue su captura. */
		Drain,
		Finished,
	};

	struct FShotRun
	{
		LiminalFilmMath::FFilmShot Shot;
		FString Label;
		/** Sistema de las claves: hub o portal de la puerta en el hub (vacío en Biome si el ancla es el hub). */
		FTransform Frame = FTransform::Identity;
		TWeakObjectPtr<const ALiminalPortal> HubPortal;
		TWeakObjectPtr<const ALiminalPortal> BiomePortal;
		/** Paso del portal del hub al de vuelta (lleva la pose desplegada al lado del bioma). */
		FTransform Transfer = FTransform::Identity;
		int32 SimFrames = 0;
		int32 ExpectedCrossings = 0;
		int32 Crossings = 0;
		int32 SavedFrames = 0;
		double MaxDeviationCm = 0.0;
		bool bComplete = false;
	};

	void OnPortalsUpdated(const FMinimalViewInfo& View);
	void OnPortalTeleport(const FLiminalPortalTeleportEvent& Event);
	void OnScreenshotCaptured(int32 Width, int32 Height, const TArray<FColor>& Colors);
	bool OnWatchdogTick(float DeltaTime);
	void OnWorldCleanup(UWorld* CleanedWorld, bool bSessionEnded, bool bCleanupResources);

	void UpdateWaitPawn();
	bool BuildShots(FString& OutError);
	void BeginShot(int32 Index);
	void UpdatePlay(const FMinimalViewInfo& View);
	/** Pose de mundo del fotograma Frame del plano en curso, en el lado en que está la cámara. */
	LiminalFilmMath::FFilmPose GetWorldPose(int32 Frame) const;
	void ApplyPose(const LiminalFilmMath::FFilmPose& Pose, bool bCut);

	void Finish(const FString& Error);
	void Unbind();
	bool WriteReport(const FString& Error) const;

	FConfig Config;
	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<ULiminalPortalSubsystem> Subsystem;
	TWeakObjectPtr<APlayerController> PlayerController;
	TWeakObjectPtr<ALiminalCharacter> Character;

	FDelegateHandle PortalsUpdatedHandle;
	FDelegateHandle TeleportHandle;
	FDelegateHandle ScreenshotHandle;
	FDelegateHandle WorldCleanupHandle;
	FTSTicker::FDelegateHandle WatchdogHandle;

	TArray<FShotRun> Shots;
	EPhase Phase = EPhase::WaitPawn;
	int32 CurrentShot = 0;
	int32 PhaseFrames = 0;
	int32 TotalFrames = 0;
	int32 TicksWithoutUpdate = 0;
	/** La cámara del plano en curso está al otro lado de la puerta (en el bioma). */
	bool bBeyondDoor = false;
	FString PendingScreenshotPath;
	FString RunError;
	FIntPoint ScreenshotSize = FIntPoint::ZeroValue;
};
