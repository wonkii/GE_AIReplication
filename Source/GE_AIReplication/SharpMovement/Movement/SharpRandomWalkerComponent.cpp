#include "SharpRandomWalkerComponent.h"
#include "../Enemy/SharpEnemyCharacter.h"
#include "../Measurement/SharpMovementRecorderComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogSharpRandomWalk, Log, All);

USharpRandomWalkerComponent::USharpRandomWalkerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void USharpRandomWalkerComponent::BeginPlay()
{
	Super::BeginPlay();
	Enemy = Cast<ASharpEnemyCharacter>(GetOwner());
	Movement = Enemy ? Enemy->GetCharacterMovement() : nullptr;
	if (Enemy && Enemy->HasAuthority() && Movement)
	{
		// The command for this frame must arrive before CMC consumes input.
		Movement->AddTickPrerequisiteComponent(this);
	}
	SetComponentTickEnabled(false);
}

void USharpRandomWalkerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RestoreMovementSettings();
	if (Movement) { Movement->RemoveTickPrerequisiteComponent(this); }
	Super::EndPlay(EndPlayReason);
}

void USharpRandomWalkerComponent::ConfigureArena(bool bEnabled, const FVector& Center,
    float YawDegrees, const FVector2D& FullSizeCm, float MarginCm,
    float LookAheadCm, bool bProbeWorldStatic, float ProbeIntervalSeconds)
{
    bArenaEnabled = bEnabled;
    ArenaCenter = Center;
    ArenaYaw = YawDegrees;
    ArenaSize = FullSizeCm;
    ArenaMargin = MarginCm;
    AvoidLookAheadCm = LookAheadCm;
    bProbeStaticWalls = bProbeWorldStatic;
    WallProbeIntervalSeconds = ProbeIntervalSeconds;
    WallProbeElapsed = 0.0f;
    bAvoidanceTurn = false;
    ConsecutiveAvoidanceAttempts = 0;
}

bool USharpRandomWalkerComponent::IsInitialPositionInsideArena(const FVector& Position,
    float CapsuleRadiusCm) const
{
    return IsArenaPositionAllowed(Position, CapsuleRadiusCm);
}

bool USharpRandomWalkerComponent::IsArenaPositionAllowed(const FVector& Position,
    float CapsuleRadiusCm) const
{
    if (!bArenaEnabled) { return true; }
    const FVector Relative = (Position - ArenaCenter).RotateAngleAxis(-ArenaYaw, FVector::UpVector);
    const float LimitX = ArenaSize.X * 0.5f - ArenaMargin - CapsuleRadiusCm;
    const float LimitY = ArenaSize.Y * 0.5f - ArenaMargin - CapsuleRadiusCm;
    return LimitX > 0.0f && LimitY > 0.0f &&
        FMath::Abs(Relative.X) <= LimitX && FMath::Abs(Relative.Y) <= LimitY;
}

bool USharpRandomWalkerComponent::IsDirectionAllowed(const FVector& Direction, float DistanceCm,
    bool bTestWall, bool& bOutBoundary) const
{
    bOutBoundary = false;
    if (!Enemy || !GetWorld()) { return false; }
    const UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent();
    if (!Capsule) { return false; }
    const FVector Horizontal = Direction.GetSafeNormal2D();
    if (Horizontal.IsNearlyZero()) { return false; }
    const FVector Start = Enemy->GetActorLocation();
    const FVector End = Start + Horizontal * DistanceCm;
    if (!IsArenaPositionAllowed(End, Capsule->GetScaledCapsuleRadius()))
    {
        bOutBoundary = true;
        return false;
    }
    if (!bTestWall || !bProbeStaticWalls) { return true; }

    // Probe only WorldStatic (walls), not other experimental Enemy capsules.
    // Keep the sphere within the upper part of the character's capsule so the
    // walkable floor is not mistaken for a wall on level surfaces.
    const float ProbeRadius = FMath::Max(5.0f, FMath::Min(
        Capsule->GetScaledCapsuleRadius() * 0.8f,
        Capsule->GetScaledCapsuleHalfHeight() * 0.7f));
    FCollisionQueryParams Query(SCENE_QUERY_STAT(SharpRandomWalkWallProbe), false, Enemy);
    FHitResult Hit;
    return !GetWorld()->SweepSingleByObjectType(Hit, Start, End, FQuat::Identity,
        FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionShape::MakeSphere(ProbeRadius), Query);
}

bool USharpRandomWalkerComponent::DetectObstacle(const FVector& Direction,
    float DeltaTime, FString& OutKind)
{
    OutKind.Empty();
    if (!bArenaEnabled) { return false; }
    const float Speed = FMath::Max(GetBehaviorSpeed(), Movement ? Movement->Velocity.Size2D() : 0.0f);
    const float LookAhead = FMath::Max(AvoidLookAheadCm, Speed * 0.6f);
    bool bBoundary = false;
    // Mathematical rectangle checks every tick; physical-wall sweeps are throttled.
    if (!IsDirectionAllowed(Direction, LookAhead, false, bBoundary))
    {
        OutKind = TEXT("Boundary");
        return true;
    }
    WallProbeElapsed += DeltaTime;
    if (bProbeStaticWalls && WallProbeElapsed >= WallProbeIntervalSeconds)
    {
        WallProbeElapsed = 0.0f;
        if (!IsDirectionAllowed(Direction, LookAhead, true, bBoundary))
        {
            OutKind = bBoundary ? TEXT("Boundary") : TEXT("Wall");
            return true;
        }
    }
    return false;
}

void USharpRandomWalkerComponent::RecordAvoidance(const FString& Kind, float SignedAngle)
{
    if (Enemy && Enemy->Recorder)
    {
        Enemy->Recorder->RecordRandomWalkCommand(SegmentIndex, FString(TEXT("Avoid_")) + Kind,
            SignedAngle, TargetYaw);
        Enemy->Recorder->RecordRandomWalkAvoidance(SegmentIndex, Kind,
            ConsecutiveAvoidanceAttempts, SignedAngle, TargetYaw);
    }
}

bool USharpRandomWalkerComponent::BeginAvoidance(const FString& Kind)
{
    if (!Enemy || !Enemy->HasAuthority() || !bArenaEnabled) { return false; }
    if (++ConsecutiveAvoidanceAttempts > MaxConsecutiveAvoidanceAttempts)
    {
        UE_LOG(LogSharpRandomWalk, Error,
            TEXT("[Sharp][Avoidance] ActorId=%d too many consecutive avoidance attempts; Last=%s"),
            Enemy->ExperimentActorId, *Kind);
        return false;
    }
    // Independent deterministic stream: obstacle frequency must not consume
    // the stream that controls ordinary Straight/Turn/Behavior commands.
    const uint32 AvoidSeed = static_cast<uint32>(ResolvedSeed) ^
        (static_cast<uint32>(SegmentIndex) * 0x9E3779B9u) ^
        (static_cast<uint32>(ConsecutiveAvoidanceAttempts) * 0x85EBCA6Bu);
    FRandomStream AvoidanceRandom;
    AvoidanceRandom.Initialize(static_cast<int32>(AvoidSeed));
    // Both sides are considered, followed by retreat and the arena centre.
    const float FirstSign = AvoidanceRandom.RandRange(0, 1) == 0 ? -1.0f : 1.0f;
    const float CandidateAngles[] = { 90.0f, 135.0f, 45.0f, 165.0f, 180.0f };
    const float TestDistance = FMath::Max(150.0f, AvoidLookAheadCm);
    FVector Chosen = FVector::ZeroVector;
    float ChosenAngle = 0.0f;
    for (float AbsoluteAngle : CandidateAngles)
    {
        const int32 Count = AbsoluteAngle == 180.0f ? 1 : 2;
        for (int32 DirectionIndex = 0; DirectionIndex < Count; ++DirectionIndex)
        {
            const float Angle = AbsoluteAngle * (DirectionIndex == 0 ? FirstSign : -FirstSign);
            const FVector Candidate = Forward.RotateAngleAxis(Angle, FVector::UpVector).GetSafeNormal2D();
            bool bBoundary = false;
            if (IsDirectionAllowed(Candidate, TestDistance, true, bBoundary))
            {
                Chosen = Candidate;
                ChosenAngle = Angle;
                break;
            }
        }
        if (!Chosen.IsNearlyZero()) { break; }
    }
    if (Chosen.IsNearlyZero())
    {
        const FVector ToCenter = (ArenaCenter - Enemy->GetActorLocation()).GetSafeNormal2D();
        bool bBoundary = false;
        if (!ToCenter.IsNearlyZero() && IsDirectionAllowed(ToCenter, TestDistance, true, bBoundary))
        {
            Chosen = ToCenter;
            ChosenAngle = FMath::FindDeltaAngleDegrees(Forward.Rotation().Yaw, Chosen.Rotation().Yaw);
        }
    }
    if (Chosen.IsNearlyZero())
    {
        UE_LOG(LogSharpRandomWalk, Warning, TEXT("[Sharp][Avoidance] ActorId=%d no clear escape from %s at %s"),
            Enemy->ExperimentActorId, *Kind, *Enemy->GetActorLocation().ToCompactString());
        return false;
    }
    TargetYaw = FRotator::NormalizeAxis(Chosen.Rotation().Yaw);
    bAvoidanceTurn = true; // Brake even for WalkTurn/RunTurn, then resume mixed behaviour.
    Phase = ESharpRandomWalkPhase::Braking;
    StallSeconds = ProgressCheckpoint = 0.0f;
    WallProbeElapsed = 0.0f;
    RecordAvoidance(Kind, ChosenAngle);
    UE_LOG(LogSharpRandomWalk, Display,
        TEXT("[Sharp][Avoidance] ActorId=%d Segment=%d Kind=%s Attempt=%d Angle=%.1f TargetYaw=%.1f"),
        Enemy->ExperimentActorId, SegmentIndex, *Kind, ConsecutiveAvoidanceAttempts, ChosenAngle, TargetYaw);
    return true;
}

bool USharpRandomWalkerComponent::ValidateRuntime(FString& OutReason) const
{
	if (!IsValid(Enemy) || !Enemy->HasAuthority() || !Movement || !Enemy->GetController() ||
		Enemy->ExperimentActorId <= 0 || !Movement->IsComponentTickEnabled() || Enemy->IsMoveInputIgnored() ||
		(Movement->MovementMode != MOVE_Walking && !Movement->IsFalling()))
	{
		OutReason = TEXT("RandomWalk requires an authoritative, possessed Enemy with enabled Walking/Falling CMC");
		return false;
	}
	OutReason.Empty(); return true;
}

bool USharpRandomWalkerComponent::Prepare(int32 ActorId, FString& OutReason)
{
	if (!Enemy || !Enemy->HasAuthority() || ActorId <= 0 || ActorId != Enemy->ExperimentActorId)
	{
		OutReason = TEXT("RandomWalk must be prepared on the server with a valid ActorId"); return false;
	}
	Reset();
	PreparedSettings = Settings;
	if (!PreparedSettings.IsValid(OutReason)) { SetState(ESharpRandomWalkState::Failed, OutReason); return false; }
	// Calculate with unsigned arithmetic to avoid signed overflow for arbitrary seeds.
    ResolvedSeed = bHasManagerSeed ? ManagerSeed :
        static_cast<int32>(static_cast<uint32>(PreparedSettings.BaseSeed) + static_cast<uint32>(ActorId));
	Random.Initialize(ResolvedSeed);
    if (bArenaEnabled && !IsInitialPositionInsideArena(Enemy->GetActorLocation(),
        Enemy->GetCapsuleComponent()->GetScaledCapsuleRadius()))
    {
        OutReason = TEXT("RandomWalk start is outside the configured Manager arena (including capsule and margin)");
        SetState(ESharpRandomWalkState::Failed, OutReason);
        return false;
    }
	Forward = Enemy->GetActorForwardVector().GetSafeNormal2D();
	if (Forward.IsNearlyZero()) { OutReason = TEXT("RandomWalk initial forward direction must be horizontal"); SetState(ESharpRandomWalkState::Failed, OutReason); return false; }
	SegmentIndex = 0;
    ConsecutiveAvoidanceAttempts = 0;
    bAvoidanceTurn = false;
    WallProbeElapsed = 0.0f;
	Phase = ESharpRandomWalkPhase::Straight;
	SegmentDistance = SegmentProgress = 0.0f;
	SetState(ESharpRandomWalkState::Ready);
	UE_LOG(LogSharpRandomWalk, Display, TEXT("[Sharp][RandomWalk] Prepared ActorId=%d Seed=%d Distance=[%.1f,%.1f] Turn=[%.1f,%.1f] Speed=%.1f"),
		ActorId, ResolvedSeed, PreparedSettings.MinimumStraightDistance, PreparedSettings.MaximumStraightDistance,
		PreparedSettings.MinimumTurnAngle, PreparedSettings.MaximumTurnAngle, PreparedSettings.TargetSpeed);
	return true;
}

bool USharpRandomWalkerComponent::CanStart(FString& OutReason) const
{
	if (State != ESharpRandomWalkState::Ready && State != ESharpRandomWalkState::Stopped && State != ESharpRandomWalkState::Suspended)
	{
		OutReason = TEXT("RandomWalk is not Ready, Stopped or Suspended; Prepare/Reset first"); return false;
	}
	return ValidateRuntime(OutReason);
}

bool USharpRandomWalkerComponent::Start(FString& OutReason)
{
	if (!CanStart(OutReason)) { return false; }
	if (!bSpeedOverridden)
	{
		OriginalMaxWalkSpeed = Movement->MaxWalkSpeed;
		bOriginalOrientToMovement = Movement->bOrientRotationToMovement;
		bOriginalUseControllerRotationYaw = Enemy->bUseControllerRotationYaw;
		bSpeedOverridden = true;
	}
	Movement->MaxWalkSpeed = GetBehaviorSpeed();
	Movement->bOrientRotationToMovement = false;
	Enemy->bUseControllerRotationYaw = false;
	const bool bFresh = SegmentIndex == 0;
	SetState(ESharpRandomWalkState::Moving);
	if (bFresh) { BeginStraight(); }
	return true;
}

bool USharpRandomWalkerComponent::Resume(FString& OutReason)
{
	if (State != ESharpRandomWalkState::Suspended) { OutReason = TEXT("Only suspended RandomWalk can resume"); return false; }
	return Start(OutReason);
}

void USharpRandomWalkerComponent::Suspend()
{
	if (Enemy && Enemy->HasAuthority() && State == ESharpRandomWalkState::Moving)
	{
		Enemy->ConsumeMovementInputVector();
		SetState(ESharpRandomWalkState::Suspended);
	}
}

void USharpRandomWalkerComponent::Stop()
{
	if (!Enemy || !Enemy->HasAuthority() ||
		(State != ESharpRandomWalkState::Moving && State != ESharpRandomWalkState::Suspended)) { return; }
	Enemy->ConsumeMovementInputVector();
	Movement->StopMovementImmediately();
	Movement->ClearAccumulatedForces();
	RestoreMovementSettings();
	SetState(ESharpRandomWalkState::Stopped);
}

void USharpRandomWalkerComponent::Fail(const FString& Reason)
{
	if (!Enemy || !Enemy->HasAuthority()) { return; }
	Enemy->ConsumeMovementInputVector();
	if (Movement) { Movement->StopMovementImmediately(); Movement->ClearAccumulatedForces(); }
	RestoreMovementSettings();
	SetState(ESharpRandomWalkState::Failed, Reason);
}

void USharpRandomWalkerComponent::Reset()
{
	if (!Enemy || !Enemy->HasAuthority()) { return; }
	Enemy->ConsumeMovementInputVector();
	RestoreMovementSettings();
	SegmentDistance = SegmentProgress = StallSeconds = ProgressCheckpoint = 0.0f;
	SegmentIndex = 0;
    ConsecutiveAvoidanceAttempts = 0;
    bAvoidanceTurn = false;
    WallProbeElapsed = 0.0f;
	Phase = ESharpRandomWalkPhase::Straight;
    ActiveBehavior = ESharpRandomWalkBehavior::StopTurnWalk;
	LastFailure.Empty();
	SetState(ESharpRandomWalkState::Idle);
}

void USharpRandomWalkerComponent::RestoreMovementSettings()
{
	if (Enemy && Movement && bSpeedOverridden)
	{
		Movement->MaxWalkSpeed = OriginalMaxWalkSpeed;
		Movement->bOrientRotationToMovement = bOriginalOrientToMovement;
		Enemy->bUseControllerRotationYaw = bOriginalUseControllerRotationYaw;
	}
	bSpeedOverridden = false;
}

void USharpRandomWalkerComponent::SetState(ESharpRandomWalkState NewState, const FString& Reason)
{
	State = NewState;
	LastFailure = Reason;
	SetComponentTickEnabled(State == ESharpRandomWalkState::Moving && Enemy && Enemy->HasAuthority());
	OnStateChanged.Broadcast(this, State, Reason);
}

float USharpRandomWalkerComponent::GetBehaviorSpeed() const
{
    return (ActiveBehavior == ESharpRandomWalkBehavior::RunStopTurn ||
            ActiveBehavior == ESharpRandomWalkBehavior::RunTurn)
        ? PreparedSettings.RunSpeed : PreparedSettings.TargetSpeed;
}

bool USharpRandomWalkerComponent::TurnsWhileMoving() const
{
    return ActiveBehavior == ESharpRandomWalkBehavior::WalkTurn ||
           ActiveBehavior == ESharpRandomWalkBehavior::RunTurn;
}

float USharpRandomWalkerComponent::GetBehaviorTurnRate() const
{
    if (!TurnsWhileMoving()) { return PreparedSettings.TurnRateDegreesPerSecond; }
    return ActiveBehavior == ESharpRandomWalkBehavior::RunTurn
        ? PreparedSettings.RunTurningRateDegreesPerSecond
        : PreparedSettings.WalkTurningRateDegreesPerSecond;
}

ESharpRandomWalkBehavior USharpRandomWalkerComponent::PickBehavior()
{
    if (PreparedSettings.BehaviorSelection == ESharpRandomWalkBehaviorSelection::Mixed)
    {
        return static_cast<ESharpRandomWalkBehavior>(Random.RandRange(0, 3));
    }
    return static_cast<ESharpRandomWalkBehavior>(
        static_cast<uint8>(PreparedSettings.BehaviorSelection) - 1);
}

void USharpRandomWalkerComponent::BeginStraight()
{
	if (!Enemy || State != ESharpRandomWalkState::Moving) { return; }
	Phase = ESharpRandomWalkPhase::Straight;
    bAvoidanceTurn = false;
    // Distribute 64 actors' wall sweeps across frames instead of all on one tick.
    WallProbeElapsed = (static_cast<float>(Enemy->ExperimentActorId % 8) / 8.0f) * WallProbeIntervalSeconds;
	SegmentStart = Enemy->GetActorLocation();
    SegmentDistance = Random.FRandRange(PreparedSettings.MinimumStraightDistance, PreparedSettings.MaximumStraightDistance);
    ActiveBehavior = PickBehavior();
    Movement->MaxWalkSpeed = GetBehaviorSpeed();
	SegmentProgress = ProgressCheckpoint = StallSeconds = 0.0f;
	++SegmentIndex;
	if (Enemy->Recorder)
	{
        // Preserve existing CSV columns: Behavior_* rows carry the target speed in Value.
        const FString BehaviorCommand = FString::Printf(TEXT("Behavior_%s"),
            *UEnum::GetValueAsString(ActiveBehavior));
        Enemy->Recorder->RecordRandomWalkCommand(SegmentIndex, BehaviorCommand,
            GetBehaviorSpeed(), Enemy->GetActorRotation().Yaw);
		Enemy->Recorder->RecordRandomWalkCommand(SegmentIndex, TEXT("Straight"), SegmentDistance, Enemy->GetActorRotation().Yaw);
	}
	UE_LOG(LogSharpRandomWalk, Verbose, TEXT("[Sharp][RandomWalk] ActorId=%d Segment=%d StraightCm=%.1f"), Enemy->ExperimentActorId, SegmentIndex, SegmentDistance);
}

void USharpRandomWalkerComponent::BeginTurn()
{
	if (!Enemy || State != ESharpRandomWalkState::Moving) { return; }
    // Walking/running turns keep sending input while the facing direction changes.
    // Stopped turns use natural CMC braking, as in the original RandomWalk.
    Phase = TurnsWhileMoving() ? ESharpRandomWalkPhase::Turning : ESharpRandomWalkPhase::Braking;
	// The decisions are made only by the server's own deterministic stream.
	const float Sign = Random.RandRange(0, 1) == 0 ? -1.0f : 1.0f;
	const float Angle = Random.FRandRange(PreparedSettings.MinimumTurnAngle, PreparedSettings.MaximumTurnAngle);
	TargetYaw = FRotator::NormalizeAxis(Forward.Rotation().Yaw + Sign * Angle);
	if (Enemy->Recorder)
	{
		Enemy->Recorder->RecordRandomWalkCommand(SegmentIndex, TEXT("Turn"), Sign * Angle, TargetYaw);
	}
	UE_LOG(LogSharpRandomWalk, Verbose, TEXT("[Sharp][RandomWalk] ActorId=%d Segment=%d TurnDeg=%.1f TargetYaw=%.1f"),
		Enemy->ExperimentActorId, SegmentIndex, Sign * Angle, TargetYaw);
}

void USharpRandomWalkerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!Enemy || !Enemy->HasAuthority() || State != ESharpRandomWalkState::Moving || DeltaTime <= 0.0f) { return; }
	FString Reason;
	if (!ValidateRuntime(Reason)) { Fail(Reason); return; }
	if (Phase == ESharpRandomWalkPhase::Straight)
	{
		const FVector Delta = Enemy->GetActorLocation() - SegmentStart;
		SegmentProgress = FVector::DotProduct(FVector(Delta.X, Delta.Y, 0.0f), Forward);
		if (SegmentProgress >= SegmentDistance)
		{
			BeginTurn();
			return;
		}
		if (Movement->IsFalling() && !PreparedSettings.bAllowInputWhileFalling) { StallSeconds = 0.0f; return; }
        FString ObstacleKind;
        if (DetectObstacle(Forward, DeltaTime, ObstacleKind))
        {
            if (!BeginAvoidance(ObstacleKind))
            {
                Fail(TEXT("RandomWalk could not find a safe route away from ") + ObstacleKind);
            }
            return;
        }
		if (SegmentProgress >= ProgressCheckpoint + PreparedSettings.MinimumProgressDistance)
		{
			ProgressCheckpoint = SegmentProgress;
			StallSeconds = 0.0f;
            if (SegmentProgress >= 100.0f) { ConsecutiveAvoidanceAttempts = 0; }
		}
		else { StallSeconds += DeltaTime; }
		if (PreparedSettings.BlockedTimeoutSeconds > 0.0f && StallSeconds >= PreparedSettings.BlockedTimeoutSeconds)
		{
            if (!bArenaEnabled || !BeginAvoidance(TEXT("Stall")))
            {
                Fail(TEXT("RandomWalk cannot escape stalled movement; check wall collision and arena placement"));
            }
            return;
		}
        Movement->MaxWalkSpeed = GetBehaviorSpeed();
		Enemy->AddMovementInput(Forward);
		return;
	}
	if (Phase == ESharpRandomWalkPhase::Braking)
	{
		// Do not start the turn until CMC has naturally braked; no velocity teleport.
		if (Movement->Velocity.Size2D() <= PreparedSettings.TurnStartSpeedTolerance)
		{
			Phase = ESharpRandomWalkPhase::Turning;
		}
		return;
	}
    const float Current = Enemy->GetActorRotation().Yaw;
    const float NewYaw = FMath::FixedTurn(Current, TargetYaw, GetBehaviorTurnRate() * DeltaTime);
    Enemy->SetActorRotation(FRotator(0.0f, NewYaw, 0.0f));
    Forward = FRotator(0.0f, NewYaw, 0.0f).Vector().GetSafeNormal2D();
    if (TurnsWhileMoving() && !bAvoidanceTurn &&
        (!Movement->IsFalling() || PreparedSettings.bAllowInputWhileFalling))
    {
        FString ObstacleKind;
        if (DetectObstacle(Forward, DeltaTime, ObstacleKind))
        {
            if (!BeginAvoidance(ObstacleKind))
            {
                Fail(TEXT("RandomWalk moving turn could not escape ") + ObstacleKind);
            }
            return;
        }
        Movement->MaxWalkSpeed = GetBehaviorSpeed();
        Enemy->AddMovementInput(Forward);
    }
    if (FMath::Abs(FMath::FindDeltaAngleDegrees(NewYaw, TargetYaw)) <= 0.1f)
    {
        BeginStraight();
    }
}
