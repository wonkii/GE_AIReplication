// Fill out your copyright notice in the Description page of Project Settings.


#include "SharpMovement/Net/SharpPolicyDiagnosticActor.h"
#include "SharpMovement/Net/SharpUpdatePolicy.h"
#include "SharpMovement/Enemy/SharpEnemyCharacter.h"
#include "SharpMovement/Experiment/SharpEnemyExperimentComponent.h"
#include "SharpMoveSnapshot.h"

#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogSharpPolicyDiagnostic, Log, All);

namespace
{
    // 진단 전용 합성 페이로드. 실제 Detector의 사건으로 취급하면 안 된다.
    // 현재 서버 위치/CMC 사후 속도를 읽지만 Sharp Movement 발생 여부를 판정하지 않는다.
    FSharpMoveSnapshot MakeSyntheticSnapshot(
        const ASharpEnemyCharacter& Enemy,
        const UWorld& World,
        uint32 SequenceId)
    {
        FSharpMoveSnapshot Snapshot;
        Snapshot.SequenceId = SequenceId;
		Snapshot.Generation = Enemy.Experiment->Status.Generation;
		Snapshot.Segment = Enemy.Experiment->Status.Segment;
        Snapshot.ServerTime = World.GetTimeSeconds();
        Snapshot.StartPosition = Enemy.GetActorLocation();
        Snapshot.PostVelocity = Enemy.GetVelocity();

        const UCharacterMovementComponent* Movement =
            Enemy.GetCharacterMovement();
        Snapshot.MovementMode = Movement
            ? static_cast<uint8>(Movement->MovementMode)
            : 0;
        Snapshot.PredictionType = ESharpPredictionType::None;
        return Snapshot;
    }
}

ASharpPolicyDiagnosticActor::ASharpPolicyDiagnosticActor()
{
    PrimaryActorTick.bCanEverTick = false;
    SetReplicates(false);
    SetReplicateMovement(false);
}

void ASharpPolicyDiagnosticActor::BeginPlay()
{
    Super::BeginPlay();

    if (!bEnableSyntheticTest)
    {
        return;
    }

    const UWorld* World = GetWorld();

    // PIE의 서버에서만 동작한다. 패키징한 게임이나 클라이언트에서는 비활성.
    if (!World || World->WorldType != EWorldType::PIE ||
        !HasAuthority() ||
        (GetNetMode() != NM_DedicatedServer && GetNetMode() != NM_ListenServer))
    {
        return;
    }

    SearchStartedSeconds = World->GetTimeSeconds();

    UE_LOG(LogSharpPolicyDiagnostic, Warning,
        TEXT("[M5][Diag] SYNTHETIC_TEST_ENABLED TargetActorId=%d "
            "NotARealSharpMovementEvent=1"),
        TargetActorId);

    // Wait for the manually/explicitly started Enemy session to initialize Policy.
    GetWorldTimerManager().SetTimer(
        FindPolicyTimerHandle,
        this,
        &ASharpPolicyDiagnosticActor::TryStartTest,
        0.1f,
        true);
}

void ASharpPolicyDiagnosticActor::TryStartTest()
{
    UWorld* World = GetWorld();
    if (!World || bStarted)
    {
        return;
    }

    for (TActorIterator<ASharpEnemyCharacter> It(World); It; ++It)
    {
        ASharpEnemyCharacter* Candidate = *It;
        if (!IsValid(Candidate) || !Candidate->HasAuthority() ||
            Candidate->ExperimentActorId != TargetActorId)
        {
            continue;
        }

        USharpUpdatePolicy* CandidatePolicy =
            Candidate->FindComponentByClass<USharpUpdatePolicy>();

        if (!IsValid(CandidatePolicy) || !CandidatePolicy->IsPolicyInitialized())
        {
            continue;
        }

        const ESharpExperimentMode Mode = CandidatePolicy->GetActiveMode();
        if (Mode != ESharpExperimentMode::SingleShot &&
            Mode != ESharpExperimentMode::Burst &&
            Mode != ESharpExperimentMode::EventOnly &&
            Mode != ESharpExperimentMode::EventPredictor)
        {
            UE_LOG(LogSharpPolicyDiagnostic, Error,
                TEXT("[M5][Diag] Aborted Reason=UnsupportedMode "
                    "ActorId=%d Mode=%s"),
                TargetActorId,
                *StaticEnum<ESharpExperimentMode>()->GetNameStringByValue(
                    static_cast<int64>(Mode)));
            GetWorldTimerManager().ClearTimer(FindPolicyTimerHandle);
            bStarted = true;
            return;
        }

        TargetEnemy = Candidate;
        TargetPolicy = CandidatePolicy;
        BaselineHz = Candidate->GetNetUpdateFrequency();
        bStarted = true;
        GetWorldTimerManager().ClearTimer(FindPolicyTimerHandle);

        UE_LOG(LogSharpPolicyDiagnostic, Display,
            TEXT("[M5][Diag] Started Actor=%s ActorId=%d "
                "Mode=%s BaselineHz=%.1f SyntheticEvents=1,1,2"),
            *GetNameSafe(Candidate),
            TargetActorId,
            *StaticEnum<ESharpExperimentMode>()->GetNameStringByValue(
                static_cast<int64>(Mode)),
            BaselineHz);

        // 모든 모드에서 첫 진단 입력은 0.2초 후에 전달한다.
        // Snapshot 모드는 두 프로퍼티 갱신 사이에 충분한 시간을 두어
        // 첫 번째 Snapshot을 네트워크에서 관찰하기 쉽게 만든다.
        GetWorldTimerManager().SetTimer(
            FirstEventTimerHandle,
            this,
            &ASharpPolicyDiagnosticActor::SendFirstSyntheticEvent,
            0.2f,
            false);
        return;
    }

    // 배치된 Actor 또는 Policy 설정이 잘못되었을 때 무한 탐색하지 않는다.
    if (World->GetTimeSeconds() - SearchStartedSeconds >= 5.0)
    {
        UE_LOG(LogSharpPolicyDiagnostic, Error,
            TEXT("[M5][Diag] Aborted Reason=PolicyNotReady "
                "TargetActorId=%d TimeoutSeconds=5.0"),
            TargetActorId);
        GetWorldTimerManager().ClearTimer(FindPolicyTimerHandle);
    }
}

void ASharpPolicyDiagnosticActor::SendFirstSyntheticEvent()
{
    if (!IsValid(TargetEnemy.Get()) || !IsValid(TargetPolicy.Get()) ||
        !GetWorld())
    {
        return;
    }

    const ESharpExperimentMode Mode = TargetPolicy->GetActiveMode();
    if (Mode == ESharpExperimentMode::EventOnly ||
        Mode == ESharpExperimentMode::EventPredictor)
    {
        // 이 입력은 실제 Sharp Movement 이벤트가 아니다.
        // 서버 현재 사후 상태를 합성 페이로드로 감싼 테스트 전용 호출이다.
        const FSharpMoveSnapshot Snapshot = MakeSyntheticSnapshot(
            *TargetEnemy.Get(), *GetWorld(), 1u);
        bFirstAccepted = TargetEnemy->PublishDetectedSharpMoveSnapshot(Snapshot);
        bDuplicateAccepted = TargetEnemy->PublishDetectedSharpMoveSnapshot(Snapshot);

        // Seq=3의 무효 사건은 거부되어야 하며, 이후 유효 Seq=2를 막으면 안 된다.
        FSharpMoveSnapshot BadTime = MakeSyntheticSnapshot(
            *TargetEnemy.Get(), *GetWorld(), 3u);
        BadTime.ServerTime = -1.0;
        bInvalidPayloadAccepted =
            TargetEnemy->PublishDetectedSharpMoveSnapshot(BadTime);

        FSharpMoveSnapshot BadPrediction = MakeSyntheticSnapshot(
            *TargetEnemy.Get(), *GetWorld(), 3u);
        BadPrediction.PredictionType = ESharpPredictionType::ConstantVelocity;
        bInvalidPredictionTypeAccepted =
            TargetEnemy->PublishDetectedSharpMoveSnapshot(BadPrediction);

        FSharpMoveSnapshot BadMode = MakeSyntheticSnapshot(
            *TargetEnemy.Get(), *GetWorld(), 3u);
        BadMode.MovementMode = static_cast<uint8>(MOVE_MAX);
        bInvalidMovementModeAccepted =
            TargetEnemy->PublishDetectedSharpMoveSnapshot(BadMode);
    }
    else
    {
        // STEP45의 SingleShot/Burst 검증 경로를 변경하지 않는다.
        const double EventTime = GetWorld()->GetTimeSeconds();
        bFirstAccepted = TargetPolicy->HandleSharpMovementEvent(1u, EventTime);
        bDuplicateAccepted = TargetPolicy->HandleSharpMovementEvent(1u, EventTime);
    }

    FrequencyAfterFirstHz = TargetEnemy->GetNetUpdateFrequency();

    UE_LOG(LogSharpPolicyDiagnostic, Display,
        TEXT("[M5][Diag] FirstAndDuplicate SequenceId=1 "
            "FirstAccepted=%d DuplicateAccepted=%d AfterHz=%.1f "
            "Synthetic=1"),
        bFirstAccepted ? 1 : 0,
        bDuplicateAccepted ? 1 : 0,
        FrequencyAfterFirstHz);

    // EventOnly / EventPredictor에서는 첫 Snapshot이 전송될 틈을 제공한다.
    // 실제 RepNotify 도착/횟수는 클라이언트 로그로 별도 확인해야 한다.
    const float DelaySeconds =
        (Mode == ESharpExperimentMode::EventOnly ||
            Mode == ESharpExperimentMode::EventPredictor) ? 0.6f : 0.1f;
    GetWorldTimerManager().SetTimer(
        SecondEventTimerHandle,
        this,
        &ASharpPolicyDiagnosticActor::SendSecondSyntheticEvent,
        DelaySeconds,
        false);
}

void ASharpPolicyDiagnosticActor::SendSecondSyntheticEvent()
{
    if (!IsValid(TargetEnemy.Get()) || !IsValid(TargetPolicy.Get()) ||
        !GetWorld())
    {
        return;
    }

    const ESharpExperimentMode Mode = TargetPolicy->GetActiveMode();
    if (Mode == ESharpExperimentMode::EventOnly ||
        Mode == ESharpExperimentMode::EventPredictor)
    {
        const FSharpMoveSnapshot Snapshot = MakeSyntheticSnapshot(
            *TargetEnemy.Get(), *GetWorld(), 2u);
        bSecondAccepted = TargetEnemy->PublishDetectedSharpMoveSnapshot(Snapshot);
    }
    else
    {
        bSecondAccepted = TargetPolicy->HandleSharpMovementEvent(
            2u, GetWorld()->GetTimeSeconds());
    }

    FrequencyAfterSecondHz = TargetEnemy->GetNetUpdateFrequency();

    UE_LOG(LogSharpPolicyDiagnostic, Display,
        TEXT("[M5][Diag] Second SequenceId=2 Accepted=%d "
            "AfterHz=%.1f Synthetic=1"),
        bSecondAccepted ? 1 : 0,
        FrequencyAfterSecondHz);

    // Burst(기본 0.25초) 복원 검증까지 유지한다.
    // Snapshot 모드는 두 번째 복제 요청 후 클라이언트 수신 로그를 관찰한다.
    GetWorldTimerManager().SetTimer(
        VerifyTimerHandle,
        this,
        &ASharpPolicyDiagnosticActor::VerifyTestResult,
        0.7f,
        false);
}

void ASharpPolicyDiagnosticActor::VerifyTestResult()
{
    if (!IsValid(TargetEnemy.Get()) || !IsValid(TargetPolicy.Get()))
    {
        return;
    }

    const ESharpExperimentMode Mode = TargetPolicy->GetActiveMode();
    const float EndHz = TargetEnemy->GetNetUpdateFrequency();
    const bool bCorrectEventHandling =
        bFirstAccepted && !bDuplicateAccepted && bSecondAccepted;

    const bool bRestored = FMath::IsNearlyEqual(EndHz, BaselineHz, 0.01f);
    const bool bFrequencyBehaviorOK = Mode == ESharpExperimentMode::Burst
        ? (FrequencyAfterFirstHz > BaselineHz &&
            FrequencyAfterSecondHz > BaselineHz && bRestored)
        : (FMath::IsNearlyEqual(FrequencyAfterFirstHz, BaselineHz, 0.01f) &&
            FMath::IsNearlyEqual(FrequencyAfterSecondHz, BaselineHz, 0.01f) &&
            bRestored);

    const bool bSnapshotMode =
        Mode == ESharpExperimentMode::EventOnly ||
        Mode == ESharpExperimentMode::EventPredictor;
    const bool bInvalidSnapshotsRejected =
        !bInvalidPayloadAccepted &&
        !bInvalidPredictionTypeAccepted &&
        !bInvalidMovementModeAccepted;
    const bool bPassed = bCorrectEventHandling && bFrequencyBehaviorOK &&
        (!bSnapshotMode || bInvalidSnapshotsRejected);

    // EventOnly와 EventPredictor 모두 서버 게시 성공만 판정한다.
    // 두 클라이언트의 OnRep 실제 수신은 별도 로그로 검증해야 한다.
    if (bSnapshotMode)
    {
        const FString ModeName = StaticEnum<ESharpExperimentMode>()
            ->GetNameStringByValue(static_cast<int64>(Mode));
        UE_LOG(LogSharpPolicyDiagnostic, Display,
            TEXT("[M5][Diag] Result Mode=%s ActorId=%d "
                "FirstAccepted=%d DuplicateRejected=%d SecondAccepted=%d "
                "InvalidDataRejected=%d InvalidPredictionRejected=%d "
                "InvalidMovementModeRejected=%d "
                "BaselineHz=%.1f FirstHz=%.1f SecondHz=%.1f EndHz=%.1f "
                "FrequencyCheck=%d Result=%s Synthetic=1 "
                "Scope=ServerPublishOnly ClientReceiptCheck=Separate"),
            *ModeName, TargetActorId,
            bFirstAccepted ? 1 : 0,
            !bDuplicateAccepted ? 1 : 0,
            bSecondAccepted ? 1 : 0,
            !bInvalidPayloadAccepted ? 1 : 0,
            !bInvalidPredictionTypeAccepted ? 1 : 0,
            !bInvalidMovementModeAccepted ? 1 : 0,
            BaselineHz, FrequencyAfterFirstHz,
            FrequencyAfterSecondHz, EndHz,
            bFrequencyBehaviorOK ? 1 : 0,
            bPassed ? TEXT("PASS") : TEXT("FAIL"));
        return;
    }

    UE_LOG(LogSharpPolicyDiagnostic, Display,
        TEXT("[M5][Diag] Result Mode=%s ActorId=%d "
            "FirstAccepted=%d DuplicateRejected=%d SecondAccepted=%d "
            "BaselineHz=%.1f FirstHz=%.1f SecondHz=%.1f EndHz=%.1f "
            "FrequencyCheck=%d Result=%s Synthetic=1"),
        *StaticEnum<ESharpExperimentMode>()->GetNameStringByValue(
            static_cast<int64>(Mode)),
        TargetActorId,
        bFirstAccepted ? 1 : 0,
        !bDuplicateAccepted ? 1 : 0,
        bSecondAccepted ? 1 : 0,
        BaselineHz,
        FrequencyAfterFirstHz,
        FrequencyAfterSecondHz,
        EndHz,
        bFrequencyBehaviorOK ? 1 : 0,
        bPassed ? TEXT("PASS") : TEXT("FAIL"));
}

void ASharpPolicyDiagnosticActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UWorld* World = GetWorld())
    {
        FTimerManager& TimerManager = World->GetTimerManager();
        TimerManager.ClearTimer(FindPolicyTimerHandle);
        TimerManager.ClearTimer(FirstEventTimerHandle);
        TimerManager.ClearTimer(SecondEventTimerHandle);
        TimerManager.ClearTimer(VerifyTimerHandle);
    }

    Super::EndPlay(EndPlayReason);
}
