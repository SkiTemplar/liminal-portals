#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "Templates/Function.h"
#include "LiminalPortalSubsystem.generated.h"

class ALiminalPortal;
class APlayerController;

/** Estado del pawn justo antes y justo después de un paso por portal (lo verifica Liminal.SelfTest). */
struct FLiminalPortalTeleportEvent
{
	ALiminalPortal* Source = nullptr;
	ALiminalPortal* Target = nullptr;
	FTransform Transfer = FTransform::Identity;
	FVector LocationBefore = FVector::ZeroVector;
	FVector LocationAfter = FVector::ZeroVector;
	FVector VelocityBefore = FVector::ZeroVector;
	FVector VelocityAfter = FVector::ZeroVector;
	/** Cámara del fotograma que detectó el cruce (ya detrás del plano de Source). */
	FVector CameraBefore = FVector::ZeroVector;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnLiminalPortalTeleport, const FLiminalPortalTeleportEvent& /*Event*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnLiminalPortalsUpdated, const FMinimalViewInfo& /*FinalView*/);

/**
 * Registro de portales del mundo y orden de fotograma (decisión 4): lo llama
 * ALiminalPlayerCameraManager tras calcular la cámara. Primero comprueba si la cámara cruzó un
 * portal y teletransporta al pawn; después captura los portales visibles con la pose final.
 *
 * Banco de pruebas de un solo jugador local: el historial de cámara es único.
 */
UCLASS()
class LIMINALPORTALS_API ULiminalPortalSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	void RegisterPortal(ALiminalPortal* Portal);
	void UnregisterPortal(ALiminalPortal* Portal);

	/**
	 * Cruces, teletransporte y capturas del fotograma.
	 * @param RecomputeView Recalcula la cámara tras un teletransporte (una sola vez, sin recursión).
	 */
	void UpdatePortals(APlayerController* PlayerController, const FMinimalViewInfo& View,
		TFunctionRef<FMinimalViewInfo()> RecomputeView);

	/** Olvida la posición de cámara anterior (p. ej. tras un teletransporte ajeno a los portales). */
	UFUNCTION(BlueprintCallable, Category = "Liminal|Portals")
	void ResetCameraHistory();

	UFUNCTION(BlueprintPure, Category = "Liminal|Portals")
	int32 GetNumPortals() const { return Portals.Num(); }

	/** Portales capturados en el último fotograma. */
	UFUNCTION(BlueprintPure, Category = "Liminal|Portals")
	int32 GetLastCaptureCount() const { return LastCaptureCount; }

	/** Tamaño del render target de las capturas del último fotograma que capturó algo ((0, 0) si ninguno). */
	FIntPoint GetLastCaptureSize() const { return LastCaptureSize; }

	/** Portal cuyo destino se precalentó en el último fotograma (nulo si ninguno, D-37). */
	const ALiminalPortal* GetLastPrewarmPortal() const { return LastPrewarmPortal.Get(); }

	/** Teletransportes desde que empezó el mundo. */
	UFUNCTION(BlueprintPure, Category = "Liminal|Portals")
	int32 GetTeleportCount() const { return TeleportCount; }

	/** Se emite tras cada teletransporte por portal. */
	FOnLiminalPortalTeleport OnPortalTeleport;

	/**
	 * Se emite al final de UpdatePortals con la vista final del fotograma (la que renderiza el
	 * viewport), después de las capturas. Lo usa Liminal.Calibrate para pedir la captura de pantalla.
	 */
	FOnLiminalPortalsUpdated OnPortalsUpdated;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** @return true si hubo teletransporte. */
	bool ProcessCrossings(APlayerController* PlayerController, const FVector& CameraLocation);
	void CaptureVisiblePortals(APlayerController* PlayerController, const FMinimalViewInfo& View);
	/** Libera el estado de render de los portales lejanos que llevan tiempo sin capturar (D-31). */
	void ReleaseIdleCaptureStates(const FVector& CameraLocation);
	/** Da a Lumen como origen extra el destino de la puerta más cercana a la que se acerca la cámara (D-37). */
	void PrewarmNearestDestination(const FVector& CameraLocation);
	const FMinimalViewInfo& SelectCaptureView(const FMinimalViewInfo& View);
	void PushViewHistory(const FMinimalViewInfo& View);

	TArray<TWeakObjectPtr<ALiminalPortal>> Portals;

	TOptional<FVector> PreviousCameraLocation;

	/** Poses de cámara recientes (índice 0 = fotograma anterior) para Liminal.Portal.LagFrames. */
	TArray<FMinimalViewInfo> ViewHistory;

	int32 LastCaptureCount = 0;
	FIntPoint LastCaptureSize = FIntPoint::ZeroValue;
	int32 TeleportCount = 0;
	TWeakObjectPtr<const ALiminalPortal> LastPrewarmPortal;
};
