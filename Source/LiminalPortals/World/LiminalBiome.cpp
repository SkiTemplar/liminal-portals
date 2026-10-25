#include "World/LiminalBiome.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/LocalFogVolumeComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "LiminalPortals.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "World/LiminalBlockout.h"
#include "World/LiminalScatterMath.h"
#include "World/LiminalTerrainMath.h"
#include "World/LiminalWorldLayout.h"

// Namespace con nombre propio (D-22): el unity build junta todos los .cpp del módulo.
namespace LiminalBiomeDetail
{
	/** Margen sin dispersión en el borde del terreno, en cm. */
	constexpr double ScatterEdgeMargin = 500.0;

	/**
	 * Niebla local por tipo de bioma, en las unidades de ULocalFogVolumeComponent. Ojo (D-25): el
	 * shader de UE 5.6 combina las opacidades radial y de altura multiplicándolas en el espacio de
	 * la esfera unidad (T = 1 − (1 − T_radial)(1 − T_altura)) y escala después por el radio en
	 * metros; con la densidad radial a 0 no hay niebla, y con densidades "por metro" pequeñas
	 * tampoco. Los valores salen de reproducir esa integral (cobertura a 50, 100 y 200 m en
	 * horizontal y hacia el cielo) con una esfera de 300 m de radio.
	 */
	struct FBiomeFog
	{
		bool bEnabled = false;
		/** Radio de la esfera de niebla, en cm (la esfera base del componente mide 500 cm). */
		double Radius = 30000.0;
		float RadialExtinction = 0.f;
		float HeightExtinction = 0.f;
		/** Valor de la interfaz: ×0,01 por unidad de esfera; 100 · Radio / caída en e. */
		float HeightFalloff = 1000.f;
		float PhaseG = 0.2f;
		FLinearColor Albedo = FLinearColor::White;
	};

	FBiomeFog GetBiomeFog(ELiminalBiomeType Type)
	{
		FBiomeFog Fog;
		switch (Type)
		{
		case ELiminalBiomeType::Jungle:
			// Niebla baja: la altura cae a 1/e cada 4 m (100 · 30000 / 400 = 7500). Cobertura en
			// horizontal ~10 % a 50 m, ~50 % a 100 m y ~90 % a 200 m; el cielo a 30° queda limpio (1 %).
			Fog.bEnabled = true;
			Fog.RadialExtinction = 2.f;
			Fog.HeightExtinction = 0.04f;
			Fog.HeightFalloff = 7500.f;
			Fog.PhaseG = 0.3f;
			Fog.Albedo = FLinearColor(0.85f, 0.95f, 0.85f);
			break;
		case ELiminalBiomeType::Antarctic:
			// Neblina alta (1/e cada 37,5 m) que vela el horizonte y el cielo: aproximación de cielo
			// cubierto con el sol compartido. Cobertura ~50 % a 100 m en horizontal y ~60 % hacia el cielo a 30°.
			Fog.bEnabled = true;
			Fog.RadialExtinction = 2.f;
			Fog.HeightExtinction = 0.03f;
			Fog.HeightFalloff = 800.f;
			Fog.PhaseG = 0.1f;
			Fog.Albedo = FLinearColor(0.95f, 0.97f, 1.0f);
			break;
		default:
			break;
		}
		return Fog;
	}

	LiminalTerrainMath::FTerrainParams MakeTerrainParams(ELiminalBiomeType Type, int32 TerrainSeed)
	{
		LiminalTerrainMath::FTerrainParams Params;
		Params.Type = Type;
		Params.Seed = TerrainSeed;
		return Params;
	}

	LiminalTerrainMath::FBiomeSeeds SeedsFor(int32 Seed)
	{
		FRandomStream Stream(Seed);
		return LiminalTerrainMath::DeriveSeeds(Stream);
	}

	FLinearColor ScatterColor(ELiminalScatterCategory Category, float& OutRoughness)
	{
		OutRoughness = 0.85f;
		switch (Category)
		{
		case ELiminalScatterCategory::TreeTrunk:
			OutRoughness = 0.9f;
			return FLinearColor(0.12f, 0.07f, 0.035f);
		case ELiminalScatterCategory::TreeCrown:
			return FLinearColor(0.04f, 0.16f, 0.03f);
		case ELiminalScatterCategory::Bush:
			return FLinearColor(0.05f, 0.20f, 0.04f);
		case ELiminalScatterCategory::Undergrowth:
			OutRoughness = 0.8f;
			return FLinearColor(0.08f, 0.28f, 0.05f);
		case ELiminalScatterCategory::RockLarge:
			OutRoughness = 0.9f;
			return FLinearColor(0.42f, 0.20f, 0.09f);
		case ELiminalScatterCategory::RockBlock:
			OutRoughness = 0.9f;
			return FLinearColor(0.36f, 0.17f, 0.08f);
		case ELiminalScatterCategory::Pebble:
			OutRoughness = 0.9f;
			return FLinearColor(0.30f, 0.20f, 0.12f);
		case ELiminalScatterCategory::DryShrub:
			OutRoughness = 0.9f;
			return FLinearColor(0.16f, 0.15f, 0.06f);
		case ELiminalScatterCategory::IceBlock:
			OutRoughness = 0.15f;
			return FLinearColor(0.50f, 0.70f, 0.85f);
		case ELiminalScatterCategory::IcePlate:
			OutRoughness = 0.25f;
			return FLinearColor(0.62f, 0.78f, 0.90f);
		case ELiminalScatterCategory::SnowMound:
			OutRoughness = 0.7f;
			return FLinearColor(0.85f, 0.88f, 0.92f);
		case ELiminalScatterCategory::FencePost:
			return FLinearColor(0.20f, 0.12f, 0.06f);
		case ELiminalScatterCategory::FenceRail:
			return FLinearColor(0.24f, 0.15f, 0.08f);
		case ELiminalScatterCategory::HayBale:
			OutRoughness = 0.95f;
			return FLinearColor(0.55f, 0.42f, 0.14f);
		default:
			return FLinearColor(0.5f, 0.5f, 0.5f);
		}
	}
}

ALiminalBiome::ALiminalBiome()
{
	PrimaryActorTick.bCanEverTick = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(LiminalBlockout::CubeMeshPath);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeFinder(TEXT("/Engine/BasicShapes/Cone.Cone"));
	CubeMesh = CubeFinder.Succeeded() ? CubeFinder.Object : nullptr;
	SphereMesh = SphereFinder.Succeeded() ? SphereFinder.Object : nullptr;
	CylinderMesh = CylinderFinder.Succeeded() ? CylinderFinder.Object : nullptr;
	ConeMesh = ConeFinder.Succeeded() ? ConeFinder.Object : nullptr;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	SetRootComponent(Root);

	ReturnWall = LiminalBlockout::CreateBoxes(this, TEXT("ReturnWall"), Root, CubeMesh);

	Fog = CreateDefaultSubobject<ULocalFogVolumeComponent>(TEXT("Fog"));
	Fog->SetupAttachment(Root);
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

TArray<ELiminalScatterCategory> ALiminalBiome::GetAllScatterCategories()
{
	TArray<ELiminalScatterCategory> Categories;
	for (int32 Index = 0; Index < LiminalBiomeTypes::NumScatterCategories; ++Index)
	{
		Categories.Add(static_cast<ELiminalScatterCategory>(Index));
	}
	return Categories;
}

FString ALiminalBiome::GetScatterCategoryName(ELiminalScatterCategory Category)
{
	const UEnum* Enum = StaticEnum<ELiminalScatterCategory>();
	return Enum ? Enum->GetNameStringByValue(static_cast<int64>(Category)) : FString::FromInt(static_cast<int32>(Category));
}

FLinearColor ALiminalBiome::GetScatterColor(ELiminalScatterCategory Category, float& OutRoughness)
{
	return LiminalBiomeDetail::ScatterColor(Category, OutRoughness);
}

double ALiminalBiome::GetTerrainHeightAt(const FVector& WorldLocation) const
{
	const FTransform ActorTransform = GetActorTransform();
	const FVector Local = ActorTransform.InverseTransformPositionNoScale(WorldLocation);
	double Height = 0.0;
	if (bGenerateProceduralContent)
	{
		const LiminalTerrainMath::FBiomeSeeds Seeds = LiminalBiomeDetail::SeedsFor(Seed);
		Height = LiminalTerrainMath::SampleHeight(LiminalBiomeDetail::MakeTerrainParams(BiomeType, Seeds.Terrain), Local.X, Local.Y);
	}
	return ActorTransform.TransformPositionNoScale(FVector(Local.X, Local.Y, Height)).Z;
}

void ALiminalBiome::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Rebuild();
}

void ALiminalBiome::BeginPlay()
{
	Super::BeginPlay();
	// En el juego no se vuelve a ejecutar OnConstruction y los componentes procedurales no se guardan
	// en el mapa (D-23): se regeneran aquí desde la semilla.
	if (!HasProceduralContent())
	{
		FRandomStream Stream(Seed);
		BuildProceduralContent(Stream);
	}
}

bool ALiminalBiome::HasProceduralContent() const
{
	if (!bGenerateProceduralContent)
	{
		return false;
	}
	return Terrain && Terrain->GetOwner() == this && Terrain->IsRegistered();
}

void ALiminalBiome::Rebuild()
{
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
	DestroyProceduralComponents();
	ApplyAtmosphere();
	if (!bGenerateProceduralContent)
	{
		return;
	}
	const LiminalTerrainMath::FBiomeSeeds Seeds = LiminalTerrainMath::DeriveSeeds(Stream);
	BuildTerrain(Seeds.Terrain);
	BuildScatter(Seeds.Terrain, Seeds.Scatter);
	UE_LOG(LogLiminalPortals, Display, TEXT("%s (%s, semilla %d): terreno de %d vértices, %d instancias en %d componentes"),
		*GetName(), *GetBiomeTypeName(BiomeType), Seed, TerrainVertexCount, ScatterInstanceCount, ScatterComponents.Num());
}

void ALiminalBiome::DestroyProceduralComponents()
{
	if (Terrain)
	{
		Terrain->DestroyComponent();
		Terrain = nullptr;
	}
	for (UHierarchicalInstancedStaticMeshComponent* Component : ScatterComponents)
	{
		if (Component)
		{
			Component->DestroyComponent();
		}
	}
	ScatterComponents.Reset();
	ScatterInstanceCount = 0;
	TerrainVertexCount = 0;
}

void ALiminalBiome::BuildTerrain(int32 TerrainSeed)
{
	const LiminalTerrainMath::FTerrainParams Params = LiminalBiomeDetail::MakeTerrainParams(BiomeType, TerrainSeed);
	const LiminalTerrainMath::FTerrainGrid Grid = LiminalTerrainMath::MakeGrid(GroundSize, TerrainSpacing);
	LiminalTerrainMath::FTerrainMesh Mesh;
	if (!LiminalTerrainMath::BuildTerrainMesh(Params, Grid, Mesh))
	{
		UE_LOG(LogLiminalPortals, Error, TEXT("%s: rejilla de terreno no válida (tamaño %s, separación %.1f)."),
			*GetName(), *GroundSize.ToString(), TerrainSpacing);
		return;
	}
	TArray<FProcMeshTangent> Tangents;
	Tangents.Reserve(Mesh.Tangents.Num());
	for (const FVector& Tangent : Mesh.Tangents)
	{
		Tangents.Emplace(Tangent, false);
	}

	// Transitorio (D-23): no se guarda en el mapa; se regenera desde la semilla.
	Terrain = NewObject<UProceduralMeshComponent>(this, MakeUniqueObjectName(this, UProceduralMeshComponent::StaticClass(), TEXT("Terrain")), RF_Transient);
	Terrain->bUseAsyncCooking = false;
	Terrain->bUseComplexAsSimpleCollision = true;
	Terrain->SetMobility(EComponentMobility::Static);
	Terrain->SetCanEverAffectNavigation(false);
	Terrain->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Terrain->SetupAttachment(Root);
	Terrain->RegisterComponent();
	// Colores por vértice en sRGB: M_Terrain los devuelve a lineal (más precisión en los tonos oscuros).
	Terrain->CreateMeshSection_LinearColor(0, Mesh.Vertices, Mesh.Triangles, Mesh.Normals, Mesh.UVs, Mesh.Colors, Tangents, true, true);
	if (GroundMaterial)
	{
		Terrain->SetMaterial(0, GroundMaterial);
	}
	TerrainVertexCount = Mesh.Vertices.Num();
}

void ALiminalBiome::BuildScatter(int32 TerrainSeed, int32 ScatterSeed)
{
	const LiminalTerrainMath::FTerrainParams TerrainParams = LiminalBiomeDetail::MakeTerrainParams(BiomeType, TerrainSeed);
	LiminalTerrainMath::FBiomeSeeds Seeds;
	Seeds.Terrain = TerrainSeed;
	Seeds.Scatter = ScatterSeed;
	LiminalScatterMath::FScatterParams Params = LiminalScatterMath::MakeScatterParams(BiomeType, Seeds, GroundSize,
		LiminalBiomeDetail::ScatterEdgeMargin);
	Params.Farm = TerrainParams.Farm;
	const LiminalScatterMath::FBiomeScatter Scatter = LiminalScatterMath::BuildBiomeScatter(Params,
		[&TerrainParams](double X, double Y) { return LiminalTerrainMath::SampleHeight(TerrainParams, X, Y); });

	for (int32 Index = 0; Index < LiminalBiomeTypes::NumScatterCategories; ++Index)
	{
		const TArray<FTransform>& Instances = Scatter.Categories[Index];
		if (Instances.IsEmpty())
		{
			continue;
		}
		const ELiminalScatterCategory Category = static_cast<ELiminalScatterCategory>(Index);
		const FName BaseName(*FString::Printf(TEXT("Scatter_%s"), *GetScatterCategoryName(Category)));
		UHierarchicalInstancedStaticMeshComponent* Component = NewObject<UHierarchicalInstancedStaticMeshComponent>(this,
			MakeUniqueObjectName(this, UHierarchicalInstancedStaticMeshComponent::StaticClass(), BaseName), RF_Transient);
		Component->SetStaticMesh(GetShapeMesh(LiminalScatterMath::GetCategoryShape(Category)));
		Component->SetMobility(EComponentMobility::Static);
		// Sin navegación (D-17) y colisión solo en troncos, rocas grandes y bloques de hielo.
		Component->SetCanEverAffectNavigation(false);
		Component->SetCollisionProfileName(LiminalScatterMath::HasCollision(Category)
			? UCollisionProfile::BlockAll_ProfileName : UCollisionProfile::NoCollision_ProfileName);
		const double CullDistance = LiminalScatterMath::GetCullDistance(Category);
		if (CullDistance > 0.0)
		{
			Component->SetCullDistances(FMath::RoundToInt32(CullDistance * 0.8), FMath::RoundToInt32(CullDistance));
		}
		Component->SetupAttachment(Root);
		Component->RegisterComponent();
		if (const TObjectPtr<UMaterialInterface>* Material = ScatterMaterials.Find(Category); Material && *Material)
		{
			Component->SetMaterial(0, *Material);
		}
		Component->AddInstances(Instances, false, false, false);
		ScatterComponents.Add(Component);
		ScatterInstanceCount += Instances.Num();
	}
}

void ALiminalBiome::ApplyAtmosphere()
{
	if (!Fog)
	{
		return;
	}
	const LiminalBiomeDetail::FBiomeFog Settings = LiminalBiomeDetail::GetBiomeFog(BiomeType);
	const bool bEnabled = Settings.bEnabled && bGenerateProceduralContent;
	Fog->SetRelativeScale3D(FVector(Settings.Radius / ULocalFogVolumeComponent::GetBaseVolumeSize()));
	Fog->SetRadialFogExtinction(bEnabled ? Settings.RadialExtinction : 0.f);
	Fog->SetHeightFogExtinction(bEnabled ? Settings.HeightExtinction : 0.f);
	Fog->SetHeightFogFalloff(Settings.HeightFalloff);
	Fog->SetHeightFogOffset(0.f);
	Fog->SetFogPhaseG(Settings.PhaseG);
	Fog->SetFogAlbedo(Settings.Albedo);
	Fog->SetVisibility(bEnabled);
}

UStaticMesh* ALiminalBiome::GetShapeMesh(ELiminalBasicShape Shape) const
{
	switch (Shape)
	{
	case ELiminalBasicShape::Sphere:
		return SphereMesh;
	case ELiminalBasicShape::Cylinder:
		return CylinderMesh;
	case ELiminalBasicShape::Cone:
		return ConeMesh;
	default:
		return CubeMesh;
	}
}
