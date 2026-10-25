#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LiminalPortal.generated.h"

class UMaterialInstanceDynamic;
class UPrimitiveComponent;
class USceneCaptureComponent2D;
class UStaticMeshComponent;
class UTextureRenderTarget2D;
struct FMinimalViewInfo;

/**
 * Portal fijo empotrado: marco, superficie (caja fina two-sided), placa trasera sólida y plano de
 * reserva visible solo en capturas. El origen del actor es el centro inferior del hueco (a ras de
 * suelo) y el eje X apunta hacia fuera de la cara visible.
 *
 * El portal no hace tick: ULiminalPortalSubsystem lo actualiza desde el PlayerCameraManager
 * después de calcular la cámara del fotograma (decisión 4).
 */
UCLASS(Blueprintable)
class LIMINALPORTALS_API ALiminalPortal : public AActor
{
	GENERATED_BODY()

public:
	ALiminalPortal();

	/** Portal de destino. El enlace no es simétrico por sí solo: cada portal apunta al suyo. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal")
	TObjectPtr<ALiminalPortal> LinkedPortal;

	/** Ancho y alto del hueco, en cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal", meta = (ClampMin = "10.0"))
	FVector2D PortalSize = FVector2D(120.0, 220.0);

	/** Distancia por delante del plano a la que la superficie gana grosor (decisión 7), en cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal|Surface", meta = (ClampMin = "0.0"))
	float NearThicknessDistance = 60.f;

	/** Profundidad de la caja de superficie detrás del plano en reposo, en cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal|Surface", meta = (ClampMin = "0.1"))
	float SurfaceBaseDepth = 1.f;

	/** Profundidad de la caja cuando la cámara está cerca; debe quedar por delante de la placa trasera. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal|Surface", meta = (ClampMin = "0.1"))
	float SurfaceExtendedDepth = 40.f;

	/** Distancia de la cara frontal de la placa trasera al plano del portal, en cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal|Frame", meta = (ClampMin = "1.0"))
	float BackPlateDistance = 60.f;

	/** Grosor de los montantes y el dintel del marco, en cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal|Frame", meta = (ClampMin = "1.0"))
	float FrameThickness = 12.f;

	/**
	 * Portal empotrado (decisión 3): marco en túnel hasta la placa trasera sólida. Desactivado, el
	 * marco es un aro fino centrado en el plano y no hay placa; lo usa la calibración, donde A y B
	 * coinciden y la placa de cada uno taparía la cara frontal del otro.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal|Frame")
	bool bEmbedded = true;

	/**
	 * Muestra los montantes y el dintel. Apagado, siguen colisionando pero no se dibujan: lo usan las
	 * puertas con marco low-poly propio (SM_Hub_DoorFrame, SM_ReturnWall), que ocupan el mismo volumen y
	 * darían z-fighting (D-32).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal|Frame")
	bool bShowFrame = true;

	/**
	 * Solo de salida: sin superficie visible, sin captura y sin cruce desde él; recibe a quien entra
	 * por el portal que lo enlaza. Lo usa la calibración (B coincide con A y su superficie taparía la de A).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal")
	bool bExitOnly = false;

	/** Desplazamiento del clip plane respecto al plano del destino (negativo = detrás), en cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal|Capture")
	float ClipPlaneOffset = -1.f;

	/** Transform rígido del actor (sin escala): define el paso de suelo a suelo entre portales. */
	UFUNCTION(BlueprintPure, Category = "Portal")
	FTransform GetPortalTransform() const;

	/** Transform rígido del centro del rectángulo del hueco, sobre el plano del portal. */
	UFUNCTION(BlueprintPure, Category = "Portal")
	FTransform GetPlaneTransform() const;

	/** Medio ancho (X) y medio alto (Y) del hueco. */
	UFUNCTION(BlueprintPure, Category = "Portal")
	FVector2D GetHalfExtents() const { return PortalSize * 0.5; }

	/** Paso de este portal a LinkedPortal; identidad si no hay enlace. */
	UFUNCTION(BlueprintPure, Category = "Portal")
	FTransform GetTransferToLinked() const;

	UFUNCTION(BlueprintPure, Category = "Portal")
	UTextureRenderTarget2D* GetRenderTarget() const { return RenderTarget; }

	UPrimitiveComponent* GetSurfaceComponent() const;
	UPrimitiveComponent* GetFallbackComponent() const;

	/**
	 * Oculta o muestra la superficie en la vista del jugador (las capturas ya la ocultan siempre).
	 * Un portal bExitOnly la mantiene oculta.
	 */
	UFUNCTION(BlueprintCallable, Category = "Portal")
	void SetSurfaceHiddenInGame(bool bHideSurface);

	/** Ajusta la profundidad de la caja de superficie según la posición de la cámara. */
	void UpdateSurfaceDepth(const FVector& CameraLocation);

	/**
	 * Coloca la captura en la vista llevada a través del par de portales y la renderiza en el
	 * momento. Crea o redimensiona el render target a RenderTargetSize.
	 * @param HiddenComponents Superficies de todos los portales y reservas del origen y del destino.
	 * @return false si no hay enlace o tamaño válido y no se capturó.
	 */
	bool CaptureView(const FMinimalViewInfo& View, const FIntPoint& RenderTargetSize,
		TConstArrayView<UPrimitiveComponent*> HiddenComponents);

	/**
	 * Libera el estado de render persistente de la captura (FSceneViewState y su escena de Lumen con
	 * atlas propio, D-31) si lleva IdleSeconds sin capturar y la cámara está a ReleaseDistance o más.
	 * La siguiente captura lo vuelve a crear. El render target se conserva.
	 * @return true si se liberó en esta llamada.
	 */
	bool ReleaseCaptureStateIfIdle(double NowSeconds, const FVector& CameraLocation, double IdleSeconds,
		double ReleaseDistance);

	/** Si la captura tiene ahora un estado de render persistente (para depuración y tests). */
	bool HasCaptureState() const { return bHasCaptureState; }

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void ApplyLayout();
	void SetSurfaceDepth(float Depth);
	void ApplyMaterials();
	bool EnsureRenderTarget(const FIntPoint& Size);

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> FrameLeft;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> FrameRight;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> FrameTop;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Surface;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BackPlate;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Fallback;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USceneCaptureComponent2D> Capture;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> RenderTarget;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SurfaceMaterial;

	float CurrentSurfaceDepth = -1.f;

	/** Instante (segundos reales del mundo) de la última captura; solo vale si bHasCaptureState. */
	double LastCaptureSeconds = 0.0;
	bool bHasCaptureState = false;
};
