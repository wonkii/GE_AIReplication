// Fill out your copyright notice in the Description page of Project Settings.


#include "SharpMovement/Enemy/SharpEnemyAIController.h"
#include "SharpMovement/Enemy/SharpEnemyCharacter.h"

DEFINE_LOG_CATEGORY_STATIC(LogSharpEnemyAI_M1, Log, All);

namespace
{
    // 서버 기준선 왕복 이동 거리 (cm)
    constexpr double BaselineTravelDistanceCm = 1000.0;
}

// 생성자
ASharpEnemyAIController::ASharpEnemyAIController()
{
    // AIController의 Tick 활성화
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;
}

// Enemy Possess
void ASharpEnemyAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);

    if (!HasAuthority())
    {
        UE_LOG(LogSharpEnemyAI_M1, Warning,
            TEXT("[M1][AI] OnPossess without Authority!"));

        return;
    }

    ASharpEnemyCharacter* Enemy =
        Cast<ASharpEnemyCharacter>(GetPawn());

    if (!IsValid(Enemy))
    {
        UE_LOG(LogSharpEnemyAI_M1, Warning,
            TEXT("[M1][AI] Invalid Enemy! "
                "RequestedPawn=%s PossessedPawn=%s"),
            *GetNameSafe(InPawn),
            *GetNameSafe(GetPawn()));

        return;
    }

    UE_LOG(LogSharpEnemyAI_M1, Display,
        TEXT("[M1][AI] Possess Success! "
            "Controller=%s Enemy=%s Authority=%d NetMode=%d"),
        *GetName(),
        *GetNameSafe(Enemy),
        HasAuthority() ? 1 : 0,
        static_cast<int32>(GetNetMode()));

    // 이동 시작 위치 저장
    BaselineStartLocation = Enemy->GetActorLocation();

    // 최초 이동 방향은 월드 +X
    BaselineMoveDirection = 1;

    // 이동 시작
    bBaselineMovementActive = true;

    UE_LOG(LogSharpEnemyAI_M1, Display,
        TEXT("[M1][Move] Start Enemy=%s "
            "StartPosition=%s Direction=+X "
            "TravelDistance=%.1f"),
        *GetNameSafe(Enemy),
        *BaselineStartLocation.ToCompactString(),
        BaselineTravelDistanceCm);
}

// 매 프레임 이동 입력 처리
void ASharpEnemyAIController::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    // 서버 권한이 없거나 이동이 비활성화된 경우
    if (!HasAuthority() || !bBaselineMovementActive)
    {
        return;
    }

    ASharpEnemyCharacter* Enemy =
        Cast<ASharpEnemyCharacter>(GetPawn());

    if (!IsValid(Enemy))
    {
        bBaselineMovementActive = false;
        return;
    }

    // 시작 위치로부터 X축 이동 거리 계산
    const double DistanceX =
        Enemy->GetActorLocation().X - BaselineStartLocation.X;

    // ========================================
    // 1. +X 방향 이동 중 반환점 도달
    // ========================================

    if (BaselineMoveDirection > 0 &&
        DistanceX >= BaselineTravelDistanceCm)
    {
        // 이동 방향을 -X로 반전
        BaselineMoveDirection = -1;

        UE_LOG(LogSharpEnemyAI_M1, Display,
            TEXT("[M1][Move] Reverse "
                "Enemy=%s Direction=-X "
                "DistanceX=%.1f Position=%s"),
            *GetNameSafe(Enemy),
            DistanceX,
            *Enemy->GetActorLocation().ToCompactString());
    }

    // ========================================
    // 2. -X 방향 이동 중 시작점 도달
    // ========================================

    else if (BaselineMoveDirection < 0 &&
        DistanceX <= 0.0)
    {
        // 이동 방향을 +X로 반전
        BaselineMoveDirection = 1;

        UE_LOG(LogSharpEnemyAI_M1, Display,
            TEXT("[M1][Move] Reverse "
                "Enemy=%s Direction=+X "
                "DistanceX=%.1f Position=%s"),
            *GetNameSafe(Enemy),
            DistanceX,
            *Enemy->GetActorLocation().ToCompactString());
    }

    // ========================================
    // 3. 현재 방향으로 CMC 이동 입력
    // ========================================

    // +1이면 +X 방향, -1이면 -X 방향
    const FVector MoveDirection =
        FVector::ForwardVector *
        static_cast<double>(BaselineMoveDirection);

    // 서버에서만 이동 입력을 전달
    // 실제 이동은 CharacterMovementComponent가 처리
    Enemy->AddMovementInput(MoveDirection, 1.0f);
}
