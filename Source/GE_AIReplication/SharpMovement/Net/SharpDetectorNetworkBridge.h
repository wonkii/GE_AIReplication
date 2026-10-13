// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

class ASharpEnemyCharacter;
struct FSharpMoveSnapshot;

// Detector가 이미 관찰/승인한 사건을 M5 정책에 전달하는 결과.
// BaselineSkipped는 오류가 아니며 Default/Adaptive/AlwaysHigh에서 추가 전송이 없음을 뜻한다.
enum class ESharpDetectorNetworkDispatchResult : uint8
{
    Requested,
    BaselineSkipped,
    Rejected
};

// Unreal Actor/CMC 복제를 직접 변경하지 않는 연결 계층.
// Detector 이벤트 타입을 임의로 정의하지 않으며, Detector 측에서 FSharpMoveSnapshot 계약에 맞게
// 값을 변환한 뒤 오직 서버 CMC의 사후 상태에 대해 한 번 호출해야 한다.
// 진단 Actor/합성 이벤트는 정식 실험에 사용하지 않는다.
// Producer must capture Generation and Segment with the observed state; dispatch never
// stamps queued events with the current execution generation.
class GE_AIREPLICATION_API FSharpDetectorNetworkBridge
{
public:
    static ESharpDetectorNetworkDispatchResult DispatchDetectedEvent(
        ASharpEnemyCharacter* Enemy,
        const FSharpMoveSnapshot& ObservedEvent);
};
