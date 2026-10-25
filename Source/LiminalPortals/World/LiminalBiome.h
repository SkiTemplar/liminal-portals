#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Math/RandomStream.h"
#include "LiminalBiome.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class ELiminalBiomeType : uint8
{
	Jungle,
	Desert,
	Antarctic,
	Farmland,
};

/**
 * Bioma en blockout (etapa E3): suelo plano coloreado por tipo y una pared exenta con la puerta de
 * vuelta al hub. El origen del actor es el de la puerta de vuelta (centro inferior del hueco, a ras
 * de suelo) y su eje X apunta hacia el bioma.
 *
 * La generación procedural (E4) entra por BuildProceduralContent, que recibe un FRandomStream
 * sembrado con Seed; la lógica que mueve números irá en funciones puras (LiminalTerrainMath,
 * LiminalScatterMath) con sus tests. No hace tick.
 */
UCLASS(Blueprintable)
class LIMINALPORTALS_API ALiminalBiome : public AActor
{
	GENERATED_BODY()

public:
	ALiminalBiome();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biome")
	ELiminalBiomeType BiomeType = ELiminalBiomeType::Jungle;

	/** Semilla de la generación procedural (E4): misma semilla ⇒ mismo bioma. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biome")
	int32 Seed = 1;

	/** Tamaño del suelo (X, Y), centrado en la puerta de vuelta, en cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biome|Ground")
	FVector2D GroundSize = FVector2D(40000.0, 40000.0);

	/** Hueco libre de la puerta: debe coincidir con PortalSize del portal de vuelta. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biome|Return Door")
	FVector2D DoorSize = FVector2D(140.0, 230.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biome|Return Door", meta = (ClampMin = "0.0"))
	float DoorFrameThickness = 12.f;

	/** Pared exenta: ancho (Y), alto (Z) y grosor (X, hacia detrás del plano de la puerta). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biome|Return Door")
	FVector WallSize = FVector(20.0, 600.0, 350.0);

	/** Material del suelo; build_content.py asigna uno por tipo con GetBiomeBaseColor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biome|Materials")
	TObjectPtr<UMaterialInterface> GroundMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biome|Materials")
	TObjectPtr<UMaterialInterface> WallMaterial;

	/** Transform de mundo de la puerta de vuelta (origen del portal, eje X hacia el bioma, sin escala). */
	UFUNCTION(BlueprintCallable, Category = "Liminal|Biome")
	FTransform GetReturnDoorTransform() const;

	/** Color base del suelo en blockout para cada tipo (lineal). */
	UFUNCTION(BlueprintPure, Category = "Liminal|Biome")
	static FLinearColor GetBiomeBaseColor(ELiminalBiomeType Type);

	/** Nombre estable del tipo (para etiquetas, rutas y capturas). */
	UFUNCTION(BlueprintPure, Category = "Liminal|Biome")
	static FString GetBiomeTypeName(ELiminalBiomeType Type);

protected:
	virtual void OnConstruction(const FTransform& Transform) override;

	/**
	 * Punto de extensión de E4: terreno y dispersión deterministas a partir de Stream. En E3 no hace
	 * nada; el suelo y la pared ya están construidos cuando se llama.
	 */
	virtual void BuildProceduralContent(FRandomStream& Stream);

private:
	void Rebuild();

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Ground;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UInstancedStaticMeshComponent> ReturnWall;
};
