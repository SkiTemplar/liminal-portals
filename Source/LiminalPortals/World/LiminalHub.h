#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/LiminalWorldLayout.h"
#include "LiminalHub.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class URectLightComponent;

/** Puerta del hub sobre una de las dos paredes largas. */
USTRUCT(BlueprintType)
struct FLiminalHubDoor
{
	GENERATED_BODY()

	/** Posición del centro del hueco a lo largo de la sala (X local), en cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hub")
	float PositionX = 0.f;

	/** true: pared de +Y (la puerta mira a −Y); false: pared de −Y. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hub")
	bool bOnPositiveYWall = true;
};

/**
 * Hub liminal en blockout (etapa E3): sala cerrada con paredes claras, moqueta, techo con paneles
 * fluorescentes (RectLight) y cuatro huecos de puerta del tamaño del portal con su marco. La
 * geometría sale de LiminalWorldLayout (pura) y se regenera en OnConstruction como instancias de
 * cubo; los portales no son parte del hub: se colocan en GetDoorTransform (Tools/Content/build_content.py).
 *
 * Origen: centro del suelo de la sala. No hace tick.
 */
UCLASS(Blueprintable)
class LIMINALPORTALS_API ALiminalHub : public AActor
{
	GENERATED_BODY()

public:
	/** Número de paneles de techo; cada uno lleva una RectLight creada en el constructor. */
	static constexpr int32 NumCeilingLights = 4;

	ALiminalHub();

	/** Interior de la sala: largo (X), ancho (Y) y alto (Z), en cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hub|Layout")
	FVector InteriorSize = FVector(1600.0, 600.0, 320.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hub|Layout", meta = (ClampMin = "1.0"))
	float WallThickness = 20.f;

	/** Hueco libre de la puerta (ancho, alto): debe coincidir con PortalSize del portal que va en ella. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hub|Doors")
	FVector2D DoorSize = FVector2D(140.0, 230.0);

	/** Grosor del marco del portal; el hueco de la pared es el exterior del marco. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hub|Doors", meta = (ClampMin = "0.0"))
	float DoorFrameThickness = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hub|Doors")
	TArray<FLiminalHubDoor> Doors;

	/** Luminancia de cada panel fluorescente (nits). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hub|Lighting", meta = (ClampMin = "0.0"))
	float LightNits = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hub|Lighting")
	FLinearColor LightColor = FLinearColor(1.0f, 0.96f, 0.86f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hub|Materials")
	TObjectPtr<UMaterialInterface> WallMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hub|Materials")
	TObjectPtr<UMaterialInterface> CarpetMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hub|Materials")
	TObjectPtr<UMaterialInterface> CeilingMaterial;

	/** Material emisivo de los paneles (no proyectan sombra ni bloquean su luz). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hub|Materials")
	TObjectPtr<UMaterialInterface> LightPanelMaterial;

	/**
	 * Transform de mundo de la puerta Index (origen del portal: centro inferior del hueco, eje X
	 * hacia el interior de la sala). Fuera de rango devuelve el transform del hub y avisa.
	 */
	UFUNCTION(BlueprintCallable, Category = "Liminal|Hub")
	FTransform GetDoorTransform(int32 Index) const;

	UFUNCTION(BlueprintPure, Category = "Liminal|Hub")
	int32 GetNumDoors() const { return Doors.Num(); }

	/** Layout puro equivalente a las propiedades actuales. */
	LiminalWorldLayout::FHubLayout MakeLayout() const;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;

private:
	void Rebuild();

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UInstancedStaticMeshComponent> Walls;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UInstancedStaticMeshComponent> Carpet;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UInstancedStaticMeshComponent> Ceiling;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UInstancedStaticMeshComponent> LightPanels;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TArray<TObjectPtr<URectLightComponent>> CeilingLights;
};
