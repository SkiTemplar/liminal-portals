#include "Player/LiminalCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "LiminalPortals.h"
#include "Portal/LiminalPortalMath.h"

namespace
{
	constexpr float EyeHeight = 64.f;
	constexpr int32 MappingPriority = 0;

	UInputAction* MakeAction(UObject* Outer, FName Name, EInputActionValueType ValueType)
	{
		UInputAction* Action = NewObject<UInputAction>(Outer, Name, RF_Transient);
		Action->ValueType = ValueType;
		return Action;
	}

	template <typename TModifier>
	TModifier* AddModifier(FEnhancedActionKeyMapping& Mapping, UObject* Outer)
	{
		TModifier* Modifier = NewObject<TModifier>(Outer, NAME_None, RF_Transient);
		Mapping.Modifiers.Add(Modifier);
		return Modifier;
	}

	/** Move es Axis2D con X = derecha e Y = delante; las teclas dan 1D y se reordenan. */
	void MapMoveKey(UInputMappingContext* Context, const UInputAction* Action, const FKey& Key, bool bForwardAxis, bool bNegate)
	{
		FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action, Key);
		if (bForwardAxis)
		{
			AddModifier<UInputModifierSwizzleAxis>(Mapping, Context)->Order = EInputAxisSwizzle::YXZ;
		}
		if (bNegate)
		{
			AddModifier<UInputModifierNegate>(Mapping, Context);
		}
	}

	/** El pitch se invierte para que mover el ratón o el stick hacia arriba mire hacia arriba. */
	void NegateLookY(FEnhancedActionKeyMapping& Mapping, UObject* Outer)
	{
		UInputModifierNegate* Negate = AddModifier<UInputModifierNegate>(Mapping, Outer);
		Negate->bX = false;
		Negate->bY = true;
		Negate->bZ = false;
	}
}

ALiminalCharacter::ALiminalCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	GetCapsuleComponent()->InitCapsuleSize(34.f, 88.f);
	BaseEyeHeight = EyeHeight;

	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
	FirstPersonCamera->SetRelativeLocation(FVector(0.f, 0.f, EyeHeight));
	FirstPersonCamera->bUsePawnControlRotation = true;
	FirstPersonCamera->SetFieldOfView(90.f);

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->MaxWalkSpeed = WalkSpeed;
	Movement->bOrientRotationToMovement = false;
	Movement->JumpZVelocity = 420.f;
	Movement->AirControl = 0.3f;
}

void ALiminalCharacter::TeleportThroughPortal(const FTransform& Transfer)
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	const FVector NewVelocity = LiminalPortalMath::TransferVector(Transfer, Movement->Velocity);
	const FVector NewLocation = LiminalPortalMath::TransferPoint(Transfer, GetActorLocation());

	FRotator NewActorRotation = LiminalPortalMath::TransferControlRotation(Transfer, GetActorRotation());
	NewActorRotation.Pitch = 0.0;
	if (AController* PawnController = GetController())
	{
		const FRotator NewControlRotation = LiminalPortalMath::TransferControlRotation(Transfer, PawnController->GetControlRotation());
		PawnController->SetControlRotation(NewControlRotation);
		NewActorRotation.Yaw = NewControlRotation.Yaw;
	}

	// TeleportPhysics: sin barrido ni velocidad inducida por el salto de posición.
	const bool bMoved = SetActorLocationAndRotation(NewLocation, NewActorRotation, false, nullptr, ETeleportType::TeleportPhysics);
	if (!bMoved)
	{
		UE_LOG(LogLiminalPortals, Warning, TEXT("%s: el teletransporte por portal no movió al personaje."), *GetName());
		return;
	}
	Movement->Velocity = NewVelocity;
	Movement->UpdateComponentVelocity();
}

void ALiminalCharacter::BeginPlay()
{
	Super::BeginPlay();
	// WalkSpeed puede venir editada en una subclase o en el nivel.
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
}

void ALiminalCharacter::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();
	EnsureInputAssets();

	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	const ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer))
	{
		InputSubsystem->RemoveMappingContext(MappingContext);
		InputSubsystem->AddMappingContext(MappingContext, MappingPriority);
	}
}

void ALiminalCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	EnsureInputAssets();

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Input)
	{
		UE_LOG(LogLiminalPortals, Error, TEXT("%s: el InputComponent no es de Enhanced Input; revisa DefaultInput.ini."), *GetName());
		return;
	}

	Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ALiminalCharacter::Move);
	Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &ALiminalCharacter::Look);
	Input->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
	Input->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
	Input->BindAction(SprintAction, ETriggerEvent::Started, this, &ALiminalCharacter::StartSprint);
	Input->BindAction(SprintAction, ETriggerEvent::Completed, this, &ALiminalCharacter::StopSprint);
}

void ALiminalCharacter::EnsureInputAssets()
{
	if (MappingContext)
	{
		return;
	}

	MoveAction = MakeAction(this, TEXT("IA_Move"), EInputActionValueType::Axis2D);
	LookAction = MakeAction(this, TEXT("IA_Look"), EInputActionValueType::Axis2D);
	JumpAction = MakeAction(this, TEXT("IA_Jump"), EInputActionValueType::Boolean);
	SprintAction = MakeAction(this, TEXT("IA_Sprint"), EInputActionValueType::Boolean);

	MappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Liminal"), RF_Transient);

	MapMoveKey(MappingContext, MoveAction, EKeys::W, true, false);
	MapMoveKey(MappingContext, MoveAction, EKeys::S, true, true);
	MapMoveKey(MappingContext, MoveAction, EKeys::D, false, false);
	MapMoveKey(MappingContext, MoveAction, EKeys::A, false, true);
	AddModifier<UInputModifierDeadZone>(MappingContext->MapKey(MoveAction, EKeys::Gamepad_Left2D), MappingContext);

	NegateLookY(MappingContext->MapKey(LookAction, EKeys::Mouse2D), MappingContext);

	FEnhancedActionKeyMapping& StickLook = MappingContext->MapKey(LookAction, EKeys::Gamepad_Right2D);
	AddModifier<UInputModifierDeadZone>(StickLook, MappingContext);
	NegateLookY(StickLook, MappingContext);
	AddModifier<UInputModifierScaleByDeltaTime>(StickLook, MappingContext);
	AddModifier<UInputModifierScalar>(StickLook, MappingContext)->Scalar = FVector(GamepadLookRate);

	MappingContext->MapKey(JumpAction, EKeys::SpaceBar);
	MappingContext->MapKey(JumpAction, EKeys::Gamepad_FaceButton_Bottom);
	MappingContext->MapKey(SprintAction, EKeys::LeftShift);
	MappingContext->MapKey(SprintAction, EKeys::Gamepad_LeftThumbstick);
}

void ALiminalCharacter::Move(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	if (!Controller)
	{
		return;
	}
	const FRotator YawRotation(0.0, Controller->GetControlRotation().Yaw, 0.0);
	const FRotationMatrix YawMatrix(YawRotation);
	AddMovementInput(YawMatrix.GetUnitAxis(EAxis::X), Axis.Y);
	AddMovementInput(YawMatrix.GetUnitAxis(EAxis::Y), Axis.X);
}

void ALiminalCharacter::Look(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(Axis.Y);
}

void ALiminalCharacter::StartSprint()
{
	GetCharacterMovement()->MaxWalkSpeed = SprintSpeed;
}

void ALiminalCharacter::StopSprint()
{
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
}
