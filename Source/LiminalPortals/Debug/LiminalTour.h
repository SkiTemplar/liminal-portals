#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraTypes.h"
#include "Containers/Ticker.h"
#include "Delegates/IDelegateInstance.h"

class ALiminalCharacter;
class ALiminalPortal;
class APlayerController;
class ULiminalPortalSubsystem;
class UWorld;

/**
 * Ejecución de Liminal.Tour (etapas E3 y E4 de docs/PLAN-NOCHE.md): coloca al jugador en una serie
 * de puntos de vista fijos (vista general del hub y, para cada puerta, desde el hub mirando la
 * puerta, desde el bioma mirando la puerta de vuelta y desde dentro del bioma de espaldas a la
 * puerta), espera a que TSR y Lumen converjan y guarda la captura del viewport en
 * <Salida>/NN_<nombre>.png con un índice tour.json. En las vistas de puerta el índice lleva los
 * polígonos en píxeles del hueco, del marco y de la franja de pared que lo rodea (check_tour.py).
 * Al acabar pide salir.
 *
 * Igual que Liminal.Calibrate, todo se decide en ULiminalPortalSubsystem::OnPortalsUpdated, con la
 * vista final del fotograma; la captura se resuelve en el Draw de ese mismo fotograma.
 */
class FLiminalTourRun
{
public:
	struct FConfig
	{
		/** Directorio absoluto de salida (se vacían sus PNG al empezar). */
		FString OutputDir;
		/** Fotogramas quietos en cada punto antes de la captura. */
		int32 SettleFrames = 90;
		/** Distancia de los pies al plano de la puerta, desde el hub y desde el bioma, en cm. */
		double HubViewDistance = 350.0;
		double BiomeViewDistance = 450.0;
		/** Vista desde dentro del bioma: pies a esta distancia de la puerta, mirando al paisaje. */
		double InsideViewDistance = 600.0;
		double InsideViewPitch = -4.0;
		/** Franja de pared alrededor del marco que se mide en las vistas de puerta, en cm. */
		double WallBand = 50.0;
		int32 MaxTotalFrames = 5000;
	};

	FLiminalTourRun(UWorld* InWorld, const FConfig& InConfig);
	~FLiminalTourRun();

	FLiminalTourRun(const FLiminalTourRun&) = delete;
	FLiminalTourRun& operator=(const FLiminalTourRun&) = delete;

	bool Start(FString& OutError);
	bool IsFinished() const { return Phase == EPhase::Finished; }

private:
	enum class EPhase : uint8
	{
		WaitPawn,
		Settle,
		WaitScreenshot,
		Finished,
	};

	struct FShot
	{
		FString Name;
		/** hub, from_hub, from_biome o inside. */
		FString Kind;
		/** Puerta y bioma (doorN_<Tipo>); vacío en la vista general. */
		FString Biome;
		/** Portal que se mira (nulo en la vista general y desde dentro). */
		TWeakObjectPtr<const ALiminalPortal> Portal;
		FVector Feet = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		/** Vista real del fotograma capturado. */
		FVector ViewLocation = FVector::ZeroVector;
		FRotator ViewRotation = FRotator::ZeroRotator;
		FIntPoint Viewport = FIntPoint::ZeroValue;
		/** Polígonos en píxeles del viewport: hueco, exterior del marco y exterior de la franja de pared. */
		TArray<FVector2D> PortalCorners;
		TArray<FVector2D> FrameCorners;
		TArray<FVector2D> WallCorners;
		FString Path;
		bool bSaved = false;
	};

	void OnPortalsUpdated(const FMinimalViewInfo& View);
	void OnScreenshotCaptured(int32 Width, int32 Height, const TArray<FColor>& Colors);
	bool OnWatchdogTick(float DeltaTime);
	void OnWorldCleanup(UWorld* CleanedWorld, bool bSessionEnded, bool bCleanupResources);

	void UpdateWaitPawn();
	bool BuildShots(FString& OutError);
	void BeginShot(int32 Index);
	void RecordPolygons(FShot& Shot, const FMinimalViewInfo& View) const;

	void Finish(const FString& Error);
	void Unbind();
	bool WriteReport(const FString& Error) const;

	FConfig Config;
	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<ULiminalPortalSubsystem> Subsystem;
	TWeakObjectPtr<APlayerController> PlayerController;
	TWeakObjectPtr<ALiminalCharacter> Character;

	FDelegateHandle PortalsUpdatedHandle;
	FDelegateHandle ScreenshotHandle;
	FDelegateHandle WorldCleanupHandle;
	FTSTicker::FDelegateHandle WatchdogHandle;

	TArray<FShot> Shots;
	EPhase Phase = EPhase::WaitPawn;
	int32 CurrentShot = 0;
	int32 PhaseFrames = 0;
	int32 TotalFrames = 0;
	int32 TicksWithoutUpdate = 0;
	FString PendingScreenshotPath;
	FString ScreenshotError;
	FIntPoint ScreenshotSize = FIntPoint::ZeroValue;
};
