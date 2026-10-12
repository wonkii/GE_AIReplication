// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SharpMovement/Experiment/SharpExperimentSettings.h"
#include "SharpMovement/Experiment/SharpExperimentTypes.h"
#include "TimerManager.h"
#include "SharpUpdatePolicy.generated.h"

// M5: Enemy별 서버 전송 정책의 상태를 보관할 컴포넌트.
// Default / Adaptive / AlwaysHigh / SingleShot / Burst / EventOnly / EventPredictor를 Enemy별로 관리한다.
// 글로벌 Adaptive CVar는 이 컴포넌트에서 변경하지 않는다.
UCLASS(ClassGroup = (SharpMovement))
class GE_AIREPLICATION_API USharpUpdatePolicy : public UActorComponent
{
    GENERATED_BODY()

public:
    // 서버 Policy 초기화 및 원래 업데이트 빈도 복원.
    bool InitializePolicy(
        ESharpExperimentMode InMode,
        USharpExperimentSettings* InSettings);

    void RestorePolicyState();

    // 실제 서버 Detector 사건의 단조 증가 SequenceId와 관찰 시각을 전달받는다.
    // 사건 판정을 수행하거나 SequenceId를 직접 발급하지 않는다.
    // true: SingleShot 한 번 요청 또는 Burst 시작/연장 성공.
    // false: 무효/중복 사건, 권한 없음, 미초기화 또는 미지원 모드.
    // EventOnly / EventPredictor는 단순 SequenceId 호출로 처리하지 않는다.
    // Snapshot 페이로드가 준비된 이후 별도 계약에서 연결한다.
    bool HandleSharpMovementEvent(
        uint32 InSequenceId,
        double InServerEventTimeSeconds);

    // 현재 정책 모드 확인용. 초기 상태는 Default다.
    ESharpExperimentMode GetActiveMode() const
    {
        return ActiveMode;
    }

    // 서버에서 정책 초기화가 완료됐는지 확인하기 위한 상태.
    bool IsPolicyInitialized() const
    {
        return bPolicyInitialized;
    }

protected:
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    // 정책 상태는 Actor/Enemy별 컴포넌트 인스턴스에 보관한다.
    // 복제 상태가 아니며, 클라이언트 전송용 데이터도 아니다.
    UPROPERTY(Transient)
    ESharpExperimentMode ActiveMode = ESharpExperimentMode::Default;

    // ExperimentManager의 DataAsset을 읽기 전용 설정으로 사용한다.
    UPROPERTY(Transient)
    TObjectPtr<USharpExperimentSettings> CachedSettings = nullptr;

    UPROPERTY(Transient)
    bool bPolicyInitialized = false;

    // AlwaysHigh / Burst 종료 시 원래 NetUpdateFrequency를 복원한다.
    // Default 모드에서는 저장 및 변경 작업을 수행하지 않는다.
    UPROPERTY(Transient)
    float SavedNetUpdateFrequencyHz = 0.0f;

    UPROPERTY(Transient)
    bool bHasSavedNetUpdateFrequency = false;

    // Adaptive에서는 Actor의 최소 업데이트 빈도만 변경한다.
    // 복원할 때 이전 MinNetUpdateFrequency를 정확히 되돌린다.
    UPROPERTY(Transient)
    float SavedMinNetUpdateFrequencyHz = 0.0f;

    UPROPERTY(Transient)
    bool bHasSavedMinNetUpdateFrequency = false;

    // 실제 Detector가 발급한 마지막으로 처리한 사건 번호.
    // 0은 아직 처리한 사건이 없다는 뜻이다. 이번 버전에서는 wrap-around 미지원.
    UPROPERTY(Transient)
    uint32 LastHandledSequenceId = 0;

    // Burst 지속 시간. 임시 실험 파라미터이며 초기화 로그에 기록한다.
    // 실제 연구 비교 전에는 설정값/비교 조건을 명시적으로 고정해야 한다.
    UPROPERTY(EditDefaultsOnly, Category = "Sharp Experiment|Network",
        meta = (ClampMin = "0.01"))
    float BurstDurationSeconds = 0.25f;

    // 서버 타이머가 만료되면 Burst 진입 전 Actor의 Frequency로 복원한다.
    void FinishBurst();

    // Burst 타이머도 Enemy별로 독립적으로 보관한다.
    UPROPERTY(Transient)
    bool bBurstActive = false;

    FTimerHandle BurstTimerHandle;
};
