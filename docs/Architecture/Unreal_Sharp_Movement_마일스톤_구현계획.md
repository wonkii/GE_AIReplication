# GE_AIReplication Enemy AI Sharp Movement 구현 계획

> 2026-10-03 저장소 확인 결과를 반영했다. 아래의 `기존`은 파일 존재와 코드·설정으로 확인한 사실이고, `예정`은 아직 구현하지 않은 실험 파일이다. 기존 템플릿의 동작을 실험 기능의 완료로 간주하지 않는다.

## 0. 현재 프로젝트 상태

| 항목 | 확인 결과 |
|---|---|
| Unreal 프로젝트 | 저장소 루트의 `GE_AIReplication.uproject`, `EngineAssociation: 5.8` |
| 런타임 모듈 | `AIReplicationCore`, `AIReplicationNet`, `GE_AIReplication` 세 모듈이 `.uproject`에 등록됨 |
| 모듈 의존성 | `GE_AIReplication`은 Core·Net을 비공개 참조하고, Net은 Core를 비공개 참조함. Core·Net에는 현재 모듈 시작 코드만 있음 |
| 기존 AI 참고 코드 | `Variant_Combat/AI/CombatEnemy.*`는 `ACharacter`와 CMC를 사용하며 `AddImpulse()` 호출 예시가 있음. Combat·SideScrolling에는 StateTree 기반 AIController와 Blueprint 에셋이 있음 |
| 현재 기본 맵 | `Config/DefaultEngine.ini`의 게임·에디터 기본 맵은 `ThirdPerson/Lvl_ThirdPerson` |
| 실험 구현 | `SharpMovementExperiment` 맵, 전용 Enemy·Impulse Volume·Detector·Snapshot·Predictor·측정 코드는 아직 없음 |

`CombatEnemy`의 Impulse 코드는 참고 자료일 뿐이다. 전투 피해, StateTree, 물리 블렌딩이 측정에 섞이지 않도록 첫 실험은 전용 Enemy와 맵에서 시작한다. 기존 `Variant_*`, `ThirdPerson`, 공용 캐릭터 에셋은 이동하거나 이름을 바꾸지 않는다.

## 1. 프로젝트 목표와 범위

Dedicated Server가 조종하는 Enemy AI에 순간적인 Impulse·급회전·급정지가 발생하면, 클라이언트의 `Simulated Proxy`는 다음 서버 이동 상태가 도착할 때까지 이전 상태로 이동한다. 새 상태가 도착한 후에는 CMC(Character Movement Component)의 Network Smoothing이 표시 위치를 수렴시킨다. 이 과정의 순간 위치 오차와 눈에 띄는 보정을 **실측**하고 줄이는 것이 목표다.

연구 질문은 다음 두 가지다.

1. Sharp Movement 직후 **서버 상태를 한 번 빨리 보내는 것**만으로 충분한가? 짧은 고빈도 복제가 필요한가?
2. 첫 급변 상태를 받은 뒤, 클라이언트가 **짧은 구간을 사건에 맞게 예측**하면 같은 네트워크 비용에서 기본 CMC보다 위치 오차와 시각적 튐을 줄일 수 있는가?

Impulse Volume은 반복 가능한 사건과 Ground Truth를 만드는 장치다. **Detector는 Volume의 Overlap·Impulse 호출 여부를 읽지 않고, 적용 후 실제 Velocity·Direction 변화만 관찰해야 한다.** 이 원칙은 모든 구현과 평가에서 지킨다.

### 범위와 전제

- Unreal의 표준 `ACharacter`, CMC, Actor Replication, RepNotify, AIController, Networking Insights, Network Emulation을 사용한다. 외부 네트워크·예측 라이브러리는 쓰지 않는다. Blueprint와 프로젝트 C++ 코드는 허용한다.
- AI 이동의 권한은 서버에 있다. 클라이언트는 충돌 판정용 Capsule을 임의로 옮기지 않는다. 첫 예측 구현은 **표시용 Mesh만** 대상으로 한다.
- 기존 `Replicate Movement`를 유지한다. 제안 Snapshot은 이를 대체하는 권위 상태가 아니라, 다음 복제 사이의 표시 경로를 보조하는 자료다.
- 구현 대상 프로젝트는 루트의 `GE_AIReplication.uproject`이며 UE 5.8로 설정되어 있다. 아래 공식 문서의 API를 실제 구현 시 이 버전과 대조한다.
- `RepNotify` Snapshot과 기본 이동 상태가 같은 Actor의 복제 경로를 타므로, Snapshot이 첫 이동 패킷보다 자동으로 빨리 도착한다고 가정하지 않는다. 제안 방식의 주된 검증 대상은 **첫 수신 이후의 예측 품질 및 전송량**이다.

공식 동작 근거: [CMC 네트워크 이동](https://dev.epicgames.com/documentation/en-us/unreal-engine/understanding-networked-movement-in-the-character-movement-component-for-unreal-engine), [Actor 복제 흐름](https://dev.epicgames.com/documentation/unreal-engine/detailed-actor-replication-flow-in-unreal-engine).

## 2. 제안 시스템 구성

```text
Dedicated Server
  AIController ──> ASharpEnemyCharacter (ACharacter + CMC)
                         │
  ASharpImpulseVolume ──> AddImpulse() + Ground Truth 기록
                         │ 실제 CMC 이동 결과만 관찰
                         └── USharpMovementDetectorComponent
                                  └── 실제 Δv·Δθ 판정·Risk 이벤트
                                            └── SharpUpdatePolicy
                                                 └── Enemy의 Snapshot 갱신
                                                      + ForceNetUpdate 요청
                                      │
                                      ▼
Client: Simulated Proxy ──> OnRep_SharpMoveSnapshot
                              └── USharpVisualPredictorComponent
                                   └── 짧게 Mesh 위치 예측
                                         └── 다음 서버 상태에서 수렴
```

다음 이름은 구현자가 그대로 사용해도 되는 **권장 이름**이다.

| 구성 | 책임 | 복제 여부 |
|---|---|---|
| `ASharpEnemyCharacter` 또는 `BP_SharpEnemy` | AI의 `ACharacter`, CMC, 이동 복제, Snapshot 소유 | Actor와 Movement 복제 |
| `ASharpImpulseVolume` 또는 `BP_SharpImpulseVolume` | 서버에서 Overlap 감지, Impulse 1회 적용, Ground Truth 기록 | Volume 자체의 복제는 실험에 불필요 |
| `USharpMovementDetectorComponent` | 서버의 이동 전후 속도 비교, Risk 진입·해제 | 자체 상태는 복제하지 않음 |
| `FSharpMoveSnapshot` | 급변 후 예측 시작 상태를 하나의 Struct로 전달 | Enemy의 `ReplicatedUsing` 프로퍼티 |
| `USharpVisualPredictorComponent` | 클라이언트 Simulated Proxy의 시각적 위치 계산·수렴 | 계산 상태는 클라이언트 로컬 |
| `ASharpExperimentManager` | 시나리오·반복 횟수·모드·로그 식별자 관리 | 실험 제어는 서버; 필요한 설정만 클라이언트에 전달 |

Blueprint만으로 시나리오와 초기 Detector를 만들 수 있지만, 기본 이동 패킷 수신 시점과 `SmoothCorrection`을 정확히 기록하고 시각 예측을 CMC와 안정적으로 통합하려면 프로젝트 C++ 클래스를 권장한다. 이는 외부 기술 도입이 아니라 Unreal 내장 API의 확장이다.

## 3. 공통 데이터 계약

### 3.1 서버가 보내는 최신 급변 Snapshot

`FSharpMoveSnapshot` 권장 필드:

| 필드 | 의미 |
|---|---|
| `SequenceId` (`uint32`) | 이전 사건 또는 늦게 도착한 Snapshot을 무시하기 위한 증가 번호 |
| `ServerTime` | 사건이 **실제 이동 업데이트에서 관찰된** 서버 시각 |
| `StartPosition` | 해당 시각의 서버 위치 |
| `PostVelocity` | Impulse·급변이 반영된 **후**의 Velocity |
| `MovementMode` | Walking/Falling 등 예측 모델 및 종료 조건 판단 |
| `PredictionType` | `None`, `ConstantVelocity`, `Airborne` 등 최소 모델 선택값 |

처음부터 Raw Impulse까지 중복 전송하지 않는다. `PostVelocity`를 받은 클라이언트가 같은 Impulse를 또 더하면 이중 적용된다. 위치·속도의 양자화(`FVector_NetQuantize` 계열)는 정확도 기준선을 확보한 후 전송량 최적화 단계에서 적용한다. [Epic의 복제 대역폭 권고](https://dev.epicgames.com/documentation/unreal-engine/performance-and-bandwidth-tips-for-unreal-engine).

하나의 Struct를 `ReplicatedUsing=OnRep_SharpMoveSnapshot`으로 복제한다. 서로 다른 RepNotify의 호출 순서는 보장되지 않으므로 관련 값을 흩어 놓지 않는다. RepNotify는 **마지막 상태 전달**에 적합하며, 서버에서 한 복제 주기 동안 여러 번 값이 바뀌면 모든 중간 사건의 수신을 보장하지 않는다. 연속 Impulse 테스트에서 이 한계를 확인하고, 모든 사건이 꼭 필요할 때만 RPC 또는 별도 큐를 검토한다. [프로퍼티 복제](https://dev.epicgames.com/documentation/en-us/unreal-engine/replicate-actor-properties-in-unreal-engine), [복제 실행 순서](https://dev.epicgames.com/documentation/en-us/unreal-engine/replicated-object-execution-order-in-unreal-engine).

### 3.2 로그의 공통 키

모든 서버·클라이언트 로그에 `RunId`, `TrialId`, `ActorId`, `SequenceId`, `ServerWorldTime`, `Mode`를 넣는다. `AGameStateBase::GetServerWorldTimeSeconds()`를 공통 시각 기준으로 사용하되, 로그 분석에서는 서버 궤적을 해당 시각으로 보간해 비교한다. 서버의 **현재 위치**와 클라이언트의 **과거 수신 상태**를 단순히 같은 행에 놓고 비교하면 지연과 예측 오차가 섞인다. [Game State 시간](https://dev.epicgames.com/documentation/unreal-engine/game-mode-and-game-state-in-unreal-engine).

## 4. 전체 파일 구조와 마일스톤 안내

아래는 전체 구조다. `docs/M0`~`docs/M8`에는 단계별 개발 계획을 두고, 구현 소스는 기능별 폴더에 둔다. 소스 파일 옆에는 최초 도입 마일스톤을 표시했다. `.h/.cpp`는 동일한 이름의 헤더와 구현 파일 한 쌍을 뜻한다. `Content`의 `.uasset`·`.umap`은 Unreal Editor에서 생성한다.

```text
GE_AIReplication/                                      [기존 프로젝트 루트]
├─ GE_AIReplication.uproject                           [기존, UE 5.8]
├─ Config/
│  └─ DefaultEngine.ini                                [기존, 기본 맵은 ThirdPerson]
├─ Source/
│  ├─ AIReplicationCore/                               [기존 모듈, 순수 계산]
│  │  ├─ Public/Detector/
│  │  │  ├─ SharpMovementSample.h                       [M4 예정]
│  │  │  ├─ SharpDetectorConfig.h                       [M4 예정]
│  │  │  └─ SharpMovementClassifier.h                   [M4 예정]
│  │  └─ Private/Detector/
│  │     └─ SharpMovementClassifier.cpp                 [M4 예정]
│  ├─ AIReplicationNet/                                [기존 모듈, 전송 데이터 계약]
│  │  └─ Public/
│  │     └─ SharpMoveSnapshot.h                         [M5 예정]
│  └─ GE_AIReplication/                                [기존 모듈, Unreal Actor·실험 통합]
│     ├─ Variant_Combat/AI/CombatEnemy.h/.cpp           [기존, 참고용]
│     └─ SharpMovement/
│        ├─ Enemy/
│        │  ├─ SharpEnemyCharacter.h/.cpp               [M1 생성, M3·M4·M5·M6 확장]
│        │  └─ SharpEnemyAIController.h/.cpp            [M1 예정]
│        ├─ Experiment/
│        │  ├─ SharpExperimentTypes.h                   [M0 예정: 모드·공통 ID]
│        │  ├─ SharpExperimentSettings.h/.cpp           [M0 예정: 설정]
│        │  └─ SharpExperimentManager.h/.cpp            [M0 생성, M2~M8 확장]
│        ├─ Scenario/
│        │  └─ SharpImpulseVolume.h/.cpp                 [M2 예정]
│        ├─ Measurement/
│        │  ├─ SharpMovementRecorder.h/.cpp             [M2 생성, M3·M8 확장]
│        │  └─ SharpMovementMetrics.h/.cpp              [M3 생성, M8 확장]
│        ├─ Detector/
│        │  └─ SharpMovementDetectorComponent.h/.cpp    [M4 예정: 서버 전용]
│        ├─ Net/
│        │  └─ SharpUpdatePolicy.h/.cpp                  [M5 예정]
│        └─ Prediction/
│           └─ SharpVisualPredictorComponent.h/.cpp    [M6 생성, M7 확장]
├─ Content/SharpMovementExperiment/
│  ├─ Maps/SharpMovementExperiment.umap                [M0 예정]
│  ├─ Blueprints/BP_SharpEnemy.uasset                  [M1 예정]
│  ├─ Blueprints/BP_SharpExperimentManager.uasset      [M0 예정]
│  ├─ Blueprints/BP_SharpImpulseVolume.uasset          [M2 예정]
│  └─ Data/DA_SharpExperimentSettings.uasset           [M0 예정]
└─ docs/
   ├─ PROJECT_OVERVIEW.md                              [기존: 모듈 경계]
   ├─ Architecture/
   │  ├─ Unreal_Sharp_Movement_마일스톤_구현계획.md      [이 문서: 전체 구조]
   │  └─ SharpMovement_실험결과.md                     [M8 예정]
   ├─ M0/M0_실험_설정.md                               [M0 개발 계획]
   ├─ M1/M1_서버_AI_복제.md                            [M1 개발 계획]
   ├─ M2/M2_Impulse_재현.md                           [M2 개발 계획]
   ├─ M3/M3_CMC_기준선_측정.md                         [M3 개발 계획]
   ├─ M4/M4_Detector.md                               [M4 개발 계획]
   ├─ M5/M5_전송_정책.md                              [M5 개발 계획]
   ├─ M6/M6_시각_예측.md                              [M6 개발 계획]
   ├─ M7/M7_견고성.md                                 [M7 개발 계획]
   └─ M8/M8_최종_평가.md                              [M8 개발 계획]
```

| 단계 | 개발 내용 | 상세 문서 |
|---|---|---|
| M0 | 실험 맵과 설정 고정 | [M0 개발 계획](../M0/M0_실험_설정.md) |
| M1 | 서버 권한 Enemy AI와 기본 이동 복제 | [M1 개발 계획](../M1/M1_서버_AI_복제.md) |
| M2 | 서버 전용 Impulse Volume과 Ground Truth | [M2 개발 계획](../M2/M2_Impulse_재현.md) |
| M3 | 기본 CMC의 오차와 Rubber Banding 측정 | [M3 개발 계획](../M3/M3_CMC_기준선_측정.md) |
| M4 | Volume과 독립적인 Sharp Movement Detector | [M4 개발 계획](../M4/M4_Detector.md) |
| M5 | 사건 직후 전송 방식 비교 | [M5 개발 계획](../M5/M5_전송_정책.md) |
| M6 | 사건 기반 클라이언트 시각 예측 | [M6 개발 계획](../M6/M6_시각_예측.md) |
| M7 | 손실·연속 사건·충돌에 대한 견고성 | [M7 개발 계획](../M7/M7_견고성.md) |
| M8 | 고정 네트워크 예산 비교와 최종 정리 | [M8 개발 계획](../M8/M8_최종_평가.md) |

**모듈 경계:** `AIReplicationCore`에는 Actor, CMC, 복제, UI를 넣지 않는다. Detector의 샘플·설정·판정 계산만 둔다. `GE_AIReplication`의 Component가 CMC를 읽어 Core 판정기를 호출하며, Enemy와 전송 정책을 연결한다. `AIReplicationNet`은 Snapshot의 데이터 계약을 둔다. Unreal `USTRUCT`를 여기에 추가할 때는 현재 `AIReplicationNet.Build.cs`의 `Core`만으로 부족하므로 실제 사용 타입에 맞춰 `CoreUObject`·`Engine` 의존성을 추가한다. 다른 모듈의 공개 헤더가 이 타입을 노출한다면 공개 의존성도 함께 점검한다.

**Detector 담당 경계:** `SharpMovementDetectorComponent`는 Volume·Overlap·Impulse 호출을 참조하지 않는다. 출력은 `SequenceId`, 관측 시각, 위치, 사후 Velocity, Movement Mode, `Δv`, `Δθ`, 판정 이유와 Risk 상태 변화다. `SharpUpdatePolicy`가 이를 받아 전송을 결정하고, Recorder가 Volume의 정답 로그와 사후 결합한다.

## 5. 구현 순서와 중간 의사결정

```text
M0 실험 맵·설정
  ↓
M1 서버 AI 복제
  ↓
M2 Impulse 재현
  ↓
M3 기본 CMC 측정 ── 문제가 미미하면 조건 재설계
  ↓
M4 Detector
  ↓
M5 SingleShot/Burst/EventOnly 비교
  ├─ 첫 패킷 이후 오차가 거의 없음 → 전송 정책 중심으로 마무리
  └─ 이후에도 오차·보정이 큼 → M6 사건 기반 시각 예측
                                  ↓
                              M7 견고성
                                  ↓
                              M8 고정 예산 최종 평가
```

### 아직 결정·검증할 사항

1. 전용 `ASharpEnemyCharacter`는 `ACharacter + CMC`로 구현한다. 기존 `ACombatEnemy`도 이 구조를 사용하지만 전용 Enemy의 생성·복제는 아직 검증되지 않았다.
2. 첫 실험의 AI 이동 방식을 고정한다. 기존 Combat·SideScrolling AI는 StateTree를 사용하지만 새 실험 Enemy의 `AI MoveTo`/NavMesh 또는 직접 이동 방식은 아직 선택되지 않았다. 선택한 방식과 Impulse 직후 경로 추종 동작을 기록한다.
3. 첫 장면은 장애물 없는 직선 이동과 1회 Impulse로 시작한다. 서버 1개·클라이언트 2개의 실행, 전용 Actor 복제, Network Emulation은 아직 검증되지 않았다.
4. M3에서 기본 CMC의 실제 실패 조건을 확인한 뒤 M6 예측 모델의 범위를 결정한다.
