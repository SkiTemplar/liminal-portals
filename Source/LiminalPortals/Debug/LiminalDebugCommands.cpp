#include "Debug/LiminalDebugCommands.h"

#include "Debug/LiminalBench.h"
#include "Debug/LiminalCalibration.h"
#include "Debug/LiminalSelfTest.h"
#include "Debug/LiminalTour.h"
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
	TUniquePtr<FLiminalSelfTestRun> GSelfTestRun;
	TUniquePtr<FLiminalTourRun> GTourRun;
	TUniquePtr<FLiminalBenchRun> GBenchRun;

	constexpr int32 MaxSelfTestCycles = 1000;
	/** Mínimos de Liminal.Bench fijados por el plan (E5) y tope para no dejar la ejecución colgada. */
	constexpr int32 MinBenchMeasureFrames = 120;
	constexpr int32 MinBenchWarmupFrames = 60;
	constexpr int32 MaxBenchFrames = 5000;

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

	void RunSelfTestCommand(const TArray<FString>& Args, UWorld* World)
	{
		int32 Cycles = FLiminalSelfTestRun::FConfig().CyclesPerDoor;
		if (!Args.IsEmpty() && !LexTryParseString(Cycles, *Args[0]))
		{
			UE_LOG(LogLiminalPortals, Error, TEXT("Liminal.SelfTest: '%s' no es un número de ciclos"), *Args[0]);
			return;
		}
		FString Error;
		if (!LiminalDebugCommands::StartSelfTest(World, Cycles, Error))
		{
			UE_LOG(LogLiminalPortals, Error, TEXT("Liminal.SelfTest: %s"), *Error);
		}
	}

	void RunTourCommand(const TArray<FString>& Args, UWorld* World)
	{
		FString Error;
		if (!LiminalDebugCommands::StartTour(World, Error))
		{
			UE_LOG(LogLiminalPortals, Error, TEXT("Liminal.Tour: %s"), *Error);
		}
	}

	void RunBenchCommand(const TArray<FString>& Args, UWorld* World)
	{
		const FLiminalBenchRun::FConfig Defaults;
		int32 MeasureFrames = Defaults.MeasureFrames;
		int32 WarmupFrames = Defaults.WarmupFrames;
		const bool bParsed = (Args.Num() < 1 || LexTryParseString(MeasureFrames, *Args[0]))
			&& (Args.Num() < 2 || LexTryParseString(WarmupFrames, *Args[1]));
		if (!bParsed)
		{
			UE_LOG(LogLiminalPortals, Error, TEXT("Liminal.Bench: uso Liminal.Bench [FotogramasMedidos] [FotogramasDeCalentamiento]"));
			return;
		}
		FString Error;
		if (!LiminalDebugCommands::StartBench(World, MeasureFrames, WarmupFrames, Error))
		{
			UE_LOG(LogLiminalPortals, Error, TEXT("Liminal.Bench: %s"), *Error);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs BenchCommand(
		TEXT("Liminal.Bench"),
		TEXT("Mide en L_Hub el tiempo de GPU, render thread y game thread con 0, 1, 2 y 4 portales visibles y con los ")
		TEXT("portales apagados; escribe Saved/Bench.json y sale. Uso: Liminal.Bench [Medidos >= 120] [Calentamiento >= 60]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunBenchCommand));

	FAutoConsoleCommandWithWorldAndArgs SelfTestCommand(
		TEXT("Liminal.SelfTest"),
		TEXT("Cruza cada puerta del hub ida y vuelta N veces (20 por defecto) con entrada simulada y comprueba ")
		TEXT("teletransporte, velocidad y cámara; escribe Saved/SelfTest.json y sale. Uso: Liminal.SelfTest [Ciclos]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunSelfTestCommand));

	FAutoConsoleCommandWithWorldAndArgs TourCommand(
		TEXT("Liminal.Tour"),
		TEXT("Capturas del hub y de cada puerta (desde el hub y desde el bioma) en Saved/Tour/ y sale."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunTourCommand));

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

	bool StartSelfTest(UWorld* World, int32 CyclesPerDoor, FString& OutError)
	{
		if (GSelfTestRun && !GSelfTestRun->IsFinished())
		{
			OutError = TEXT("ya hay una prueba en curso");
			return false;
		}
		if (CyclesPerDoor <= 0 || CyclesPerDoor > MaxSelfTestCycles)
		{
			OutError = FString::Printf(TEXT("ciclos fuera de rango (1-%d): %d"), MaxSelfTestCycles, CyclesPerDoor);
			return false;
		}
		UWorld* GameWorld = ResolveGameWorld(World);
		if (!GameWorld)
		{
			OutError = TEXT("no hay mundo de juego");
			return false;
		}

		FLiminalSelfTestRun::FConfig Config;
		Config.OutputPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SelfTest.json")));
		Config.CyclesPerDoor = CyclesPerDoor;

		GSelfTestRun.Reset();
		TUniquePtr<FLiminalSelfTestRun> Run = MakeUnique<FLiminalSelfTestRun>(GameWorld, Config);
		if (!Run->Start(OutError))
		{
			return false;
		}
		GSelfTestRun = MoveTemp(Run);
		return true;
	}

	bool StartTour(UWorld* World, FString& OutError)
	{
		if (GTourRun && !GTourRun->IsFinished())
		{
			OutError = TEXT("ya hay un recorrido en curso");
			return false;
		}
		UWorld* GameWorld = ResolveGameWorld(World);
		if (!GameWorld)
		{
			OutError = TEXT("no hay mundo de juego");
			return false;
		}

		FLiminalTourRun::FConfig Config;
		Config.OutputDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Tour")));

		GTourRun.Reset();
		TUniquePtr<FLiminalTourRun> Run = MakeUnique<FLiminalTourRun>(GameWorld, Config);
		if (!Run->Start(OutError))
		{
			return false;
		}
		GTourRun = MoveTemp(Run);
		return true;
	}

	bool StartBench(UWorld* World, int32 MeasureFrames, int32 WarmupFrames, FString& OutError)
	{
		if (GBenchRun && !GBenchRun->IsFinished())
		{
			OutError = TEXT("ya hay una medida en curso");
			return false;
		}
		if (MeasureFrames < MinBenchMeasureFrames || MeasureFrames > MaxBenchFrames
			|| WarmupFrames < MinBenchWarmupFrames || WarmupFrames > MaxBenchFrames)
		{
			OutError = FString::Printf(TEXT("fotogramas fuera de rango: medidos %d (%d-%d), calentamiento %d (%d-%d)"),
				MeasureFrames, MinBenchMeasureFrames, MaxBenchFrames, WarmupFrames, MinBenchWarmupFrames, MaxBenchFrames);
			return false;
		}
		UWorld* GameWorld = ResolveGameWorld(World);
		if (!GameWorld)
		{
			OutError = TEXT("no hay mundo de juego");
			return false;
		}

		FLiminalBenchRun::FConfig Config;
		Config.OutputPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Bench.json")));
		Config.MeasureFrames = MeasureFrames;
		Config.WarmupFrames = WarmupFrames;
		Config.MaxTotalFrames = 5 * (2 * MeasureFrames + WarmupFrames + 600) + Config.FirstCaseExtraWarmup + 1000;

		GBenchRun.Reset();
		TUniquePtr<FLiminalBenchRun> Run = MakeUnique<FLiminalBenchRun>(GameWorld, Config);
		if (!Run->Start(OutError))
		{
			return false;
		}
		GBenchRun = MoveTemp(Run);
		return true;
	}
}
