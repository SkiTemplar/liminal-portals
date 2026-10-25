#include "Misc/AutomationTest.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/LocalFogVolumeComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/LiminalCharacter.h"
#include "Portal/LiminalPortal.h"
#include "Portal/LiminalPortalMath.h"
#include "Portal/LiminalPortalSubsystem.h"
#include "ProceduralMeshComponent.h"
#include "World/LiminalBiome.h"
#include "World/LiminalHub.h"
#include "World/LiminalWorldLayout.h"

// Tests de integración ligeros: mundo de juego vacío sin render, actores reales y sin mapa.

namespace LiminalPortalWorldTests
{
	constexpr EAutomationTestFlags WorldTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/**
	 * Mundo de juego temporal con los actores inicializados para jugar (mismo esquema que
	 * FActorTestSpawner de CQTest); se destruye al salir del ámbito.
	 */
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;

		FScopedGameWorld()
		{
			const FName WorldName = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("LiminalTestWorld"),
				EUniqueObjectNameOptions::GloballyUnique);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}

		~FScopedGameWorld()
		{
			if (!World)
			{
				return;
			}
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				It->RouteEndPlay(EEndPlayReason::LevelTransition);
			}
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
			World->RemoveFromRoot();
		}

		template <typename TActor>
		TActor* Spawn(const FTransform& Transform)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			return World->SpawnActor<TActor>(TActor::StaticClass(), Transform, Params);
		}
	};
}

using namespace LiminalPortalWorldTests;

namespace LiminalPortalWorldTests
{
	UPrimitiveComponent* FindPrimitive(const AActor* Actor, FName Name)
	{
		TInlineComponentArray<UPrimitiveComponent*> Components(Actor);
		for (UPrimitiveComponent* Component : Components)
		{
			if (Component->GetFName() == Name)
			{
				return Component;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalPortalCalibrationFlagsTest, "LiminalPortals.World.Portal.CalibrationFlags", WorldTestFlags)
bool FLiminalPortalCalibrationFlagsTest::RunTest(const FString& Parameters)
{
	AddExpectedMessage(TEXT("Material de"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
	// El portal empotrado del caso negativo no tiene enlace a propósito.
	AddExpectedMessage(TEXT("no tiene LinkedPortal"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);

	FScopedGameWorld Scope;
	UTEST_NOT_NULL(TEXT("Mundo"), Scope.World);

	// Pareja de calibración: A y B coincidentes mirando a lados opuestos, sin empotrar; B solo de salida.
	ALiminalPortal* PortalA = Scope.Spawn<ALiminalPortal>(FTransform(FRotator::ZeroRotator, FVector::ZeroVector));
	ALiminalPortal* PortalB = Scope.Spawn<ALiminalPortal>(FTransform(FRotator(0.0, 180.0, 0.0), FVector::ZeroVector));
	ALiminalPortal* Embedded = Scope.Spawn<ALiminalPortal>(FTransform(FRotator::ZeroRotator, FVector(0.0, 5000.0, 0.0)));
	UTEST_NOT_NULL(TEXT("Portal A"), PortalA);
	UTEST_NOT_NULL(TEXT("Portal B"), PortalB);
	UTEST_NOT_NULL(TEXT("Portal empotrado"), Embedded);
	PortalA->bEmbedded = false;
	PortalB->bEmbedded = false;
	PortalB->bExitOnly = true;
	PortalA->LinkedPortal = PortalB;
	PortalB->LinkedPortal = PortalA;
	PortalA->DispatchBeginPlay();
	PortalB->DispatchBeginPlay();
	Embedded->DispatchBeginPlay();

	TestTrue(TEXT("Paso A→B = identidad"), PortalA->GetTransferToLinked().Equals(FTransform::Identity, 1e-6));

	const UPrimitiveComponent* PlateA = FindPrimitive(PortalA, TEXT("BackPlate"));
	UTEST_NOT_NULL(TEXT("Placa de A"), PlateA);
	TestFalse(TEXT("Sin empotrar no hay placa visible"), PlateA->IsVisible());
	TestEqual(TEXT("Sin empotrar la placa no colisiona"), PlateA->GetCollisionEnabled(), ECollisionEnabled::NoCollision);

	TestTrue(TEXT("La superficie de B (solo salida) está oculta"), PortalB->GetSurfaceComponent()->bHiddenInGame != 0);
	PortalB->SetSurfaceHiddenInGame(false);
	TestTrue(TEXT("Solo salida: sigue oculta aunque se pida mostrarla"), PortalB->GetSurfaceComponent()->bHiddenInGame != 0);

	TestFalse(TEXT("La superficie de A se ve"), PortalA->GetSurfaceComponent()->bHiddenInGame != 0);
	PortalA->SetSurfaceHiddenInGame(true);
	TestTrue(TEXT("A se oculta a petición"), PortalA->GetSurfaceComponent()->bHiddenInGame != 0);
	PortalA->SetSurfaceHiddenInGame(false);
	TestFalse(TEXT("A se vuelve a mostrar"), PortalA->GetSurfaceComponent()->bHiddenInGame != 0);

	// Negativo: el portal empotrado por defecto conserva la placa sólida.
	const UPrimitiveComponent* PlateEmbedded = FindPrimitive(Embedded, TEXT("BackPlate"));
	UTEST_NOT_NULL(TEXT("Placa del empotrado"), PlateEmbedded);
	TestTrue(TEXT("Empotrado: placa visible"), PlateEmbedded->IsVisible());
	TestEqual(TEXT("Empotrado: placa sólida"), PlateEmbedded->GetCollisionEnabled(), ECollisionEnabled::QueryAndPhysics);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalPortalActorTest, "LiminalPortals.World.Portal.RegisterLinkAndLayout", WorldTestFlags)
bool FLiminalPortalActorTest::RunTest(const FString& Parameters)
{
	// Si los materiales de Content/ no existen, el portal avisa y sigue (no debe romper).
	AddExpectedMessage(TEXT("Material de"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);

	FScopedGameWorld Scope;
	UTEST_NOT_NULL(TEXT("Mundo"), Scope.World);
	ULiminalPortalSubsystem* Subsystem = Scope.World->GetSubsystem<ULiminalPortalSubsystem>();
	UTEST_NOT_NULL(TEXT("Subsistema en mundo de juego"), Subsystem);

	ALiminalPortal* PortalA = Scope.Spawn<ALiminalPortal>(FTransform(FRotator::ZeroRotator, FVector::ZeroVector));
	ALiminalPortal* PortalB = Scope.Spawn<ALiminalPortal>(FTransform(FRotator(0.0, 90.0, 0.0), FVector(20000.0, 0.0, 0.0)));
	UTEST_NOT_NULL(TEXT("Portal A"), PortalA);
	UTEST_NOT_NULL(TEXT("Portal B"), PortalB);
	PortalA->LinkedPortal = PortalB;
	PortalB->LinkedPortal = PortalA;
	PortalA->DispatchBeginPlay();
	PortalB->DispatchBeginPlay();
	TestEqual(TEXT("Dos portales registrados"), Subsystem->GetNumPortals(), 2);

	// El plano está en el centro del hueco y el paso coincide con la función pura.
	TestTrue(TEXT("Centro del plano"), PortalA->GetPlaneTransform().GetLocation().Equals(FVector(0.0, 0.0, 110.0), 1e-3));
	const FTransform Expected = LiminalPortalMath::MakeTransfer(PortalA->GetPortalTransform(), PortalB->GetPortalTransform());
	TestTrue(TEXT("Paso A→B"), PortalA->GetTransferToLinked().Equals(Expected, 1e-4));

	// Superficie extendida con la cámara cerca por delante; base lejos o por detrás.
	UPrimitiveComponent* Surface = PortalA->GetSurfaceComponent();
	PortalA->UpdateSurfaceDepth(FVector(20.0, 0.0, 160.0));
	TestNearlyEqual(TEXT("Profundidad extendida"), Surface->GetRelativeScale3D().X, PortalA->SurfaceExtendedDepth / 100.0, 1e-4);
	PortalA->UpdateSurfaceDepth(FVector(500.0, 0.0, 160.0));
	TestNearlyEqual(TEXT("Profundidad base"), Surface->GetRelativeScale3D().X, PortalA->SurfaceBaseDepth / 100.0, 1e-4);
	TestTrue(TEXT("El reserva solo se ve en capturas"), PortalA->GetFallbackComponent()->bVisibleInSceneCaptureOnly != 0);
	TestEqual(TEXT("La superficie no colisiona"), Surface->GetCollisionEnabled(), ECollisionEnabled::NoCollision);

	// Negativos: un portal sin enlace no transfiere y al destruirse se da de baja.
	PortalB->LinkedPortal = nullptr;
	TestTrue(TEXT("Sin enlace ⇒ identidad"), PortalB->GetTransferToLinked().Equals(FTransform::Identity));
	PortalB->Destroy();
	TestEqual(TEXT("Baja al destruir"), Subsystem->GetNumPortals(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalCharacterTeleportTest, "LiminalPortals.World.Character.TeleportThroughPortal", WorldTestFlags)
bool FLiminalCharacterTeleportTest::RunTest(const FString& Parameters)
{
	FScopedGameWorld Scope;
	UTEST_NOT_NULL(TEXT("Mundo"), Scope.World);

	const FTransform Source(FRotator::ZeroRotator, FVector::ZeroVector);
	const FTransform Target(FRotator(0.0, 90.0, 0.0), FVector(40000.0, 300.0, 0.0));
	const FTransform Transfer = LiminalPortalMath::MakeTransfer(Source, Target);

	ALiminalCharacter* Character = Scope.Spawn<ALiminalCharacter>(FTransform(FRotator(0.0, 180.0, 0.0), FVector(30.0, 10.0, 100.0)));
	UTEST_NOT_NULL(TEXT("Personaje"), Character);
	const FVector Velocity(-350.0, 20.0, 0.0);
	Character->GetCharacterMovement()->Velocity = Velocity;
	const FVector StartLocation = Character->GetActorLocation();

	Character->TeleportThroughPortal(Transfer);

	TestTrue(TEXT("Posición transferida"), Character->GetActorLocation().Equals(LiminalPortalMath::TransferPoint(Transfer, StartLocation), 1e-2));
	const FVector NewVelocity = Character->GetCharacterMovement()->Velocity;
	TestNearlyEqual(TEXT("Módulo de la velocidad"), NewVelocity.Size(), Velocity.Size(), 1e-3);
	TestTrue(TEXT("Velocidad girada"), NewVelocity.Equals(LiminalPortalMath::TransferVector(Transfer, Velocity), 1e-3));
	TestNearlyEqual(TEXT("Yaw del actor"), FRotator::NormalizeAxis(Character->GetActorRotation().Yaw), 90.0, 1e-3);
	TestNearlyEqual(TEXT("Roll del actor"), Character->GetActorRotation().Roll, 0.0, 1e-6);

	// Negativo: el personaje ya no está en el hub.
	TestTrue(TEXT("Se ha movido al destino"), FVector::Dist(Character->GetActorLocation(), StartLocation) > 30000.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalHubActorTest, "LiminalPortals.World.Hub.DoorsAndGeometry", WorldTestFlags)
bool FLiminalHubActorTest::RunTest(const FString& Parameters)
{
	// Un índice fuera de rango avisa (caso negativo de abajo).
	AddExpectedMessage(TEXT("no existe"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);

	FScopedGameWorld Scope;
	UTEST_NOT_NULL(TEXT("Mundo"), Scope.World);
	const FTransform HubTransform(FRotator(0.0, 30.0, 0.0), FVector(1000.0, -2000.0, 50.0));
	ALiminalHub* Hub = Scope.Spawn<ALiminalHub>(HubTransform);
	UTEST_NOT_NULL(TEXT("Hub"), Hub);
	TestEqual(TEXT("Cuatro puertas"), Hub->GetNumDoors(), 4);

	// GetDoorTransform es el layout puro llevado al transform del hub.
	const LiminalWorldLayout::FHubLayout Layout = Hub->MakeLayout();
	for (int32 Index = 0; Index < Hub->GetNumDoors(); ++Index)
	{
		FTransform Local;
		UTEST_TRUE(TEXT("Puerta del layout"), LiminalWorldLayout::GetHubDoorTransform(Layout, Index, Local));
		TestTrue(TEXT("Puerta en mundo"), Hub->GetDoorTransform(Index).Equals(Local * HubTransform, 1e-3));
	}

	LiminalWorldLayout::FHubGeometry Geometry;
	UTEST_TRUE(TEXT("Geometría"), LiminalWorldLayout::BuildHubGeometry(Layout, Geometry));
	const UInstancedStaticMeshComponent* Walls = Cast<UInstancedStaticMeshComponent>(FindPrimitive(Hub, TEXT("Walls")));
	UTEST_NOT_NULL(TEXT("Paredes"), Walls);
	TestEqual(TEXT("Una instancia por caja de pared"), Walls->GetInstanceCount(), Geometry.Walls.Num());
	TInlineComponentArray<URectLightComponent*> Lights(Hub);
	TestEqual(TEXT("Una RectLight por panel"), Lights.Num(), ALiminalHub::NumCeilingLights);
	const UInstancedStaticMeshComponent* Frames = Cast<UInstancedStaticMeshComponent>(FindPrimitive(Hub, TEXT("Dressing_SM_Hub_DoorFrame_01")));
	UTEST_NOT_NULL(TEXT("Marcos de puerta"), Frames);
	TestEqual(TEXT("Un marco low-poly por puerta"), Frames->GetInstanceCount(), Hub->GetNumDoors());
	TestEqual(TEXT("El marco no colisiona (lo hace el portal)"), Frames->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	const UInstancedStaticMeshComponent* Panels = Cast<UInstancedStaticMeshComponent>(FindPrimitive(Hub, TEXT("Dressing_SM_Hub_LightPanel_01")));
	UTEST_NOT_NULL(TEXT("Paneles fluorescentes"), Panels);
	TestEqual(TEXT("Un panel por RectLight"), Panels->GetInstanceCount(), ALiminalHub::NumCeilingLights);

	// La cápsula del jugador pasa por el hueco de la puerta y no atraviesa la pared de al lado.
	const FCollisionShape Capsule = FCollisionShape::MakeCapsule(34.f, 88.f);
	const FTransform Door = Hub->GetDoorTransform(0);
	const FVector Inside = Door.TransformPositionNoScale(FVector(100.0, 0.0, 92.0));
	const FVector Outside = Door.TransformPositionNoScale(FVector(-100.0, 0.0, 92.0));
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LiminalHubTest), false);
	TestFalse(TEXT("La cápsula cruza el hueco"), Scope.World->SweepTestByChannel(Inside, Outside, FQuat::Identity, ECC_Pawn, Capsule, Params));
	const FVector Beside(0.0, Layout.DoorSize.X, 0.0);
	TestTrue(TEXT("La pared de al lado bloquea"), Scope.World->SweepTestByChannel(Inside + Door.TransformVectorNoScale(Beside),
		Outside + Door.TransformVectorNoScale(Beside), FQuat::Identity, ECC_Pawn, Capsule, Params));

	TestTrue(TEXT("Fuera de rango ⇒ transform del hub"), Hub->GetDoorTransform(7).Equals(HubTransform, 1e-6));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalBiomeActorTest, "LiminalPortals.World.Biome.ReturnDoorAndTerrain", WorldTestFlags)
bool FLiminalBiomeActorTest::RunTest(const FString& Parameters)
{
	FScopedGameWorld Scope;
	UTEST_NOT_NULL(TEXT("Mundo"), Scope.World);
	const FVector Origin = LiminalWorldLayout::GetBiomeOrigin(2);
	ALiminalBiome* Biome = Scope.Spawn<ALiminalBiome>(FTransform(FRotator(0.0, 45.0, 0.0), Origin));
	UTEST_NOT_NULL(TEXT("Bioma"), Biome);

	const FTransform Door = Biome->GetReturnDoorTransform();
	TestTrue(TEXT("Puerta en el origen del bioma"), Door.GetLocation().Equals(Origin, 1e-6));
	TestTrue(TEXT("Puerta sin escala"), Door.GetScale3D().Equals(FVector::OneVector));
	TestNearlyEqual(TEXT("Mira con el bioma"), Door.Rotator().Yaw, 45.0, 1e-4);

	// Terreno procedural (E4): transitorio, con colisión y a ras de la puerta en la zona llana.
	TInlineComponentArray<UProceduralMeshComponent*> Terrains(Biome);
	UTEST_EQUAL(TEXT("Un terreno"), Terrains.Num(), 1);
	const UProceduralMeshComponent* Terrain = Terrains[0];
	TestTrue(TEXT("Terreno transitorio (no se guarda en el mapa)"), Terrain->HasAnyFlags(RF_Transient));
	TestEqual(TEXT("Terreno facetado de 160 × 160 celdas a 2,5 m"), Biome->TerrainVertexCount, 160 * 160 * 6);
	TestTrue(TEXT("El terreno bloquea"), Terrain->GetCollisionEnabled() != ECollisionEnabled::NoCollision);
	FHitResult Hit;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(LiminalBiomeTest), true);
	const FVector Above = Door.TransformPositionNoScale(FVector(300.0, 0.0, 500.0));
	UTEST_TRUE(TEXT("El suelo delante de la puerta tiene colisión"), Scope.World->LineTraceSingleByChannel(Hit, Above, Above - FVector(0.0, 0.0, 1000.0), ECC_Visibility, Query));
	TestNearlyEqual(TEXT("Suelo a ras de la puerta"), Hit.ImpactPoint.Z, Origin.Z, 0.5);
	TestTrue(TEXT("El impacto es el terreno"), Hit.GetComponent() == Terrain);
	TestNearlyEqual(TEXT("Altura del terreno en la puerta"), Biome->GetTerrainHeightAt(Door.TransformPositionNoScale(FVector(500.0, 0.0, 0.0))), Origin.Z, 1e-6);

	// Dispersión: HISM por categoría, dentro del presupuesto, y el camino de salida despejado.
	TInlineComponentArray<UHierarchicalInstancedStaticMeshComponent*> Scatter(Biome);
	TestTrue(TEXT("Hay dispersión"), Scatter.Num() > 0 && Biome->ScatterInstanceCount > 0);
	TestTrue(TEXT("Dentro del presupuesto de 15.000 instancias"), Biome->ScatterInstanceCount <= 15000);
	const FCollisionShape Capsule = FCollisionShape::MakeCapsule(34.f, 88.f);
	const FVector PathStart = Door.TransformPositionNoScale(FVector(100.0, 0.0, 95.0));
	const FVector PathEnd = Door.TransformPositionNoScale(FVector(3000.0, 0.0, 95.0));
	TestFalse(TEXT("Camino de salida de 30 m sin obstáculos"), Scope.World->SweepTestByChannel(PathStart, PathEnd, FQuat::Identity, ECC_Pawn, Capsule, Query));
	TInlineComponentArray<ULocalFogVolumeComponent*> Fogs(Biome);
	UTEST_EQUAL(TEXT("Una niebla local"), Fogs.Num(), 1);
	TestTrue(TEXT("La selva tiene niebla baja"), Fogs[0]->IsVisible() && Fogs[0]->HeightFogExtinction > 0.f);

	const UInstancedStaticMeshComponent* Wall = Cast<UInstancedStaticMeshComponent>(FindPrimitive(Biome, TEXT("ReturnWall")));
	UTEST_NOT_NULL(TEXT("Pared exenta"), Wall);
	TestEqual(TEXT("Dos tramos y dintel"), Wall->GetInstanceCount(), 3);
	TestFalse(TEXT("Las cajas de la pared solo colisionan (D-32)"), Wall->IsVisible());
	TestTrue(TEXT("Las cajas de la pared bloquean"), Wall->GetCollisionEnabled() != ECollisionEnabled::NoCollision);
	const UStaticMeshComponent* WallMesh = Cast<UStaticMeshComponent>(FindPrimitive(Biome, TEXT("ReturnWallMesh")));
	UTEST_NOT_NULL(TEXT("Malla de la pared"), WallMesh);
	TestTrue(TEXT("Pared low-poly del bioma (musgo en la selva)"), WallMesh->GetStaticMesh() && WallMesh->GetStaticMesh()->GetName() == TEXT("SM_ReturnWall_Moss_01"));
	TestEqual(TEXT("La malla de la pared no colisiona"), WallMesh->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	TestTrue(TEXT("Triángulos contados"), Biome->ScatterTriangleCount > Biome->ScatterInstanceCount && Biome->NearInstanceCount <= Biome->ScatterInstanceCount);

	// Cada tipo tiene un color propio (el suelo de blockout los distingue).
	const FLinearColor Jungle = ALiminalBiome::GetBiomeBaseColor(ELiminalBiomeType::Jungle);
	TestFalse(TEXT("Selva ≠ desierto"), Jungle.Equals(ALiminalBiome::GetBiomeBaseColor(ELiminalBiomeType::Desert)));
	TestFalse(TEXT("Antártida ≠ cultivos"), ALiminalBiome::GetBiomeBaseColor(ELiminalBiomeType::Antarctic)
		.Equals(ALiminalBiome::GetBiomeBaseColor(ELiminalBiomeType::Farmland)));
	TestEqual(TEXT("Nombre estable"), ALiminalBiome::GetBiomeTypeName(ELiminalBiomeType::Antarctic), FString(TEXT("Antarctic")));

	// Negativo: si la puerta no cabe en la pared, no hay pared (y se avisa).
	AddExpectedMessage(TEXT("no cabe en la pared"), ELogVerbosity::Error, EAutomationExpectedMessageFlags::Contains, 1);
	ALiminalBiome* Narrow = Scope.World->SpawnActorDeferred<ALiminalBiome>(ALiminalBiome::StaticClass(), FTransform::Identity);
	UTEST_NOT_NULL(TEXT("Bioma estrecho"), Narrow);
	Narrow->WallSize = FVector(20.0, 100.0, 350.0);
	Narrow->FinishSpawning(FTransform::Identity);
	const UInstancedStaticMeshComponent* NarrowWall = Cast<UInstancedStaticMeshComponent>(FindPrimitive(Narrow, TEXT("ReturnWall")));
	UTEST_NOT_NULL(TEXT("Pared del estrecho"), NarrowWall);
	TestEqual(TEXT("Sin pared"), NarrowWall->GetInstanceCount(), 0);

	// Negativo: sin generación procedural no hay terreno ni dispersión, y la niebla queda apagada.
	ALiminalBiome* Bare = Scope.World->SpawnActorDeferred<ALiminalBiome>(ALiminalBiome::StaticClass(), FTransform(FVector(0.0, 100000.0, 0.0)));
	UTEST_NOT_NULL(TEXT("Bioma sin contenido"), Bare);
	Bare->bGenerateProceduralContent = false;
	Bare->FinishSpawning(FTransform(FVector(0.0, 100000.0, 0.0)));
	TInlineComponentArray<UProceduralMeshComponent*> BareTerrains(Bare);
	TestEqual(TEXT("Sin terreno"), BareTerrains.Num(), 0);
	TestEqual(TEXT("Sin instancias"), Bare->ScatterInstanceCount, 0);
	TInlineComponentArray<ULocalFogVolumeComponent*> BareFogs(Bare);
	TestTrue(TEXT("Niebla apagada"), BareFogs.Num() == 1 && !BareFogs[0]->IsVisible());
	return true;
}
