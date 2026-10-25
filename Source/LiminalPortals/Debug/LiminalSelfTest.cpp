#include "Debug/LiminalSelfTest.h"

#include "CollisionQueryParams.h"
#include "Debug/LiminalDebugShared.h"
#include "Dom/JsonObject.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "InputActionValue.h"
#include "LiminalPortals.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Player/LiminalCharacter.h"
#include "Portal/LiminalPortal.h"
#include "Portal/LiminalPortalMath.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "World/LiminalHub.h"

namespace
{
	constexpr int32 MaxGroundingFrames = 300;
	/** Errores de detalle que se guardan por puerta (el recuento va aparte). */
	constexpr int32 MaxErrorsPerDoor = 8;
	/** Hub y biomas están a decenas de km: un paso de menos de 1 km no es el teletransporte esperado. */
	constexpr double MinTeleportDistanceCm = 100000.0;
	/** Profundidad por detrás del plano que se vigila: túnel del marco, placa trasera y 1 m más. */
	constexpr double PortalVolumeExtraDepth = 100.0;

	void AddDoorError(TArray<FString>& Errors, const FString& Error)
	{
		UE_LOG(LogLiminalPortals, Warning, TEXT("Liminal.SelfTest: %s"), *Error);
		if (Errors.Num() < MaxErrorsPerDoor)
		{
			Errors.Add(Error);
		}
	}
}

bool FLiminalSelfTestRun::FCrossingRecord::Passed(double MinNextFrameSpeedRatio) const
{
	const bool bNextFrameOk = NextFrameSpeedRatio < 0.0 || NextFrameSpeedRatio >= MinNextFrameSpeedRatio;
	return Check.Passed() && bExpectedPortals && TeleportDistanceCm >= MinTeleportDistanceCm
		&& CameraFrontDistanceCm > 0.0 && bNextFrameOk;
}

FLiminalSelfTestRun::FLiminalSelfTestRun(UWorld* InWorld, const FConfig& InConfig)
	: Config(InConfig)
	, World(InWorld)
{
}

FLiminalSelfTestRun::~FLiminalSelfTestRun()
{
	if (Phase != EPhase::Finished)
	{
		Unbind();
	}
}

bool FLiminalSelfTestRun::Start(FString& OutError)
{
	UWorld* GameWorld = World.Get();
	ULiminalPortalSubsystem* PortalSubsystem = GameWorld ? GameWorld->GetSubsystem<ULiminalPortalSubsystem>() : nullptr;
	if (!PortalSubsystem)
	{
		OutError = TEXT("no hay mundo de juego con ULiminalPortalSubsystem");
		return false;
	}
	if (Config.CyclesPerDoor <= 0)
	{
		OutError = TEXT("CyclesPerDoor debe ser positivo");
		return false;
	}

	IFileManager::Get().Delete(*Config.OutputPath, false, true, true);
	Subsystem = PortalSubsystem;
	PortalsUpdatedHandle = PortalSubsystem->OnPortalsUpdated.AddRaw(this, &FLiminalSelfTestRun::OnPortalsUpdated);
	TeleportHandle = PortalSubsystem->OnPortalTeleport.AddRaw(this, &FLiminalSelfTestRun::OnPortalTeleport);
	WatchdogHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FLiminalSelfTestRun::OnWatchdogTick));
	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddRaw(this, &FLiminalSelfTestRun::OnWorldCleanup);

	UE_LOG(LogLiminalPortals, Display, TEXT("Liminal.SelfTest: %d ciclos por puerta, paso fijo %s (%.4f s), informe %s"),
		Config.CyclesPerDoor, FApp::UseFixedTimeStep() ? TEXT("sí") : TEXT("no"), FApp::GetFixedDeltaTime(), *Config.OutputPath);
	return true;
}

bool FLiminalSelfTestRun::OnWatchdogTick(float DeltaTime)
{
	if (Phase == EPhase::Finished)
	{
		return false;
	}
	if (++TicksWithoutUpdate > LiminalDebugShared::MaxTicksWithoutUpdate)
	{
		Finish(TEXT("ULiminalPortalSubsystem no actualiza: ¿hay PlayerController local y Liminal.Portals.Enable 1?"));
		return false;
	}
	return true;
}

void FLiminalSelfTestRun::OnWorldCleanup(UWorld* CleanedWorld, bool bSessionEnded, bool bCleanupResources)
{
	if (CleanedWorld == World.Get() && Phase != EPhase::Finished)
	{
		Finish(TEXT("el mundo se destruyó durante la prueba"));
	}
}

void FLiminalSelfTestRun::OnPortalTeleport(const FLiminalPortalTeleportEvent& Event)
{
	PendingTeleport = Event;
}

void FLiminalSelfTestRun::OnPortalsUpdated(const FMinimalViewInfo& View)
{
	TicksWithoutUpdate = 0;
	if (Phase == EPhase::Finished)
	{
		return;
	}
	if (++TotalFrames > Config.MaxTotalFrames)
	{
		Finish(FString::Printf(TEXT("se superaron %d fotogramas"), Config.MaxTotalFrames));
		return;
	}

	// 1) Velocidad del fotograma siguiente a un cruce (antes de procesar uno nuevo).
	const ALiminalCharacter* Pawn = Character.Get();
	if (PendingNextFrameCheck != INDEX_NONE && Pawn)
	{
		FCrossingRecord& Record = Crossings[PendingNextFrameCheck];
		Record.NextFrameSpeedRatio = Pawn->GetCharacterMovement()->Velocity.Size() / FMath::Max(Record.Check.SpeedBefore, 1e-3);
		if (Record.Check.Passed() && Record.NextFrameSpeedRatio < Config.MinNextFrameSpeedRatio && Doors.IsValidIndex(Record.Door))
		{
			FDoorResult& Door = Doors[Record.Door];
			++Door.FailedCrossings;
			AddDoorError(Door.Errors, FString::Printf(TEXT("puerta %d: la velocidad cae al %.1f %% en el fotograma siguiente al cruce"),
				Record.Door, Record.NextFrameSpeedRatio * 100.0));
		}
		PendingNextFrameCheck = INDEX_NONE;
	}

	// 2) Cruce de este fotograma; 3) cámara final; 4) entrada del fotograma siguiente.
	if (PendingTeleport.IsSet())
	{
		HandleTeleport(View);
		PendingTeleport.Reset();
	}
	if (Phase == EPhase::Approach || Phase == EPhase::Depart)
	{
		CheckCamera(View);
	}

	switch (Phase)
	{
	case EPhase::WaitPawn:
		UpdateWaitPawn();
		break;
	case EPhase::Grounding:
		UpdateGrounding();
		break;
	case EPhase::Approach:
		UpdateApproach(View);
		break;
	case EPhase::Depart:
		UpdateDepart(View);
		break;
	default:
		break;
	}
}

void FLiminalSelfTestRun::UpdateWaitPawn()
{
	UWorld* GameWorld = World.Get();
	APlayerController* Controller = GameWorld ? GameWorld->GetFirstPlayerController() : nullptr;
	ALiminalCharacter* Pawn = Controller ? Cast<ALiminalCharacter>(Controller->GetPawn()) : nullptr;
	if (!Pawn || !Pawn->GetMoveAction() || !Controller->GetLocalPlayer())
	{
		return;
	}

	TArray<FLiminalDoorPair> Pairs;
	ALiminalHub* Hub = nullptr;
	FString Error;
	if (!LiminalDebugWorld::FindDoorPairs(GameWorld, Pairs, Hub, Error))
	{
		Finish(Error);
		return;
	}

	PlayerController = Controller;
	Character = Pawn;
	for (const FLiminalDoorPair& Pair : Pairs)
	{
		FDoorResult& Door = Doors.AddDefaulted_GetRef();
		Door.Pair = Pair;
		Door.HubPortalName = Pair.HubPortal->GetName();
		Door.BiomePortalName = Pair.BiomePortal->GetName();
	}
	for (TActorIterator<ALiminalPortal> It(GameWorld); It; ++It)
	{
		AllPortals.Add(*It);
	}

	// La mirada la fija la prueba; el ratón no puede desviarla.
	Controller->SetIgnoreLookInput(true);
	UE_LOG(LogLiminalPortals, Display, TEXT("Liminal.SelfTest: %d puertas, %d portales"), Doors.Num(), AllPortals.Num());
	BeginDoor(0);
}

void FLiminalSelfTestRun::BeginDoor(int32 DoorIndex)
{
	CurrentDoor = DoorIndex;
	bOnHubSide = true;
	PhaseFrames = 0;
	GroundedStreak = 0;
	Phase = EPhase::Grounding;

	const ALiminalPortal* HubPortal = GetPortal(true);
	if (!HubPortal)
	{
		FailDoor(FString::Printf(TEXT("puerta %d: el portal del hub ya no existe"), DoorIndex));
		return;
	}
	const FTransform Portal = HubPortal->GetPortalTransform();
	const FVector Feet = Portal.TransformPositionNoScale(FVector(Config.StartDistance, 0.0, 0.0));
	const FRotator Facing(0.0, Portal.Rotator().Yaw + 180.0, 0.0);
	LiminalDebugWorld::PlacePlayer(PlayerController.Get(), Character.Get(), Feet, Facing);
	UE_LOG(LogLiminalPortals, Display, TEXT("Liminal.SelfTest: puerta %d (%s)"), DoorIndex, *Doors[DoorIndex].Pair.Label);
}

void FLiminalSelfTestRun::UpdateGrounding()
{
	const ALiminalCharacter* Pawn = Character.Get();
	if (!Pawn)
	{
		Finish(TEXT("el personaje desapareció"));
		return;
	}
	if (++PhaseFrames > MaxGroundingFrames)
	{
		FailDoor(FString::Printf(TEXT("puerta %d: el personaje no llega al suelo"), CurrentDoor));
		return;
	}
	GroundedStreak = Pawn->GetCharacterMovement()->IsMovingOnGround() ? GroundedStreak + 1 : 0;
	if (GroundedStreak >= Config.GroundedFrames)
	{
		GroundedStreak = 0;
		Phase = EPhase::Approach;
		PhaseFrames = 0;
	}
}

void FLiminalSelfTestRun::UpdateApproach(const FMinimalViewInfo& View)
{
	const ALiminalPortal* Target = GetPortal(bOnHubSide);
	if (!Target)
	{
		FailDoor(FString::Printf(TEXT("puerta %d: el portal de destino ya no existe"), CurrentDoor));
		return;
	}
	if (++PhaseFrames > Config.CrossingTimeoutFrames)
	{
		FailDoor(FString::Printf(TEXT("puerta %d: sin teletransporte tras %d fotogramas caminando hacia %s"),
			CurrentDoor, Config.CrossingTimeoutFrames, *Target->GetName()));
		return;
	}
	Steer(View.Location, Target);
	InjectForward();
}

void FLiminalSelfTestRun::UpdateDepart(const FMinimalViewInfo& View)
{
	const FDoorResult& Door = Doors[CurrentDoor];
	const bool bDoorComplete = Door.HubToBiome >= Config.CyclesPerDoor && Door.BiomeToHub >= Config.CyclesPerDoor;
	const ALiminalPortal* Exit = GetPortal(bOnHubSide);
	APlayerController* Controller = PlayerController.Get();
	if (!Exit || !Controller)
	{
		FailDoor(FString::Printf(TEXT("puerta %d: falta el portal de salida o el controlador"), CurrentDoor));
		return;
	}

	// Se sigue alejando del portal de salida, mirando hacia fuera de su cara frontal.
	const FTransform ExitTransform = Exit->GetPortalTransform();
	Controller->SetControlRotation(FRotator(0.0, ExitTransform.Rotator().Yaw, 0.0));

	if (bDoorComplete && bOnHubSide)
	{
		// Se espera a medir la velocidad del fotograma siguiente al último cruce antes de recolocar.
		if (PendingNextFrameCheck == INDEX_NONE)
		{
			FinishDoor();
			return;
		}
		InjectForward();
		return;
	}

	const double Distance = LiminalPortalMath::SignedDistanceToPlane(Exit->GetPlaneTransform(), View.Location);
	if (Distance >= Config.DepartDistance)
	{
		// Media vuelta y de nuevo hacia el mismo portal.
		Phase = EPhase::Approach;
		PhaseFrames = 0;
		Steer(View.Location, Exit);
		InjectForward();
		return;
	}
	if (++PhaseFrames > Config.CrossingTimeoutFrames)
	{
		FailDoor(FString::Printf(TEXT("puerta %d: no se aleja de %s"), CurrentDoor, *Exit->GetName()));
		return;
	}
	InjectForward();
}

void FLiminalSelfTestRun::HandleTeleport(const FMinimalViewInfo& View)
{
	const FLiminalPortalTeleportEvent& Event = PendingTeleport.GetValue();
	if (!Doors.IsValidIndex(CurrentDoor))
	{
		return;
	}
	FDoorResult& Door = Doors[CurrentDoor];
	if (Phase != EPhase::Approach || !Event.Source || !Event.Target)
	{
		FailDoor(FString::Printf(TEXT("puerta %d: teletransporte inesperado (%s)"), CurrentDoor,
			Event.Source ? *Event.Source->GetName() : TEXT("?")));
		return;
	}

	const bool bHubToBiome = bOnHubSide;
	FCrossingRecord& Record = Crossings.AddDefaulted_GetRef();
	Record.Door = CurrentDoor;
	Record.bHubToBiome = bHubToBiome;
	Record.Frame = TotalFrames;
	Record.bExpectedPortals = Event.Source == GetPortal(bOnHubSide) && Event.Target == GetPortal(!bOnHubSide);
	Record.Check = LiminalSelfTestMath::CheckCrossing(Event.Source->GetPortalTransform(), Event.Target->GetPortalTransform(),
		Event.LocationBefore, Event.LocationAfter, Event.VelocityBefore, Event.VelocityAfter, Config.Tolerances);
	Record.TeleportDistanceCm = FVector::Dist(Event.LocationBefore, Event.LocationAfter);
	Record.CameraFrontDistanceCm = LiminalPortalMath::SignedDistanceToPlane(Event.Target->GetPlaneTransform(), View.Location);

	if (!Record.bExpectedPortals)
	{
		FailDoor(FString::Printf(TEXT("puerta %d: cruce por %s -> %s, no por la pareja esperada"), CurrentDoor,
			*Event.Source->GetName(), *Event.Target->GetName()));
		return;
	}

	int32& Count = bHubToBiome ? Door.HubToBiome : Door.BiomeToHub;
	++Count;
	if (!Record.Passed(Config.MinNextFrameSpeedRatio))
	{
		++Door.FailedCrossings;
		AddDoorError(Door.Errors, FString::Printf(
			TEXT("puerta %d, cruce %s %d: velocidad %.2f -> %.2f (%.3f %%), dirección %.3f°, posición %.2f cm, salto %.0f cm, cámara %.2f cm delante"),
			CurrentDoor, bHubToBiome ? TEXT("hub->bioma") : TEXT("bioma->hub"), Count, Record.Check.SpeedBefore,
			Record.Check.SpeedAfter, Record.Check.SpeedErrorRatio * 100.0, Record.Check.DirectionErrorDegrees,
			Record.Check.PositionErrorCm, Record.TeleportDistanceCm, Record.CameraFrontDistanceCm));
	}

	PendingNextFrameCheck = Crossings.Num() - 1;
	bOnHubSide = !bOnHubSide;
	Phase = EPhase::Depart;
	PhaseFrames = 0;
}

void FLiminalSelfTestRun::CheckCamera(const FMinimalViewInfo& View)
{
	for (const TWeakObjectPtr<ALiminalPortal>& Entry : AllPortals)
	{
		const ALiminalPortal* Portal = Entry.Get();
		if (!Portal)
		{
			continue;
		}
		const double Depth = Portal->BackPlateDistance + PortalVolumeExtraDepth;
		if (LiminalSelfTestMath::IsInsidePortalVolume(Portal->GetPlaneTransform(), Portal->GetHalfExtents(),
			Portal->FrameThickness, Depth, View.Location))
		{
			++CameraInsidePortalFrames;
			if (FirstCameraError.IsEmpty())
			{
				FirstCameraError = FString::Printf(TEXT("fotograma %d: cámara detrás del plano de %s (%s)"), TotalFrames,
					*Portal->GetName(), *View.Location.ToString());
			}
			break;
		}
	}

	UWorld* GameWorld = World.Get();
	if (!GameWorld)
	{
		return;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LiminalSelfTestCamera), false, Character.Get());
	if (GameWorld->OverlapAnyTestByChannel(View.Location, FQuat::Identity, ECC_Camera,
		FCollisionShape::MakeSphere(static_cast<float>(Config.CameraProbeRadius)), Params))
	{
		++CameraInGeometryFrames;
		if (FirstCameraError.IsEmpty())
		{
			FirstCameraError = FString::Printf(TEXT("fotograma %d: cámara dentro de geometría (%s)"), TotalFrames, *View.Location.ToString());
		}
	}
}

void FLiminalSelfTestRun::FailDoor(const FString& Error)
{
	if (Doors.IsValidIndex(CurrentDoor))
	{
		AddDoorError(Doors[CurrentDoor].Errors, Error);
	}
	PendingNextFrameCheck = INDEX_NONE;
	FinishDoor();
}

void FLiminalSelfTestRun::FinishDoor()
{
	if (Doors.IsValidIndex(CurrentDoor))
	{
		const FDoorResult& Door = Doors[CurrentDoor];
		UE_LOG(LogLiminalPortals, Display, TEXT("Liminal.SelfTest: puerta %d (%s) hub->bioma %d, bioma->hub %d, fallos %d"),
			CurrentDoor, *Door.Pair.Label, Door.HubToBiome, Door.BiomeToHub, Door.FailedCrossings + Door.Errors.Num());
	}
	if (CurrentDoor + 1 < Doors.Num())
	{
		BeginDoor(CurrentDoor + 1);
		return;
	}
	Finish(FString());
}

ALiminalPortal* FLiminalSelfTestRun::GetPortal(bool bHubSide) const
{
	if (!Doors.IsValidIndex(CurrentDoor))
	{
		return nullptr;
	}
	const FLiminalDoorPair& Pair = Doors[CurrentDoor].Pair;
	return bHubSide ? Pair.HubPortal.Get() : Pair.BiomePortal.Get();
}

void FLiminalSelfTestRun::Steer(const FVector& CameraLocation, const ALiminalPortal* Target)
{
	APlayerController* Controller = PlayerController.Get();
	if (!Controller || !Target)
	{
		return;
	}
	// Se apunta a un punto detrás del centro del hueco: el rumbo no oscila al acercarse al plano.
	const FTransform Plane = Target->GetPlaneTransform();
	const FVector Aim = Plane.GetLocation() - Plane.GetUnitAxis(EAxis::X) * Config.AimBehindPlane;
	const FVector Direction = (Aim - CameraLocation).GetSafeNormal2D();
	if (!Direction.IsNearlyZero())
	{
		Controller->SetControlRotation(FRotator(0.0, Direction.Rotation().Yaw, 0.0));
	}
}

void FLiminalSelfTestRun::InjectForward()
{
	const APlayerController* Controller = PlayerController.Get();
	const ALiminalCharacter* Pawn = Character.Get();
	ULocalPlayer* LocalPlayer = Controller ? Controller->GetLocalPlayer() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Input = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer);
	if (!Input || !Pawn)
	{
		return;
	}
	// Move es Axis2D con X = derecha e Y = delante; se procesa en el PlayerTick del fotograma siguiente.
	Input->InjectInputForAction(Pawn->GetMoveAction(), FInputActionValue(FVector2D(0.0, 1.0)),
		TArray<UInputModifier*>(), TArray<UInputTrigger*>());
}

bool FLiminalSelfTestRun::HasPassed(const FString& Error) const
{
	if (!Error.IsEmpty() || Doors.IsEmpty() || CameraInsidePortalFrames > 0 || CameraInGeometryFrames > 0)
	{
		return false;
	}
	for (const FDoorResult& Door : Doors)
	{
		const bool bComplete = Door.HubToBiome == Config.CyclesPerDoor && Door.BiomeToHub == Config.CyclesPerDoor;
		if (!bComplete || Door.FailedCrossings > 0 || !Door.Errors.IsEmpty())
		{
			return false;
		}
	}
	return true;
}

void FLiminalSelfTestRun::Finish(const FString& Error)
{
	if (Phase == EPhase::Finished)
	{
		return;
	}
	Phase = EPhase::Finished;
	Unbind();
	if (APlayerController* Controller = PlayerController.Get())
	{
		Controller->ResetIgnoreLookInput();
	}

	const bool bPassed = HasPassed(Error);
	const bool bWritten = WriteReport(Error);
	if (bPassed && bWritten)
	{
		UE_LOG(LogLiminalPortals, Display, TEXT("Liminal.SelfTest: OK, %d cruces en %d fotogramas"), Crossings.Num(), TotalFrames);
	}
	else
	{
		UE_LOG(LogLiminalPortals, Error, TEXT("Liminal.SelfTest: FALLO (%s)"),
			!Error.IsEmpty() ? *Error : (!bWritten ? TEXT("no se pudo escribir el informe") : TEXT("ver el informe")));
	}

	if (!GIsEditor)
	{
		FPlatformMisc::RequestExit(false, TEXT("Liminal.SelfTest"));
	}
}

void FLiminalSelfTestRun::Unbind()
{
	if (ULiminalPortalSubsystem* PortalSubsystem = Subsystem.Get())
	{
		PortalSubsystem->OnPortalsUpdated.Remove(PortalsUpdatedHandle);
		PortalSubsystem->OnPortalTeleport.Remove(TeleportHandle);
	}
	FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
	if (WatchdogHandle.IsValid())
	{
		FTSTicker::RemoveTicker(WatchdogHandle);
		WatchdogHandle.Reset();
	}
}

bool FLiminalSelfTestRun::WriteReport(const FString& Error) const
{
	double MaxSpeedError = 0.0;
	double MaxDirectionError = 0.0;
	double MaxPositionError = 0.0;
	double MinNextFrameRatio = TNumericLimits<double>::Max();
	TArray<TSharedPtr<FJsonValue>> CrossingValues;
	for (const FCrossingRecord& Record : Crossings)
	{
		MaxSpeedError = FMath::Max(MaxSpeedError, Record.Check.SpeedErrorRatio);
		MaxDirectionError = FMath::Max(MaxDirectionError, Record.Check.DirectionErrorDegrees);
		MaxPositionError = FMath::Max(MaxPositionError, Record.Check.PositionErrorCm);
		if (Record.NextFrameSpeedRatio >= 0.0)
		{
			MinNextFrameRatio = FMath::Min(MinNextFrameRatio, Record.NextFrameSpeedRatio);
		}

		const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("door"), Record.Door);
		Object->SetStringField(TEXT("direction"), Record.bHubToBiome ? TEXT("hubToBiome") : TEXT("biomeToHub"));
		Object->SetNumberField(TEXT("frame"), Record.Frame);
		Object->SetBoolField(TEXT("passed"), Record.Passed(Config.MinNextFrameSpeedRatio));
		Object->SetNumberField(TEXT("speedBefore"), Record.Check.SpeedBefore);
		Object->SetNumberField(TEXT("speedAfter"), Record.Check.SpeedAfter);
		Object->SetNumberField(TEXT("speedErrorPct"), Record.Check.SpeedErrorRatio * 100.0);
		Object->SetNumberField(TEXT("directionErrorDeg"), Record.Check.DirectionErrorDegrees);
		Object->SetNumberField(TEXT("positionErrorCm"), Record.Check.PositionErrorCm);
		Object->SetNumberField(TEXT("teleportDistanceCm"), Record.TeleportDistanceCm);
		Object->SetNumberField(TEXT("cameraFrontDistanceCm"), Record.CameraFrontDistanceCm);
		Object->SetNumberField(TEXT("nextFrameSpeedRatio"), Record.NextFrameSpeedRatio);
		CrossingValues.Add(MakeShared<FJsonValueObject>(Object));
	}

	TArray<TSharedPtr<FJsonValue>> DoorValues;
	int32 HubToBiome = 0;
	int32 BiomeToHub = 0;
	for (const FDoorResult& Door : Doors)
	{
		HubToBiome += Door.HubToBiome;
		BiomeToHub += Door.BiomeToHub;
		const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("index"), Door.Pair.DoorIndex);
		Object->SetStringField(TEXT("biome"), Door.Pair.Label);
		Object->SetStringField(TEXT("hubPortal"), Door.HubPortalName);
		Object->SetStringField(TEXT("biomePortal"), Door.BiomePortalName);
		Object->SetNumberField(TEXT("hubToBiome"), Door.HubToBiome);
		Object->SetNumberField(TEXT("biomeToHub"), Door.BiomeToHub);
		Object->SetNumberField(TEXT("failedCrossings"), Door.FailedCrossings);
		const bool bDoorPassed = Door.HubToBiome == Config.CyclesPerDoor && Door.BiomeToHub == Config.CyclesPerDoor
			&& Door.FailedCrossings == 0 && Door.Errors.IsEmpty();
		Object->SetBoolField(TEXT("passed"), bDoorPassed);
		TArray<TSharedPtr<FJsonValue>> Errors;
		for (const FString& DoorError : Door.Errors)
		{
			Errors.Add(MakeShared<FJsonValueString>(DoorError));
		}
		Object->SetArrayField(TEXT("errors"), Errors);
		DoorValues.Add(MakeShared<FJsonValueObject>(Object));
	}

	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("passed"), HasPassed(Error));
	Root->SetStringField(TEXT("error"), Error);
	Root->SetNumberField(TEXT("cyclesPerDoor"), Config.CyclesPerDoor);
	Root->SetNumberField(TEXT("frames"), TotalFrames);
	Root->SetBoolField(TEXT("fixedTimeStep"), FApp::UseFixedTimeStep());
	Root->SetNumberField(TEXT("fixedDeltaTime"), FApp::GetFixedDeltaTime());
	Root->SetNumberField(TEXT("crossings"), Crossings.Num());
	Root->SetNumberField(TEXT("hubToBiome"), HubToBiome);
	Root->SetNumberField(TEXT("biomeToHub"), BiomeToHub);
	Root->SetNumberField(TEXT("maxSpeedErrorPct"), MaxSpeedError * 100.0);
	Root->SetNumberField(TEXT("maxDirectionErrorDeg"), MaxDirectionError);
	Root->SetNumberField(TEXT("maxPositionErrorCm"), MaxPositionError);
	Root->SetNumberField(TEXT("minNextFrameSpeedRatio"), Crossings.IsEmpty() ? -1.0 : MinNextFrameRatio);
	Root->SetNumberField(TEXT("speedTolerancePct"), Config.Tolerances.SpeedRatio * 100.0);
	Root->SetNumberField(TEXT("cameraInsidePortalFrames"), CameraInsidePortalFrames);
	Root->SetNumberField(TEXT("cameraInGeometryFrames"), CameraInGeometryFrames);
	Root->SetStringField(TEXT("firstCameraError"), FirstCameraError);
	Root->SetArrayField(TEXT("doors"), DoorValues);
	Root->SetArrayField(TEXT("crossingList"), CrossingValues);

	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	if (!FJsonSerializer::Serialize(Root, Writer))
	{
		return false;
	}
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Config.OutputPath), true);
	return FFileHelper::SaveStringToFile(Json, *Config.OutputPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
