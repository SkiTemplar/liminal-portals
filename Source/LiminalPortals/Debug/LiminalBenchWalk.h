#pragma once

#include "CoreMinimal.h"
#include "Debug/LiminalBenchMath.h"
#include "Debug/LiminalGpuProfile.h"

class ALiminalCharacter;
class APlayerController;
class FJsonObject;
class ULiminalPortalSubsystem;
class UWorld;

/**
 * Recorrido del modo perfil de Liminal.Bench (D-36): pasa por LiminalBenchMath::MakeHubWalkPoses, deja
 * cada pose HoldFrames fotogramas (para que converja la oclusión) y en el último anota cuántos portales
 * captura el subsistema, cuántos de esos tienen la superficie dibujada en la vista principal
 * (UPrimitiveComponent::GetLastRenderTimeOnScreen, lo que vería una prueba de oclusión del tipo
 * WasRecentlyRendered) y qué fracción del viewport ocupa el rectángulo de pantalla de cada uno
 * (lo que renderizaría una captura recortada con proyección fuera de eje).
 */
class FLiminalBenchWalk
{
public:
	/** Fotogramas en cada pose; se mide en el último. */
	static constexpr int32 HoldFrames = 10;
	/** Segundos de juego tras los que una superficie no dibujada se da por oculta. */
	static constexpr double RenderedToleranceSeconds = 0.1;

	void Begin(const FTransform& InHubTransform);
	bool IsFinished() const { return bFinished; }

	/** Un fotograma del recorrido; coloca al jugador al empezar cada pose. */
	void Tick(UWorld* World, APlayerController* Controller, ALiminalCharacter* Character, const ULiminalPortalSubsystem& Subsystem);
	void AddGpuSample(double Milliseconds) { GpuMs.Add(Milliseconds); }

	TSharedRef<FJsonObject> ToJson() const;

private:
	struct FSample
	{
		FString Name;
		int32 Captures = 0;
		/** Portales que pasan el test de visibilidad del subsistema (lo que debería capturar). */
		int32 Potential = 0;
		/** De esos, los que tienen la superficie dibujada en la vista principal. */
		int32 Rendered = 0;
		/** Suma de las fracciones de viewport de los rectángulos de pantalla de los portales potenciales. */
		double AreaSum = 0.0;
		/** Lo mismo solo con los de superficie dibujada (recorte y oclusión juntos). */
		double RenderedAreaSum = 0.0;
		TArray<double> Areas;
	};

	void Sample(UWorld* World, APlayerController* Controller, const ULiminalPortalSubsystem& Subsystem);

	FTransform HubTransform = FTransform::Identity;
	TArray<LiminalBenchMath::FBenchPose> Poses;
	TArray<FSample> Samples;
	TArray<double> GpuMs;
	int32 PoseIndex = 0;
	int32 PoseFrames = 0;
	bool bFinished = true;
};

namespace LiminalBenchProfileJson
{
	/**
	 * Desglose de un caso: tiempo por fotograma de cada cola y de cada ruta de migas con al menos
	 * MinMsPerFrame (para que el informe no lleve miles de pases de microsegundos).
	 */
	TSharedRef<FJsonObject> SnapshotToJson(const LiminalGpuProfile::FSnapshot& Snapshot, double MinMsPerFrame);
}
