// Fill out your copyright notice in the Description page of Project Settings.


#include "SharpMovement/Enemy/SharpEnemyCharacter.h"
#include "SharpMovement/Enemy/SharpEnemyAIController.h"
#include "SharpMovement/Experiment/SharpExperimentManager.h"
#include "SharpMovement/Net/SharpUpdatePolicy.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/ReplicatedState.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogSharpEnemyM1, Log, All);

// 로그에서 NetMode를 읽기 쉬운 문자열로 출력
namespace
{
    const TCHAR* GetNetModeText(ENetMode Mode)
    {
        switch (Mode)
        {
        case NM_Standalone:      return TEXT("Standalone");
        case NM_DedicatedServer: return TEXT("DedicatedServer");
        case NM_ListenServer:    return TEXT("ListenServer");
        case NM_Client:          return TEXT("Client");
        default:                 return TEXT("Unknown");
        }
    }

    const TCHAR* GetRoleText(ENetRole Role)
    {
        switch (Role)
        {
        case ROLE_Authority:       return TEXT("Authority");
        case ROLE_AutonomousProxy: return TEXT("AutonomousProxy");
        case ROLE_SimulatedProxy:  return TEXT("SimulatedProxy");
        default:                   return TEXT("None");
        }
    }

    // 서버 게시/클라이언트 수신이 공유하는 최소 페이로드 검증.
    // 별도 Predictor 구현 전에는 두 Mode 모두 PredictionType=None을 보낸다.
    bool IsValidEventSnapshotPayload(
        const FSharpMoveSnapshot& Snapshot,
        const TCHAR*& OutReason)
    {
        OutReason = TEXT("None");
        if (!FMath::IsFinite(Snapshot.ServerTime) || Snapshot.ServerTime < 0.0 ||
            Snapshot.StartPosition.ContainsNaN() ||
            Snapshot.PostVelocity.ContainsNaN())
        {
            OutReason = TEXT("InvalidSnapshotData");
            return false;
        }
        if (Snapshot.MovementMode >= static_cast<uint8>(MOVE_MAX))
        {
            OutReason = TEXT("InvalidMovementMode");
            return false;
        }
        if (Snapshot.PredictionType != ESharpPredictionType::None)
        {
            OutReason = TEXT("PredictionTypeNotNone");
            return false;
        }
        return true;
    }
}

// 생성자
ASharpEnemyCharacter::ASharpEnemyCharacter()
{
    // Actor 자체의 네트워크 복제 활성화
    SetReplicates(true);

    // Actor 이동 정보 복제 활성화
    SetReplicateMovement(true);

    // 이 Enemy가 사용할 전용 AIController 지정
    AIControllerClass = ASharpEnemyAIController::StaticClass();

    // 맵에 배치되거나 Spawn되었을 때 AIController 자동 Possess
    AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

    // 클라이언트 관찰 함수 실행을 위한 Tick 활성화
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;

    // M5: Enemy별로 독립적인 전송 Policy 기본 컴포넌트 생성.
    // 이 컴포넌트는 자체 프로퍼티를 복제하지 않으며,
    // Default 모드에서 Actor/CMC의 복제 동작을 변경하지 않는다.
    UpdatePolicy = CreateDefaultSubobject<USharpUpdatePolicy>(
        TEXT("SharpUpdatePolicy"));

    // Default 기준선 보존:
    // NetUpdateFrequency와 CMC Smoothing은 변경하지 않음
}

void ASharpEnemyCharacter::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps
) const
{
    // ACharacter의 기존 Replication 등록 유지
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    // 실험 Enemy의 고유 ActorId를 서버에서 클라이언트로 복제
    DOREPLIFETIME(
        ASharpEnemyCharacter,
        ExperimentActorId
    );

    // M5: 단일 Snapshot의 프로퍼티 복제(기본 이동 복제는 그대로 유지).
    DOREPLIFETIME(
        ASharpEnemyCharacter,
        ReplicatedSharpMoveSnapshot
    );
}

// 게임 시작 시 실행
void ASharpEnemyCharacter::BeginPlay()
{
    Super::BeginPlay();

    const UCharacterMovementComponent* Movement =
        GetCharacterMovement();

    const int32 MovementMode = Movement
        ? static_cast<int32>(Movement->MovementMode) : -1;

    const int32 SmoothingMode = Movement
        ? static_cast<int32>(Movement->NetworkSmoothingMode) : -1;

    UE_LOG(LogSharpEnemyM1, Display,
        TEXT("[M1][Enemy] Actor=%s NetMode=%s Role=%s "
            "Authority=%d Controller=%s "
            "Position=%s Velocity=%s MovementMode=%d "
            "Replicates=%d ReplicateMovement=%d "
            "NetUpdateHz=%.1f MinNetUpdateHz=%.1f "
            "Dormancy=%d AlwaysRelevant=%d Smoothing=%d"),
        *GetName(),
        GetNetModeText(GetNetMode()),
        GetRoleText(GetLocalRole()),
        HasAuthority() ? 1 : 0,
        *GetNameSafe(GetController()),
        *GetActorLocation().ToCompactString(),
        *GetVelocity().ToCompactString(),
        MovementMode,
        GetIsReplicated() ? 1 : 0,
        IsReplicatingMovement() ? 1 : 0,
        GetNetUpdateFrequency(),
        GetMinNetUpdateFrequency(),
        static_cast<int32>(NetDormancy.GetValue()),
        bAlwaysRelevant ? 1 : 0,
        SmoothingMode);

    // 서버에서만 CMC 이동 결과 관찰
    if (HasAuthority())
    {
        OnCharacterMovementUpdated.AddUniqueDynamic(
            this,
            &ASharpEnemyCharacter::HandleCharacterMovementUpdated
        );

        UE_LOG(LogSharpEnemyM1, Display,
            TEXT("[M2][CMC] ObserverBound Actor=%s"),
            *GetName());
    }
}

void ASharpEnemyCharacter::HandleCharacterMovementUpdated(
    float DeltaSeconds,
    FVector OldLocation,
    FVector OldVelocity)
{
    if (!HasAuthority())
    {
        return;
    }

    const UCharacterMovementComponent* Movement =
        GetCharacterMovement();

    const UWorld* World = GetWorld();

    if (!Movement || !World)
    {
        return;
    }

    // CMC 이동 업데이트 이후 상태
    const FVector PostPosition = GetActorLocation();
    const FVector PostVelocity = Movement->Velocity;

    const FVector DeltaVelocityVector =
        PostVelocity - OldVelocity;

    const double DeltaVelocityMagnitude =
        DeltaVelocityVector.Size();

    // M4 입력 후보 관찰값: DeltaVelocity / DeltaTime
    // 서버 CMC 업데이트에서 계산한 속도 변화율이며 실제 Detector 판정은 아님
    const double DeltaVelocityRateCmPerSec2 =
        DeltaSeconds > SMALL_NUMBER
        ? DeltaVelocityMagnitude / static_cast<double>(DeltaSeconds)
        : -1.0;

    // M4 입력 후보 관찰값: 업데이트 전후 Velocity 사이의 3D 방향 변화각
    // 정지 상태에서는 방향을 정의할 수 없으므로 AngleValid=0, DeltaAngleDeg=-1
    constexpr double MinDirectionSpeedCmPerSec = 1.0;
    const bool bAngleValid =
        OldVelocity.SizeSquared() >
        MinDirectionSpeedCmPerSec * MinDirectionSpeedCmPerSec &&
        PostVelocity.SizeSquared() >
        MinDirectionSpeedCmPerSec * MinDirectionSpeedCmPerSec;

    double DeltaAngleDegrees = -1.0;
    if (bAngleValid)
    {
        const double DirectionDot = FMath::Clamp(
            FVector::DotProduct(
                OldVelocity.GetSafeNormal(),
                PostVelocity.GetSafeNormal()),
            -1.0,
            1.0);

        DeltaAngleDegrees = FMath::RadiansToDegrees(
            FMath::Acos(DirectionDot));
    }

    // 임시 디버깅 기준 (실제 Detector 판정 아님)
    constexpr double DebugDeltaThreshold = 300.0;

    const bool bLargeDelta =
        DeltaVelocityMagnitude >= DebugDeltaThreshold;

    // 일반 출력 시점
    const double WorldTime = World->GetTimeSeconds();

    const bool bPeriodicLog =
        LastCmcLogWorldTimeSeconds < 0.0 ||
        WorldTime - LastCmcLogWorldTimeSeconds >= 0.5;

    // 일반 출력 시점도 아니고 급변도 없다면 생략
    if (!bPeriodicLog && !bLargeDelta)
    {
        return;
    }

    // 급변 로그는 정기 출력 타이머를 초기화하지 않음
    if (bPeriodicLog)
    {
        LastCmcLogWorldTimeSeconds = WorldTime;
    }

    const AGameStateBase* GameState = World->GetGameState();

    const double ServerTime = GameState
        ? GameState->GetServerWorldTimeSeconds()
        : -1.0;

    const TCHAR* LogReason = bLargeDelta
        ? TEXT("LargeDelta")
        : TEXT("Periodic");

    UE_LOG(LogSharpEnemyM1, Display,
        TEXT("[M2][CMC] Reason=%s Actor=%s ActorId=%d "
            "ServerTime=%.3f DeltaSeconds=%.4f "
            "OldPosition=%s PostPosition=%s "
            "OldVelocity=%s PostVelocity=%s "
            "DeltaVelocity=%.2f DeltaVelocityVector=%s "
            "DeltaVelocityRateCmPerSec2=%.2f "
            "AngleValid=%d DeltaAngleDeg=%.2f "
            "MovementMode=%d"),
        LogReason,
        *GetName(),
        ExperimentActorId,
        ServerTime,
        DeltaSeconds,
        *OldLocation.ToCompactString(),
        *PostPosition.ToCompactString(),
        *OldVelocity.ToCompactString(),
        *PostVelocity.ToCompactString(),
        DeltaVelocityMagnitude,
        *DeltaVelocityVector.ToCompactString(),
        DeltaVelocityRateCmPerSec2,
        bAngleValid ? 1 : 0,
        DeltaAngleDegrees,
        static_cast<int32>(Movement->MovementMode));
}

void ASharpEnemyCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    // M5: 서버에서 Manager가 RunId/Mode를 확정한 뒤 Policy를 딱 한 번 초기화.
    // Actor의 BeginPlay 순서에 의존하지 않도록 서버 Tick에서 준비 여부를 확인한다.
    // 초기화가 끝나거나 실패한 이후에는 반복적으로 Manager를 탐색하지 않는다.
    if (HasAuthority())
    {
        if (!bPolicyInitializationAttempted && IsValid(UpdatePolicy.Get()))
        {
            const UWorld* PolicyWorld = GetWorld();
            const ASharpExperimentManager* ExperimentManager = PolicyWorld
                ? Cast<ASharpExperimentManager>(
                    UGameplayStatics::GetActorOfClass(
                        PolicyWorld,
                        ASharpExperimentManager::StaticClass()))
                : nullptr;

            const FSharpExperimentIdentity* ResolvedIdentity = ExperimentManager
                ? ExperimentManager->GetResolvedIdentity()
                : nullptr;

            if (ResolvedIdentity)
            {
                // 지원하지 않는 모드여도 매 프레임 경고하지 않도록 시도는 한 번만.
                bPolicyInitializationAttempted = true;

                const bool bInitSuccess = UpdatePolicy->InitializePolicy(
                    ResolvedIdentity->Mode,
                    ExperimentManager->Settings.Get());

                const FString ModeName = StaticEnum<ESharpExperimentMode>()
                    ->GetNameStringByValue(
                        static_cast<int64>(ResolvedIdentity->Mode));

                UE_LOG(LogSharpEnemyM1, Display,
                    TEXT("[M5][PolicyBind] Actor=%s ActorId=%d "
                        "RunId=%s TrialId=%d Mode=%s "
                        "InitSuccess=%d NetUpdateHz=%.1f"),
                    *GetName(),
                    ExperimentActorId,
                    *ResolvedIdentity->RunId,
                    ResolvedIdentity->TrialId,
                    *ModeName,
                    bInitSuccess ? 1 : 0,
                    GetNetUpdateFrequency());
            }
        }
        return;
    }

    // Client Simulated Proxy에서만 관찰
    if (GetNetMode() != NM_Client ||
        GetLocalRole() != ROLE_SimulatedProxy)
    {
        return;
    }

    const UWorld* World = GetWorld();

    const UCapsuleComponent* Capsule =
        GetCapsuleComponent();

    const USkeletalMeshComponent* VisualMesh =
        GetMesh();

    const UCharacterMovementComponent* Movement =
        GetCharacterMovement();

    if (!World || !Capsule || !VisualMesh || !Movement)
    {
        return;
    }

    // 디버깅 로그는 0.5초마다 출력
    const double LocalTime = World->GetTimeSeconds();

    // ========================================
    // M3: 정상 직선 이동 종료 관찰
    // ========================================

    // 정지 판정 속도 기준 (cm/s)
    constexpr double StationarySpeedThreshold = 1.0;

    // 최소 정지 유지 시간
    constexpr double StationaryHoldSeconds = 0.5;

    // 마지막 Movement OnRep 이후 최소 대기 시간
    constexpr double ReceiveQuietSeconds = 0.5;

    const double CurrentSpeed = Movement->Velocity.Size();

    const bool bIsMoving =
        CurrentSpeed > StationarySpeedThreshold;

    // 이동 상태가 관찰되면 종료 후보 상태 해제
    if (bIsMoving)
    {
        bHasObservedClientMovement = true;
        ClientStationaryStartTimeSeconds = -1.0;
    }
    else if (bHasObservedClientMovement &&
        ClientStationaryStartTimeSeconds < 0.0)
    {
        // 실제 이동을 관찰한 이후 처음 정지한 시각
        ClientStationaryStartTimeSeconds = LocalTime;
    }

    // 최종 통계를 아직 출력하지 않은 경우에만 검사
    if (!bBaselineSummaryLogged &&
        bHasObservedClientMovement &&
        ClientStationaryStartTimeSeconds >= 0.0 &&
        LastMovementOnRepLocalTimeSeconds >= 0.0)
    {
        const double StationaryDuration =
            LocalTime - ClientStationaryStartTimeSeconds;

        const double ReceiveQuietDuration =
            LocalTime - LastMovementOnRepLocalTimeSeconds;

        // 정지와 복제 통지 안정화 조건 확인
        const bool bTimingReady =
            StationaryDuration >= StationaryHoldSeconds &&
            ReceiveQuietDuration >= ReceiveQuietSeconds;

        // 종료 조건이 충족되었을 때만 Manager 탐색
        const ASharpExperimentManager* ExperimentManager =
            bTimingReady
            ? Cast<ASharpExperimentManager>(
                UGameplayStatics::GetActorOfClass(
                    World,
                    ASharpExperimentManager::StaticClass()))
            : nullptr;

        // 서버에서 확정되어 복제된 Identity 조회
        const FSharpExperimentIdentity* ResolvedIdentity =
            ExperimentManager
            ? ExperimentManager->GetResolvedIdentity()
            : nullptr;

        // Identity까지 준비된 경우에만 최종 통계 출력
        if (bTimingReady && ResolvedIdentity && ExperimentActorId > 0)
        {
            const double AvgIntervalMs =
                ValidReceiveIntervalCount > 0
                ? TotalReceiveIntervalMs /
                static_cast<double>(ValidReceiveIntervalCount)
                : -1.0;

            const double AvgCorrectionCm =
                CorrectionSampleCount > 0
                ? TotalCorrectionDistanceCm /
                static_cast<double>(CorrectionSampleCount)
                : -1.0;

            const FString SummaryWorldName =
                World->GetPathName();

            // 서버에서 확정된 실험 Mode를 문자열로 변환
            const FString ModeName =
                StaticEnum<ESharpExperimentMode>()
                ->GetNameStringByValue(
                    static_cast<int64>(ResolvedIdentity->Mode));

            UE_LOG(LogSharpEnemyM1, Display,
                TEXT("[M3][BaselineSummary] "
                    "World=%s Actor=%s "
                    "RunId=%s TrialId=%d ActorId=%d Mode=%s "
                    "FinalReceiveCount=%llu "
                    "IntervalSamples=%llu "
                    "AvgIntervalMs=%.3f "
                    "MaxIntervalMs=%.3f "
                    "CorrectionSamples=%llu "
                    "AvgCorrectionCm=%.3f "
                    "MaxCorrectionCm=%.3f "
                    "StationarySeconds=%.3f "
                    "ReceiveQuietSeconds=%.3f"),
                *SummaryWorldName,
                *GetName(),

                // ExperimentManager에서 가져온 식별자
                *ResolvedIdentity->RunId,
                ResolvedIdentity->TrialId,
                ExperimentActorId,
                *ModeName,

                // 기존 M3 통계
                static_cast<unsigned long long>(
                    MovementReceiveCount),
                static_cast<unsigned long long>(
                    ValidReceiveIntervalCount),
                AvgIntervalMs,
                MaxReceiveIntervalMs,
                static_cast<unsigned long long>(
                    CorrectionSampleCount),
                AvgCorrectionCm,
                MaxCorrectionDistanceCm,
                StationaryDuration,
                ReceiveQuietDuration);

            // 같은 기준선 실험에서 중복 출력 방지
            bBaselineSummaryLogged = true;
        }
    }

    if (LastClientVisualLogWorldTimeSeconds >= 0.0 &&
        LocalTime - LastClientVisualLogWorldTimeSeconds < 0.5)
    {
        return;
    }

    LastClientVisualLogWorldTimeSeconds = LocalTime;

    // 클라이언트가 추정한 서버 기준 시각
    const AGameStateBase* GameState =
        World->GetGameState();

    const double ServerTime = GameState
        ? GameState->GetServerWorldTimeSeconds()
        : -1.0;

    // 클라이언트의 실제 컴포넌트 위치
    const FVector CapsulePosition =
        Capsule->GetComponentLocation();

    const FVector MeshPosition =
        VisualMesh->GetComponentLocation();

    // Mesh가 Capsule에 대해 갖는 상대 위치
    const FVector MeshRelativePosition =
        VisualMesh->GetRelativeLocation();

    // 두 컴포넌트의 월드 위치 차이
    const FVector CapsuleToMesh =
        MeshPosition - CapsulePosition;

    const FVector Velocity = Movement->Velocity;

    const int32 MovementMode =
        static_cast<int32>(Movement->MovementMode);

    const FString WorldName = World->GetPathName();

    UE_LOG(LogSharpEnemyM1, Display,
        TEXT("[M3][ClientVisual] "
            "World=%s Actor=%s ActorId=%d ReceiveCount=%llu "
            "LocalTime=%.3f ServerTime=%.3f "
            "DeltaSeconds=%.4f "
            "CapsulePosition=%s MeshPosition=%s "
            "MeshRelativePosition=%s CapsuleToMesh=%s "
            "Velocity=%s MovementMode=%d"),
        *WorldName,
        *GetName(),
        ExperimentActorId,
        static_cast<unsigned long long>(MovementReceiveCount),
        LocalTime,
        ServerTime,
        DeltaSeconds,
        *CapsulePosition.ToCompactString(),
        *MeshPosition.ToCompactString(),
        *MeshRelativePosition.ToCompactString(),
        *CapsuleToMesh.ToCompactString(),
        *Velocity.ToCompactString(),
        MovementMode);
}

void ASharpEnemyCharacter::OnRep_ReplicatedMovement()
{
    // Client Simulated Proxy만 관찰
    if (GetNetMode() != NM_Client ||
        GetLocalRole() != ROLE_SimulatedProxy)
    {
        Super::OnRep_ReplicatedMovement();
        return;
    }

    // 복제 통지 횟수 증가
    ++MovementReceiveCount;

    const UWorld* World = GetWorld();

    const double LocalTime = World
        ? World->GetTimeSeconds()
        : -1.0;

    // ========================================
    // 1. 수신 간격 계산 및 누적
    // ========================================

    double ReceiveIntervalMs = -1.0;

    if (LocalTime >= 0.0 &&
        LastMovementOnRepLocalTimeSeconds >= 0.0 &&
        LocalTime >= LastMovementOnRepLocalTimeSeconds)
    {
        ReceiveIntervalMs =
            (LocalTime - LastMovementOnRepLocalTimeSeconds)
            * 1000.0;

        // 유효한 간격만 통계에 포함
        ++ValidReceiveIntervalCount;

        TotalReceiveIntervalMs += ReceiveIntervalMs;

        MaxReceiveIntervalMs = FMath::Max(
            MaxReceiveIntervalMs,
            ReceiveIntervalMs
        );
    }

    // 모든 OnRep에서 마지막 시각 갱신
    if (LocalTime >= 0.0)
    {
        LastMovementOnRepLocalTimeSeconds = LocalTime;
    }

    // ========================================
    // 2. 엔진 처리 이전 상태 기록
    // ========================================

    const FRepMovement& ReceivedMovement =
        GetReplicatedMovement();

    const FVector ReceivedPosition =
        ReceivedMovement.Location;

    const FVector ReceivedVelocity =
        ReceivedMovement.LinearVelocity;

    const FVector BeforeCapsule =
        GetActorLocation();

    const FVector BeforeVelocity =
        GetVelocity();

    const UCharacterMovementComponent* BeforeMovement =
        GetCharacterMovement();

    const int32 BeforeMode = BeforeMovement
        ? static_cast<int32>(BeforeMovement->MovementMode)
        : -1;

    // ========================================
    // 3. 기존 Unreal 복제 처리 유지
    // ========================================

    Super::OnRep_ReplicatedMovement();

    // ========================================
    // 4. 처리 이후 상태 및 보정 거리 계산
    // ========================================

    const FVector AfterCapsule =
        GetActorLocation();

    const FVector AfterVelocity =
        GetVelocity();

    const UCharacterMovementComponent* AfterMovement =
        GetCharacterMovement();

    const int32 AfterMode = AfterMovement
        ? static_cast<int32>(AfterMovement->MovementMode)
        : -1;

    const double CorrectionDistanceCm =
        FVector::Distance(BeforeCapsule, AfterCapsule);

    // 첫 수신은 초기 위치 동기화이므로
    // 일반 보정 거리 통계에서 제외
    if (MovementReceiveCount > 1)
    {
        ++CorrectionSampleCount;

        TotalCorrectionDistanceCm += CorrectionDistanceCm;

        MaxCorrectionDistanceCm = FMath::Max(
            MaxCorrectionDistanceCm,
            CorrectionDistanceCm
        );
    }

    // ========================================
    // 5. 로그 출력 여부 판단
    // ========================================

    // 처음 20회와 이후 20회마다 출력
    const bool bShouldLog =
        MovementReceiveCount <= 20 ||
        MovementReceiveCount % 20 == 0;

    if (!bShouldLog)
    {
        return;
    }

    // ========================================
    // 6. 누적 평균 계산
    // ========================================

    const double AvgReceiveIntervalMs =
        ValidReceiveIntervalCount > 0
        ? TotalReceiveIntervalMs /
        static_cast<double>(ValidReceiveIntervalCount)
        : -1.0;

    const double AvgCorrectionDistanceCm =
        CorrectionSampleCount > 0
        ? TotalCorrectionDistanceCm /
        static_cast<double>(CorrectionSampleCount)
        : -1.0;

    const AGameStateBase* GameState = World
        ? World->GetGameState()
        : nullptr;

    const double ServerTime = GameState
        ? GameState->GetServerWorldTimeSeconds()
        : -1.0;

    const FString WorldName = World
        ? World->GetPathName()
        : TEXT("NoWorld");

    // ========================================
    // 7. 측정값 및 누적 통계 출력
    // ========================================

    UE_LOG(LogSharpEnemyM1, Display,
        TEXT("[M3][MovementOnRep] "
            "World=%s Actor=%s Count=%llu "
            "LocalTime=%.3f ServerTime=%.3f "
            "ReceiveIntervalMs=%.3f "
            "CorrectionDistanceCm=%.3f "
            "IntervalSamples=%llu "
            "AvgIntervalMs=%.3f "
            "MaxIntervalMs=%.3f "
            "CorrectionSamples=%llu "
            "AvgCorrectionCm=%.3f "
            "MaxCorrectionCm=%.3f "
            "ReceivedPosition=%s ReceivedVelocity=%s "
            "BeforeCapsule=%s AfterCapsule=%s "
            "BeforeVelocity=%s AfterVelocity=%s "
            "BeforeMode=%d AfterMode=%d"),
        *WorldName,
        *GetName(),
        static_cast<unsigned long long>(MovementReceiveCount),
        LocalTime,
        ServerTime,
        ReceiveIntervalMs,
        CorrectionDistanceCm,
        static_cast<unsigned long long>(
            ValidReceiveIntervalCount),
        AvgReceiveIntervalMs,
        MaxReceiveIntervalMs,
        static_cast<unsigned long long>(
            CorrectionSampleCount),
        AvgCorrectionDistanceCm,
        MaxCorrectionDistanceCm,
        *ReceivedPosition.ToCompactString(),
        *ReceivedVelocity.ToCompactString(),
        *BeforeCapsule.ToCompactString(),
        *AfterCapsule.ToCompactString(),
        *BeforeVelocity.ToCompactString(),
        *AfterVelocity.ToCompactString(),
        BeforeMode,
        AfterMode);
}

// M5: 보조 사건 Snapshot의 클라이언트 수신 관찰.
// 현재는 실제 Detector/서버 Snapshot 갱신이 없으므로 일반 PIE에서 호출되지 않아도 정상이다.
void ASharpEnemyCharacter::OnRep_SharpMoveSnapshot()
{
    // 기존 연구 범위: Dedicated Server + Simulated Proxy 클라이언트.
    if (GetNetMode() != NM_Client ||
        GetLocalRole() != ROLE_SimulatedProxy)
    {
        return;
    }

    const FSharpMoveSnapshot& Snapshot = ReplicatedSharpMoveSnapshot;

    // 0은 미발급 상태. 신규 사건으로 취급하지 않는다.
    if (Snapshot.SequenceId == 0)
    {
        return;
    }

    // 오래된 번호나 중복 번호가 도착해도 이전 사건을 다시 적용하지 않는다.
    // Snapshot을 이동/시각 상태에 적용하는 기능 자체는 아직 없다.
    if (Snapshot.SequenceId <= LastObservedSnapshotSequenceId)
    {
        UE_LOG(LogSharpEnemyM1, Warning,
            TEXT("[M5][SnapshotOnRep] Ignored Actor=%s ActorId=%d "
                "SequenceId=%u LastObservedSequenceId=%u Reason=StaleOrDuplicate"),
            *GetName(),
            ExperimentActorId,
            Snapshot.SequenceId,
            LastObservedSnapshotSequenceId);
        return;
    }

    const TCHAR* InvalidReason = TEXT("None");
    if (!IsValidEventSnapshotPayload(Snapshot, InvalidReason))
    {
        UE_LOG(LogSharpEnemyM1, Warning,
            TEXT("[M5][SnapshotOnRep] Ignored Actor=%s ActorId=%d "
                "SequenceId=%u Reason=%s ServerTime=%.3f"),
            *GetName(), ExperimentActorId, Snapshot.SequenceId,
            InvalidReason, Snapshot.ServerTime);
        return;
    }

    LastObservedSnapshotSequenceId = Snapshot.SequenceId;

    const UWorld* World = GetWorld();
    const double LocalTime = World ? World->GetTimeSeconds() : -1.0;
    const FString WorldName = World ? World->GetPathName() : TEXT("NoWorld");

    // 관찰용 위치만 읽는다. 기본 Replicate Movement, CMC, Smoothing은 변경하지 않는다.
    const UCapsuleComponent* Capsule = GetCapsuleComponent();
    const USkeletalMeshComponent* VisualMesh = GetMesh();
    const FVector CapsulePosition = Capsule
        ? Capsule->GetComponentLocation() : GetActorLocation();
    const FVector MeshPosition = VisualMesh
        ? VisualMesh->GetComponentLocation() : FVector::ZeroVector;

    UE_LOG(LogSharpEnemyM1, Display,
        TEXT("[M5][SnapshotOnRep] Received World=%s Actor=%s ActorId=%d "
            "SequenceId=%u ServerTime=%.3f LocalTime=%.3f "
            "StartPosition=%s PostVelocity=%s "
            "MovementMode=%u PredictionType=%u "
            "CapsulePosition=%s MeshPosition=%s Action=ObserveOnly"),
        *WorldName,
        *GetName(),
        ExperimentActorId,
        Snapshot.SequenceId,
        Snapshot.ServerTime,
        LocalTime,
        *Snapshot.StartPosition.ToCompactString(),
        *Snapshot.PostVelocity.ToCompactString(),
        static_cast<uint32>(Snapshot.MovementMode),
        static_cast<uint32>(Snapshot.PredictionType),
        *CapsulePosition.ToCompactString(),
        *MeshPosition.ToCompactString());
}

// M5 EventOnly / EventPredictor: 실제 Detector가 제공한 사후 Snapshot 게시.
// 두 모드의 서버 전송 규칙을 동일하게 유지하고 클라이언트 Predictor는 건드리지 않는다.
bool ASharpEnemyCharacter::PublishDetectedSharpMoveSnapshot(
    const FSharpMoveSnapshot& InSnapshot)
{
    if (!HasAuthority())
    {
        UE_LOG(LogSharpEnemyM1, Warning,
            TEXT("[M5][SnapshotPublish] Rejected Actor=%s Reason=NotAuthority"),
            *GetName());
        return false;
    }

    if (!IsValid(UpdatePolicy.Get()) || !UpdatePolicy->IsPolicyInitialized())
    {
        UE_LOG(LogSharpEnemyM1, Warning,
            TEXT("[M5][SnapshotPublish] Rejected Actor=%s ActorId=%d "
                "Reason=PolicyNotReady"), *GetName(), ExperimentActorId);
        return false;
    }

    const ESharpExperimentMode Mode = UpdatePolicy->GetActiveMode();
    if (Mode != ESharpExperimentMode::EventOnly &&
        Mode != ESharpExperimentMode::EventPredictor)
    {
        UE_LOG(LogSharpEnemyM1, Warning,
            TEXT("[M5][SnapshotPublish] Rejected Actor=%s ActorId=%d "
                "Reason=ModeDoesNotPublishSnapshot"),
            *GetName(), ExperimentActorId);
        return false;
    }

    const uint32 PreviousSequenceId = ReplicatedSharpMoveSnapshot.SequenceId;
    if (InSnapshot.SequenceId == 0 ||
        InSnapshot.SequenceId <= PreviousSequenceId)
    {
        UE_LOG(LogSharpEnemyM1, Warning,
            TEXT("[M5][SnapshotPublish] Rejected Actor=%s ActorId=%d "
                "SequenceId=%u PreviousSequenceId=%u "
                "Reason=InvalidOrStaleSequence"),
            *GetName(), ExperimentActorId,
            InSnapshot.SequenceId, PreviousSequenceId);
        return false;
    }

    const TCHAR* InvalidReason = TEXT("None");
    if (!IsValidEventSnapshotPayload(InSnapshot, InvalidReason))
    {
        UE_LOG(LogSharpEnemyM1, Warning,
            TEXT("[M5][SnapshotPublish] Rejected Actor=%s ActorId=%d "
                "SequenceId=%u Reason=%s"),
            *GetName(), ExperimentActorId,
            InSnapshot.SequenceId, InvalidReason);
        return false;
    }

    if (!GetWorld())
    {
        return false;
    }

    // 사건 번호와 상태 전체를 하나의 RepNotify 프로퍼티로 교체한다.
    // EventOnly, EventPredictor 양쪽에서 동일한 한 번의 업데이트 요청만 보낸다.
    ReplicatedSharpMoveSnapshot = InSnapshot;
    ForceNetUpdate();

    const FString ModeName = StaticEnum<ESharpExperimentMode>()
        ->GetNameStringByValue(static_cast<int64>(Mode));

    UE_LOG(LogSharpEnemyM1, Display,
        TEXT("[M5][SnapshotPublish] Requested Actor=%s ActorId=%d "
            "Mode=%s SequenceId=%u ServerTime=%.3f "
            "StartPosition=%s PostVelocity=%s "
            "MovementMode=%u PredictionType=%u "
            "NetUpdateHz=%.1f Action=SnapshotAndForceNetUpdate"),
        *GetName(), ExperimentActorId, *ModeName,
        InSnapshot.SequenceId, InSnapshot.ServerTime,
        *InSnapshot.StartPosition.ToCompactString(),
        *InSnapshot.PostVelocity.ToCompactString(),
        static_cast<uint32>(InSnapshot.MovementMode),
        static_cast<uint32>(InSnapshot.PredictionType),
        GetNetUpdateFrequency());
    return true;
}
