// Fill out your copyright notice in the Description page of Project Settings.


#include "SharpMovement/Net/SharpDetectorNetworkBridge.h"

#include "SharpMovement/Enemy/SharpEnemyCharacter.h"
#include "SharpMovement/Experiment/SharpExperimentManager.h"
#include "SharpMovement/Experiment/SharpExperimentTypes.h"
#include "SharpMovement/Net/SharpUpdatePolicy.h"
#include "SharpMoveSnapshot.h"

#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"

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
        // EventPredictor도 이 단계에서는 EventOnly와 똑같은 None 메타데이터를 보낸다.
        if (Event.PredictionType != ESharpPredictionType::None)
        {
            return TEXT("PredictionTypeNotNone");
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

    const ASharpExperimentManager* Manager = Cast<ASharpExperimentManager>(
        UGameplayStatics::GetActorOfClass(World, ASharpExperimentManager::StaticClass()));
    const FSharpExperimentIdentity* Identity =
        Manager ? Manager->GetResolvedIdentity() : nullptr;
    const FString RunId = Identity ? Identity->RunId : TEXT("Unknown");
    const int32 TrialId = Identity ? Identity->TrialId : -1;
    const FString ModeName = StaticEnum<ESharpExperimentMode>()
        ->GetNameStringByValue(static_cast<int64>(Mode));

    UE_LOG(LogSharpDetectorNetworkBridge, Display,
        TEXT("[M5][DetectorBridge] RunId=%s TrialId=%d ActorId=%d Mode=%s "
            "SequenceId=%u EventServerTime=%.6f RouteWorldTime=%.6f "
            "Requested=%d Action=%s"),
        *RunId, TrialId, Enemy->ExperimentActorId, *ModeName,
        ObservedEvent.SequenceId, ObservedEvent.ServerTime,
        World->GetTimeSeconds(), bRequested ? 1 : 0,
        bRequested ? TEXT("PolicyOrSnapshotRequest") : TEXT("RejectedByPolicy"));

    return bRequested
        ? ESharpDetectorNetworkDispatchResult::Requested
        : ESharpDetectorNetworkDispatchResult::Rejected;
}
