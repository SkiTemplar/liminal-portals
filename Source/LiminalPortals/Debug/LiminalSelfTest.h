#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraTypes.h"
#include "Containers/Ticker.h"
#include "Debug/LiminalDebugWorld.h"
#include "Debug/LiminalSelfTestMath.h"
#include "Delegates/IDelegateInstance.h"
#include "Portal/LiminalPortalSubsystem.h"

class ALiminalCharacter;
class ALiminalPortal;
class APlayerController;
class UWorld;

/**
 * Ejecución de Liminal.SelfTest (etapa E3 de docs/PLAN-NOCHE.md).
 *
 * Para cada puerta del hub coloca al jugador delante y lo hace caminar con entrada simulada
 * (Enhanced Input inyectado en la acción de movimiento; la mirada se fija con la rotación de
 * control): cruza al bioma, se aleja, se da la vuelta y vuelve a cruzar al hub, CyclesPerDoor
 * veces. El teletransporte lo hace siempre el subsistema de portales.
 *
 * En cada cruce comprueba (LiminalSelfTestMath) que hubo teletransporte al portal esperado, que el
 * módulo de la velocidad se conserva (1 %), que su dirección gira con el portal, que la posición
 * se lleva al otro lado y que la velocidad no cae en el fotograma siguiente. En cada fotograma
 * comprueba que la cámara no queda detrás del plano de ningún portal (túnel del marco o detrás de
 * la placa) ni dentro de geometría. Escribe el informe JSON y pide salir.
 */
class FLiminalSelfTestRun
{
public:
	struct FConfig
	{
		/** Ruta absoluta del informe JSON. */
		FString OutputPath;
		int32 CyclesPerDoor = 20;
		/** Distancia de la cámara al plano del portal al colocar al jugador, en cm. */
		double StartDistance = 300.0;
		/** Distancia que se aleja del portal de salida antes de darse la vuelta, en cm. */
		double DepartDistance = 150.0;
		/** Punto de mira detrás del plano para caminar recto hacia el centro del hueco, en cm. */
		double AimBehindPlane = 200.0;
		int32 GroundedFrames = 10;
		int32 CrossingTimeoutFrames = 900;
		int32 MaxTotalFrames = 60000;
		/** Radio de la esfera con la que se prueba si la cámara está dentro de geometría, en cm. */
		double CameraProbeRadius = 5.0;
		/** La velocidad del fotograma siguiente al cruce no puede bajar de esta fracción. */
		double MinNextFrameSpeedRatio = 0.99;
		LiminalSelfTestMath::FCrossingTolerances Tolerances;
	};

	FLiminalSelfTestRun(UWorld* InWorld, const FConfig& InConfig);
	~FLiminalSelfTestRun();

	FLiminalSelfTestRun(const FLiminalSelfTestRun&) = delete;
	FLiminalSelfTestRun& operator=(const FLiminalSelfTestRun&) = delete;

	/** @return false (con el motivo en OutError) si no se pudo empezar. */
	bool Start(FString& OutError);

	bool IsFinished() const { return Phase == EPhase::Finished; }

private:
	enum class EPhase : uint8
	{
		WaitPawn,
		Grounding,
		Approach,
		Depart,
		Finished,
	};

	struct FCrossingRecord
	{
		int32 Door = 0;
		bool bHubToBiome = true;
		int32 Frame = 0;
		LiminalSelfTestMath::FCrossingCheck Check;
		double TeleportDistanceCm = 0.0;
		double CameraFrontDistanceCm = 0.0;
		double NextFrameSpeedRatio = -1.0;
		bool bExpectedPortals = false;
		bool Passed(double MinNextFrameSpeedRatio) const;
	};

	struct FDoorResult
	{
		FLiminalDoorPair Pair;
		FString HubPortalName;
		FString BiomePortalName;
		int32 HubToBiome = 0;
		int32 BiomeToHub = 0;
		int32 FailedCrossings = 0;
		TArray<FString> Errors;
	};

	void OnPortalsUpdated(const FMinimalViewInfo& View);
	void OnPortalTeleport(const FLiminalPortalTeleportEvent& Event);
	bool OnWatchdogTick(float DeltaTime);
	void OnWorldCleanup(UWorld* CleanedWorld, bool bSessionEnded, bool bCleanupResources);

	void UpdateWaitPawn();
	void UpdateGrounding();
	void UpdateApproach(const FMinimalViewInfo& View);
	void UpdateDepart(const FMinimalViewInfo& View);
	void HandleTeleport(const FMinimalViewInfo& View);
	void CheckCamera(const FMinimalViewInfo& View);

	void BeginDoor(int32 DoorIndex);
	void FinishDoor();
	void FailDoor(const FString& Error);
	ALiminalPortal* GetPortal(bool bHubSide) const;
	void Steer(const FVector& CameraLocation, const ALiminalPortal* Target);
	void InjectForward();

	void Finish(const FString& Error);
	void Unbind();
	bool WriteReport(const FString& Error) const;
	bool HasPassed(const FString& Error) const;

	FConfig Config;
	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<ULiminalPortalSubsystem> Subsystem;
	TWeakObjectPtr<APlayerController> PlayerController;
	TWeakObjectPtr<ALiminalCharacter> Character;

	FDelegateHandle PortalsUpdatedHandle;
	FDelegateHandle TeleportHandle;
	FDelegateHandle WorldCleanupHandle;
	FTSTicker::FDelegateHandle WatchdogHandle;

	TArray<FDoorResult> Doors;
	TArray<FCrossingRecord> Crossings;
	TArray<TWeakObjectPtr<ALiminalPortal>> AllPortals;

	EPhase Phase = EPhase::WaitPawn;
	int32 CurrentDoor = 0;
	/** true si el jugador está en el lado del hub de la puerta actual. */
	bool bOnHubSide = true;
	int32 PhaseFrames = 0;
	/** Fotogramas seguidos con el personaje en el suelo (fase Grounding). */
	int32 GroundedStreak = 0;
	int32 TotalFrames = 0;
	int32 TicksWithoutUpdate = 0;

	/** Evento recibido en este fotograma (llega dentro de UpdatePortals, antes de OnPortalsUpdated). */
	TOptional<FLiminalPortalTeleportEvent> PendingTeleport;
	int32 TeleportsThisFrame = 0;
	/** Índice en Crossings del cruce cuya velocidad se mide en el fotograma siguiente. */
	int32 PendingNextFrameCheck = INDEX_NONE;

	int32 CameraInsidePortalFrames = 0;
	int32 CameraInGeometryFrames = 0;
	FString FirstCameraError;
};
