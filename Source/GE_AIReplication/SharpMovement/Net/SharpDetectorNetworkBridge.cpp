// Fill out your copyright notice in the Description page of Project Settings.


#include "SharpMovement/Net/SharpDetectorNetworkBridge.h"

#include "SharpMovement/Enemy/SharpEnemyCharacter.h"
#include "SharpMovement/Experiment/SharpEnemyExperimentComponent.h"
#include "SharpMovement/Experiment/SharpExperimentTypes.h"
#include "SharpMovement/Net/SharpUpdatePolicy.h"
#include "SharpMoveSnapshot.h"

#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogSharpDetectorNetworkBridge, Log, All);

namespace
{
    bool IsFiniteVector(const FVector& Vector)
    {
        return FMath::IsFinite(Vector.X) &&
            FMath::IsFinite(Vector.Y) &&
            FMath::IsFinite(Vector.Z);
    }

    const TCHAR* ValidateObservedEvent(const FSharpMoveSnapshot& Event)
    {
        if (Event.SequenceId == 0)
        {
            return TEXT("InvalidSequenceId");
        }
        if (!FMath::IsFinite(Event.ServerTime) || Event.ServerTime < 0.0)
        {
            return TEXT("InvalidServerTime");
        }
        if (!IsFiniteVector(Event.StartPosition) || !IsFiniteVector(Event.PostVelocity))
        {
            return TEXT("NonFiniteMovementState");
        }
        if (Event.MovementMode >= static_cast<uint8>(MOVE_MAX))
        {
            return TEXT("InvalidMovementMode");
        }
        if (Event.Gravity.ContainsNaN() || static_cast<uint8>(Event.PredictionType) > static_cast<uint8>(ESharpPredictionType::Airborne))
        {
            return TEXT("InvalidPredictionPayload");
        }
        return nullptr;
    }
}

ESharpDetectorNetworkDispatchResult FSharpDetectorNetworkBridge::DispatchDetectedEvent(
    ASharpEnemyCharacter* Enemy,
    const FSharpMoveSnapshot& ObservedEvent)
{
    if (!IsValid(Enemy) || !Enemy->HasAuthority())
    {
        UE_LOG(LogSharpDetectorNetworkBridge, Warning,
            TEXT("[M5][DetectorBridge] Rejected Reason=MissingEnemyOrNotAuthority"));
        return ESharpDetectorNetworkDispatchResult::Rejected;
    }
	const FSharpEnemyTrialStatus& Status = Enemy->Experiment->Status;
	FString BindingReason;
	if (!Enemy->Experiment->IsObservingMovement() || !Enemy->Experiment->ValidateBinding(BindingReason) ||
		ObservedEvent.Generation != Status.Generation || ObservedEvent.Segment != Status.Segment ||
		ObservedEvent.ServerTime < Status.SegmentStartTime ||
		ObservedEvent.ServerTime > Enemy->GetWorld()->GetTimeSeconds() + KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogSharpDetectorNetworkBridge, Warning, TEXT("[M5][DetectorBridge] Rejected ActorId=%d Reason=InactiveOrStaleExecution"), Enemy->ExperimentActorId);
		return ESharpDetectorNetworkDispatchResult::Rejected;
	}

    USharpUpdatePolicy* Policy = Enemy->FindComponentByClass<USharpUpdatePolicy>();
    if (!IsValid(Policy) || !Policy->IsPolicyInitialized())
    {
        UE_LOG(LogSharpDetectorNetworkBridge, Warning,
            TEXT("[M5][DetectorBridge] Rejected ActorId=%d Reason=PolicyNotReady"),
            Enemy->ExperimentActorId);
        return ESharpDetectorNetworkDispatchResult::Rejected;
    }

    const ESharpExperimentMode Mode = Policy->GetActiveMode();

    // Baseline에서는 네트워크 이벤트 개입을 하지 않는다.
    // Detector와 Ground Truth의 관찰 자체는 계속 가능하다.
    if (Mode == ESharpExperimentMode::Default ||
        Mode == ESharpExperimentMode::Adaptive ||
        Mode == ESharpExperimentMode::AlwaysHigh)
    {
        UE_LOG(LogSharpDetectorNetworkBridge, Display,
            TEXT("[M5][DetectorBridge] BaselineSkipped ActorId=%d SequenceId=%u "
                "Action=NoExtraReplication"),
            Enemy->ExperimentActorId, ObservedEvent.SequenceId);
        return ESharpDetectorNetworkDispatchResult::BaselineSkipped;
    }

    const TCHAR* InvalidReason = ValidateObservedEvent(ObservedEvent);
    if (InvalidReason)
    {
        UE_LOG(LogSharpDetectorNetworkBridge, Warning,
            TEXT("[M5][DetectorBridge] Rejected ActorId=%d SequenceId=%u Reason=%s"),
            Enemy->ExperimentActorId, ObservedEvent.SequenceId, InvalidReason);
        return ESharpDetectorNetworkDispatchResult::Rejected;
    }

    const UWorld* World = Enemy->GetWorld();
    if (!World)
    {
        return ESharpDetectorNetworkDispatchResult::Rejected;
    }

    // 네트워크 로그는 요청/거부 사실만 기록한다. 실제 packets/s 또는 bytes/s가 아니다.
    bool bRequested = false;
    switch (Mode)
    {
    case ESharpExperimentMode::SingleShot:
    case ESharpExperimentMode::Burst:
        bRequested = Policy->HandleSharpMovementEvent(
            ObservedEvent.SequenceId, ObservedEvent.ServerTime);
        break;

    case ESharpExperimentMode::EventOnly:
    case ESharpExperimentMode::EventPredictor:
        bRequested = Enemy->PublishDetectedSharpMoveSnapshot(ObservedEvent);
        break;

    default:
        UE_LOG(LogSharpDetectorNetworkBridge, Warning,
            TEXT("[M5][DetectorBridge] Rejected ActorId=%d Reason=UnsupportedMode"),
            Enemy->ExperimentActorId);
        return ESharpDetectorNetworkDispatchResult::Rejected;
    }

    const FString& RunId = Status.Identity.RunId;
    const int32 TrialId = Status.Identity.TrialId;
    const FString ModeName = StaticEnum<ESharpExperimentMode>()
        ->GetNameStringByValue(static_cast<int64>(Mode));

    UE_LOG(LogSharpDetectorNetworkBridge, Display,
        TEXT("[M5][DetectorBridge] RunId=%s TrialId=%d ActorId=%d Mode=%s "
            "SequenceId=%u EventServerTime=%.6f RouteWorldTime=%.6f "
            "Requested=%d Action=%s Generation=%d Segment=%d"),
        *RunId, TrialId, Enemy->ExperimentActorId, *ModeName,
        ObservedEvent.SequenceId, ObservedEvent.ServerTime,
        World->GetTimeSeconds(), bRequested ? 1 : 0,
        bRequested ? TEXT("PolicyOrSnapshotRequest") : TEXT("RejectedByPolicy"), Status.Generation, Status.Segment);

    return bRequested
        ? ESharpDetectorNetworkDispatchResult::Requested
        : ESharpDetectorNetworkDispatchResult::Rejected;
}
