# 13. 경사면 피격 Root Motion 개발 계획

2026-11-12 KST

## 목표와 범위

걸을 수 있는 정적 경사면에 서 있는 Enemy가 정면에서 피격되면, 현재 `AM_HitReaction_Front`의 반동과 회복 타이밍을 살리면서 뒤로 밀려 언덕을 오른다. 평지에서 경사로 진입하는 경우와 경사 위에서 벽에 가까워지는 경우도 같은 Montage로 검증한다. 첫 구현의 네트워크 범위는 서버가 제어하는 AI Enemy와 이를 관찰하는 원격 클라이언트다.

## 현재 경로에서 확인할 문제

- `ReactionEnvironmentComponent::FindObstacle`은 시작 높이에서 캡슐을 스윕해 접촉을 검출한다. `ReactionWarpComponent::PlanMulti`는 이 접촉으로 이동 예산을 줄이고, 보정된 궤적을 검증할 때도 지형을 따라 캡슐 높이를 갱신하지 않는다. 따라서 걸을 수 있는 경사가 정지 제약으로 들어가거나, 계획상 빈 공간과 오르막에서의 실제 캡슐 경로가 달라질 수 있다. 이 가설은 동일 조건의 `Off`와 `MultiPhase` 재생으로 확인한다. [장애물 탐색 코드](../../Source/GE_AIReplication/Combat/ReactionEnvironmentComponent.cpp#L13) · [이동 예산과 경로 검증 코드](../../Source/GE_AIReplication/Combat/ReactionWarpComponent.cpp#L222)
- 현재 다구간 Modifier는 PELT 정책에서 계산한 XY 차이만 Root Motion에 더한다. UE의 Walking에서는 애니메이션 Root Motion의 Z가 무시되며, CMC가 바닥을 따라 이동한다. 목표는 **기존 Modifier로 후퇴량과 구간 타이밍을 유지하고, CMC가 경사에서 캡슐 높이를 결정하게 하는 것**이다. [Modifier 코드](../../Source/GE_AIReplication/Combat/ReactionMultiPhaseWarpModifier.cpp#L31) · [Epic Root Motion](https://dev.epicgames.com/documentation/en-us/unreal-engine/root-motion-in-unreal-engine) · [Epic MoveAlongFloor](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UCharacterMovementComponent/MoveAlongFloor)

## 구현 순서

### 1. 같은 Montage로 경사 기준선 확보

- `/Game/YHJ/Lv_HitWallTest`에서 기존 `ReactionWarp.Stations IncludeDisabled=1`과 `ReactionWarp.Tour`로 `Slope30`·`Slope55`의 `Off → MultiPhase`를 비교한다. 별도 재현 위치를 등록해 **경사 중간에서 시작**, **평지에서 경사 진입**, **경사에서 정상부로 진입**을 각각 찍는다. 마커 이름의 각도는 지형 측정값으로 쓰지 않는다. [기존 순회 실행 절차](12_다중벽_순회_실행.md)
- Montage와 재생 속도, 피격 위치·방향, 카메라를 고정한다. 각 케이스에서 실제 바닥 법선, 충돌 컴포넌트의 slope override, CMC의 `IsWalkable` 결과와 MovementMode를 함께 기록한다. `Off`에서도 등판에 실패하면 경사 메시의 실제 콜리전, 경사 입구의 턱, 바닥 판정, 피격 시 MovementMode를 먼저 확인한다. `Off`는 오르고 `MultiPhase`만 조기 감속하면 계획 단계의 접촉 분류와 경로 검증으로 진행한다. [Epic Walkable Slope](https://dev.epicgames.com/documentation/en-us/unreal-engine/walkable-slope-in-unreal-engine)

### 2. 경사를 따라 이동 가능 거리 계산

- `ReactionEnvironmentComponent`에 서버 피격 계획용 지면 경로 질의를 추가한다. 현재 바닥을 시작점으로 후퇴 경로의 표본마다 하향 캡슐 바닥 질의로 지지면과 예상 캡슐 높이를 구하고, 직전 표본에서 다음 표본까지 캡슐 스윕으로 이동 공간을 확인한다. 평지→경사와 정상부에서는 표본 간격을 줄여 경사 변화와 지지면 연속성을 확인한다. 지면 위의 캡슐 높이는 충돌 형상과 바닥 여유를 반영한다.
- 연속된 보행 가능 경사는 경로로 사용하고, 경사 뒤 벽이나 천장 등 실제 막힘까지 탐색한다. 급경사, 바닥 단절, 표본 간 높이 불연속은 첫 버전에서 통과 가능하다고 가정하지 않는다. 지면이나 이동 공간 판정이 불확실하면 사유를 남기고 기존 원본 Montage와 CMC 처리로 돌아간다. 이 질의는 정적 지형에 대한 사전 추정이며 실제 캡슐 이동의 충돌 판정은 CMC가 담당한다. [Epic Character Movement Component API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UCharacterMovementComponent?lang=en-US)
- `PlanMulti`가 소비하는 예산의 단위는 현재와 같은 **수평 후퇴 거리(cm)**로 둔다. 장애물이 없으면 원본 후퇴량을 허용한다. 경사 뒤에 벽이 있으면 안전 여유를 뺀 수평 예산을 기존 PELT 구간 정책에 전달하고, 최종 보정 궤적도 갱신한 지면 높이에서 스윕한다. 경사 길이와 수평 이동량을 각각 기록해 경사에서 체감 속도가 달라지는 경우를 확인한다. 급격한 이동 속도 변화가 측정되면 `bMaintainHorizontalGroundVelocity`의 영향을 별도 비교한다. [Epic bMaintainHorizontalGroundVelocity](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/GameFramework/UCharacterMovementComponent/bMaintainHorizon-?application_version=5.5)

### 3. 발 접지와 골반 보정

- 캡슐이 자연스럽게 오르는 것을 확인한 뒤, 기존 발·발볼 좌표와 접지 이동량 기록에 발별 지면 거리와 법선을 추가한다. PELT 분석의 접지 후보를 초기 타이밍으로 활용하고, 실제 지면과 발의 높이·속도로 접지 구간을 검증한다. 접지 중 발이 미끄러지거나 지면을 뚫는 경우에만 다음 보정을 적용한다. [현재 발 접지 계측](../../Source/GE_AIReplication/Combat/Probe/HitWallProbeComponent.cpp#L348) · [PELT 분석 절차](08_관절추출_PELT_개발계획과테스트.md)
- Enemy AnimBP에서 하체 IK가 이미 활성화되어 있는지 확인하고, 활성 경로가 있으면 재사용한다. 접지 중인 발에 지면 위치·법선을 적용하고, 허용 범위 내에서 골반 높이를 보정한다. 발을 떼는 구간에는 IK 가중치를 부드럽게 낮춰 피격 직후 반동과 회복 자세를 살린다. IK 가동 범위를 반복적으로 넘는 각도에서만 경사용 피격 애니메이션 후보를 검토한다. [Epic Foot Placement API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/AnimationWarpingRuntime/FAnimNode_FootPlacement?lang=en-US) · [Kovar 외, Footskate Cleanup for Motion Capture Editing](https://graphics.cs.wisc.edu/Papers/2002/KSG02/)

### 4. 멀티플레이 검증

- 서버가 지면 질의와 이동 예산을 결정한다. `HitReactionComponent`가 공유하는 기존 `ReactionId`, 시작 시각, Montage, 보정 계획을 사용해 원격 클라이언트가 동일한 보정 키로 재생하는지 확인한다. 발 IK는 표시 포즈에 적용한다. [피격 상태 복제 코드](../../Source/GE_AIReplication/Combat/HitReactionComponent.h#L24) · [Epic Networked Character Movement](https://dev.epicgames.com/documentation/en-us/unreal-engine/understanding-networked-movement-in-the-character-movement-component-for-unreal-engine)
- UE 5.8 Development Editor 빌드 후 Standalone과 Listen Server + 원격 Client에서 실행한다. 가능하면 Dedicated Server + Client에서도 반복하고, LAN 기준과 지연·손실 조건을 비교한다. 공격자 소유 클라이언트에서도 AI Enemy는 Simulated Proxy이므로, 플레이어 피격의 Autonomous Proxy 예측·재실행 검증은 별도 후속 범위로 둔다.

## 검증 케이스와 합격 기준

| 케이스 | 확인할 결과 |
|---|---|
| 평지, 장애물 없음 / 기존 벽 | 평지 후퇴·회복과 기존 벽 안전거리가 기준선에서 퇴행하지 않는다. |
| 경사 중간 시작 / 평지→경사 / 경사→정상부 | 걸을 수 있는 경사에서 발이 붙은 채 의도한 방향으로 오르고, 경계에서 조기 정지·비의도적 낙하·눈에 띄는 속도 급변이 없다. 정면 등판 통과 뒤 사선도 비교한다. |
| 경사 뒤 벽 / 보행 한계 바로 아래·위 / 낭떠러지 | 경사면 자체에서 예산을 줄이지 않고, 실제 벽에는 안전 여유를 둔다. 보행 불가 면이나 바닥이 끊긴 구간에서 계획이 통과를 보장하지 않는다. |
| 서버 / 원격 클라이언트 | 같은 피격 ID와 계획 해시를 사용한다. 서버 시각과 Montage 위치 기준으로 비교했을 때 궤적 차이, 위치 보정량, 종료 후 스냅이 평지 네트워크 기준선 대비 악화되지 않는다. |

모든 비교에서 구간별 수평 변위·속도, 캡슐 Z와 바닥 기준 높이, MovementMode, 벽 여유·관통 여부, 접지 중 발의 지면 접선 방향 이동량과 발 높이 오차를 기록한다. 경사를 따라 오른 높이는 바닥에서 떠오른 높이와 구분한다. IK의 합격 기준은 같은 경로에서 적용 전보다 접지 중 미끄러짐과 발 높이 오차가 줄고, 영상에서 반동과 회복이 자연스러운 것이다. 수치 허용치는 1단계 반복 측정의 프레임별 편차와 평지 기준선으로 정한 뒤 구현 전 고정한다.

비교 CSV·빌드 로그·영상은 기존 기록 경로를 재사용하거나 `Saved/Tasks/` 아래 작업 폴더에 README와 함께 저장한다. 이번 문서는 코드 및 문서 검토를 바탕으로 작성했다. 경사 재생, 빌드, 멀티플레이는 구현 단계의 검증 항목이다.
