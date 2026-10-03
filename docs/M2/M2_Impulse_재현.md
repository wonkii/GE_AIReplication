# M2. 서버 전용 Impulse Volume과 Ground Truth

> 상태: 개발 예정. [전체 아키텍처](../Architecture/Unreal_Sharp_Movement_마일스톤_구현계획.md) · 선행: [M1](../M1/M1_서버_AI_복제.md) · 다음: [M3](../M3/M3_CMC_기준선_측정.md)

Volume은 Ground Truth를 생성한다. Detector에 Overlap 또는 AddImpulse 호출 사실을 전달하지 않는다.

## 담당 파일과 폴더

아래 경로는 저장소 루트 기준이다. 파일이 기존 단계에서 생성되었다면 이 단계에서는 확장한다.

| 경로 | 역할 |
|---|---|
| `Source/GE_AIReplication/SharpMovement/Scenario/SharpImpulseVolume.h/.cpp` | 서버 전용 Overlap·Impulse 적용 |
| `Source/GE_AIReplication/SharpMovement/Measurement/SharpMovementRecorder.h/.cpp` | Volume 사건과 실제 이동 시각 로그 |
| `Content/SharpMovementExperiment/Blueprints/BP_SharpImpulseVolume.uasset` | Impulse 방향·세기별 장면 설정 |

## 개발 및 검증 사항

**목표:** 같은 급변 상황을 반복 발생시키고 실제 적용 시점을 기록한다.

**구현 작업**

1. Box Collision을 가진 `ASharpImpulseVolume`을 만들고 Overlap 이벤트에서 `HasAuthority()`를 검사한다.
2. 같은 Enemy가 한 번 진입할 때 `CharacterMovementComponent::AddImpulse(ImpulseVector, bVelocityChange)`를 한 번 호출한다. 첫 실험에서는 `bVelocityChange=true`를 사용해 질량의 영향을 제거한다.
3. `VolumeOverlapTime`, `ImpulseCallTime`, 이동 업데이트 후의 `ActualVelocityChangeTime`을 **다른 항목**으로 기록한다. `AddImpulse`는 틱 안에서 누적 후 적용될 수 있다.
4. 측면·전방·후방·상향 Impulse 강도를 단계화한다. 정상 회전, 정상 감속도 오탐 확인을 위해 따로 만든다.
5. AI의 경로 추종이 Impulse 직후 속도를 얼마나 빨리 다시 바꾸는지 확인한다. 필요하면 첫 실험은 이동 명령을 잠시 중단한 단순 장면으로 분리하고, 이후 실제 AI 경로 추종 장면으로 확장한다.

**산출물:** 사건 ID가 포함된 Impulse 재현 로그.

**완료 기준:** 같은 설정에서 급변 전후 Velocity와 Movement Mode를 반복 관찰할 수 있다.

공식 참고: [Overlap 이벤트](https://dev.epicgames.com/documentation/en-us/unreal-engine/BlueprintAPI/Collision/OnActorBeginOverlap), [CMC AddImpulse](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UCharacterMovementComponent/AddImpulse).
