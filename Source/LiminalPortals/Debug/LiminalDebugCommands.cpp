#include "Debug/LiminalDebugCommands.h"

#include "Debug/LiminalCalibration.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "LiminalPortals.h"
#include "Misc/Paths.h"

namespace
{
	constexpr int32 MaxTagLength = 32;

	TAutoConsoleVariable<int32> CVarCalibrateSettleFrames(
		TEXT("Liminal.Calibrate.SettleFrames"),
		FLiminalCalibrationRun::FConfig().SettleFrames,
		TEXT("Fotogramas quietos antes de cada pasada de Liminal.Calibrate (convergencia de TSR y Lumen)."),
		ECVF_Default);

	/** Ejecución en curso o la última terminada (se sustituye al lanzar otra). */
	TUniquePtr<FLiminalCalibrationRun> GCalibrationRun;

	/** El mundo que pasa la consola puede ser nulo con -ExecCmds: se busca el primer mundo de juego. */
	UWorld* ResolveGameWorld(UWorld* World)
	{
		if (World && World->IsGameWorld())
		{
			return World;
		}
		if (!GEngine)
		{
			return nullptr;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* Candidate = Context.World();
			if (Candidate && Candidate->IsGameWorld())
			{
				return Candidate;
			}
		}
		return nullptr;
	}

	void RunCalibrateCommand(const TArray<FString>& Args, UWorld* World)
	{
		const FString Tag = Args.IsEmpty() ? FString() : Args[0];
		FString Error;
		if (!LiminalDebugCommands::StartCalibration(World, Tag, Error))
		{
			UE_LOG(LogLiminalPortals, Error, TEXT("Liminal.Calibrate: %s"), *Error);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs CalibrateCommand(
		TEXT("Liminal.Calibrate"),
		TEXT("Calibración del portal: gira la cámara 10° por fotograma delante del portal en dos pasadas ")
		TEXT("(superficie visible y oculta), guarda capturas y máscara en Saved/Calibration[/Etiqueta] y sale. ")
		TEXT("Uso: Liminal.Calibrate [Etiqueta]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunCalibrateCommand));
}

namespace LiminalDebugCommands
{
	bool IsValidOutputTag(const FString& Tag)
	{
		if (Tag.Len() > MaxTagLength)
		{
			return false;
		}
		for (const TCHAR Character : Tag)
		{
			const bool bAllowed = FChar::IsAlnum(Character) || Character == TEXT('_') || Character == TEXT('-');
			if (!bAllowed)
			{
				return false;
			}
		}
		return true;
	}

	bool StartCalibration(UWorld* World, const FString& Tag, FString& OutError)
	{
		if (GCalibrationRun && !GCalibrationRun->IsFinished())
		{
			OutError = TEXT("ya hay una calibración en curso");
			return false;
		}
		if (!IsValidOutputTag(Tag))
		{
			OutError = FString::Printf(TEXT("etiqueta no válida '%s' (letras, dígitos, '_' o '-', máximo %d)"), *Tag, MaxTagLength);
			return false;
		}

		UWorld* GameWorld = ResolveGameWorld(World);
		if (!GameWorld)
		{
			OutError = TEXT("no hay mundo de juego");
			return false;
		}

		FLiminalCalibrationRun::FConfig Config;
		Config.OutputDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Calibration"), Tag));
		Config.SettleFrames = FMath::Clamp(CVarCalibrateSettleFrames.GetValueOnGameThread(), 1, 1000);
		Config.MaxTotalFrames = FMath::Max(Config.MaxTotalFrames, 4 * Config.SettleFrames + 2000);

		GCalibrationRun.Reset();
		TUniquePtr<FLiminalCalibrationRun> Run = MakeUnique<FLiminalCalibrationRun>(GameWorld, Config);
		if (!Run->Start(OutError))
		{
			return false;
		}
		GCalibrationRun = MoveTemp(Run);
		return true;
	}
}
