#include "World/LiminalBlockout.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"

namespace LiminalBlockout
{
	UInstancedStaticMeshComponent* CreateBoxes(AActor* Owner, FName Name, USceneComponent* Parent, UStaticMesh* Mesh)
	{
		UInstancedStaticMeshComponent* Component = Owner->CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
		Component->SetupAttachment(Parent);
		Component->SetMobility(EComponentMobility::Static);
		Component->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		// Sin navegación (no hay IA): en UE 5.6, AddInstance en un mundo de juego invalida la caché de
		// límites y el sistema de navegación la lee antes de recalcularla (ensure en GetNavigationBounds).
		Component->SetCanEverAffectNavigation(false);
		if (Mesh)
		{
			Component->SetStaticMesh(Mesh);
		}
		return Component;
	}

	FTransform BoxToCubeTransform(const FBox& Box)
	{
		return FTransform(FQuat::Identity, Box.GetCenter(), Box.GetSize() / CubeSize);
	}

	void SetBoxes(UInstancedStaticMeshComponent* Component, TConstArrayView<FBox> Boxes, UMaterialInterface* Material)
	{
		if (!Component)
		{
			return;
		}
		Component->ClearInstances();
		for (const FBox& Box : Boxes)
		{
			Component->AddInstance(BoxToCubeTransform(Box), false);
		}
		if (Material)
		{
			Component->SetMaterial(0, Material);
		}
	}
}
