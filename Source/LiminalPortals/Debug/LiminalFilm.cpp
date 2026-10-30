#include "Debug/LiminalFilm.h"

#include "Debug/LiminalDebugShared.h"
#include "Debug/LiminalDebugWorld.h"
#include "Dom/JsonObject.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "LiminalPortals.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Player/LiminalCharacter.h"
#include "Portal/LiminalPortal.h"
#include "Portal/LiminalPortalMath.h"
#include "Portal/LiminalPortalSubsystem.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"
#include "World/LiminalHub.h"

// Namespace con nombre propio (D-22): el unity build junta todos los .cpp del módulo.
namespace LiminalFilmDetail
{
	/** Una vista que se aleja más que esto de la pose pedida es un fallo grave: se aborta. */
	constexpr double AbortDeviationCm = 50.0;
}

FLiminalFilmRun::FLiminalFilmRun(UWorld* InWorld, const FConfig& InConfig)
	: Config(InConfig)
	, World(InWorld)
{
}

FLiminalFilmRun::~FLiminalFilmRun()
{
	if (Phase != EPhase::Finished)
	{
		Unbind();
	}
}

bool FLiminalFilmRun::Start(FString& OutError)
{
	UWorld* GameWorld = World.Get();
	ULiminalPortalSubsystem* PortalSubsystem = GameWorld ? GameWorld->GetSubsystem<ULiminalPortalSubsystem>() : nullptr;
	if (!PortalSubsystem)
	{
		OutError = TEXT("no hay mundo de juego con ULiminalPortalSubsystem");
		return false;
	}
	if (Config.SampleRate <= 0.0 || Config.CaptureEvery < 1 || Config.PrerollFrames < 0)
	{
		OutError = TEXT("configuración no válida (SampleRate > 0, CaptureEvery >= 1, PrerollFrames >= 0)");
		return false;
	}

	IFileManager& FileManager = IFileManager::Get();
	FileManager.DeleteDirectory(*Config.OutputDir, false, true);
	if (!FileManager.MakeDirectory(*Config.OutputDir, true))
	{
		OutError = FString::Printf(TEXT("no se pudo crear %s"), *Config.OutputDir);
		return false;
	}

	Subsystem = PortalSubsystem;
	PortalsUpdatedHandle = PortalSubsystem->OnPortalsUpdated.AddRaw(this, &FLiminalFilmRun::OnPortalsUpdated);
	TeleportHandle = PortalSubsystem->OnPortalTeleport.AddRaw(this, &FLiminalFilmRun::OnPortalTeleport);
	ScreenshotHandle = UGameViewportClient::OnScreenshotCaptured().AddRaw(this, &FLiminalFilmRun::OnScreenshotCaptured);
	WatchdogHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FLiminalFilmRun::OnWatchdogTick));
	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddRaw(this, &FLiminalFilmRun::OnWorldCleanup);
	UE_LOG(LogLiminalPortals, Display, TEXT("Liminal.Film: salida %s, %.0f Hz, 1 de cada %d fotogramas, paso fijo %s"),
		*Config.OutputDir, Config.SampleRate, Config.CaptureEvery, FApp::UseFixedTimeStep() ? TEXT("sí") : TEXT("no"));
	return true;
}

bool FLiminalFilmRun::OnWatchdogTick(float DeltaTime)
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

void FLiminalFilmRun::OnWorldCleanup(UWorld* CleanedWorld, bool bSessionEnded, bool bCleanupResources)
{
	if (CleanedWorld == World.Get() && Phase != EPhase::Finished)
	{
		Finish(TEXT("el mundo se destruyó durante el rodaje"));
	}
}

void FLiminalFilmRun::OnPortalsUpdated(const FMinimalViewInfo& View)
{
	TicksWithoutUpdate = 0;
	if (Phase == EPhase::Finished)
	{
		return;
	}
	if (!RunError.IsEmpty())
	{
		Finish(RunError);
		return;
	}
	if (++TotalFrames > Config.MaxTotalFrames)
	{
		Finish(FString::Printf(TEXT("se superaron %d fotogramas"), Config.MaxTotalFrames));
		return;
	}

	switch (Phase)
	{
	case EPhase::WaitPawn:
		UpdateWaitPawn();
		break;
	case EPhase::Preroll:
		if (++PhaseFrames >= Config.PrerollFrames)
		{
			Phase = EPhase::Play;
			PhaseFrames = 0;
			UpdatePlay(View);
		}
		break;
	case EPhase::Play:
		UpdatePlay(View);
		break;
	case EPhase::Drain:
		if (!PendingScreenshotPath.IsEmpty())
		{
			Finish(FString::Printf(TEXT("no llegó la captura %s"), *PendingScreenshotPath));
			break;
		}
		{
			FShotRun& Run = Shots[CurrentShot];
			Run.bComplete = Run.Crossings == Run.ExpectedCrossings && Run.MaxDeviationCm <= Config.MaxDeviationCm;
			UE_LOG(LogLiminalPortals, Display, TEXT("Liminal.Film: %s, %d fotogramas guardados, cruces %d de %d, desviación máxima %.4f cm"),
				*Run.Shot.Name, Run.SavedFrames, Run.Crossings, Run.ExpectedCrossings, Run.MaxDeviationCm);
		}
		if (CurrentShot + 1 < Shots.Num())
		{
			BeginShot(CurrentShot + 1);
		}
		else
		{
			Finish(FString());
		}
		break;
	default:
		break;
	}
}

void FLiminalFilmRun::UpdatePlay(const FMinimalViewInfo& View)
{
	if (!PendingScreenshotPath.IsEmpty())
	{
		Finish(FString::Printf(TEXT("no llegó la captura %s"), *PendingScreenshotPath));
		return;
	}
	FShotRun& Run = Shots[CurrentShot];
	const int32 Frame = PhaseFrames;
	const double Deviation = FVector::Dist(View.Location, GetWorldPose(Frame).Location);
	Run.MaxDeviationCm = FMath::Max(Run.MaxDeviationCm, Deviation);
	if (Deviation > LiminalFilmDetail::AbortDeviationCm)
	{
		Finish(FString::Printf(TEXT("%s: la vista del fotograma %d está a %.1f cm de la pose pedida"), *Run.Shot.Name, Frame, Deviation));
		return;
	}

	if (Frame % Config.CaptureEvery == 0)
	{
		const FString Name = FString::Printf(TEXT("frame_%04d.png"), Frame / Config.CaptureEvery);
		PendingScreenshotPath = FPaths::Combine(Config.OutputDir, Run.Shot.Name, Name);
		FScreenshotRequest::RequestScreenshot(false);
	}

	if (Frame + 1 < Run.SimFrames)
	{
		ApplyPose(GetWorldPose(Frame + 1), false);
		++PhaseFrames;
	}
	else
	{
		Phase = EPhase::Drain;
	}
}

void FLiminalFilmRun::OnPortalTeleport(const FLiminalPortalTeleportEvent& Event)
{
	if (Phase != EPhase::Play || !Shots.IsValidIndex(CurrentShot))
	{
		RunError = TEXT("paso por portal fuera de un plano en curso");
		return;
	}
	FShotRun& Run = Shots[CurrentShot];
	const ALiminalPortal* HubPortal = Run.HubPortal.Get();
	const ALiminalPortal* BiomePortal = Run.BiomePortal.Get();
	const bool bIntoBiome = Event.Source == HubPortal && Event.Target == BiomePortal;
	const bool bIntoHub = Event.Source == BiomePortal && Event.Target == HubPortal;
	if (!HubPortal || !(bIntoBiome || bIntoHub))
	{
		RunError = FString::Printf(TEXT("%s: paso por un portal ajeno a la puerta del plano"), *Run.Shot.Name);
		return;
	}
	bBeyondDoor = bIntoBiome;
	++Run.Crossings;
}

void FLiminalFilmRun::UpdateWaitPawn()
{
	UWorld* GameWorld = World.Get();
	APlayerController* Controller = GameWorld ? GameWorld->GetFirstPlayerController() : nullptr;
	ALiminalCharacter* Pawn = Controller ? Cast<ALiminalCharacter>(Controller->GetPawn()) : nullptr;
	if (!Pawn)
	{
		return;
	}
	PlayerController = Controller;
	Character = Pawn;

	FString Error;
	if (!BuildShots(Error))
	{
		Finish(Error);
		return;
	}
	Controller->SetIgnoreLookInput(true);
	Controller->SetIgnoreMoveInput(true);
	// La pose se fija en cada fotograma: sin gravedad ni suelo, la cámara puede ir a cualquier altura.
	Pawn->GetCharacterMovement()->DisableMovement();
	BeginShot(0);
}

bool FLiminalFilmRun::BuildShots(FString& OutError)
{
	TArray<FLiminalDoorPair> Pairs;
	ALiminalHub* Hub = nullptr;
	if (!LiminalDebugWorld::FindDoorPairs(World.Get(), Pairs, Hub, OutError))
	{
		return false;
	}

	for (LiminalFilmMath::FFilmShot& Shot : LiminalFilmMath::MakeDefaultFilmShots())
	{
		if (!Config.ShotFilter.IsEmpty() && !Shot.Name.Contains(Config.ShotFilter))
		{
			continue;
		}
		if (!LiminalFilmMath::IsValidShot(Shot, Pairs.Num(), &OutError))
		{
			return false;
		}
		FShotRun& Run = Shots.AddDefaulted_GetRef();
		Run.SimFrames = LiminalFilmMath::GetShotFrameCount(Shot, Config.SampleRate);
		if (Shot.Anchor == LiminalFilmMath::EFilmAnchor::Hub)
		{
			Run.Label = TEXT("Hub");
			Run.Frame = FTransform(Hub->GetActorQuat(), Hub->GetActorLocation());
		}
		else
		{
			const FLiminalDoorPair& Pair = Pairs[Shot.DoorIndex];
			const ALiminalPortal* HubPortal = Pair.HubPortal.Get();
			const ALiminalPortal* BiomePortal = Pair.BiomePortal.Get();
			Run.Label = Pair.Label;
			Run.HubPortal = HubPortal;
			Run.BiomePortal = BiomePortal;
			Run.Frame = HubPortal->GetPortalTransform();
			Run.Transfer = LiminalPortalMath::MakeTransfer(HubPortal->GetPortalTransform(), BiomePortal->GetPortalTransform());
			Run.ExpectedCrossings = LiminalFilmMath::CountDoorCrossings(Shot.Keys, Config.SampleRate, HubPortal->GetHalfExtents());
		}
		const FString ShotDir = FPaths::Combine(Config.OutputDir, Shot.Name);
		if (!IFileManager::Get().MakeDirectory(*ShotDir, true))
		{
			OutError = FString::Printf(TEXT("no se pudo crear %s"), *ShotDir);
			return false;
		}
		Run.Shot = MoveTemp(Shot);
	}
	if (Shots.IsEmpty())
	{
		OutError = FString::Printf(TEXT("ningún plano contiene '%s'"), *Config.ShotFilter);
		return false;
	}
	return true;
}

void FLiminalFilmRun::BeginShot(int32 Index)
{
	CurrentShot = Index;
	PhaseFrames = 0;
	Phase = EPhase::Preroll;
	const FShotRun& Run = Shots[Index];
	bBeyondDoor = Run.Shot.Anchor == LiminalFilmMath::EFilmAnchor::Door && Run.Shot.Keys[0].Pose.Location.X < 0.0;
	ApplyPose(GetWorldPose(0), true);
	UE_LOG(LogLiminalPortals, Display, TEXT("Liminal.Film: %s (%s), %d fotogramas, %d cruces previstos"),
		*Run.Shot.Name, *Run.Label, Run.SimFrames, Run.ExpectedCrossings);
}

LiminalFilmMath::FFilmPose FLiminalFilmRun::GetWorldPose(int32 Frame) const
{
	const FShotRun& Run = Shots[CurrentShot];
	const LiminalFilmMath::FFilmPose Local = LiminalFilmMath::EvaluateShot(Run.Shot.Keys, Frame / Config.SampleRate);
	LiminalFilmMath::FFilmPose Pose = LiminalFilmMath::ToWorld(Run.Frame, Local);
	if (bBeyondDoor)
	{
		const FRotator Rotation = LiminalPortalMath::TransferControlRotation(Run.Transfer, FRotator(Pose.Pitch, Pose.Yaw, 0.0));
		Pose.Location = LiminalPortalMath::TransferPoint(Run.Transfer, Pose.Location);
		Pose.Yaw = Rotation.Yaw;
		Pose.Pitch = Rotation.Pitch;
	}
	return Pose;
}

void FLiminalFilmRun::ApplyPose(const LiminalFilmMath::FFilmPose& Pose, bool bCut)
{
	APlayerController* Controller = PlayerController.Get();
	ALiminalCharacter* Pawn = Character.Get();
	if (!Controller || !Pawn)
	{
		RunError = TEXT("se perdió el jugador");
		return;
	}
	const FVector ActorLocation = Pose.Location - FVector(0.0, 0.0, Pawn->BaseEyeHeight);
	Pawn->SetActorLocationAndRotation(ActorLocation, FRotator(0.0, Pose.Yaw, 0.0), false, nullptr, ETeleportType::TeleportPhysics);
	Controller->SetControlRotation(FRotator(Pose.Pitch, Pose.Yaw, 0.0));
	if (!bCut)
	{
		return;
	}
	if (APlayerCameraManager* CameraManager = Controller->PlayerCameraManager)
	{
		CameraManager->SetGameCameraCutThisFrame();
	}
	if (ULiminalPortalSubsystem* PortalSubsystem = Subsystem.Get())
	{
		PortalSubsystem->ResetCameraHistory();
	}
}

void FLiminalFilmRun::OnScreenshotCaptured(int32 Width, int32 Height, const TArray<FColor>& Colors)
{
	if (PendingScreenshotPath.IsEmpty())
	{
		return;
	}
	const FString Path = MoveTemp(PendingScreenshotPath);
	PendingScreenshotPath.Reset();

	if (Width <= 0 || Height <= 0 || Colors.Num() != Width * Height)
	{
		RunError = FString::Printf(TEXT("captura vacía o incoherente (%dx%d, %d píxeles)"), Width, Height, Colors.Num());
		return;
	}
	ScreenshotSize = FIntPoint(Width, Height);
	const FImageView Image(Colors.GetData(), Width, Height, EGammaSpace::sRGB);
	if (!FImageUtils::SaveImageByExtension(*Path, Image))
	{
		RunError = FString::Printf(TEXT("no se pudo guardar %s"), *Path);
		return;
	}
	if (Shots.IsValidIndex(CurrentShot))
	{
		++Shots[CurrentShot].SavedFrames;
	}
}

void FLiminalFilmRun::Finish(const FString& Error)
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
		Controller->ResetIgnoreMoveInput();
	}
	if (ALiminalCharacter* Pawn = Character.Get())
	{
		Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	}

	const bool bWritten = WriteReport(Error);
	if (Error.IsEmpty() && bWritten)
	{
		UE_LOG(LogLiminalPortals, Display, TEXT("Liminal.Film: %d planos en %s"), Shots.Num(), *Config.OutputDir);
	}
	else
	{
		UE_LOG(LogLiminalPortals, Error, TEXT("Liminal.Film: %s"), Error.IsEmpty() ? TEXT("no se pudo escribir el índice") : *Error);
	}
	if (!GIsEditor)
	{
		FPlatformMisc::RequestExit(false, TEXT("Liminal.Film"));
	}
}

void FLiminalFilmRun::Unbind()
{
	if (ULiminalPortalSubsystem* PortalSubsystem = Subsystem.Get())
	{
		PortalSubsystem->OnPortalsUpdated.Remove(PortalsUpdatedHandle);
		PortalSubsystem->OnPortalTeleport.Remove(TeleportHandle);
	}
	UGameViewportClient::OnScreenshotCaptured().Remove(ScreenshotHandle);
	FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
	if (WatchdogHandle.IsValid())
	{
		FTSTicker::RemoveTicker(WatchdogHandle);
		WatchdogHandle.Reset();
	}
}

bool FLiminalFilmRun::WriteReport(const FString& Error) const
{
	bool bAllComplete = !Shots.IsEmpty();
	TArray<TSharedPtr<FJsonValue>> ShotValues;
	for (const FShotRun& Run : Shots)
	{
		bAllComplete &= Run.bComplete;
		const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("name"), Run.Shot.Name);
		Object->SetStringField(TEXT("label"), Run.Label);
		Object->SetNumberField(TEXT("door"), Run.Shot.DoorIndex);
		Object->SetNumberField(TEXT("durationSeconds"), Run.Shot.GetDuration());
		Object->SetNumberField(TEXT("simFrames"), Run.SimFrames);
		Object->SetNumberField(TEXT("savedFrames"), Run.SavedFrames);
		Object->SetNumberField(TEXT("expectedCrossings"), Run.ExpectedCrossings);
		Object->SetNumberField(TEXT("crossings"), Run.Crossings);
		Object->SetNumberField(TEXT("maxDeviationCm"), Run.MaxDeviationCm);
		Object->SetBoolField(TEXT("complete"), Run.bComplete);
		ShotValues.Add(MakeShared<FJsonValueObject>(Object));
	}

	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("complete"), Error.IsEmpty() && bAllComplete);
	Root->SetStringField(TEXT("error"), Error);
	Root->SetNumberField(TEXT("sampleRate"), Config.SampleRate);
	Root->SetNumberField(TEXT("captureEvery"), Config.CaptureEvery);
	Root->SetNumberField(TEXT("prerollFrames"), Config.PrerollFrames);
	Root->SetBoolField(TEXT("fixedTimeStep"), FApp::UseFixedTimeStep());
	Root->SetNumberField(TEXT("fixedDeltaTime"), FApp::GetFixedDeltaTime());
	Root->SetNumberField(TEXT("maxDeviationToleranceCm"), Config.MaxDeviationCm);
	Root->SetArrayField(TEXT("screenshotSize"), LiminalDebugShared::ToJsonArray({ static_cast<double>(ScreenshotSize.X), static_cast<double>(ScreenshotSize.Y) }));
	Root->SetArrayField(TEXT("shots"), ShotValues);

	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	if (!FJsonSerializer::Serialize(Root, Writer))
	{
		return false;
	}
	return FFileHelper::SaveStringToFile(Json, *FPaths::Combine(Config.OutputDir, TEXT("film.json")),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
