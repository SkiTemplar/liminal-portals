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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiminalPortalActorTest, "LiminalPortals.World.Portal.RegisterLinkAndLayout", TestFlags)
bool FLiminalPortalActorTest::RunTest(const FString& Parameters)
{
	// Antes de E2 los materiales no existen: el portal avisa y sigue (no debe romper).
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
