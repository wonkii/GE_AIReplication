// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "SharpEnemyAIController.generated.h"

UCLASS()
class GE_AIREPLICATION_API ASharpEnemyAIController : public AAIController
{
    GENERATED_BODY()

public:
    ASharpEnemyAIController();

    // 매 프레임 서버에서 이동 명령을 처리
    virtual void Tick(float DeltaSeconds) override;

protected:
    // Enemy Possess 시 호출
    virtual void OnPossess(APawn* InPawn) override;

private:
    // 직선 이동을 시작한 위치
    FVector BaselineStartLocation = FVector::ZeroVector;

    // 현재 기준선 이동을 진행하는지 여부
    bool bBaselineMovementActive = false;
};
