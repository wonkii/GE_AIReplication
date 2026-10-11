// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "SharpMoveSnapshot.generated.h"

// Network 전송 계약의 예측 모델 식별자.
// 이 enum은 실제 Predictor가 구현되었다는 뜻이 아니다.
// EventOnly에서는 None을 사용한다.
UENUM(BlueprintType)
enum class ESharpPredictionType : uint8
{
    None = 0 UMETA(DisplayName = "None"),
    ConstantVelocity = 1 UMETA(DisplayName = "Constant Velocity"),
    Airborne = 2 UMETA(DisplayName = "Airborne")
};

// M5/M6: 실제 서버 Detector 사건에서 생성할 단일 복제 Snapshot 계약.
// 이 파일은 데이터 타입만 정의한다.
// RepNotify, DOREPLIFETIME, ForceNetUpdate, Predictor 적용은 여기서 수행하지 않는다.
// 기본 ACharacter/CMC Movement Replication을 대체하지 않는다.
USTRUCT(BlueprintType)
struct AIREPLICATIONNET_API FSharpMoveSnapshot
{
    GENERATED_BODY()

    // Detector가 Enemy별로 발급하는 단조 증가 사건 번호.
    // 0은 아직 유효한 사건이 없는 초기 상태다.
    // Network Policy나 Snapshot이 자체적으로 번호를 생성하지 않는다.
    UPROPERTY()
    uint32 SequenceId = 0;

    // Detector가 서버에서 실제 이동 결과를 관찰한 시각(초).
    // 클라이언트 수신 시각, Overlap 시각, ForceNetUpdate 호출 시각이 아니다.
    UPROPERTY()
    double ServerTime = -1.0;

    // 사건이 관찰된 시점의 서버 Actor 월드 위치 (cm).
    UPROPERTY()
    FVector StartPosition = FVector::ZeroVector;

    // 사건 적용 후 서버 CMC에서 관찰된 속도 (cm/s).
    // Raw Impulse를 보내거나 클라이언트에서 Impulse를 재적용하지 않는다.
    UPROPERTY()
    FVector PostVelocity = FVector::ZeroVector;

    // 서버 CMC의 EMovementMode 값을 담는 바이트.
    // MOVE_None / MOVE_Walking / MOVE_Falling 등의 숫자 값이다.
    // Net 모듈 공개 헤더가 CharacterMovementComponent.h에 의존하지 않도록
    // 저장 형태를 uint8로 유지한다. 실제 변환은 Enemy 통합 코드에서 한다.
    UPROPERTY()
    uint8 MovementMode = 0;

    // 향후 시각 예측 모델을 구분할 메타데이터.
    // EventOnly는 None. EventPredictor도 서버 전송 규칙은 동일해야 한다.
    UPROPERTY()
    ESharpPredictionType PredictionType = ESharpPredictionType::None;
};
