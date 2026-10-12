// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "SharpMoveSnapshot.h"
#include "SharpEnemyCharacter.generated.h"

class USharpUpdatePolicy;

UCLASS()
class GE_AIREPLICATION_API ASharpEnemyCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    ASharpEnemyCharacter();

    // M3: 실험에서 사용하는 Enemy 고유 식별자
    // 0은 아직 지정되지 않은 상태를 의미
    UPROPERTY(EditInstanceOnly, Replicated,
        Category = "Sharp Experiment|Identity")
    int32 ExperimentActorId = 0;

    // ActorId의 Replication 등록을 위한 함수
    virtual void GetLifetimeReplicatedProps(
        TArray<FLifetimeProperty>& OutLifetimeProps
    ) const override;

    // 클라이언트의 Capsule / Mesh 관찰에 사용
    virtual void Tick(float DeltaSeconds) override;

    // M5 EventOnly / EventPredictor: 실제 Detector가 관찰한 사후 상태를 게시한다.
    // 이 함수는 사건이나 SequenceId를 스스로 만들지 않는다.
    // 두 모드는 같은 서버 Snapshot/ForceNetUpdate 경로를 사용한다.
    bool PublishDetectedSharpMoveSnapshot(const FSharpMoveSnapshot& InSnapshot);


protected:
    virtual void BeginPlay() override;

    // 클라이언트 Movement 복제 통지 관찰
    virtual void OnRep_ReplicatedMovement() override;

    // M5: 복제된 사건 Snapshot을 클라이언트에서 관찰한다.
    // Capsule/Mesh/CMC를 수정하지 않는다.
    UFUNCTION()
    void OnRep_SharpMoveSnapshot();

    // 서버 CMC 이동 업데이트 관찰
    UFUNCTION()
    void HandleCharacterMovementUpdated(
        float DeltaSeconds,
        FVector OldLocation,
        FVector OldVelocity
    );

private:
    // M5: 서버 사건 이후 상태를 담을 단일 RepNotify Snapshot.
    // 실제 Detector 연동 전에는 유효한 SequenceId를 생성하거나 값을 변경하지 않는다.
    // 클라이언트에서는 상태 관찰에만 사용하고 CMC 복제를 대체하지 않는다.
    UPROPERTY(ReplicatedUsing = OnRep_SharpMoveSnapshot)
    FSharpMoveSnapshot ReplicatedSharpMoveSnapshot;

    // 클라이언트에서 마지막으로 관찰한 유효 Snapshot 번호.
    // 이 변수는 로컬 수신 판단용이며 복제하지 않는다.
    uint32 LastObservedSnapshotSequenceId = 0;

    // M5: Enemy별 Network 정책 컴포넌트.
    // 생성자에서 생성하며 서버에서 실험 모드가 준비되면 초기화한다.
    // 컴포넌트의 자체 복제는 사용하지 않는다.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
        Category = "Sharp Experiment|Network",
        meta = (AllowPrivateAccess = "true"))
    TObjectPtr<USharpUpdatePolicy> UpdatePolicy = nullptr;

    // Manager의 서버 Identity가 준비된 뒤 Policy 초기화를 한 번만 시도한다.
    // 지원하지 않는 모드나 설정 오류가 있어도 매 Tick마다 재시도하지 않는다.
    bool bPolicyInitializationAttempted = false;

    // 서버 CMC 디버깅 로그 출력 시각
    double LastCmcLogWorldTimeSeconds = -1.0;

    // 클라이언트 시각 상태 디버깅 로그 출력 시각
    double LastClientVisualLogWorldTimeSeconds = -1.0;

    // Movement 복제 통지 횟수
    uint64 MovementReceiveCount = 0;

    // 마지막 Movement OnRep가 발생한 클라이언트 로컬 시각
    double LastMovementOnRepLocalTimeSeconds = -1.0;

    // M3: 유효한 OnRep 수신 간격 통계
    uint64 ValidReceiveIntervalCount = 0;
    double TotalReceiveIntervalMs = 0.0;
    double MaxReceiveIntervalMs = 0.0;

    // M3: Capsule 보정 거리 통계
    // 첫 번째 OnRep의 초기 위치 동기화는 제외할 예정
    uint64 CorrectionSampleCount = 0;
    double TotalCorrectionDistanceCm = 0.0;
    double MaxCorrectionDistanceCm = 0.0;

    // M3: 클라이언트에서 실제 이동 상태를 관찰했는지
    bool bHasObservedClientMovement = false;
    double ClientStationaryStartTimeSeconds = -1.0;

    // M3: 기준선 이동 종료 통계를 이미 출력했는지
    bool bBaselineSummaryLogged = false;
};
