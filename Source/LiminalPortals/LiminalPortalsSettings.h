#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "LiminalPortalsSettings.generated.h"

class UMaterialInterface;

/** Ajustes del proyecto para los portales (Project Settings → Game → Liminal Portals, DefaultGame.ini). */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Liminal Portals"))
class LIMINALPORTALS_API ULiminalPortalsSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	ULiminalPortalsSettings();

	/** Material unlit two-sided de la superficie; muestrea el render target en ScreenPosition. */
	UPROPERTY(Config, EditAnywhere, Category = "Materials")
	TSoftObjectPtr<UMaterialInterface> PortalSurfaceMaterial;

	/** Color liminal plano del plano de reserva (solo visible en capturas). */
	UPROPERTY(Config, EditAnywhere, Category = "Materials")
	TSoftObjectPtr<UMaterialInterface> PortalFallbackMaterial;

	/** Nombre del parámetro de textura del material de superficie que recibe el render target. */
	UPROPERTY(Config, EditAnywhere, Category = "Materials")
	FName SurfaceTextureParameter = TEXT("PortalTexture");

	/** Escala del render target respecto al viewport (1 = misma resolución). */
	UPROPERTY(Config, EditAnywhere, Category = "Capture", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float RenderTargetScale = 1.f;

	/** Límite del lado mayor del render target, en píxeles. */
	UPROPERTY(Config, EditAnywhere, Category = "Capture", meta = (ClampMin = "64", ClampMax = "8192"))
	int32 MaxRenderTargetDimension = 4096;
};
