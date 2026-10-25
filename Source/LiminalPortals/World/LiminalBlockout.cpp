#include "World/LiminalBlockout.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "LiminalPortals.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "World/LiminalArtCatalog.h"

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

	UStaticMesh* LoadArtMesh(const FString& MeshName, const UObject* Context)
	{
		const FString ObjectPath = LiminalArtCatalog::MakeMeshObjectPath(MeshName);
		if (ObjectPath.IsEmpty() || !FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(ObjectPath)))
		{
			UE_LOG(LogLiminalPortals, Warning, TEXT("%s: falta la malla %s (%s); importar el arte con Tools/content.ps1 -Mode Art."),
				*GetNameSafe(Context), *MeshName, *ObjectPath);
			return nullptr;
		}
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *ObjectPath);
		if (!Mesh)
		{
			UE_LOG(LogLiminalPortals, Warning, TEXT("%s: no se pudo cargar %s."), *GetNameSafe(Context), *ObjectPath);
		}
		return Mesh;
	}
}
