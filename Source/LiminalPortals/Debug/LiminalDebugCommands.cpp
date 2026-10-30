#include "Debug/LiminalDebugCommands.h"

#include "Debug/LiminalBench.h"
#include "Debug/LiminalCalibration.h"
#include "Debug/LiminalSelfTest.h"
#include "Debug/LiminalFilm.h"
#include "Debug/LiminalTour.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/App.h"
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
	TUniquePtr<FLiminalFilmRun> GFilmRun;
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

	/** Código de salida del juego cuando una orden Liminal.* no puede empezar. */
	constexpr uint8 StartFailureExitCode = 2;

	/**
	 * Registra el error de arranque de una orden y, en un juego desatendido (-unattended, como lo lanza
	 * verify.ps1), lo cierra con un código distinto de 0: solo Finish() de cada ejecución pide salir, y
	 * sin esto verify.ps1 esperaría a su tiempo máximo con el juego parado en el mapa. Si ya hay una
	 * ejecución en curso no se cierra: la que está en marcha terminará y saldrá sola.
	 * La salida es forzada (vacía el log y termina el proceso) porque en UE 5.6 la salida ordenada
	 * descarta el código: GuardedMain devuelve 0 aunque se pida otro con RequestExitWithStatus.
	 */
	void FailCommandStart(const TCHAR* Command, const FString& Error, bool bRunInProgress)
	{
		UE_LOG(LogLiminalPortals, Error, TEXT("%s: %s"), Command, *Error);
		if (!GIsEditor && FApp::IsUnattended() && !bRunInProgress)
		{
			FPlatformMisc::RequestExitWithStatus(true, StartFailureExitCode, Command);
		}
	}

	template <typename RunType>
	bool IsInProgress(const TUniquePtr<RunType>& Run)
	{
		return Run && !Run->IsFinished();
	}

	void RunCalibrateCommand(const TArray<FString>& Args, UWorld* World)
	{
		const FString Tag = Args.IsEmpty() ? FString() : Args[0];
		FString Error;
		const bool bInProgress = IsInProgress(GCalibrationRun);
		if (!LiminalDebugCommands::StartCalibration(World, Tag, Error))
		{
			FailCommandStart(TEXT("Liminal.Calibrate"), Error, bInProgress);
		}
	}

	void RunSelfTestCommand(const TArray<FString>& Args, UWorld* World)
	{
		int32 Cycles = FLiminalSelfTestRun::FConfig().CyclesPerDoor;
		if (!Args.IsEmpty() && !LexTryParseString(Cycles, *Args[0]))
		{
			FailCommandStart(TEXT("Liminal.SelfTest"), FString::Printf(TEXT("'%s' no es un número de ciclos"), *Args[0]),
				IsInProgress(GSelfTestRun));
			return;
		}
		FString Error;
		const bool bInProgress = IsInProgress(GSelfTestRun);
		if (!LiminalDebugCommands::StartSelfTest(World, Cycles, Error))
		{
			FailCommandStart(TEXT("Liminal.SelfTest"), Error, bInProgress);
		}
	}

	void RunTourCommand(const TArray<FString>& Args, UWorld* World)
	{
		FString Error;
		const bool bInProgress = IsInProgress(GTourRun);
		if (!LiminalDebugCommands::StartTour(World, Error))
		{
			FailCommandStart(TEXT("Liminal.Tour"), Error, bInProgress);
		}
	}

	void RunFilmCommand(const TArray<FString>& Args, UWorld* World)
	{
		// «*» o sin argumento: todos los planos.
		const FString Filter = (Args.IsEmpty() || Args[0] == TEXT("*")) ? FString() : Args[0];
		int32 CaptureEvery = FLiminalFilmRun::FConfig().CaptureEvery;
		if (Args.Num() >= 2 && (!LexTryParseString(CaptureEvery, *Args[1]) || CaptureEvery < 1))
		{
			FailCommandStart(TEXT("Liminal.Film"), TEXT("uso Liminal.Film [Filtro|*] [UnoDeCada >= 1]"), IsInProgress(GFilmRun));
			return;
		}
		FString Error;
		const bool bInProgress = IsInProgress(GFilmRun);
		if (!LiminalDebugCommands::StartFilm(World, Filter, CaptureEvery, Error))
		{
			FailCommandStart(TEXT("Liminal.Film"), Error, bInProgress);
		}
	}

	void RunBenchCommand(
const TArray<FString>& Args, UWorld* World)
	{
		const FLiminalBenchRun::FConfig Defaults;
		int32 MeasureFrames = Defaults.MeasureFrames;
		int32 WarmupFrames = Defaults.WarmupFrames;
		const bool bParsed = (Args.Num() < 1 || LexTryParseString(MeasureFrames, *Args[0]))
			&& (Args.Num() < 2 || LexTryParseString(WarmupFrames, *Args[1]))
			&& (Args.Num() < 3 || Args[2].Equals(TEXT("profile"), ESearchCase::IgnoreCase)
				|| Args[2].Equals(TEXT("cross"), ESearchCase::IgnoreCase));
		const bool bCrossOnly = Args.Num() >= 3 && Args[2].Equals(TEXT("cross"), ESearchCase::IgnoreCase);
		const bool bProfile = Args.Num() >= 3 && !bCrossOnly;
		if (!bParsed)
		{
			FailCommandStart(TEXT("Liminal.Bench"), TEXT("uso Liminal.Bench [FotogramasMedidos] [FotogramasDeCalentamiento] [profile|cross]"),
				IsInProgress(GBenchRun));
			return;
		}
		FString Error;
		const bool bInProgress = IsInProgress(GBenchRun);
		if (!LiminalDebugCommands::StartBench(World, MeasureFrames, WarmupFrames, bProfile, Error, bCrossOnly))
		{
			FailCommandStart(TEXT("Liminal.Bench"), Error, bInProgress);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs BenchCommand(
		TEXT("Liminal.Bench"),
		TEXT("Mide en L_Hub el tiempo de GPU, render thread y game thread con 0, 1, 2 y 4 portales visibles y con los ")
		TEXT("portales apagados; escribe Saved/Bench.json y sale. Con profile, también el desglose de GPU por pase y el ")
		TEXT("recorrido del hub en Saved/BenchProfile.json. Con cross, solo los cruces por las puertas y el giro tras cada uno ")
		TEXT("en Saved/BenchCross.json. Uso: Liminal.Bench [Medidos >= 120] [Calentamiento >= 60] [profile|cross]"),
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

	FAutoConsoleCommandWithWorldAndArgs FilmCommand(
		TEXT("Liminal.Film"),
		TEXT("Rueda los planos del vídeo del proyecto (recorrido de cámara determinista con cruces reales) y guarda ")
		TEXT("los fotogramas en Saved/Film/<Plano>/ con el índice Saved/Film/film.json; después sale. ")
		TEXT("Uso: Liminal.Film [Filtro|*] [UnoDeCada >= 1]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunFilmCommand));

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

	bool StartFilm(UWorld* World, const FString& ShotFilter, int32 CaptureEvery, FString& OutError)
	{
		if (GFilmRun && !GFilmRun->IsFinished())
		{
			OutError = TEXT("ya hay un rodaje en curso");
			return false;
		}
		UWorld* GameWorld = ResolveGameWorld(World);
		if (!GameWorld)
		{
			OutError = TEXT("no hay mundo de juego");
			return false;
		}

		FLiminalFilmRun::FConfig Config;
		Config.OutputDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Film")));
		Config.ShotFilter = ShotFilter;
		Config.CaptureEvery = CaptureEvery;

		GFilmRun.Reset();
		TUniquePtr<FLiminalFilmRun> Run = MakeUnique<FLiminalFilmRun>(GameWorld, Config);
		if (!Run->Start(OutError))
		{
			return false;
		}
		GFilmRun = MoveTemp(Run);
		return true;
	}

	bool StartBench(UWorld* World, int32 MeasureFrames
, int32 WarmupFrames, bool bProfile, FString& OutError, bool bCrossOnly)
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
		Config.bProfile = bProfile;
		Config.ProfileOutputPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BenchProfile.json")));
		Config.bCrossOnly = bCrossOnly;
		Config.CrossOutputPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BenchCross.json")));
		// Cruces: cuatro puertas con la espera a 300 fps como mucho, la caminata, la ventana posterior y el giro.
		const int32 CrossingFrames = 4 * FLiminalBenchCrossing::Passes * (static_cast<int32>(FLiminalBenchCrossing::HoldSeconds * 300.0)
			+ FLiminalBenchCrossing::MaxWalkFrames + FLiminalBenchCrossing::WindowAfter + FLiminalBenchCrossing::TurnWindow + 50);
		if (bCrossOnly)
		{
			Config.MaxTotalFrames = CrossingFrames + 1000;
		}
		else if (bProfile)
		{
			// Recorrido: todas las poses de LiminalBenchMath::MakeHubWalkPoses, HoldFrames fotogramas cada una.
			Config.MaxTotalFrames += LiminalBenchMath::MakeHubWalkPoses().Num() * FLiminalBenchWalk::HoldFrames + 100;
			Config.MaxTotalFrames += CrossingFrames;
		}

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
