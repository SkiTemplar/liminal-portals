#pragma once

#include "CoreMinimal.h"

class AActor;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class USceneComponent;
class UStaticMesh;

/** Utilidades de blockout compartidas por ALiminalHub y ALiminalBiome (cubos del motor como cajas). */
namespace LiminalBlockout
{
	/** Ruta del cubo básico del motor: 100 cm de lado, centrado en su origen. */
	inline constexpr const TCHAR* CubeMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
	/** Lado del cubo básico del motor, en cm. */
	inline constexpr double CubeSize = 100.0;

	/** Crea en el constructor un ISM de cubos con colisión de bloqueo; Mesh puede ser nulo. */
	UInstancedStaticMeshComponent* CreateBoxes(AActor* Owner, FName Name, USceneComponent* Parent, UStaticMesh* Mesh);

	/** Transform local de la instancia de cubo que ocupa Box. */
	FTransform BoxToCubeTransform(const FBox& Box);

	/** Sustituye las instancias por las cajas dadas y aplica el material (si no es nulo). */
	void SetBoxes(UInstancedStaticMeshComponent* Component, TConstArrayView<FBox> Boxes, UMaterialInterface* Material);

	/**
	 * Carga síncrona de una malla del catálogo de arte (/Game/Art, LiminalArtCatalog). Si el paquete no
	 * existe (arte sin importar: Tools/content.ps1 -Mode Art) avisa con Context y devuelve nullptr.
	 */
	UStaticMesh* LoadArtMesh(const FString& MeshName, const UObject* Context);
}
