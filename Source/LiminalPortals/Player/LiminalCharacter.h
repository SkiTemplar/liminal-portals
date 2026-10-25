#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "LiminalCharacter.generated.h"

class UCameraComponent;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/**
 * Personaje en primera persona. Las acciones y el contexto de Enhanced Input se crean en C++ en
 * tiempo de ejecución (decisión 12): WASD y stick izquierdo para moverse, ratón y stick derecho
 * para mirar, espacio / A para saltar y Mayúsculas / L3 para correr.
 */
UCLASS()
class LIMINALPORTALS_API ALiminalCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ALiminalCharacter();

	/**
	 * Aplica el paso de portal: posición, rotación de control (solo yaw y pitch; roll 0) y
	 * velocidad de CharacterMovement, que conserva su módulo.
	 */
	UFUNCTION(BlueprintCallable, Category = "Liminal|Portal")
	void TeleportThroughPortal(const FTransform& Transfer);

	UCameraComponent* GetFirstPersonCamera() const { return FirstPersonCamera; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liminal|Movement", meta = (ClampMin = "0.0"))
	float WalkSpeed = 400.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liminal|Movement", meta = (ClampMin = "0.0"))
	float SprintSpeed = 700.f;

	/** Grados por segundo con el stick derecho a fondo (antes de la escala de entrada del controlador). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liminal|Input", meta = (ClampMin = "0.0"))
	float GamepadLookRate = 60.f;

protected:
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void NotifyControllerChanged() override;

private:
	void EnsureInputAssets();
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void StartSprint();
	void StopSprint();

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MappingContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> SprintAction;
};
