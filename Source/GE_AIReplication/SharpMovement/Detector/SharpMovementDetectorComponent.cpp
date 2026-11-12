#include "SharpMovementDetectorComponent.h"
#include "../Enemy/SharpEnemyCharacter.h"
#include "../Experiment/SharpEnemyExperimentComponent.h"
#include "../Experiment/SharpExperimentSettings.h"
#include "../Measurement/SharpMovementRecorderComponent.h"
#include "../Net/SharpDetectorNetworkBridge.h"
#include "../Net/SharpUpdatePolicy.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"

void USharpMovementDetectorComponent::BeginPlay()
{
	Super::BeginPlay(); Enemy = Cast<ASharpEnemyCharacter>(GetOwner());
	if (Enemy && Enemy->HasAuthority()) { Enemy->OnCharacterMovementUpdated.AddUniqueDynamic(this, &USharpMovementDetectorComponent::ObserveMovement); }
}

FSharpMovementSample USharpMovementDetectorComponent::Sample() const
{
	FSharpMovementSample Result;
	Result.ServerTime = GetWorld()->GetTimeSeconds(); Result.Position = Enemy->GetActorLocation(); Result.Velocity = Enemy->GetVelocity();
	Result.MovementMode = static_cast<uint8>(Enemy->GetCharacterMovement()->MovementMode);
	Result.CustomMode = Enemy->GetCharacterMovement()->CustomMovementMode;
	return Result;
}

void USharpMovementDetectorComponent::Start(const USharpExperimentSettings& Settings)
{
	if (!Enemy || !Enemy->HasAuthority()) { return; }
	Config = Settings.Detection.MakeConfig(Settings.VelocityChangeThreshold, Settings.DirectionChangeThresholdDegrees);
	bAirborneModel = Settings.Prediction.bEnableAirborneModel;
	Classifier.Reset(); Classifier.Observe(Sample(), Config); bObserving = true;
}

void USharpMovementDetectorComponent::Stop()
{
	bObserving = false; Classifier.Reset();
}

bool USharpMovementDetectorComponent::BuildSnapshot(const FSharpMovementSample& Observed, FSharpMoveSnapshot& Event)
{
    if (!Enemy || !Enemy->HasAuthority() || !bObserving || !Enemy->Experiment->IsObservingMovement()) { return false; }
    if (Sequence == MAX_uint32)
    {
        Enemy->Experiment->Fail(TEXT("Detector/state sequence exhausted; start a new world"));
        return false;
    }
    Event.SequenceId = ++Sequence;
    Event.Generation = Enemy->Experiment->Status.Generation;
    Event.Segment = Enemy->Experiment->Status.Segment;
    Event.ServerTime = Observed.ServerTime;
    Event.StartPosition = Observed.Position;
    Event.StartRotation = Enemy->GetActorRotation();
    Event.PostVelocity = Observed.Velocity;
    Event.MovementMode = Observed.MovementMode;
    Event.CustomMovementMode = Observed.CustomMode;
    Event.Gravity = FVector(0, 0, Enemy->GetCharacterMovement()->GetGravityZ());
    if (Observed.MovementMode == MOVE_Walking || Observed.MovementMode == MOVE_NavWalking)
    {
        Event.PredictionType = ESharpPredictionType::ConstantVelocity;
    }
    else if (Observed.MovementMode == MOVE_Falling && bAirborneModel)
    {
        Event.PredictionType = ESharpPredictionType::Airborne;
    }
    return true;
}

bool USharpMovementDetectorComponent::PublishStateKeyframe(bool bInitial)
{
    if (!Enemy || !Enemy->HasAuthority() || !bObserving || !Enemy->UpdatePolicy || !Enemy->UpdatePolicy->IsPolicyInitialized())
    {
        return false;
    }
    const ESharpExperimentMode Mode = Enemy->UpdatePolicy->GetActiveMode();
    if (Mode != ESharpExperimentMode::EventOnly && Mode != ESharpExperimentMode::EventPredictor) { return false; }
    FSharpMoveSnapshot State;
    if (!BuildSnapshot(Sample(), State)) { return false; }
    State.Kind = bInitial ? ESharpSnapshotKind::InitialState : ESharpSnapshotKind::RecoveryKeyframe;
    const bool bSent = Enemy->PublishDetectedSharpMoveSnapshot(State);
    Enemy->Recorder->RecordDiagnostic(bInitial ? TEXT("EventInitialPublish") : TEXT("EventRecoveryPublish"),
        State.SequenceId, State.ServerTime, bSent ? 1.0 : 0.0, 0, bSent ? TEXT("Accepted") : TEXT("Rejected"));
    return bSent;
}

void USharpMovementDetectorComponent::ObserveMovement(float DeltaSeconds, FVector OldLocation, FVector OldVelocity)
{
	if (!bObserving || !Enemy || !Enemy->HasAuthority() || !Enemy->Experiment->IsObservingMovement()) { return; }
	const FSharpMovementSample Observed = Sample();
	const double Begin = FPlatformTime::Seconds();
	const FSharpClassification Result = Classifier.Observe(Observed, Config);
	const double Cost = (FPlatformTime::Seconds() - Begin) * 1000000.0;
	FString Route = TEXT("NoEvent");
	if (Result.bSharpEvent)
	{
        FSharpMoveSnapshot Event;
        if (!BuildSnapshot(Observed, Event)) { return; }
        Event.Kind = ESharpSnapshotKind::DetectorEvent;
        const ESharpDetectorNetworkDispatchResult Dispatch = FSharpDetectorNetworkBridge::DispatchDetectedEvent(Enemy, Event);
        Route = Dispatch == ESharpDetectorNetworkDispatchResult::Requested ? TEXT("Requested") :
            Dispatch == ESharpDetectorNetworkDispatchResult::BaselineSkipped ? TEXT("BaselineSkipped") : TEXT("Rejected");
	}
	Enemy->Recorder->RecordDetection(Sequence, Result, Cost, Route);
}

void USharpMovementDetectorComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	Stop();
	if (Enemy) { Enemy->OnCharacterMovementUpdated.RemoveDynamic(this, &USharpMovementDetectorComponent::ObserveMovement); }
	Super::EndPlay(Reason);
}
