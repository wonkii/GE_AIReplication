// Fill out your copyright notice in the Description page of Project Settings.


#include "SharpMovement/Enemy/SharpEnemyAIController.h"
#include "SharpMovement/Enemy/SharpEnemyCharacter.h"

DEFINE_LOG_CATEGORY_STATIC(LogSharpEnemyAI_M1, Log, All);

namespace
{
    // 서버 기준선 이동 거리 (cm)
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

    // 이동 시작
    bBaselineMovementActive = true;

    UE_LOG(LogSharpEnemyAI_M1, Display,
        TEXT("[M1][Move] Start Enemy=%s StartPosition=%s"),
        *GetNameSafe(Enemy),
        *BaselineStartLocation.ToCompactString());
}

// 매 프레임 이동 입력 처리
void ASharpEnemyAIController::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    // 서버 권한이 없거나 이동이 끝났다면 실행하지 않음
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

    // 목표 이동 거리에 도달한 경우
    if (DistanceX >= BaselineTravelDistanceCm)
    {
        bBaselineMovementActive = false;

        UE_LOG(LogSharpEnemyAI_M1, Display,
            TEXT("[M1][Move] InputStopped "
                "Enemy=%s DistanceX=%.1f Position=%s"),
            *GetNameSafe(Enemy),
            DistanceX,
            *Enemy->GetActorLocation().ToCompactString());

        return;
    }

    // 월드 +X 방향으로 이동 입력
    Enemy->AddMovementInput(FVector::ForwardVector, 1.0f);
}
