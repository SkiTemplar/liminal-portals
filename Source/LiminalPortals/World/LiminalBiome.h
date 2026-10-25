#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Math/RandomStream.h"
#include "World/LiminalBiomeTypes.h"
#include "LiminalBiome.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UInstancedStaticMeshComponent;
class ULocalFogVolumeComponent;
class UMaterialInterface;
class UProceduralMeshComponent;
class UStaticMesh;

/**
 * Bioma procedural (etapas E3 y E4): terreno con relieve, dispersión de formas básicas, niebla local
 * y una pared exenta con la puerta de vuelta al hub. El origen del actor es el de la puerta de vuelta
 * (centro inferior del hueco, a ras de suelo) y su eje X apunta hacia el bioma.
 *
 * La pared se regenera en OnConstruction y queda guardada en el mapa (D-17). El terreno
 * (UProceduralMeshComponent) y la dispersión (un HISM por categoría) son componentes transitorios
 * que BuildProceduralContent recrea desde Seed en OnConstruction y, en el juego, en BeginPlay: no se
 * guardan en el mapa (D-23). La lógica que mueve números está en LiminalTerrainMath y
 * LiminalScatterMath, con sus tests. No hace tick.
 */
UCLASS(Blueprintable)
class LIMINALPORTALS_API ALiminalBiome : public AActor
{
	GENERATED_BODY()

public:
	ALiminalBiome();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biome")
	ELiminalBiomeType BiomeType = ELiminalBiomeType::Jungle;

	/** Semilla de la generación procedural: misma semilla ⇒ mismo bioma. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biome")
	int32 Seed = 1;

	/** Genera terreno, dispersión y niebla; desactivado, solo queda la pared (para medir el coste). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biome")
	bool bGenerateProceduralContent = true;

	/** Tamaño del terreno (X, Y), centrado en la puerta de vuelta, en cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biome|Ground")
	FVector2D GroundSize = FVector2D(40000.0, 40000.0);

	/** Separación de los vértices del terreno, en cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biome|Ground", meta = (ClampMin = "50.0"))
	float TerrainSpacing = 200.f;

	/** Hueco libre de la puerta: debe coincidir con PortalSize del portal de vuelta. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biome|Return Door")
	FVector2D DoorSize = FVector2D(140.0, 230.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biome|Return Door", meta = (ClampMin = "0.0"))
	float DoorFrameThickness = 12.f;

	/** Pared exenta: ancho (Y), alto (Z) y grosor (X, hacia detrás del plano de la puerta). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biome|Return Door")
	FVector WallSize = FVector(20.0, 600.0, 350.0);

	/** Material del terreno (MI_Biome_<Tipo>, hijo de M_Terrain: color base por vértice). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biome|Materials")
	TObjectPtr<UMaterialInterface> GroundMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biome|Materials")
	TObjectPtr<UMaterialInterface> WallMaterial;

	/** Material de cada categoría de dispersión (MI_Scatter_<Categoría>); sin entrada, el de la forma. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biome|Materials")
	TMap<ELiminalScatterCategory, TObjectPtr<UMaterialInterface>> ScatterMaterials;

	/** Instancias dispersadas en la última generación (informativo). */
	UPROPERTY(VisibleInstanceOnly, Transient, BlueprintReadOnly, Category = "Biome|Stats")
	int32 ScatterInstanceCount = 0;

	/** Vértices del terreno en la última generación (informativo). */
	UPROPERTY(VisibleInstanceOnly, Transient, BlueprintReadOnly, Category = "Biome|Stats")
	int32 TerrainVertexCount = 0;

	/** Transform de mundo de la puerta de vuelta (origen del portal, eje X hacia el bioma, sin escala). */
	UFUNCTION(BlueprintCallable, Category = "Liminal|Biome")
	FTransform GetReturnDoorTransform() const;

	/** Color base del terreno para cada tipo (lineal). */
	UFUNCTION(BlueprintPure, Category = "Liminal|Biome")
	static FLinearColor GetBiomeBaseColor(ELiminalBiomeType Type);

	/** Nombre estable del tipo (para etiquetas, rutas y capturas). */
	UFUNCTION(BlueprintPure, Category = "Liminal|Biome")
	static FString GetBiomeTypeName(ELiminalBiomeType Type);

	/** Todas las categorías de dispersión (para crear sus materiales desde Python). */
	UFUNCTION(BlueprintPure, Category = "Liminal|Biome")
	static TArray<ELiminalScatterCategory> GetAllScatterCategories();

	/** Nombre estable de la categoría (MI_Scatter_<Nombre>). */
	UFUNCTION(BlueprintPure, Category = "Liminal|Biome")
	static FString GetScatterCategoryName(ELiminalScatterCategory Category);

	/** Color base (lineal) y rugosidad del material de cada categoría. */
	UFUNCTION(BlueprintPure, Category = "Liminal|Biome")
	static FLinearColor GetScatterColor(ELiminalScatterCategory Category, float& OutRoughness);

	/** Altura del terreno (sin la dispersión) en un punto de mundo, en cm de mundo. */
	UFUNCTION(BlueprintPure, Category = "Liminal|Biome")
	double GetTerrainHeightAt(const FVector& WorldLocation) const;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	/**
	 * Terreno, dispersión y niebla deterministas a partir de Stream (sembrado con Seed). La pared ya
	 * está construida cuando se llama. Sustituye los componentes procedurales anteriores.
	 */
	virtual void BuildProceduralContent(FRandomStream& Stream);

private:
	void Rebuild();
	void DestroyProceduralComponents();
	void BuildTerrain(int32 TerrainSeed);
	void BuildScatter(int32 TerrainSeed, int32 ScatterSeed);
	void ApplyAtmosphere();
	bool HasProceduralContent() const;
	UStaticMesh* GetShapeMesh(ELiminalBasicShape Shape) const;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UInstancedStaticMeshComponent> ReturnWall;

	/** Niebla local del bioma (el componente de ALocalFogVolume); apagada en los biomas sin niebla. */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<ULocalFogVolumeComponent> Fog;

	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> Terrain;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> ScatterComponents;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> ConeMesh;
};
