#include "SharpSnapshotChannelComponent.h"
#include "../Enemy/SharpEnemyCharacter.h"
#include "../Experiment/SharpEnemyExperimentComponent.h"
#include "../Detector/SharpMovementDetectorComponent.h"
#include "SharpUpdatePolicy.h"
#include "../Measurement/SharpMovementRecorderComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogSharpSnapshot, Log, All);

namespace
{
    bool IsPayloadValid(const FSharpMoveSnapshot& Value)
    {
        return Value.SequenceId != 0 && Value.Generation > 0 && Value.Segment > 0 &&
            FMath::IsFinite(Value.ServerTime) && Value.ServerTime >= 0 &&
            !Value.StartPosition.ContainsNaN() && !Value.StartRotation.ContainsNaN() &&
            !Value.PostVelocity.ContainsNaN() && !Value.Gravity.ContainsNaN() &&
            Value.MovementMode < MOVE_MAX &&
            static_cast<uint8>(Value.PredictionType) <= static_cast<uint8>(ESharpPredictionType::Airborne) &&
            static_cast<uint8>(Value.Kind) <= static_cast<uint8>(ESharpSnapshotKind::RecoveryKeyframe);
    }

    bool IsEventMode(ESharpExperimentMode Mode)
    {
        return Mode == ESharpExperimentMode::EventOnly || Mode == ESharpExperimentMode::EventPredictor;
    }

    FVector Extrapolate(const FSharpMoveSnapshot& Anchor, double ServerNow, float Horizon)
    {
        const double Age = FMath::Clamp(ServerNow - Anchor.ServerTime, 0.0, static_cast<double>(Horizon));
        if (Anchor.PredictionType == ESharpPredictionType::None) { return Anchor.StartPosition; }
        FVector Position = Anchor.StartPosition + Anchor.PostVelocity * Age;
        if (Anchor.PredictionType == ESharpPredictionType::Airborne)
        {
            Position += Anchor.Gravity * (0.5 * Age * Age);
        }
        return Position;
    }
}

USharpSnapshotChannelComponent::USharpSnapshotChannelComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
    SetIsReplicatedByDefault(true);
}

void USharpSnapshotChannelComponent::BeginPlay()
{
    Super::BeginPlay();
    if (const ASharpEnemyCharacter* Enemy = Cast<ASharpEnemyCharacter>(GetOwner()))
    {
        // Client CMC is intentionally tick-disabled during event trials.
        // Do not create a tick prerequisite on a disabled client component.
        if (Enemy->HasAuthority()) { AddTickPrerequisiteComponent(Enemy->GetCharacterMovement()); }
    }
    HandleSessionChanged();
}

void USharpSnapshotChannelComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(USharpSnapshotChannelComponent, Snapshot);
}

bool USharpSnapshotChannelComponent::Publish(const FSharpMoveSnapshot& Value)
{
    ASharpEnemyCharacter* Enemy = Cast<ASharpEnemyCharacter>(GetOwner());
    if (!Enemy || !Enemy->HasAuthority() || !Enemy->Experiment->IsObservingMovement() ||
        !Enemy->UpdatePolicy->IsPolicyInitialized() || !IsPayloadValid(Value) ||
        Value.SequenceId <= LastPublishedSequence ||
        Value.Generation != Enemy->Experiment->Status.Generation || Value.Segment != Enemy->Experiment->Status.Segment ||
        Value.ServerTime < Enemy->Experiment->Status.SegmentStartTime ||
        Value.ServerTime > GetWorld()->GetTimeSeconds() + KINDA_SMALL_NUMBER)
    {
        UE_LOG(LogSharpSnapshot, Warning, TEXT("[Sharp][EventTransport] PublishRejected Actor=%s Seq=%u Generation=%d"),
            *GetNameSafe(Enemy), Value.SequenceId, Value.Generation);
        return false;
    }
    const ESharpExperimentMode Mode = Enemy->UpdatePolicy->GetActiveMode();
    if (!IsEventMode(Mode) || Enemy->IsReplicatingMovement())
    {
        UE_LOG(LogSharpSnapshot, Error, TEXT("[Sharp][EventTransport] Rejected Actor=%s Reason=FRepMovementStillEnabledOrWrongMode"), *GetNameSafe(Enemy));
        return false;
    }
    LastPublishedSequence = Value.SequenceId;
    LastPublishedServerTime = Value.ServerTime;
    Snapshot = Value;
    // This is a latest-state property, not a reliable per-event RPC. Multiple
    // detector events in one net period may intentionally coalesce.
    Enemy->ForceNetUpdate();
    UE_LOG(LogSharpSnapshot, Verbose,
        TEXT("[Sharp][EventTransport] Published ActorId=%d Mode=%s Kind=%d Seq=%u G=%d S=%d Time=%.3f"),
        Enemy->ExperimentActorId, *UEnum::GetValueAsString(Mode), static_cast<int32>(Value.Kind),
        Value.SequenceId, Value.Generation, Value.Segment, Value.ServerTime);
    return true;
}

void USharpSnapshotChannelComponent::Invalidate()
{
    if (GetOwner() && GetOwner()->HasAuthority())
    {
        Snapshot = FSharpMoveSnapshot();
        LastPublishedServerTime = -1.0;
    }
}

void USharpSnapshotChannelComponent::UpdateClientMovementOwnership()
{
    ASharpEnemyCharacter* Enemy = Cast<ASharpEnemyCharacter>(GetOwner());
    if (!Enemy || Enemy->HasAuthority() || Enemy->GetLocalRole() != ROLE_SimulatedProxy) { return; }
    const FSharpEnemyTrialStatus& Status = Enemy->Experiment->Status;
    const bool bActive = Status.State == ESharpEnemyTrialState::Running || Status.State == ESharpEnemyTrialState::Suspended;
    const bool bWantsEventTransport = bActive && IsEventMode(Status.Identity.Mode);
    const bool bNewContext = ActiveGeneration != Status.Generation || ActiveSegment != Status.Segment;

    if (bClientEventMode && !bWantsEventTransport) { ReleaseClientMovementOwnership(); }
    if (bWantsEventTransport && !bClientEventMode)
    {
        UCharacterMovementComponent* Movement = Enemy->GetCharacterMovement();
        bSavedCMCTickEnabled = Movement->IsComponentTickEnabled();
        SavedSmoothing = Movement->NetworkSmoothingMode;
        Movement->SetComponentTickEnabled(false); // No second competing proxy simulation.
        Movement->NetworkSmoothingMode = ENetworkSmoothingMode::Disabled;
        Enemy->GetMesh()->SetRelativeLocationAndRotation(Enemy->GetBaseTranslationOffset(), Enemy->GetBaseRotationOffset());
        bClientEventMode = true;
        UE_LOG(LogSharpSnapshot, Display, TEXT("[Sharp][EventTransport] ProxyEnabled ActorId=%d ReplicateMovement=%d CMC_Tick=0"),
            Enemy->ExperimentActorId, Enemy->IsReplicatingMovement() ? 1 : 0);
    }
    if (bNewContext)
    {
        ActiveGeneration = Status.Generation;
        ActiveSegment = Status.Segment;
        bHasAnchor = false;
        bPredictionWithinHorizon = false;
        CorrectionOffset = FVector::ZeroVector;
        CorrectionElapsed = 0.0f;
    }
    bPredictorMode = bWantsEventTransport && Status.Identity.Mode == ESharpExperimentMode::EventPredictor && Status.Prediction.bEnablePrediction;
    SetComponentTickEnabled(bWantsEventTransport);
}

void USharpSnapshotChannelComponent::ReleaseClientMovementOwnership()
{
    if (!bClientEventMode) { return; }
    if (ASharpEnemyCharacter* Enemy = Cast<ASharpEnemyCharacter>(GetOwner()))
    {
        UCharacterMovementComponent* Movement = Enemy->GetCharacterMovement();
        if (Movement)
        {
            Movement->NetworkSmoothingMode = SavedSmoothing;
            Movement->SetComponentTickEnabled(bSavedCMCTickEnabled);
            if (FNetworkPredictionData_Client_Character* Data = Movement->GetPredictionData_Client_Character())
            {
                Data->MeshTranslationOffset = FVector::ZeroVector;
                Data->OriginalMeshTranslationOffset = FVector::ZeroVector;
                Data->MeshRotationOffset = FQuat::Identity;
                Data->MeshRotationTarget = FQuat::Identity;
            }
        }
        if (Enemy->GetMesh())
        {
            Enemy->GetMesh()->SetRelativeLocationAndRotation(Enemy->GetBaseTranslationOffset(), Enemy->GetBaseRotationOffset());
        }
        UE_LOG(LogSharpSnapshot, Display, TEXT("[Sharp][EventTransport] ProxyRestored ActorId=%d CMC_Tick=%d"),
            Enemy->ExperimentActorId, bSavedCMCTickEnabled ? 1 : 0);
    }
    bClientEventMode = false;
    bPredictorMode = false;
    bHasAnchor = false;
    bPredictionWithinHorizon = false;
    SetComponentTickEnabled(false);
}

void USharpSnapshotChannelComponent::HandleSessionChanged()
{
    ASharpEnemyCharacter* Enemy = Cast<ASharpEnemyCharacter>(GetOwner());
    if (!Enemy || !Enemy->Experiment) { return; }
    if (Enemy->HasAuthority())
    {
        const FSharpEnemyTrialStatus& Status = Enemy->Experiment->Status;
        const bool bActive = Status.State == ESharpEnemyTrialState::Running || Status.State == ESharpEnemyTrialState::Suspended;
        const bool bUseEvent = bActive && IsEventMode(Status.Identity.Mode);
        SetComponentTickEnabled(bUseEvent);
        if (bUseEvent) { SetComponentTickInterval(0.05f); }
    }
    else
    {
        UpdateClientMovementOwnership();
        // RepNotify order across components is not guaranteed. Retry the
        // most recently received snapshot once its trial context is present.
        OnRep_Snapshot();
    }
}

void USharpSnapshotChannelComponent::OnRep_Snapshot()
{
    ASharpEnemyCharacter* Enemy = Cast<ASharpEnemyCharacter>(GetOwner());
    if (!Enemy || Enemy->HasAuthority() || Enemy->GetLocalRole() != ROLE_SimulatedProxy || !IsPayloadValid(Snapshot)) { return; }
    const FSharpEnemyTrialStatus& Status = Enemy->Experiment->Status;
    if (!IsEventMode(Status.Identity.Mode) ||
        (Status.State != ESharpEnemyTrialState::Running && Status.State != ESharpEnemyTrialState::Suspended)) { return; }
    if (Snapshot.Generation > Status.Generation ||
        (Snapshot.Generation == Status.Generation && Snapshot.Segment > Status.Segment)) { return; }
    if (Snapshot.Generation != Status.Generation || Snapshot.Segment != Status.Segment ||
        Status.SegmentStartTime < 0 || Snapshot.ServerTime < Status.SegmentStartTime ||
        (Status.ServerEndTime >= 0 && Snapshot.ServerTime > Status.ServerEndTime) ||
        Snapshot.SequenceId <= LastObservedSequence) { return; }

    UpdateClientMovementOwnership();
    if (!bClientEventMode || !GetWorld()->GetGameState()) { return; }
    const double ServerNow = GetWorld()->GetGameState()->GetServerWorldTimeSeconds();
    const double Age = ServerNow - Snapshot.ServerTime;
    if (LastObservedSequence > 0 && LastObservedGeneration == Snapshot.Generation && LastObservedSegment == Snapshot.Segment &&
        Snapshot.SequenceId > LastObservedSequence + 1)
    {
        Enemy->Recorder->RecordDiagnostic(TEXT("SnapshotSequenceGap"), Snapshot.SequenceId, Snapshot.ServerTime,
            Snapshot.SequenceId - LastObservedSequence - 1, 0, TEXT("LatestStateCoalesced"));
    }
    LastObservedSequence = Snapshot.SequenceId;
    LastObservedGeneration = Snapshot.Generation;
    LastObservedSegment = Snapshot.Segment;

    if (Enemy->IsReplicatingMovement())
    {
        Enemy->Recorder->RecordDiagnostic(TEXT("EventTransportUnexpectedFRepMovement"),
            Snapshot.SequenceId, Snapshot.ServerTime, 1.0, 0, TEXT("CheckClientFlagAndModeTransition"));
    }
    UCharacterMovementComponent* Movement = Enemy->GetCharacterMovement();
    Movement->SetMovementMode(static_cast<EMovementMode>(Snapshot.MovementMode), Snapshot.CustomMovementMode);
    Movement->Velocity = bPredictorMode ? Snapshot.PostVelocity : FVector::ZeroVector;

    if (bPredictorMode)
    {
        const float Horizon = Status.Prediction.EventExtrapolationHorizonSeconds;
        const FVector Target = Extrapolate(Snapshot, ServerNow, Horizon);
        // Keep presentation continuous during ordinary corrections, but bound
        // the divergence so a late packet cannot postpone an important snap.
        CorrectionOffset = bHasAnchor
            ? (Enemy->GetActorLocation() - Target).GetClampedToMaxSize(Status.Prediction.MaximumVisualOffset)
            : FVector::ZeroVector;
        CorrectionElapsed = 0.0f;
        Anchor = Snapshot;
        bHasAnchor = true;
        ApplyClientState(0.0f);
    }
    else
    {
        // Strict EventOnly: NO between-event extrapolation. Movement may
        // intentionally appear stepped if detector thresholds are sparse.
        Anchor = Snapshot;
        bHasAnchor = true;
        bPredictionWithinHorizon = false;
        Enemy->SetActorLocationAndRotation(Snapshot.StartPosition, Snapshot.StartRotation,
            false, nullptr, ETeleportType::TeleportPhysics);
    }
    Enemy->Recorder->ObserveEventReceipt(Snapshot.ServerTime, Snapshot.SequenceId);
    Enemy->Recorder->RecordDiagnostic(TEXT("SnapshotApplied"), Snapshot.SequenceId, Snapshot.ServerTime,
        Age, 0, UEnum::GetValueAsString(Snapshot.Kind));
}

void USharpSnapshotChannelComponent::ApplyClientState(float DeltaSeconds)
{
    ASharpEnemyCharacter* Enemy = Cast<ASharpEnemyCharacter>(GetOwner());
    if (!Enemy || !bClientEventMode || !bPredictorMode || !bHasAnchor) { return; }
    const AGameStateBase* GameState = GetWorld()->GetGameState();
    if (!GameState) { return; }
    const FSharpVisualPredictionSettings& Settings = Enemy->Experiment->Status.Prediction;
    const double ServerNow = GameState->GetServerWorldTimeSeconds();
    const double Age = FMath::Max(0.0, ServerNow - Anchor.ServerTime);
    const float Horizon = Settings.EventExtrapolationHorizonSeconds;
    bPredictionWithinHorizon = Anchor.PredictionType != ESharpPredictionType::None && Age < Horizon;
    CorrectionElapsed += DeltaSeconds;
    const float Remaining = Settings.CorrectionSeconds <= KINDA_SMALL_NUMBER ? 0.0f :
        1.0f - FMath::Clamp(CorrectionElapsed / Settings.CorrectionSeconds, 0.0f, 1.0f);
    const FVector Target = Extrapolate(Anchor, ServerNow, Horizon) + CorrectionOffset * Remaining;
    if (!Target.ContainsNaN())
    {
        // A simulated proxy is not authoritative: the server handles collision;
        // periodic keyframes correct divergence near walls and impacts.
        Enemy->SetActorLocationAndRotation(Target, Anchor.StartRotation,
            false, nullptr, ETeleportType::TeleportPhysics);
    }
    Enemy->GetCharacterMovement()->Velocity = bPredictionWithinHorizon ? Anchor.PostVelocity : FVector::ZeroVector;
}

void USharpSnapshotChannelComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    ASharpEnemyCharacter* Enemy = Cast<ASharpEnemyCharacter>(GetOwner());
    if (!Enemy || !Enemy->Experiment) { return; }
    const FSharpEnemyTrialStatus& Status = Enemy->Experiment->Status;
    if (!IsEventMode(Status.Identity.Mode) || !Enemy->Experiment->IsObservingMovement()) { return; }

    if (Enemy->HasAuthority())
    {
        const float Interval = Status.Prediction.EventRecoveryIntervalSeconds;
        if (Interval > 0 && LastPublishedServerTime >= 0 &&
            GetWorld()->GetTimeSeconds() - LastPublishedServerTime >= Interval)
        {
            Enemy->Detector->PublishStateKeyframe(false);
        }
    }
    else if (Enemy->GetLocalRole() == ROLE_SimulatedProxy)
    {
        UpdateClientMovementOwnership();
        if (!bHasAnchor)
        {
            // Handles the case in which the GameState arrived after Snapshot.
            OnRep_Snapshot();
        }
        ApplyClientState(DeltaTime);
    }
}

void USharpSnapshotChannelComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    ReleaseClientMovementOwnership();
    Super::EndPlay(Reason);
}
