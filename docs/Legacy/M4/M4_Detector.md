# M4. Volume과 독립적인 Sharp Movement Detector

> 상태: 개발 예정. [전체 아키텍처](../Architecture/Unreal_Sharp_Movement_마일스톤_구현계획.md) · 선행: [M3](../M3/M3_CMC_기준선_측정.md) · 다음: [M5](../M5/M5_전송_정책.md)

Detector 담당 단계. Core는 Actor와 복제에 의존하지 않는 계산만 수행하고, Component는 서버 이동 업데이트 이후의 실제 Velocity를 관측한다.

## 담당 파일과 폴더

아래 경로는 저장소 루트 기준이다. 파일이 기존 단계에서 생성되었다면 이 단계에서는 확장한다.

| 경로 | 역할 |
|---|---|
| `Source/AIReplicationCore/Public/Detector/SharpMovementSample.h` | 이동 업데이트 관측값 |
| `Source/AIReplicationCore/Public/Detector/SharpDetectorConfig.h` | 판정·해제 임계값과 대기 시간 |
| `Source/AIReplicationCore/Public/Detector/SharpMovementClassifier.h` | 순수 판정기 인터페이스 |
| `Source/AIReplicationCore/Private/Detector/SharpMovementClassifier.cpp` | Δv·Δθ와 Risk 상태 판정 |
| `Source/GE_AIReplication/SharpMovement/Detector/SharpMovementDetectorComponent.h/.cpp` | 서버 CMC 샘플 수집·이벤트·로그 |
| `Source/GE_AIReplication/SharpMovement/Enemy/SharpEnemyCharacter.h/.cpp` | Detector 부착과 이벤트 구독 |

## 개발 및 검증 사항

**목표:** 서버가 실제 이동 결과만으로 급변을 판정한다.

**구현 작업**

1. `USharpMovementDetectorComponent`를 Enemy에 붙이고 서버에서만 실행한다.
2. CMC 이동 업데이트 **후**의 Velocity를 이전 업데이트와 비교한다. 시작 지표는 `|v_now-v_prev|`와 속도가 충분할 때의 이동 방향각 `Δθ`다. 일반적인 이동 틱 간격 변화에 민감하면 `Δv/Δt`도 기록한다.
3. 거의 정지한 상태의 방향각은 사용하지 않고, 급정지는 `Δv`로 다룬다.
4. 판정 임계값·해제 임계값·재발동 대기 시간을 둔다. `Risk` 상태의 시작/끝 시각과 이유를 남긴다.
5. Ground Truth의 Volume 사건과 사후 비교해 탐지 지연, 오탐, 미탐을 계산한다. 임계값 조정용 실행과 최종 평가용 실행을 구분한다.

**산출물:** Detector 이벤트 로그와 오탐·미탐 표.

**완료 기준:** Volume 참조가 없는 Detector가 목표 Impulse를 포착하고, 정상 이동에서의 오탐 빈도를 보고할 수 있다.

공식 참고: [On Character Movement Updated](https://dev.epicgames.com/documentation/en-us/unreal-engine/BlueprintAPI/Character/OnCharacterMovementUpdated), [CMC Velocity·Acceleration](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UCharacterMovementComponent).
