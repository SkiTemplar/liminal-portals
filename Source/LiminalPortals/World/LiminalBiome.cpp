#include "World/LiminalBiome.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "LiminalPortals.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "World/LiminalBlockout.h"
#include "World/LiminalWorldLayout.h"

namespace
{
	constexpr double GroundThickness = 20.0;
}

ALiminalBiome::ALiminalBiome()
{
	PrimaryActorTick.bCanEverTick = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(LiminalBlockout::CubeMeshPath);
	UStaticMesh* CubeMesh = CubeFinder.Succeeded() ? CubeFinder.Object : nullptr;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	SetRootComponent(Root);

	Ground = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ground"));
	Ground->SetupAttachment(Root);
	Ground->SetMobility(EComponentMobility::Static);
	Ground->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	if (CubeMesh)
	{
		Ground->SetStaticMesh(CubeMesh);
	}

	ReturnWall = LiminalBlockout::CreateBoxes(this, TEXT("ReturnWall"), Root, CubeMesh);
}

FTransform ALiminalBiome::GetReturnDoorTransform() const
{
	return FTransform(GetActorQuat(), GetActorLocation());
}

FLinearColor ALiminalBiome::GetBiomeBaseColor(ELiminalBiomeType Type)
{
	switch (Type)
	{
	case ELiminalBiomeType::Jungle:
		return FLinearColor(0.05f, 0.22f, 0.06f);
	case ELiminalBiomeType::Desert:
		return FLinearColor(0.62f, 0.45f, 0.24f);
	case ELiminalBiomeType::Antarctic:
		return FLinearColor(0.80f, 0.85f, 0.90f);
	case ELiminalBiomeType::Farmland:
		return FLinearColor(0.30f, 0.33f, 0.10f);
	default:
		return FLinearColor(0.5f, 0.5f, 0.5f);
	}
}

FString ALiminalBiome::GetBiomeTypeName(ELiminalBiomeType Type)
{
	switch (Type)
	{
	case ELiminalBiomeType::Jungle:
		return TEXT("Jungle");
	case ELiminalBiomeType::Desert:
		return TEXT("Desert");
	case ELiminalBiomeType::Antarctic:
		return TEXT("Antarctic");
	case ELiminalBiomeType::Farmland:
		return TEXT("Farmland");
	default:
		return TEXT("Unknown");
	}
}

void ALiminalBiome::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Rebuild();
}

void ALiminalBiome::Rebuild()
{
	// Suelo: losa con la cara superior en Z = 0, a ras del origen de la puerta (D-01).
	const FBox GroundBox(FVector(-GroundSize.X * 0.5, -GroundSize.Y * 0.5, -GroundThickness),
		FVector(GroundSize.X * 0.5, GroundSize.Y * 0.5, 0.0));
	const FTransform GroundTransform = LiminalBlockout::BoxToCubeTransform(GroundBox);
	Ground->SetRelativeLocation(GroundTransform.GetLocation());
	Ground->SetRelativeScale3D(GroundTransform.GetScale3D());
	if (GroundMaterial)
	{
		Ground->SetMaterial(0, GroundMaterial);
	}

	LiminalWorldLayout::FFreestandingWall Wall;
	Wall.Thickness = WallSize.X;
	Wall.Width = WallSize.Y;
	Wall.Height = WallSize.Z;
	Wall.DoorSize = DoorSize;
	Wall.DoorFrameThickness = DoorFrameThickness;
	TArray<FBox> WallBoxes;
	if (!LiminalWorldLayout::BuildFreestandingDoorWall(Wall, WallBoxes))
	{
		UE_LOG(LogLiminalPortals, Error, TEXT("%s: la puerta de vuelta no cabe en la pared exenta; no se genera la pared."), *GetName());
	}
	LiminalBlockout::SetBoxes(ReturnWall, WallBoxes, WallMaterial);

	FRandomStream Stream(Seed);
	BuildProceduralContent(Stream);
}

void ALiminalBiome::BuildProceduralContent(FRandomStream& Stream)
{
	// E4: terreno (UProceduralMeshComponent desde un campo de alturas puro) y dispersión (HISM).
}
