#include "Debug/LiminalTour.h"

#include "Debug/LiminalDebugShared.h"
#include "Debug/LiminalDebugWorld.h"
#include "Dom/JsonObject.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "LiminalPortals.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Player/LiminalCharacter.h"
#include "Portal/LiminalPortal.h"
#include "Portal/LiminalPortalSubsystem.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"
#include "World/LiminalHub.h"

namespace
{
	/** Fotogramas que se espera a que llegue una captura pedida antes de abortar. */
	constexpr int32 MaxScreenshotWaitFrames = 10;
	/** La vista general se toma desde 1,2 m del fondo de la sala, mirando a lo largo de ella. */
	constexpr double OverviewInsetCm = 120.0;
	constexpr double OverviewPitch = -6.0;

	FRotator LookAt(const FVector& From, const FVector& To)
	{
		FRotator Rotation = (To - From).Rotation();
		Rotation.Roll = 0.0;
		return Rotation;
	}
}

FLiminalTourRun::FLiminalTourRun(UWorld* InWorld, const FConfig& InConfig)
	: Config(InConfig)
	, World(InWorld)
{
}

FLiminalTourRun::~FLiminalTourRun()
{
	if (Phase != EPhase::Finished)
	{
		Unbind();
	}
}

bool FLiminalTourRun::Start(FString& OutError)
{
	UWorld* GameWorld = World.Get();
	ULiminalPortalSubsystem* PortalSubsystem = GameWorld ? GameWorld->GetSubsystem<ULiminalPortalSubsystem>() : nullptr;
	if (!PortalSubsystem)
	{
		OutError = TEXT("no hay mundo de juego con ULiminalPortalSubsystem");
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
	PortalsUpdatedHandle = PortalSubsystem->OnPortalsUpdated.AddRaw(this, &FLiminalTourRun::OnPortalsUpdated);
	ScreenshotHandle = UGameViewportClient::OnScreenshotCaptured().AddRaw(this, &FLiminalTourRun::OnScreenshotCaptured);
	WatchdogHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FLiminalTourRun::OnWatchdogTick));
	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddRaw(this, &FLiminalTourRun::OnWorldCleanup);
	UE_LOG(LogLiminalPortals, Display, TEXT("Liminal.Tour: salida %s, %d fotogramas por punto"), *Config.OutputDir, Config.SettleFrames);
	return true;
}

bool FLiminalTourRun::OnWatchdogTick(float DeltaTime)
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

void FLiminalTourRun::OnWorldCleanup(UWorld* CleanedWorld, bool bSessionEnded, bool bCleanupResources)
{
	if (CleanedWorld == World.Get() && Phase != EPhase::Finished)
	{
		Finish(TEXT("el mundo se destruyó durante el recorrido"));
	}
}

void FLiminalTourRun::OnPortalsUpdated(const FMinimalViewInfo& View)
{
	TicksWithoutUpdate = 0;
	if (Phase == EPhase::Finished)
	{
		return;
	}
	if (!ScreenshotError.IsEmpty())
	{
		Finish(ScreenshotError);
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
	case EPhase::Settle:
		if (++PhaseFrames >= Config.SettleFrames)
		{
			FShot& Shot = Shots[CurrentShot];
			Shot.ViewLocation = View.Location;
			Shot.ViewRotation = View.Rotation;
			PendingScreenshotPath = Shot.Path;
			FScreenshotRequest::RequestScreenshot(false);
			Phase = EPhase::WaitScreenshot;
			PhaseFrames = 0;
		}
		break;
	case EPhase::WaitScreenshot:
		if (PendingScreenshotPath.IsEmpty())
		{
			Shots[CurrentShot].bSaved = true;
			if (CurrentShot + 1 < Shots.Num())
			{
				BeginShot(CurrentShot + 1);
			}
			else
			{
				Finish(FString());
			}
		}
		else if (++PhaseFrames > MaxScreenshotWaitFrames)
		{
			Finish(FString::Printf(TEXT("no llegó la captura %s"), *PendingScreenshotPath));
		}
		break;
	default:
		break;
	}
}

void FLiminalTourRun::UpdateWaitPawn()
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
	BeginShot(0);
}

bool FLiminalTourRun::BuildShots(FString& OutError)
{
	TArray<FLiminalDoorPair> Pairs;
	ALiminalHub* Hub = nullptr;
	if (!LiminalDebugWorld::FindDoorPairs(World.Get(), Pairs, Hub, OutError))
	{
		return false;
	}
	const ALiminalCharacter* Pawn = Character.Get();

	auto AddShot = [this](const FString& Name, const FVector& Feet, const FRotator& Rotation)
	{
		FShot& Shot = Shots.AddDefaulted_GetRef();
		Shot.Name = FString::Printf(TEXT("%02d_%s"), Shots.Num() - 1, *Name);
		Shot.Feet = Feet;
		Shot.Rotation = Rotation;
		Shot.Path = FPaths::Combine(Config.OutputDir, Shot.Name + TEXT(".png"));
	};

	const FTransform HubTransform(Hub->GetActorQuat(), Hub->GetActorLocation());
	const FVector OverviewFeet = HubTransform.TransformPositionNoScale(FVector(-Hub->InteriorSize.X * 0.5 + OverviewInsetCm, 0.0, 0.0));
	AddShot(TEXT("hub_overview"), OverviewFeet, FRotator(OverviewPitch, HubTransform.Rotator().Yaw, 0.0));

	for (const FLiminalDoorPair& Pair : Pairs)
	{
		const ALiminalPortal* HubPortal = Pair.HubPortal.Get();
		const ALiminalPortal* BiomePortal = Pair.BiomePortal.Get();
		const FVector HubFeet = HubPortal->GetPortalTransform().TransformPositionNoScale(FVector(Config.HubViewDistance, 0.0, 0.0));
		AddShot(FString::Printf(TEXT("door%d_%s_from_hub"), Pair.DoorIndex, *Pair.Label), HubFeet,
			LookAt(LiminalDebugWorld::GetEyeLocation(Pawn, HubFeet), HubPortal->GetPlaneTransform().GetLocation()));

		const FVector BiomeFeet = BiomePortal->GetPortalTransform().TransformPositionNoScale(FVector(Config.BiomeViewDistance, 0.0, 0.0));
		AddShot(FString::Printf(TEXT("door%d_%s_from_biome"), Pair.DoorIndex, *Pair.Label), BiomeFeet,
			LookAt(LiminalDebugWorld::GetEyeLocation(Pawn, BiomeFeet), BiomePortal->GetPlaneTransform().GetLocation()));
	}
	return true;
}

void FLiminalTourRun::BeginShot(int32 Index)
{
	CurrentShot = Index;
	PhaseFrames = 0;
	Phase = EPhase::Settle;
	const FShot& Shot = Shots[Index];
	LiminalDebugWorld::PlacePlayer(PlayerController.Get(), Character.Get(), Shot.Feet, Shot.Rotation);
	UE_LOG(LogLiminalPortals, Display, TEXT("Liminal.Tour: %s"), *Shot.Name);
}

void FLiminalTourRun::OnScreenshotCaptured(int32 Width, int32 Height, const TArray<FColor>& Colors)
{
	if (PendingScreenshotPath.IsEmpty())
	{
		return;
	}
	const FString Path = MoveTemp(PendingScreenshotPath);
	PendingScreenshotPath.Reset();

	if (Width <= 0 || Height <= 0 || Colors.Num() != Width * Height)
	{
		ScreenshotError = FString::Printf(TEXT("captura vacía o incoherente (%dx%d, %d píxeles)"), Width, Height, Colors.Num());
		return;
	}
	ScreenshotSize = FIntPoint(Width, Height);
	const FImageView Image(Colors.GetData(), Width, Height, EGammaSpace::sRGB);
	if (!FImageUtils::SaveImageByExtension(*Path, Image))
	{
		ScreenshotError = FString::Printf(TEXT("no se pudo guardar %s"), *Path);
	}
}

void FLiminalTourRun::Finish(const FString& Error)
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

	const bool bWritten = WriteReport(Error);
	if (Error.IsEmpty() && bWritten)
	{
		UE_LOG(LogLiminalPortals, Display, TEXT("Liminal.Tour: %d capturas en %s"), Shots.Num(), *Config.OutputDir);
	}
	else
	{
		UE_LOG(LogLiminalPortals, Error, TEXT("Liminal.Tour: %s"), Error.IsEmpty() ? TEXT("no se pudo escribir el índice") : *Error);
	}
	if (!GIsEditor)
	{
		FPlatformMisc::RequestExit(false, TEXT("Liminal.Tour"));
	}
}

void FLiminalTourRun::Unbind()
{
	if (ULiminalPortalSubsystem* PortalSubsystem = Subsystem.Get())
	{
		PortalSubsystem->OnPortalsUpdated.Remove(PortalsUpdatedHandle);
	}
	UGameViewportClient::OnScreenshotCaptured().Remove(ScreenshotHandle);
	FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
	if (WatchdogHandle.IsValid())
	{
		FTSTicker::RemoveTicker(WatchdogHandle);
		WatchdogHandle.Reset();
	}
}

bool FLiminalTourRun::WriteReport(const FString& Error) const
{
	int32 Saved = 0;
	TArray<TSharedPtr<FJsonValue>> ShotValues;
	for (const FShot& Shot : Shots)
	{
		Saved += Shot.bSaved ? 1 : 0;
		const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("name"), Shot.Name);
		Object->SetStringField(TEXT("file"), FPaths::GetCleanFilename(Shot.Path));
		Object->SetBoolField(TEXT("saved"), Shot.bSaved);
		Object->SetArrayField(TEXT("viewLocation"), LiminalDebugShared::ToJsonArray({ Shot.ViewLocation.X, Shot.ViewLocation.Y, Shot.ViewLocation.Z }));
		Object->SetArrayField(TEXT("viewRotation"), LiminalDebugShared::ToJsonArray({ Shot.ViewRotation.Pitch, Shot.ViewRotation.Yaw, Shot.ViewRotation.Roll }));
		ShotValues.Add(MakeShared<FJsonValueObject>(Object));
	}

	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("complete"), Error.IsEmpty() && !Shots.IsEmpty() && Saved == Shots.Num());
	Root->SetStringField(TEXT("error"), Error);
	Root->SetNumberField(TEXT("settleFrames"), Config.SettleFrames);
	Root->SetArrayField(TEXT("screenshotSize"), LiminalDebugShared::ToJsonArray({ static_cast<double>(ScreenshotSize.X), static_cast<double>(ScreenshotSize.Y) }));
	Root->SetArrayField(TEXT("shots"), ShotValues);

	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	if (!FJsonSerializer::Serialize(Root, Writer))
	{
		return false;
	}
	return FFileHelper::SaveStringToFile(Json, *FPaths::Combine(Config.OutputDir, TEXT("tour.json")),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
