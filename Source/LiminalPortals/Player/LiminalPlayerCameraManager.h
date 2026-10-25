#pragma once

#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "LiminalPlayerCameraManager.generated.h"

/**
 * Fija el orden de fotograma de los portales (decisión 4): calcula la cámara con Super y luego
 * deja que ULiminalPortalSubsystem compruebe cruces y capture con la pose final. Si hubo
 * teletransporte, recalcula la cámara una sola vez antes de capturar.
 */
UCLASS()
class LIMINALPORTALS_API ALiminalPlayerCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()

public:
	/**
	 * Marca corte de cámara al teletransportar (descarta el histórico de TSR/TAA). Desactivado por
	 * defecto para que el paso sea continuo; ver docs/DECISIONES.md.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal")
	bool bCameraCutOnTeleport = false;

	virtual void UpdateCamera(float DeltaTime) override;

private:
	FMinimalViewInfo RecomputeAfterTeleport();

	bool bIsRecomputingCamera = false;
};
