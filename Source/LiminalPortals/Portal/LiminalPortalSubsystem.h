#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "Templates/Function.h"
#include "LiminalPortalSubsystem.generated.h"

class ALiminalPortal;
class APlayerController;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnLiminalPortalTeleport, ALiminalPortal* /*Source*/, ALiminalPortal* /*Target*/);
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
	const FMinimalViewInfo& SelectCaptureView(const FMinimalViewInfo& View);
	void PushViewHistory(const FMinimalViewInfo& View);

	TArray<TWeakObjectPtr<ALiminalPortal>> Portals;

	TOptional<FVector> PreviousCameraLocation;

	/** Poses de cámara recientes (índice 0 = fotograma anterior) para Liminal.Portal.LagFrames. */
	TArray<FMinimalViewInfo> ViewHistory;

	int32 LastCaptureCount = 0;
	int32 TeleportCount = 0;
};
