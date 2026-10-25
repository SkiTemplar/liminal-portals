#include "World/LiminalHub.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/RectLightComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "LiminalPortals.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "World/LiminalBlockout.h"

namespace
{
	/** La RectLight queda un poco por debajo del panel para que este no la tape. */
	constexpr double LightBelowPanel = 1.0;
	constexpr float LightAttenuationRadius = 1500.f;
}

ALiminalHub::ALiminalHub()
{
	PrimaryActorTick.bCanEverTick = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(LiminalBlockout::CubeMeshPath);
	UStaticMesh* CubeMesh = CubeFinder.Succeeded() ? CubeFinder.Object : nullptr;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	SetRootComponent(Root);

	Walls = LiminalBlockout::CreateBoxes(this, TEXT("Walls"), Root, CubeMesh);
	Carpet = LiminalBlockout::CreateBoxes(this, TEXT("Carpet"), Root, CubeMesh);
	Ceiling = LiminalBlockout::CreateBoxes(this, TEXT("Ceiling"), Root, CubeMesh);

	// Los paneles son emisivos: sin sombra ni colisión, para no tapar su propia RectLight.
	LightPanels = LiminalBlockout::CreateBoxes(this, TEXT("LightPanels"), Root, CubeMesh);
	LightPanels->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	LightPanels->SetCastShadow(false);

	for (int32 Index = 0; Index < NumCeilingLights; ++Index)
	{
		URectLightComponent* Light = CreateDefaultSubobject<URectLightComponent>(*FString::Printf(TEXT("CeilingLight%d"), Index));
		Light->SetupAttachment(Root);
		Light->SetMobility(EComponentMobility::Movable);
		// Apunta hacia abajo: el eje X de la luz es la dirección de emisión.
		Light->SetRelativeRotation(FRotator(-90.0, 0.0, 0.0));
		Light->SetIntensityUnits(ELightUnits::Nits);
		Light->SetAttenuationRadius(LightAttenuationRadius);
		Light->SetCastShadows(true);
		CeilingLights.Add(Light);
	}

	for (const LiminalWorldLayout::FHubDoorSpec& Spec : LiminalWorldLayout::MakeDefaultHubDoors())
	{
		FLiminalHubDoor& Door = Doors.AddDefaulted_GetRef();
		Door.PositionX = static_cast<float>(Spec.PositionX);
		Door.bOnPositiveYWall = Spec.bPositiveYWall;
	}
}

LiminalWorldLayout::FHubLayout ALiminalHub::MakeLayout() const
{
	LiminalWorldLayout::FHubLayout Layout;
	Layout.InteriorSize = InteriorSize;
	Layout.WallThickness = WallThickness;
	Layout.DoorSize = DoorSize;
	Layout.DoorFrameThickness = DoorFrameThickness;
	Layout.NumCeilingLights = NumCeilingLights;
	for (const FLiminalHubDoor& Door : Doors)
	{
		Layout.Doors.Add({ Door.PositionX, Door.bOnPositiveYWall });
	}
	return Layout;
}

FTransform ALiminalHub::GetDoorTransform(int32 Index) const
{
	const FTransform HubTransform(GetActorQuat(), GetActorLocation());
	FTransform LocalDoor;
	if (!LiminalWorldLayout::GetHubDoorTransform(MakeLayout(), Index, LocalDoor))
	{
		UE_LOG(LogLiminalPortals, Warning, TEXT("%s: la puerta %d no existe (hay %d)."), *GetName(), Index, Doors.Num());
		return HubTransform;
	}
	return LocalDoor * HubTransform;
}

void ALiminalHub::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Rebuild();
}

void ALiminalHub::Rebuild()
{
	const LiminalWorldLayout::FHubLayout Layout = MakeLayout();
	LiminalWorldLayout::FHubGeometry Geometry;
	FString Error;
	if (!LiminalWorldLayout::IsValidHubLayout(Layout, &Error) || !LiminalWorldLayout::BuildHubGeometry(Layout, Geometry))
	{
		UE_LOG(LogLiminalPortals, Error, TEXT("%s: layout del hub no válido (%s); no se genera geometría."), *GetName(), *Error);
		for (UInstancedStaticMeshComponent* Component : { Walls.Get(), Carpet.Get(), Ceiling.Get(), LightPanels.Get() })
		{
			Component->ClearInstances();
		}
		for (URectLightComponent* Light : CeilingLights)
		{
			Light->SetVisibility(false);
		}
		return;
	}

	LiminalBlockout::SetBoxes(Walls, Geometry.Walls, WallMaterial);
	LiminalBlockout::SetBoxes(Carpet, MakeArrayView(&Geometry.Floor, 1), CarpetMaterial);
	LiminalBlockout::SetBoxes(Ceiling, MakeArrayView(&Geometry.Ceiling, 1), CeilingMaterial);
	LiminalBlockout::SetBoxes(LightPanels, Geometry.LightPanels, LightPanelMaterial);

	for (int32 Index = 0; Index < CeilingLights.Num(); ++Index)
	{
		URectLightComponent* Light = CeilingLights[Index];
		const bool bHasPanel = Geometry.LightLocations.IsValidIndex(Index);
		Light->SetVisibility(bHasPanel);
		if (!bHasPanel)
		{
			continue;
		}
		Light->SetRelativeLocation(Geometry.LightLocations[Index] - FVector(0.0, 0.0, LightBelowPanel));
		// Con la luz mirando a −Z, el ancho de la fuente va en Y local (X del panel) y el alto en Z (Y del panel).
		const FBox& Panel = Geometry.LightPanels[Index];
		Light->SetSourceWidth(Panel.GetSize().Y);
		Light->SetSourceHeight(Panel.GetSize().X);
		Light->SetIntensity(LightNits);
		Light->SetLightColor(LightColor);
	}
}
