// Copyright Epic Games, Inc. All Rights Reserved.

#include "GE_AIReplicationCharacter.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "GE_AIReplication.h"
#include "Combat/MeleeAttackComponent.h"

AGE_AIReplicationCharacter::AGE_AIReplicationCharacter()
{
	MeleeAttack = CreateDefaultSubobject<UMeleeAttackComponent>(TEXT("MeleeAttack"));
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);
		
	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);

	// Note: For faster iteration times these variables, and many more, can be tweaked in the Character Blueprint
	// instead of recompiling to adjust them
	GetCharacterMovement()->JumpZVelocity = 500.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->bUsePawnControlRotation = true;

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character) 
	// are set in the derived blueprint asset named ThirdPersonCharacter (to avoid direct content references in C++)
}

void AGE_AIReplicationCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Apply after Blueprint defaults have loaded so packaged players share one LAN profile.
	// Keep movement prediction/correction in CMC; only tune replication and presentation.
	SetReplicates(true);
	SetReplicateMovement(true);
	SetNetUpdateFrequency(60.0f);
	SetMinNetUpdateFrequency(30.0f);
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->NetworkSmoothingMode = ENetworkSmoothingMode::Exponential;
	// Shorter LAN smoothing reduces how far remote players trail their latest state.
	Movement->NetworkSimulatedSmoothLocationTime = 0.06f;
	Movement->NetworkSimulatedSmoothRotationTime = 0.05f;
	Movement->ListenServerNetworkSimulatedSmoothLocationTime = 0.05f;
	Movement->ListenServerNetworkSimulatedSmoothRotationTime = 0.06f;
	// Initial replication may have allocated prediction data before BeginPlay.
	// Refresh the cached smoothing times as well as the component properties.
	if (GetNetMode() == NM_Client || GetNetMode() == NM_ListenServer)
	{
		FNetworkPredictionData_Client_Character* Prediction = Movement->GetPredictionData_Client_Character();
		Prediction->SmoothNetUpdateTime = GetNetMode() == NM_ListenServer
			? Movement->ListenServerNetworkSimulatedSmoothLocationTime : Movement->NetworkSimulatedSmoothLocationTime;
		Prediction->SmoothNetUpdateRotationTime = GetNetMode() == NM_ListenServer
			? Movement->ListenServerNetworkSimulatedSmoothRotationTime : Movement->NetworkSimulatedSmoothRotationTime;
	}
	UE_LOG(LogGE_AIReplication, Display,
		TEXT("[LAN-Player] Actor=%s NetMode=%d Role=%d NetUpdateHz=%.0f MinNetUpdateHz=%.0f Smoothing=Exponential ClientLocationSeconds=%.2f ListenLocationSeconds=%.2f"),
		*GetName(), static_cast<int32>(GetNetMode()), static_cast<int32>(GetLocalRole()), GetNetUpdateFrequency(), GetMinNetUpdateFrequency(),
		Movement->NetworkSimulatedSmoothLocationTime, Movement->ListenServerNetworkSimulatedSmoothLocationTime);
}

void AGE_AIReplicationCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {
		
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &AGE_AIReplicationCharacter::DoJumpStart);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AGE_AIReplicationCharacter::Move);
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AGE_AIReplicationCharacter::Look);

		// Looking
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AGE_AIReplicationCharacter::Look);
		MeleeAttack->BindInput(EnhancedInputComponent);
	}
	else
	{
		UE_LOG(LogGE_AIReplication, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void AGE_AIReplicationCharacter::Move(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

	// route the input
	DoMove(MovementVector.X, MovementVector.Y);
}

void AGE_AIReplicationCharacter::Look(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// route the input
	DoLook(LookAxisVector.X, LookAxisVector.Y);
}

void AGE_AIReplicationCharacter::DoMove(float Right, float Forward)
{
	if (MeleeAttack && MeleeAttack->IsAttacking()) return;
	if (GetController() != nullptr)
	{
		// find out which way is forward
		const FRotator Rotation = GetController()->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);

		// get right vector 
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		// add movement 
		AddMovementInput(ForwardDirection, Forward);
		AddMovementInput(RightDirection, Right);
	}
}

bool AGE_AIReplicationCharacter::CanJumpInternal_Implementation() const
{
	return (!MeleeAttack || !MeleeAttack->IsAttacking()) && Super::CanJumpInternal_Implementation();
}

void AGE_AIReplicationCharacter::DoLook(float Yaw, float Pitch)
{
	if (GetController() != nullptr)
	{
		// add yaw and pitch input to controller
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void AGE_AIReplicationCharacter::DoJumpStart()
{
	if (MeleeAttack && MeleeAttack->IsAttacking()) return;
	// signal the character to jump
	Jump();
}

void AGE_AIReplicationCharacter::DoJumpEnd()
{
	// signal the character to stop jumping
	StopJumping();
}
