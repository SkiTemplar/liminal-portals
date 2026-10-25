#include "Misc/AutomationTest.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/LiminalCharacter.h"
#include "Portal/LiminalPortal.h"
#include "Portal/LiminalPortalMath.h"
#include "Portal/LiminalPortalSubsystem.h"

// Tests de integración ligeros: mundo de juego vacío sin render, actores reales y sin mapa.

namespace LiminalPortalWorldTests
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalPortalCalibrationFlagsTest, "LiminalPortals.World.Portal.CalibrationFlags", TestFlags)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalPortalActorTest, "LiminalPortals.World.Portal.RegisterLinkAndLayout", TestFlags)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalCharacterTeleportTest, "LiminalPortals.World.Character.TeleportThroughPortal", TestFlags)
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
