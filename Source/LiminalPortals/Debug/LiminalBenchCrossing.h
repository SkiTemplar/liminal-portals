#pragma once

#include "CoreMinimal.h"

class ALiminalCharacter;
class ALiminalPortal;
class APlayerController;
class FJsonObject;
class ULiminalPortalSubsystem;
class UWorld;

/**
 * Cruces del modo perfil de Liminal.Bench (D-36): por cada puerta del hub coloca al jugador a
 * StartDistance del plano mirándola, espera HoldSeconds (los portales del bioma liberan su estado de
 * render, D-31, como en una partida normal), camina hasta cruzar y guarda el tiempo de fotograma,
 * de GPU, de render thread y de game thread de los WindowBefore fotogramas anteriores al cruce y los
 * WindowAfter siguientes: los picos al entrar en el bioma. Después se da la vuelta (TurnDegrees en TurnFrames
 * fotogramas, sin andar) hacia la puerta de vuelta del bioma y guarda TurnWindow fotogramas desde el inicio del
 * giro, con el estado de render que tenía esa puerta justo antes: el tirón al mirar atrás. Luego devuelve al
 * jugador al hub.
 */
class FLiminalBenchCrossing
{
public:
	static constexpr double StartDistance = 250.0;
	static constexpr double HoldSeconds = 12.0;
	static constexpr int32 WindowBefore = 15;
	static constexpr int32 WindowAfter = 15;
	/** Fotogramas de caminata sin cruce tras los que se aborta la puerta. */
	static constexpr int32 MaxWalkFrames = 600;
	/** Pasadas por todas las puertas. */
	static constexpr int32 Passes = 2;
	/** Giro tras cruzar: grados de yaw, fotogramas que dura y fotogramas que se guardan desde su inicio. */
	static constexpr double TurnDegrees = 180.0;
	static constexpr int32 TurnFrames = 20;
	static constexpr int32 TurnWindow = 60;

	void Begin(UWorld* World, const FTransform& HubTransform);
	bool IsFinished() const { return bFinished; }
	/** Error de la última puerta abortada ("" si todas cruzaron). */
	const FString& GetError() const { return Error; }

	void Tick(float DeltaTime, APlayerController* Controller, ALiminalCharacter* Character,
		const ULiminalPortalSubsystem& Subsystem);
	void AddGpuSample(double Milliseconds);

	TSharedRef<FJsonObject> ToJson() const;

private:
	enum class EStep : uint8
	{
		Place,
		Hold,
		Walk,
		After,
		Turn,
	};

	struct FFrame
	{
		double FrameMs = 0.0;
		double RenderThreadMs = 0.0;
		double GameThreadMs = 0.0;
		int32 Captures = 0;
	};

	struct FDoor
	{
		FString Portal;
		int32 Pass = 1;
		bool bCrossed = false;
		/** Fotogramas de espera y caminata con el destino de esta puerta precalentado en Lumen (D-37). */
		int32 PrewarmFrames = 0;
		/** Fotogramas de la ventana; el del cruce es el índice WindowBefore. */
		TArray<FFrame> Frames;
		/** Muestras de GPU llegadas durante la ventana (llegan con unos fotogramas de retraso). */
		TArray<double> GpuMs;
		/** Puerta de vuelta del bioma (la enlazada con la del hub) y si tenía estado de render al cruzar y al empezar a girar. */
		FString ReturnPortal;
		bool bReturnHadStateAtCross = false;
		bool bReturnHadStateBeforeTurn = false;
		/** Fotograma del giro en que la puerta de vuelta recupera el estado de render (INDEX_NONE si no lo hace). */
		int32 ReturnStateFrame = INDEX_NONE;
		/** Fotogramas desde el inicio del giro (índice 0: el primero con la rotación nueva aplicada). */
		TArray<FFrame> TurnFrames;
		/** Muestras de GPU llegadas durante el giro (las primeras pueden ser aún de la caminata). */
		TArray<double> TurnGpuMs;
	};

	void NextDoor();
	void BeginTurn(APlayerController& Controller, const ALiminalPortal& Portal);
	void TickTurn(const FFrame& Frame, APlayerController& Controller, const ALiminalPortal& Portal);
	static TSharedRef<FJsonObject> TurnToJson(const FDoor& Door);

	TArray<TWeakObjectPtr<ALiminalPortal>> Doors;
	TArray<FDoor> Results;
	TArray<FFrame> History;
	EStep Step = EStep::Place;
	int32 DoorIndex = 0;
	int32 StepFrames = 0;
	double HoldStart = 0.0;
	int32 TeleportsBefore = 0;
	double TurnStartYaw = 0.0;
	int32 PrewarmFrames = 0;
	bool bRecordingGpu = false;
	bool bRecordingTurnGpu = false;
	bool bFinished = true;
	FString Error;
};
