#include "Debug/LiminalCalibration.h"

#include "Debug/LiminalCalibrationMath.h"
#include "Debug/LiminalDebugShared.h"
#include "Dom/JsonObject.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "LiminalPortals.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Player/LiminalCharacter.h"
#include "Portal/LiminalPortal.h"
#include "Portal/LiminalPortalMath.h"
#include "Portal/LiminalPortalSubsystem.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"

namespace
{
	const TCHAR* const PassNames[2] = { TEXT("visible"), TEXT("hidden") };
	constexpr int32 NumPasses = 2;
	/** Tolerancia al comparar la rotación de la vista con la esperada, en grados. */
	constexpr double RotationTolerance = 0.01;

	int32 GetIntCVar(const TCHAR* Name, int32 Fallback)
	{
		const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
		return Variable ? Variable->GetInt() : Fallback;
	}

	float GetFloatCVar(const TCHAR* Name, float Fallback)
	{
		const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
		return Variable ? Variable->GetFloat() : Fallback;
	}
}

FLiminalCalibrationRun::FLiminalCalibrationRun(UWorld* InWorld, const FConfig& InConfig)
	: Config(InConfig)
	, World(InWorld)
{
}

FLiminalCalibrationRun::~FLiminalCalibrationRun()
{
	// Sin informe ni salida: solo se sueltan los delegados si la ejecución no terminó.
	if (Phase != EPhase::Finished)
	{
		Unbind();
	}
}

bool FLiminalCalibrationRun::Start(FString& OutError)
{
	UWorld* GameWorld = World.Get();
	ULiminalPortalSubsystem* PortalSubsystem = GameWorld ? GameWorld->GetSubsystem<ULiminalPortalSubsystem>() : nullptr;
	if (!PortalSubsystem)
	{
		OutError = TEXT("no hay mundo de juego con ULiminalPortalSubsystem");
		return false;
	}

	YawSweep = LiminalCalibrationMath::MakeYawSweep(Config.StepDegrees, Config.AmplitudeDegrees, Config.FramesPerPass);
	if (YawSweep.IsEmpty())
	{
		OutError = TEXT("configuración de barrido no válida");
		return false;
	}

	IFileManager& FileManager = IFileManager::Get();
	for (const TCHAR* PassName : PassNames)
	{
		const FString PassDir = FPaths::Combine(Config.OutputDir, PassName);
		FileManager.DeleteDirectory(*PassDir, false, true);
		if (!FileManager.MakeDirectory(*PassDir, true))
		{
			OutError = FString::Printf(TEXT("no se pudo crear %s"), *PassDir);
			return false;
		}
	}
	FileManager.Delete(*FPaths::Combine(Config.OutputDir, TEXT("calibration.json")), false, true, true);

	Subsystem = PortalSubsystem;
	PortalsUpdatedHandle = PortalSubsystem->OnPortalsUpdated.AddRaw(this, &FLiminalCalibrationRun::OnPortalsUpdated);
	ScreenshotHandle = UGameViewportClient::OnScreenshotCaptured().AddRaw(this, &FLiminalCalibrationRun::OnScreenshotCaptured);
	WatchdogHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FLiminalCalibrationRun::OnWatchdogTick));
	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddRaw(this, &FLiminalCalibrationRun::OnWorldCleanup);

	UE_LOG(LogLiminalPortals, Display, TEXT("Liminal.Calibrate: %d fotogramas por pasada, paso %.1f°, LagFrames %d, salida %s"),
		YawSweep.Num(), Config.StepDegrees, GetIntCVar(TEXT("Liminal.Portal.LagFrames"), 0), *Config.OutputDir);
	return true;
}

bool FLiminalCalibrationRun::OnWatchdogTick(float DeltaTime)
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

void FLiminalCalibrationRun::OnWorldCleanup(UWorld* CleanedWorld, bool bSessionEnded, bool bCleanupResources)
{
	if (CleanedWorld == World.Get() && Phase != EPhase::Finished)
	{
		Finish(TEXT("el mundo se destruyó durante la calibración"));
	}
}

void FLiminalCalibrationRun::OnPortalsUpdated(const FMinimalViewInfo& View)
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
		UpdateWaitPawn(View);
		break;
	case EPhase::Settle:
		if (++PhaseFrames >= Config.SettleFrames)
		{
			// La rotación del paso 0 ya está aplicada desde BeginPass: este fotograma es el primero.
			Phase = EPhase::Sweep;
			UpdateSweep(View);
		}
		break;
	case EPhase::Sweep:
		UpdateSweep(View);
		break;
	case EPhase::SwitchPass:
		UpdateSwitchPass();
		break;
	default:
		break;
	}
}

void FLiminalCalibrationRun::UpdateWaitPawn(const FMinimalViewInfo& View)
{
	UWorld* GameWorld = World.Get();
	APlayerController* Controller = GameWorld ? GameWorld->GetFirstPlayerController() : nullptr;
	const ALiminalCharacter* Character = Controller ? Cast<ALiminalCharacter>(Controller->GetPawn()) : nullptr;
	if (!Character || !Character->GetCharacterMovement()->IsMovingOnGround())
	{
		PhaseFrames = 0;
		return;
	}
	if (++PhaseFrames < Config.GroundedFrames)
	{
		return;
	}

	ALiminalPortal* Found = FindCalibrationPortal(View.Location);
	if (!Found)
	{
		Finish(TEXT("no hay un portal enlazado, de entrada (no bExitOnly) y con la cámara delante"));
		return;
	}

	PlayerController = Controller;
	Portal = Found;
	PortalName = Found->GetName();
	BaseLocation = View.Location;
	BaseRotation = (Found->GetPlaneTransform().GetLocation() - View.Location).Rotation();
	BaseRotation.Roll = 0.0;

	// La secuencia es determinista: ni el ratón ni el teclado pueden mover la cámara.
	Controller->SetIgnoreLookInput(true);
	Controller->SetIgnoreMoveInput(true);
	BeginPass(0);
}

void FLiminalCalibrationRun::BeginPass(int32 PassIndex)
{
	CurrentPass = PassIndex;
	StepIndex = 0;
	PhaseFrames = 0;
	Phase = EPhase::Settle;
	if (ALiminalPortal* CalibrationPortal = Portal.Get())
	{
		CalibrationPortal->SetSurfaceHiddenInGame(PassIndex == 1);
	}
	ApplyStepRotation(0);
	UE_LOG(LogLiminalPortals, Display, TEXT("Liminal.Calibrate: pasada %s"), PassNames[PassIndex]);
}

void FLiminalCalibrationRun::ApplyStepRotation(int32 Step)
{
	if (APlayerController* Controller = PlayerController.Get())
	{
		Controller->SetControlRotation(BaseRotation + FRotator(0.0, YawSweep[Step], 0.0));
	}
}

void FLiminalCalibrationRun::UpdateSweep(const FMinimalViewInfo& View)
{
	if (!PendingScreenshotPath.IsEmpty())
	{
		Finish(FString::Printf(TEXT("no llegó la captura de pantalla %s"), *PendingScreenshotPath));
		return;
	}

	const FRotator Expected = BaseRotation + FRotator(0.0, YawSweep[StepIndex], 0.0);
	if (!View.Rotation.Equals(Expected, RotationTolerance))
	{
		Finish(FString::Printf(TEXT("la vista del paso %d tiene rotación %s y se esperaba %s"),
			StepIndex, *View.Rotation.ToString(), *Expected.ToString()));
		return;
	}

	FFrameRecord Record;
	if (!RecordFrame(View, Record))
	{
		Finish(FString::Printf(TEXT("el portal no se proyecta entero en el paso %d"), StepIndex));
		return;
	}
	Frames.Add(MoveTemp(Record));
	MaxCameraDriftCm = FMath::Max(MaxCameraDriftCm, FVector::Dist(View.Location, BaseLocation));

	// Se resuelve en el Draw de este mismo fotograma (UGameViewportClient::ProcessScreenShots).
	PendingScreenshotPath = GetFramePath(CurrentPass, StepIndex);
	FScreenshotRequest::RequestScreenshot(false);

	++StepIndex;
	if (StepIndex < YawSweep.Num())
	{
		ApplyStepRotation(StepIndex);
	}
	else
	{
		Phase = EPhase::SwitchPass;
	}
}

void FLiminalCalibrationRun::UpdateSwitchPass()
{
	if (!PendingScreenshotPath.IsEmpty())
	{
		Finish(FString::Printf(TEXT("no llegó la captura de pantalla %s"), *PendingScreenshotPath));
		return;
	}
	if (CurrentPass + 1 < NumPasses)
	{
		BeginPass(CurrentPass + 1);
		return;
	}
	Finish(FString());
}

ALiminalPortal* FLiminalCalibrationRun::FindCalibrationPortal(const FVector& CameraLocation) const
{
	ALiminalPortal* Best = nullptr;
	double BestDistance = TNumericLimits<double>::Max();
	for (TActorIterator<ALiminalPortal> It(World.Get()); It; ++It)
	{
		ALiminalPortal* Candidate = *It;
		if (!Candidate->LinkedPortal || Candidate->bExitOnly)
		{
			continue;
		}
		const double Distance = LiminalPortalMath::SignedDistanceToPlane(Candidate->GetPlaneTransform(), CameraLocation);
		if (Distance > 0.0 && Distance < BestDistance)
		{
			Best = Candidate;
			BestDistance = Distance;
		}
	}
	return Best;
}

bool FLiminalCalibrationRun::RecordFrame(const FMinimalViewInfo& View, FFrameRecord& OutRecord)
{
	const ALiminalPortal* CalibrationPortal = Portal.Get();
	const APlayerController* Controller = PlayerController.Get();
	if (!CalibrationPortal || !Controller)
	{
		return false;
	}

	int32 SizeX = 0;
	int32 SizeY = 0;
	Controller->GetViewportSize(SizeX, SizeY);
	ViewportSize = FIntPoint(SizeX, SizeY);

	OutRecord.Pass = CurrentPass;
	OutRecord.Index = StepIndex;
	OutRecord.YawOffset = YawSweep[StepIndex];
	OutRecord.Rotation = View.Rotation;

	const auto Corners = LiminalCalibrationMath::GetPortalCorners(CalibrationPortal->GetPlaneTransform(),
		CalibrationPortal->GetHalfExtents());
	for (const FVector& Corner : Corners)
	{
		FVector2D Pixel;
		if (!LiminalCalibrationMath::ProjectToViewport(View.Location, View.Rotation, View.FOV, ViewportSize, Corner, Pixel))
		{
			return false;
		}
		OutRecord.Corners.Add(Pixel);

		// Contraste con la proyección del motor (misma caché de cámara de este fotograma).
		FVector2D EnginePixel;
		if (Controller->ProjectWorldLocationToScreen(Corner, EnginePixel, false))
		{
			MaxProjectionErrorPx = FMath::Max(MaxProjectionErrorPx, FVector2D::Distance(Pixel, EnginePixel));
		}
	}
	return true;
}

FString FLiminalCalibrationRun::GetFramePath(int32 PassIndex, int32 Step) const
{
	return FPaths::Combine(Config.OutputDir, PassNames[PassIndex], FString::Printf(TEXT("%03d.png"), Step));
}

void FLiminalCalibrationRun::OnScreenshotCaptured(int32 Width, int32 Height, const TArray<FColor>& Colors)
{
	if (PendingScreenshotPath.IsEmpty())
	{
		return;
	}
	const FString Path = MoveTemp(PendingScreenshotPath);
	PendingScreenshotPath.Reset();

	if (Width <= 0 || Height <= 0 || Colors.Num() != Width * Height)
	{
		ScreenshotError = FString::Printf(TEXT("captura de pantalla vacía o incoherente (%dx%d, %d píxeles)"), Width, Height, Colors.Num());
		return;
	}
	ScreenshotSize = FIntPoint(Width, Height);
	const FImageView Image(Colors.GetData(), Width, Height, EGammaSpace::sRGB);
	if (!FImageUtils::SaveImageByExtension(*Path, Image))
	{
		ScreenshotError = FString::Printf(TEXT("no se pudo guardar %s"), *Path);
		return;
	}
	++SavedScreenshots;
}

void FLiminalCalibrationRun::Finish(const FString& Error)
{
	if (Phase == EPhase::Finished)
	{
		return;
	}
	Phase = EPhase::Finished;
	Unbind();
	RestorePlayer();

	const bool bWritten = WriteReport(Error);
	if (Error.IsEmpty() && bWritten)
	{
		UE_LOG(LogLiminalPortals, Display, TEXT("Liminal.Calibrate: completada, %d capturas en %s"), SavedScreenshots, *Config.OutputDir);
	}
	else
	{
		UE_LOG(LogLiminalPortals, Error, TEXT("Liminal.Calibrate: %s"), Error.IsEmpty() ? TEXT("no se pudo escribir el informe") : *Error);
	}

	// En PIE no se cierra el editor; en -game la calibración termina con la aplicación.
	if (!GIsEditor)
	{
		FPlatformMisc::RequestExit(false, TEXT("Liminal.Calibrate"));
	}
}

void FLiminalCalibrationRun::Unbind()
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

void FLiminalCalibrationRun::RestorePlayer()
{
	if (ALiminalPortal* CalibrationPortal = Portal.Get())
	{
		CalibrationPortal->SetSurfaceHiddenInGame(false);
	}
	if (APlayerController* Controller = PlayerController.Get())
	{
		Controller->ResetIgnoreLookInput();
		Controller->ResetIgnoreMoveInput();
	}
}

bool FLiminalCalibrationRun::WriteReport(const FString& Error) const
{
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	const int32 ExpectedScreenshots = NumPasses * YawSweep.Num();
	Root->SetBoolField(TEXT("complete"), Error.IsEmpty() && SavedScreenshots == ExpectedScreenshots);
	Root->SetStringField(TEXT("error"), Error);
	Root->SetStringField(TEXT("portal"), PortalName);
	Root->SetNumberField(TEXT("lagFrames"), GetIntCVar(TEXT("Liminal.Portal.LagFrames"), -1));
	Root->SetNumberField(TEXT("flipClipPlane"), GetIntCVar(TEXT("Liminal.Portal.FlipClipPlane"), -1));
	Root->SetNumberField(TEXT("antiAliasingMethod"), GetIntCVar(TEXT("r.AntiAliasingMethod"), -1));
	Root->SetNumberField(TEXT("screenPercentage"), GetFloatCVar(TEXT("r.ScreenPercentage"), -1.f));
	// Experimentos de coste de E5 (D-30): escala y reflejos de la captura, y render target del último fotograma.
	Root->SetNumberField(TEXT("captureScale"), GetFloatCVar(TEXT("Liminal.Portal.CaptureScale"), -1.f));
	Root->SetNumberField(TEXT("captureReflectionMethod"), GetIntCVar(TEXT("Liminal.Portal.CaptureReflectionMethod"), -2));
	const FIntPoint CaptureSize = Subsystem.IsValid() ? Subsystem->GetLastCaptureSize() : FIntPoint::ZeroValue;
	Root->SetArrayField(TEXT("captureSize"), LiminalDebugShared::ToJsonArray({ static_cast<double>(CaptureSize.X), static_cast<double>(CaptureSize.Y) }));
	Root->SetNumberField(TEXT("stepDegrees"), Config.StepDegrees);
	Root->SetNumberField(TEXT("framesPerPass"), YawSweep.Num());
	Root->SetNumberField(TEXT("savedScreenshots"), SavedScreenshots);
	Root->SetNumberField(TEXT("maxProjectionErrorPx"), MaxProjectionErrorPx);
	Root->SetNumberField(TEXT("maxCameraDriftCm"), MaxCameraDriftCm);
	Root->SetArrayField(TEXT("viewport"), LiminalDebugShared::ToJsonArray({ static_cast<double>(ViewportSize.X), static_cast<double>(ViewportSize.Y) }));
	Root->SetArrayField(TEXT("screenshotSize"), LiminalDebugShared::ToJsonArray({ static_cast<double>(ScreenshotSize.X), static_cast<double>(ScreenshotSize.Y) }));

	TArray<TSharedPtr<FJsonValue>> FrameValues;
	for (const FFrameRecord& Frame : Frames)
	{
		const TSharedRef<FJsonObject> FrameObject = MakeShared<FJsonObject>();
		FrameObject->SetStringField(TEXT("pass"), PassNames[Frame.Pass]);
		FrameObject->SetNumberField(TEXT("index"), Frame.Index);
		FrameObject->SetNumberField(TEXT("yawOffset"), Frame.YawOffset);
		FrameObject->SetArrayField(TEXT("rotation"), LiminalDebugShared::ToJsonArray({ Frame.Rotation.Pitch, Frame.Rotation.Yaw, Frame.Rotation.Roll }));
		TArray<TSharedPtr<FJsonValue>> CornerValues;
		for (const FVector2D& Corner : Frame.Corners)
		{
			CornerValues.Add(MakeShared<FJsonValueArray>(LiminalDebugShared::ToJsonArray({ Corner.X, Corner.Y })));
		}
		FrameObject->SetArrayField(TEXT("corners"), CornerValues);
		FrameValues.Add(MakeShared<FJsonValueObject>(FrameObject));
	}
	Root->SetArrayField(TEXT("frames"), FrameValues);

	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	if (!FJsonSerializer::Serialize(Root, Writer))
	{
		return false;
	}
	const FString ReportPath = FPaths::Combine(Config.OutputDir, TEXT("calibration.json"));
	return FFileHelper::SaveStringToFile(Json, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
