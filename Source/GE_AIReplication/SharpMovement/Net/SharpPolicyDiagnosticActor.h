// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "SharpPolicyDiagnosticActor.generated.h"

class ASharpEnemyCharacter;
class USharpUpdatePolicy;

// M5 진단 전용: PIE 서버에서 가상 사건을 명시적으로 주입한다.
// 실제 Detector나 Sharp Movement의 존재를 의미하지 않는다.
// 실험 데이터 수집 시에는 이 Actor를 제거하거나 bEnableSyntheticTest를 꺼야 한다.
UCLASS()
class GE_AIREPLICATION_API ASharpPolicyDiagnosticActor : public AActor
{
    GENERATED_BODY()

public:
    ASharpPolicyDiagnosticActor();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    // 기본 OFF. 맵에 배치한 뒤 상세 패널에서 명시적으로 켜야 한다.
    UPROPERTY(EditInstanceOnly, Category = "Sharp Experiment|Diagnostics")
    bool bEnableSyntheticTest = false;

    // 대상 Enemy의 서버 ExperimentActorId.
    UPROPERTY(EditInstanceOnly, Category = "Sharp Experiment|Diagnostics",
        meta = (ClampMin = "1"))
    int32 TargetActorId = 1;

    UPROPERTY(Transient)
    TObjectPtr<ASharpEnemyCharacter> TargetEnemy = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<USharpUpdatePolicy> TargetPolicy = nullptr;

    FTimerHandle FindPolicyTimerHandle;
    FTimerHandle FirstEventTimerHandle;
    FTimerHandle SecondEventTimerHandle;
    FTimerHandle VerifyTimerHandle;

    double SearchStartedSeconds = 0.0;
    float BaselineHz = 0.0f;
    float FrequencyAfterFirstHz = 0.0f;
    float FrequencyAfterSecondHz = 0.0f;
    bool bFirstAccepted = false;
    bool bDuplicateAccepted = false;
    bool bSecondAccepted = false;
    bool bInvalidPayloadAccepted = false;
    bool bInvalidPredictionTypeAccepted = false;
    bool bInvalidMovementModeAccepted = false;
    bool bStarted = false;

    void TryStartTest();
    void SendFirstSyntheticEvent();
    void SendSecondSyntheticEvent();
    void VerifyTestResult();
};
