#include "SharpSplineFollowerComponent.h"

#include "../Scenario/SharpSplinePath.h"
#include "Components/SplineComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

USharpSplineFollowerComponent::USharpSplineFollowerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void USharpSplineFollowerComponent::BeginPlay()
{
	Super::BeginPlay();
	Character = Cast<ACharacter>(GetOwner());
	Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (Character && Character->HasAuthority() && Movement)
	{
		Movement->AddTickPrerequisiteComponent(this);
		Character->OnCharacterMovementUpdated.AddDynamic(this, &USharpSplineFollowerComponent::ObserveMovement);
	}
	SetComponentTickEnabled(false);
}

void USharpSplineFollowerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RestoreSpeed();
	if (Character)
	{
		Character->OnCharacterMovementUpdated.RemoveDynamic(this, &USharpSplineFollowerComponent::ObserveMovement);
	}
	if (Movement)
	{
		Movement->RemoveTickPrerequisiteComponent(this);
	}
	Super::EndPlay(EndPlayReason);
}

bool USharpSplineFollowerComponent::ValidateRuntime(FString& OutReason) const
{
	if (!Character || !Character->HasAuthority() || !Movement || !Character->GetController())
	{
		OutReason = TEXT("Follower requires an authoritative possessed Character with CMC");
		return false;
	}
	if (!IsValid(Path) || !Path->ValidatePath(OutReason) || !PreparedSettings.IsValid(OutReason))
	{
		if (!IsValid(Path)) { OutReason = TEXT("Spline path is missing"); }
		return false;
	}
	if (!Path->Spline->GetComponentTransform().Equals(PreparedPathTransform) || Path->Spline->GetVersion() != PreparedSplineVersion ||
		Path->PathId != PreparedPathId || Path->PathRevision != PreparedPathRevision)
	{
		OutReason = TEXT("Path transform, geometry or identity changed after preparation; reset the trial");
		return false;
	}
	if (Character->IsMoveInputIgnored())
	{
		OutReason = TEXT("Controller is ignoring movement input");
		return false;
	}
	OutReason.Empty();
	return true;
}

bool USharpSplineFollowerComponent::Prepare(ASharpSplinePath* InPath, FString& OutReason)
{
	if (!Character || !Character->HasAuthority())
	{
		OutReason = TEXT("Prepare must run on the server after BeginPlay");
		return false;
	}
	Reset();
	Path = InPath;
	PreparedSettings = Settings;
	PreparedPathTransform = IsValid(Path) ? Path->Spline->GetComponentTransform() : FTransform::Identity;
	PreparedSplineVersion = IsValid(Path) ? Path->Spline->GetVersion() : 0;
	PreparedPathId = IsValid(Path) ? Path->PathId : NAME_None;
	PreparedPathRevision = IsValid(Path) ? Path->PathRevision : 0;
	if (!ValidateRuntime(OutReason))
	{
		SetState(ESharpSplineFollowState::Failed, OutReason);
		return false;
	}
	ActualDistance = Path->ProjectDistance(Character->GetActorLocation());
	TargetDistance = ActualDistance;
	CrossTrackError = FVector::Dist2D(Character->GetActorLocation(), Path->GetLocation(ActualDistance));
	ProgressReference = ActualDistance;
	SetState(ESharpSplineFollowState::Ready);
	return true;
}

bool USharpSplineFollowerComponent::Start(FString& OutReason)
{
	if (State != ESharpSplineFollowState::Ready)
	{
		OutReason = TEXT("Prepare the follower before starting");
		return false;
	}
	if (!ValidateRuntime(OutReason)) { SetState(ESharpSplineFollowState::Failed, OutReason); return false; }
	OriginalMaxWalkSpeed = Movement->MaxWalkSpeed;
	bSpeedOverridden = true;
	ActualDistance = Path->ProjectDistance(Character->GetActorLocation());
	SecondsWithoutProgress = 0.0f;
	ProgressReference = ActualDistance;
	SetState(ESharpSplineFollowState::Following);
	return true;
}

void USharpSplineFollowerComponent::Suspend()
{
	if (Character && Character->HasAuthority() && State == ESharpSplineFollowState::Following)
	{
		Character->ConsumeMovementInputVector();
		SetState(ESharpSplineFollowState::Suspended);
	}
}

bool USharpSplineFollowerComponent::Resume(FString& OutReason)
{
	if (State != ESharpSplineFollowState::Suspended)
	{
		OutReason = TEXT("Only a suspended follower can resume");
		return false;
	}
	if (!ValidateRuntime(OutReason)) { SetState(ESharpSplineFollowState::Failed, OutReason); return false; }
	ActualDistance = Path->ProjectDistance(Character->GetActorLocation());
	ProgressReference = ActualDistance;
	SecondsWithoutProgress = 0.0f;
	SetState(ESharpSplineFollowState::Following);
	return true;
}

void USharpSplineFollowerComponent::Reset()
{
	if (!Character || !Character->HasAuthority()) { return; }
	Character->ConsumeMovementInputVector();
	RestoreSpeed();
	Path = nullptr;
	ActualDistance = TargetDistance = CrossTrackError = ProgressReference = SecondsWithoutProgress = 0.0f;
	LastFailure.Empty();
	SetState(ESharpSplineFollowState::Idle);
}

void USharpSplineFollowerComponent::RestoreSpeed()
{
	if (Movement && bSpeedOverridden) { Movement->MaxWalkSpeed = OriginalMaxWalkSpeed; }
	bSpeedOverridden = false;
}

void USharpSplineFollowerComponent::SetState(ESharpSplineFollowState NewState, const FString& Reason)
{
	State = NewState;
	LastFailure = Reason;
	SetComponentTickEnabled(State == ESharpSplineFollowState::Following);
	if (State == ESharpSplineFollowState::Completed || State == ESharpSplineFollowState::Failed)
	{
		if (Character) { Character->ConsumeMovementInputVector(); }
		RestoreSpeed();
	}
	OnStateChanged.Broadcast(State, Reason);
}

void USharpSplineFollowerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!Character || !Character->HasAuthority() || State != ESharpSplineFollowState::Following) { return; }
	FString Reason;
	if (!ValidateRuntime(Reason)) { SetState(ESharpSplineFollowState::Failed, Reason); return; }
	if (Movement->MovementMode != MOVE_Walking && !Movement->IsFalling())
	{
		SetState(ESharpSplineFollowState::Failed, TEXT("M1 supports Walking and Falling only"));
		return;
	}
	const float EndDistance = PreparedSettings.bReverseDirection ? 0.0f : Path->GetLength();
	const FVector Position = Character->GetActorLocation();
	const float EndError = FVector::Dist2D(Position, Path->GetLocation(EndDistance));
	const bool bAtEnd = EndError <= PreparedSettings.AcceptanceRadius && Movement->IsMovingOnGround();
	if (bAtEnd && Movement->Velocity.Size() <= PreparedSettings.StopSpeedTolerance)
	{
		SetState(ESharpSplineFollowState::Completed);
		return;
	}
	SecondsWithoutProgress += DeltaTime;
	if (PreparedSettings.BlockedTimeoutSeconds > 0.0f && SecondsWithoutProgress >= PreparedSettings.BlockedTimeoutSeconds)
	{
		SetState(ESharpSplineFollowState::Failed, TEXT("No path progress within Blocked Timeout"));
		return;
	}
	// No new input near the endpoint: CMC braking, with the same configurable stall timeout.
	if (bAtEnd) { return; }
	if (Movement->IsFalling() && !PreparedSettings.bAllowInputWhileFalling) { return; }
	const float Sign = PreparedSettings.bReverseDirection ? -1.0f : 1.0f;
	TargetDistance = FMath::Clamp(ActualDistance + Sign * PreparedSettings.LookAheadDistance, 0.0f, Path->GetLength());
	FVector Direction = Path->GetLocation(TargetDistance) - Position;
	Direction.Z = 0.0f;
	if (!Direction.Normalize()) { SetState(ESharpSplineFollowState::Failed, TEXT("Spline target direction is zero")); return; }
	const float RemainingDistance = FMath::Max(FMath::Abs(EndDistance - ActualDistance), EndError);
	Movement->MaxWalkSpeed = FMath::Clamp(PreparedSettings.TargetSpeed * RemainingDistance / PreparedSettings.EndSlowdownDistance,
		PreparedSettings.MinimumApproachSpeed, PreparedSettings.TargetSpeed);
	Character->AddMovementInput(Direction);
}

void USharpSplineFollowerComponent::ObserveMovement(float DeltaSeconds, FVector OldLocation, FVector OldVelocity)
{
	if (!Character || !Character->HasAuthority() || !IsValid(Path)) { return; }
	if (State != ESharpSplineFollowState::Following && State != ESharpSplineFollowState::Suspended) { return; }
	const float ProjectedDistance = Path->ProjectDistance(Character->GetActorLocation());
	const float Allowance = PreparedSettings.ProjectionSlackDistance +
		FMath::Max(OldVelocity.Size2D(), Movement->Velocity.Size2D()) * DeltaSeconds * PreparedSettings.ProjectionSpeedMultiplier;
	ActualDistance = FMath::Clamp(ProjectedDistance, FMath::Max(0.0f, ActualDistance - Allowance),
		FMath::Min(Path->GetLength(), ActualDistance + Allowance));
	CrossTrackError = FVector::Dist2D(Character->GetActorLocation(), Path->GetLocation(ActualDistance));
	const float Sign = PreparedSettings.bReverseDirection ? -1.0f : 1.0f;
	if ((ActualDistance - ProgressReference) * Sign >= PreparedSettings.MinimumProgressDistance)
	{
		ProgressReference = ActualDistance;
		SecondsWithoutProgress = 0.0f;
	}
}
