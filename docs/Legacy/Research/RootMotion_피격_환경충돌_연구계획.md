# 환경 충돌 위험을 고려한 Root Motion 피격 동작 선택과 행동 동기화 연구계획

작성일: 2026-11-02 KST. 상태: 설계안. 이 문서의 신규 클래스·에셋·명령줄 옵션·측정기는 앞으로 구현할 대상이며 현재 기능이 아니다.

## 1. 연구 질문과 범위

> 환경 충돌 위험을 고려한 Root Motion 피격 동작 선택과 행동 동기화가, 동일한 측정 네트워크 비용 구간에서 관통·비의도적 상승·큰 Correction을 줄이는가?

핵심은 충돌 후 화면을 오래 보간하는 것보다, 환경에서 실행 가능한 피격 동작을 고르고 서버와 클라이언트가 같은 행동 계획을 재현하도록 만드는 것이다. Correction 0을 보장하지 않는다. 정상적인 CMC 권한 검사·복제·재시뮬레이션·충돌은 모든 비교군에서 유지한다.

- H1: 환경을 고려한 후보 선택은 고정 피격 동작보다 서버 자체의 벽 관통·비의도적 상승과 무리한 이동 요구를 줄인다.
- H2: 서버가 확정한 목표·제한·상태 전환 공유는 각 실행 지점이 이를 따로 계산하는 방식보다 행동 불일치와 큰 네트워크 보정을 줄인다.
- H3: 두 방법의 결합은 실제 bytes/s 예산을 맞췄을 때도 개선을 유지한다.
- 품질 제약: 피격을 모두 제자리 동작으로 바꾸거나 반응 시작을 늦추어 수치만 좋아지게 하지 않는다. 요청 대비 실제 밀림, 반응 지연, 동작 연속성도 평가한다.

### 주 실험과 확장 실험

주 실험은 평지, 정적인 수직 벽, 하나의 공격과 하나의 피격, 지상 이동 Root Motion Montage로 제한한다. 실제 문제는 공격자 플레이어와 다른 클라이언트가 소유한 피격자이므로 Autonomous Proxy 검증을 필수로 한다.

다른 Enemy와의 충돌, 움직이는 장애물, 경사·계단, 연속 피격은 주 실험 통과 후 확장한다. 전체 물리 월드 rollback, 신경망 신규 학습, 전체 Motion Matching 교체, GAS/Mover 전환, 래그돌·공중 피격은 첫 구현에서 제외한다.

## 2. 선행기술과 이번 구현의 위치

| 근거 | 가져올 원리 | 그대로 주장하지 않을 것 |
|---|---|---|
| [Environment-aware Motion Matching, 2025](https://arxiv.org/html/2510.22632v1) | 주변 장애물 비용을 동작 검색에 포함 | Montage 후보 점수화를 논문 전체 재현이라고 부르지 않음 |
| [EMM 공식 Unity 코드](https://github.com/UPC-ViRVIG/Environment-aware-Motion-Matching) | 환경 특징·검색 비용의 구현 참고. 코드 MIT; 에셋·의존성은 별도 확인 | UE 플러그인으로 바로 사용 가능하다고 가정하지 않음 |
| [Physics-Aware Dead Reckoning, 2012](https://mural.maynoothuniversity.ie/id/eprint/4164/1/SMcL_ISSC2012_physics.pdf) | 접촉 시작·해제와 관련 객체 갱신, 위험 구간의 추가 전송 | P2P·Box2D의 권한 이전을 서버 권한 게임에 그대로 도입하지 않음 |
| [Predictive Dead Reckoning, 2024](https://ece.uwaterloo.ca/~sl2smith/papers/2023TOG-Predictive_Dead_Reckoning.pdf) | 일반 이동·충돌·복귀 단계를 나누어 평가 | 차량·정적 장애물 결과를 동적 Root Motion 충돌의 검증으로 사용하지 않음 |
| [Unreal Animation Blueprint](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-blueprint-editor-in-unreal-engine), [Montage](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-montage-in-unreal-engine) | 네트워크용 Montages Only와 올바른 재생 명령 동기화 | Montage 재생만으로 게임 행동 상태가 전부 일치한다고 가정하지 않음 |
| [CMC 네트워크 이동](https://dev.epicgames.com/documentation/en-us/unreal-engine/understanding-networked-movement-in-the-character-movement-component-for-unreal-engine), [FRepRootMotionMontage](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/FRepRootMotionMontage) | 엔진 Root Motion 복제·복구를 사용 | AI Simulated Proxy와 소유 플레이어의 correction 경로를 혼동하지 않음 |
| [Motion Warping](https://dev.epicgames.com/documentation/en-us/unreal-engine/motion-warping-in-unreal-engine) | 적정 범위에서 안전한 목표에 Root Motion을 맞춤 | 자동 충돌 회피·완전한 동기화 기능으로 간주하지 않음 |
| [Epic CMC 모범 사례](https://dev.epicgames.com/community/learning/tutorials/ywD1/unreal-engine-best-practices-for-networked-movement-abilities) | 이동 처리 통합, 과거 이동 의존성 기록, 다른 세계 상태 참조로 인한 차이 분석 | 소유 플레이어용 saved move 경로를 AI에 무조건 추가하지 않음 |

이번 기여 후보는 기존 기법을 UE 5.8 CMC의 피격 행동에 통합하고, 동작 선택과 행동 계획 공유의 효과를 분리하며, 실제 네트워크 비용·행동 품질까지 통제해 검증하는 것이다. 최초 기법이라는 주장은 추가 문헌 검토 없이 하지 않는다.

## 3. 현재 저장소 재사용과 변경 경계

2026-11-02 확인 기준으로 프로젝트는 UE 5.8이고 Core / Net / GE_AIReplication 모듈을 사용한다. MotionWarping은 현재 `.uproject`에 명시적으로 활성화되어 있지 않으며 통합 모듈의 직접 의존성에도 없다.

작업 트리에는 기존 LAN 연결·플레이어 smoothing·기록 관리 변경과 `AM_HitReaction_Back/Front` 에셋 등이 있다. 이번 설계 작성에서는 이를 수정하지 않았다. 에셋 이름만으로 Root Motion 추출·Section·Warp Window·네트워크 재생이 검증됐다고 보지 않는다. 별도 실험 GameMode/Character를 사용해 기존 플레이어 BeginPlay의 빈도·smoothing 설정이 비교 설정을 덮어쓰지 않게 한다.

| 기존 자산·코드 | 재사용 방법 | 필요한 보완 |
|---|---|---|
| AIReplicationCore | Actor를 참조하지 않는 후보 점수·정책 계산 | 충돌 쿼리는 통합 모듈에서 하고 수치 특징만 입력 |
| AIReplicationNet | 작은 데이터 계약과 직렬화 경계 | SharpMoveSnapshot과 별개의 ReactionPlan 계약 |
| FSharpRecordingFiles | CSV 버퍼·경로·종료 처리 원리 | 반응 실험 식별자와 별도 schema |
| SharpMovementRecorder/Diagnostics | 샘플링·보정 로그 구현 참고 | ASharpEnemyCharacter 캐스팅과 SimulatedProxy 전제가 있어 그대로 부착하지 않음 |
| SharpConnectionRecorder | 연결 누적 bytes/packets 카운터 수집 로직 | 기존 Manager/Summary 의존성을 분리한 반응 실험용 어댑터 |
| RunSharpTrial.ps1 | 독립 프로세스 실행·네트워크 프로필·종료 검증 | 기존 Enemy/7모드 로그 검사를 사용하는 별도 RunReactionTrial.ps1 |
| Analysis/native_reports.py | 품질 검사·동일 비용 비교 방법 | Reaction 전용 수집·지표·보고서 추가 |
| 기존 VisualPredictor | 기존 실험의 기능으로 유지 | 반응 실험에서는 부착하지 않아 등속 예측·Mesh smoothing 중복 방지 |
| 기존 Spline 이동·Impulse 맵 | 기존 실험 회귀 확인 장면 | 새 맵에서 피격 실험. Root Motion 중 Spline 입력으로 제어하지 않음 |

기존 7개 전송 모드 enum을 피격 알고리즘 enum으로 바꾸지 않는다. 기존 M0~M8 결과와 신규 실험 결과를 섞지 않는다. 양쪽이 안정화된 후에만 공통 Recorder 인터페이스를 추출한다.

## 4. 실행 구성과 권한

Dedicated Server 1개와 독립 클라이언트 2개를 기본 구성으로 한다.

| 실행 지점 | 공격자 A | 피격자 V |
|---|---|---|
| 서버 | Authority | Authority |
| 클라이언트 1 | Autonomous Proxy | Simulated Proxy: 피격자 관찰 화면 |
| 클라이언트 2 | Simulated Proxy | Autonomous Proxy: 피격자 소유 화면 |

초기 디버깅은 서버 AI 피격자를 사용할 수 있으나 이것만으로 주 실험을 완료 처리하지 않는다. 두 클라이언트의 역할을 바꾸어도 결과가 유지되는지 확인한다. 재현용 GameMode/PlayerController에서 실험 캐릭터 possession과 입력·고정 카메라를 제공하고, 기존 LAN 접속 UI/Host 진입 상태에 의존하지 않는 독립 시작 경로를 둔다. 최종 LAN 시연은 별도 회귀 항목으로 둔다.

- 명중·피해·경직·반응 계획의 최종 결정은 서버가 한다.
- 공격 클라이언트가 피격 캐릭터를 직접 이동시키거나 명중 전에 피격 이동을 강제하지 않는다.
- 모든 비교군은 동일한 서버 명중 확정 규칙과 입력 잠금 정책을 사용한다.
- 1차 계측에서는 정해진 시각·사거리의 서버 명중 사건으로 반복성을 확보한다. 이후 공격 Montage의 서버 공격 창/무기 sweep으로 연결해 시연한다. 두 실행 종류를 manifest에 구분한다.
- 공격과 피격의 별도 Root Motion을 각각 CMC로 처리한다. 같은 변위에 AddImpulse와 Montage 이동을 이중 적용하지 않는다.

## 5. 신규 구성과 파일 계획

아래 이름은 제안이며 구현 전에 네이밍만 조정할 수 있다. 책임 경계는 유지한다.

```text
Source/AIReplicationCore/{Public,Private}/Reaction/
  SharpReactionScore                 # 수치 특징·유효성·후보 순위
Source/AIReplicationNet/{Public,Private}/Reaction/
  SharpReactionPlan                  # 행동 의도·revision·직렬화
Source/GE_AIReplication/SharpReaction/
  Character/SharpReactionCharacter    # ACharacter, player/AI possession 선택
  Experiment/SharpReactionGameMode, SharpReactionPlayerController
  Movement/SharpReactionMovementComponent # CMC subclass, 이동/보정 계측
  Animation/SharpReactionLibrary      # 후보 DataAsset·ID registry
  Animation/SharpReactionTrajectoryBaker # Editor 전용 추출
  Environment/SharpReactionRiskEvaluator # sweep·overlap·특징 생성
  Planning/SharpReactionPlanner      # 후보 평가·안전 목표·fallback
  Execution/SharpReactionComponent   # 권위·수신·행동 상태 머신
  Net/SharpReactionMoveData          # 소유자 saved move/packed move 확장
  Measurement/SharpReactionRecorder  # 역할별 Root Motion·접촉·보정 로그
  Measurement/SharpReactionConnectionRecorder
  Experiment/SharpReactionExperimentManager
  Experiment/SharpReactionScenario   # 바닥·벽·시작점·명중 정의
Content/SharpReactionExperiment/
  Maps/L_ReactionWallLab
  Blueprints/BP_ReactionCharacter, BP_ReactionManager
  Animation/                         # Montage·AnimBP·Warps
  Data/DA_ReactionLibrary, DA_ReactionSettings
Scripts/RunReactionTrial.ps1
Scripts/Analysis/reaction_collect.py
Scripts/Analysis/reaction_metrics.py
docs/Research/RootMotion_피격_환경충돌_실험결과.md
```

첫 구현은 C++ CMC/계측/계획 + Blueprint Montage/장면 설정으로 한다. MotionWarping 플러그인과 모듈 의존성을 추가한다. Editor 추출기는 Editor 타깃에서만 컴파일한다. GAS·GameplayAbilities·PoseSearch는 필요가 확인되기 전 추가하지 않는다.

## 6. 구현 상세 A: 피격 애니메이션 라이브러리

첫 후보군은 같은 방향의 BackLong, BackShort, InPlace, WallHit 네 가지로 제한한다. 이후 좌우 방향과 회전형을 늘린다. 기존 에셋에서 실제 Root Motion·Skeleton·리타기팅 상태를 확인하고, 부족하면 적법하게 사용할 수 있는 에셋을 확보하는 작업을 R1 선행 조건으로 둔다.

각 후보의 DataAsset 항목:

- 고정 CandidateId, Montage 참조, Section, 라이브러리 revision/hash.
- 허용 공격 방향·피격 자세·MovementMode·피격 강도.
- 유효 이동 구간, 명시적 Warp Window, WallHit 전환 구간.
- 루트 궤적의 시간·누적 위치·회전 샘플, 선택적인 몸통/머리 proxy.
- 최소/최대 이동 스케일·허용 회전량·종료 자세·기본 지속 시간.
- 의도한 수직 이동 여부. 주 실험 후보는 비의도적 수직 이동을 요구하지 않음.

Editor에서 Montage의 지정 Section/구간을 샘플링해 루트 이동을 누적 추출한다. 최초에는 30~60Hz로 베이크하고 그 사이의 각 선분을 연속 Capsule sweep한다. 단순히 끝점이나 0.3초 간격의 위치만 검사하지 않는다. 실제 엔진의 리타기팅·블렌드·Warp 결과와 baked 궤적이 일치하는지 장애물 없는 장면에서 확인하고, 설정 변경 시 bake/hash를 갱신한다.

InPlace는 비상 fallback 또는 실제로 공간이 없는 조건에서 허용한다. 모든 사건을 InPlace로 바꾸는 정책은 개선으로 인정하지 않는다. 원래 피격 의도와 실제 밀림 차이를 별도 보고한다.

## 7. 구현 상세 B: 환경 위험 평가와 후보 선택

### B1. 충돌 쿼리

피격자의 실제 scaled Capsule 크기와 준비된 collision profile을 사용한다. 초기 버전은 별도의 ReactorObstacle 채널을 설정하고 실제 CMC에서 막히는 벽과 일치하는지 자동/수동 검증한다. 기존 Visibility 채널을 그대로 재사용하지 않는다.

1. 시작 Capsule overlap을 먼저 확인한다. 이미 깊게 겹친 사건은 정상 후보 평가와 구분한다.
2. 후보의 root-relative 궤적을 시작 Actor/Root 기준 world-space로 변환한다.
3. 각 선분에서 Capsule sweep으로 첫 blocking hit와 이동 가능 구간을 수집한다.
4. 바닥 접촉과 수직 벽 충돌을 구분한다. 바닥을 제거하기 위해 모든 WorldStatic을 무시하지 않는다.
5. 1차는 정적인 벽만 사용한다. 동적 객체는 후속 단계에서 속도·시각·수명 정보를 함께 기록한다.
6. 몸통·머리의 간단한 bone proxy 검사는 시각적 관통 위험 측정용이며 추가 blocking collision을 켜지 않는다.

### B2. 후보 비용과 제약

명중 방향·MovementMode·후보 에셋 유효성은 hard constraint로 처리한다. 비용 예시는 다음과 같다. 모든 항은 단위가 없도록 정규화하고 가중치는 최종 평가 전에 고정한다.

`J = wc*CollisionRisk + wz*UnexpectedRiseRisk + wp*PoseDiscontinuity + wk*KnockbackLoss + ww*WarpDistortion`

- CollisionRisk: 첫 장애물까지의 여유, 예상 접촉 구간, 초기 침투 여부.
- UnexpectedRiseRisk: 비의도적 높이 변화나 걷기 가능한 장애물 경사와의 접촉 위험. 실제 상승을 보장하는 예측값으로 부르지 않는다.
- PoseDiscontinuity: 현재 자세와 후보 시작 자세의 차이. 첫 버전은 방향/자세 태그, 후속 버전은 root-relative 관절 거리.
- KnockbackLoss: 공격이 요구한 밀림 거리·방향과 후보 결과의 차이.
- WarpDistortion: 원래 이동 대비 스케일·yaw 변경의 크기.

같은 비용이면 CandidateId 순서로 선택하여 선택 순서가 프로세스별로 달라지지 않게 한다. 첫 버전은 학습 없이 완전 탐색하며 후보 수·sweep 수·계산 시간을 기록한다.

### B3. 안전 목표와 Warping

첫 blocking hit의 swept Capsule 중심을 기준으로 진행 방향 반대쪽에 작은 safety margin을 둔다. 벽 표면의 ImpactPoint를 Capsule 중심 목표로 직접 사용하지 않는다. 여유 거리가 짧으면 긴 동작을 과도하게 압축하지 않고 BackShort/InPlace/WallHit를 선택한다.

Warp한 궤적은 원래 궤적과 달라질 수 있다. 동일 변환을 모사해 전체 경로를 재검사하고, 이를 정확히 예측하기 어려운 회전·곡선 Warp는 첫 버전에서 제외한다. 최초 지원은 직선 지상 translation, 회전 고정, 검증된 Warp Window다. 최종 Capsule 이동은 항상 CMC sweep에 맡긴다.

## 8. 구현 상세 C: 실행과 벽 반응

```text
Idle -> Planned -> Playing
                    |-> WallContact -> WallReaction -> Recovering -> Idle
                    |-> Recovering -> Idle
모든 단계 -> Cancelled / Replaced
```

서버가 상태 전환을 결정한다. 플레이어 제어 잠금·damage·stun 지속 시간은 알고리즘 간 동일하게 유지한다. Montage의 시작/종료와 gameplay 경직 길이를 반드시 같게 할 필요는 없으며 변경 사실을 기록한다.

- Playing: 지정 Montage·Section·WarpTarget을 적용한다. Root Motion이 이동을 만들고 CMC가 충돌을 처리한다.
- WallContact: 실제 blocking hit와 요청 이동 대비 실제 이동 부족을 기록한다. callback 재진입을 막고 접촉 유지/해제에 짧은 hysteresis를 둔다.
- WallReaction: 벽 내부로 향하는 이동을 계속 생성하지 않는 검증된 Section/Montage로 전환한다. 단순 StopMovementImmediately만으로 Root Motion이 멈춘다고 가정하지 않는다.
- Recovering: 이동 제어 복귀, 입력 잠금 해제, 마지막 authoritative 상태를 게시한다.
- Replaced: 더 최신 피격 ID로 교체한다. 이전 행동의 타이머·notify·취소 콜백은 새 행동을 끝내지 못한다.

공통 안전 정책은 모든 비교군에 동일하게 적용한다. 벽 충돌 중에만 Flying으로 바꾸어 우회하거나, 전역 MaxStepHeight=0으로 계단을 제거하거나, 벽 collision을 끄는 해결은 사용하지 않는다. 별도 상승 원인 실험에서 해당 벽의 step-up·walkability 설정을 확인하되 알고리즘별로 다르게 하지 않는다.

커스텀 Root Motion 제한이 필요하면 CMC의 이동 평가 안에서 적용한다. `ProcessRootMotionPre/PostConvertToWorld` 같은 훅은 MotionWarping이 사용하는 delegate와 충돌하지 않게 통합하고 실행 순서를 검사한다. Tick/OnRep에서 Capsule을 직접 Lerp/SetActorLocation하지 않는다. 첫 구현은 내장 CMC 충돌·Montage 전환을 사용하고 추가 변위 투영은 실패 사례가 남을 때 도입한다.

## 9. 구현 상세 D: 행동 의도 동기화

### D1. 계약

`FSharpReactionPlan`은 위치를 강제로 덮어쓰는 별도 권위 상태가 아니라 CMC가 실행할 행동 의도다. 초기에는 충분한 정밀도를 사용하고 측정 후 NetSerialize·양자화를 최적화한다.

| 필드 | 용도 |
|---|---|
| SchemaVersion, Run/Trial/Generation 문맥 | 구버전·다른 실행의 데이터 배제 |
| ReactionId, PlanRevision | 행동 교체와 동일 행동 내 변경 구분 |
| CandidateId, SectionId, LibraryRevision | 동일한 애니메이션 자원 확인 |
| Phase, PhaseRevision, PhaseStartServerTime | 현재 상태의 재구성, 늦은 수신·join 처리 |
| HitServerTime, DecisionServerTime | 명중과 계획 계산 시점 분리 |
| ServerPlanOrigin, TargetPosition, TargetYaw | 평가 기준과 실행 목표. 직접 teleport 명령 아님 |
| PlayRate, AllowedTranslationScale, MovementPolicy | 재생·이동 제약 |
| ContactId/ObstacleId, ContactNormal (필요 시) | 접촉 상태·전환 해석 |
| Duration/Expiry, EndReason | 만료·종료 처리 |

상대 Actor 참조가 아직 resolve되지 않아도 정적인 world target만으로 실행 가능한 주 실험을 먼저 만든다. 라이브러리 hash 불일치와 지원하지 않는 schema는 진단 후 해당 Trial을 invalid 처리한다.

### D2. 권한·전달·중복 처리

- 서버는 자신이 승인한 계획만 활성화한다. 클라이언트는 Candidate/Target을 권위 있게 선택해 서버에 강요하지 않는다.
- 기본 구현은 피격 Actor의 단일 RepNotify 상태 Struct를 사용한다. start/phase/target을 서로 다른 OnRep 호출 순서에 의존하게 나누지 않는다.
- owner 수신 지연이 문제라면 행동 시작/교체당 1회 reliable owner RPC를 비교할 수 있다. property와 RPC는 같은 ID/revision으로 deduplicate하고 추가 비용을 전부 센다. 주 비교에서는 전달 수단을 고정한다.
- 모든 군에서 최소 Montage 재생 명령은 정상 동기화한다. 일부러 재생 복제를 누락한 baseline을 만들지 않는다.
- 수신 측은 최신 문맥·ID·revision·expiry를 검사한다. 중복 데이터는 재생을 다시 시작하지 않는다.
- 종료·교체 상태를 일정 시간 유지해 최신 상태만으로도 복구 가능하게 한다. 모든 transition의 수신이 보장된다고 가정하지 않는다.
- LateJoin/중간 상태 수신 시 시각용 pose seek와 실제 이동 catch-up을 구분한다. 지나간 Root Motion 전체를 현재 위치에서 다시 더하지 않는다.

### D3. 소유 플레이어의 재시뮬레이션

이 부분은 AI 표시 실험과 별도의 필수 작업이다.

1. 기본 Montage Root Motion의 saved move·correction 경로를 먼저 확인한다. 임의로 대체하지 않는다.
2. 신규 이동 파라미터가 재시뮬레이션에 필요하면 `FSavedMove_Character` 파생 타입에 당시 ReactionId/revision·목표·제한·상태를 저장한다.
3. `SetMoveFor`/`PrepMoveFor` 및 실제 5.8 시그니처를 확인하고 과거 이동 평가에서 해당 값을 복원한다. 새 행동 파라미터가 있는 move는 잘못 결합되지 않도록 `CanCombineWith`도 검사한다.
4. 서버의 move 평가에 필요한 경우 `FCharacterNetworkMoveData`/Container를 확장해 ID/revision 등 필요한 참조를 같은 packed move에 넣는다. CMC client timestamp를 임의로 server world time으로 치환하지 않는다.
5. 서버는 클라이언트가 보고한 행동 참조를 승인된 계획 이력과 대조한다. 수신한 최신 계획을 모든 과거 move에 적용하지 않는다. 아직 클라이언트가 모르는 행동에 대한 초기 불일치는 기록하고 정상 correction으로 수렴시킨다.
6. 계획 이력은 saved move 보존 기간을 덮을 수 있게 bounded buffer로 유지한다. 늦은 계획·만료된 history·이전 revision을 강제하는 입력은 명시적으로 복구/거부한다.
7. HitServerTime은 행동 사건의 시각이고 실제 CMC 적용은 각 역할의 이동 틱에서 일어난다. 서버 사건 시각, owner 수신, 첫 적용 move timestamp, root track position을 모두 기록한다.
8. `Now - StartTime`만큼 Montage를 무조건 앞으로 seek해서 물리 이동을 건너뛰지 않는다. 엔진 correction/catch-up과의 상호작용을 R2에서 검증한 뒤 지원한다.

owner에게 아직 알려지지 않은 외부 피격을 완벽히 예측할 수 있다고 주장하지 않는다. 최초 피격 전달 지연과 행동을 공유한 이후의 지속적인 충돌 불일치를 분리한다. 시작을 미래로 예약하는 추가 지연 기법은 후속 대조군이며 주 방법에 숨겨 넣지 않는다.

### D4. 동적 장애물 확장

정적인 벽에서는 동일한 과거 환경을 재구성하기 쉽다. 움직이는 문·다른 Character는 현재 위치를 과거 move 재현에 사용하면 결과가 달라질 수 있다. 확장 단계에서는 서버의 contact constraint/장애물 Transform 이력을 기록하고, 새 위험 발생 시 revision을 갱신한다. 전체 장애물 rollback을 구현하지 않았다면 제한사항으로 명시하고 정적 벽 결과와 합치지 않는다.

## 10. 비교군: 공식 baseline과 2×2 분리 실험

기본 Montage 재생·CMC correction·안전 collision profile·최종 swept movement는 전부 유지한다. 입력 잠금·피해·공격 판정·smoothing 설정도 공통이다. 모든 군의 최신 행동 ID는 공통으로 전달한다.

| 군 | 후보 선택 | 안전 경로/목표 실행 | 목적 |
|---|---|---|---|
| U: UnrealNative | 고정 후보 | 정상 Montage 복제와 CMC 충돌. 추가 사전 Warp 없음 | 엔진 기본 기준선 |
| F-L: FixedLocalPlan | 고정 후보 | 같은 안전 planner를 각 실행 지점의 현재 상태에서 평가 | 사전 검사·Warp만 추가한 실용 기준선 |
| F-S: FixedSharedPlan | 고정 후보 | 서버가 확정한 plan/목표/전환을 공유 | 고정 동작에서 행동 공유 효과 |
| E-L: EnvLocalPlan | 서버가 환경 비용으로 후보 선택, ID는 공유 | 선택 후 실행 목표·제한은 각 지점에서 계산 | 후보 선택 효과 |
| E-S: EnvSharedPlan | 서버가 환경 비용으로 후보 선택 | 서버가 확정한 plan/목표/전환 공유 | 제안 방식 |

F-L/F-S/E-L/E-S는 동일한 planner·후보별 Warp 제약·fallback 코드를 사용한다. LocalPlan은 잘못된 Montage 복제를 의미하지 않으며, 동일한 계획 생성 함수를 각 지점의 상태에서 실행하는 방식이다. E-L의 후보를 각 클라이언트가 따로 선택하게 하여 의도적으로 애니메이션이 다르게 만드는 비교는 하지 않는다.

- E-S vs F-S: 동기화 조건을 맞춘 후보 선택 효과.
- F-S vs F-L, E-S vs E-L: 후보 선택 조건을 맞춘 실행 계획 공유 효과.
- F-L vs U: 사전 검사·Warp 자체의 효과.
- E-S vs U: 전체 개선 효과. 이 비교만으로 각 기능의 기여를 주장하지 않는다.

추가 Burst/접촉 해제 update는 5개 주 비교군 결과 후 별도 실험으로 추가한다. 주 방법에만 고빈도 복제를 켜서 효과를 섞지 않는다. packet payload를 같게 만드는 실험은 인과 분석용 보조 실험으로만 두고, 실제 효율 평가는 필요한 데이터만 보낸 비용을 쓴다.

## 11. 동일 네트워크 비용 비교

설정된 NetUpdateFrequency와 실제 bytes/s는 다르다. 특히 소유자의 CMC move traffic과 Root Motion이 차지하는 비용을 제외하면 안 된다.

### 비용 정의

- 고정된 episode 시간 동안 서버의 각 연결 Out/In bytes를 수집한다. 서버 관점의 합계로 비용을 정의하고 양쪽 카운터를 더해 같은 패킷을 두 번 세지 않는다.
- 서버->owner, owner->server, 서버->observer를 분리 보고하고 전체 비용도 제시한다.
- custom plan/RPC, engine movement/root motion, ACK/재전송을 포함한 연결 전체 비용을 주 지표로 한다. Actor/프로퍼티별 trace 비용은 보조 분석으로 둔다.
- NetTrace의 bit 수와 engine connection bytes/NIC bytes는 같은 값이 아니다. 지표 scope를 명시한다.

### 예산 맞추는 절차

1. 결과 평가와 분리한 calibration 장면/seed에서 각 군의 실제 bytes/s를 측정한다.
2. 공통 시작 설정 예: 서버/클라이언트 60fps, 기본 Actor 갱신 10Hz, 추가 Burst 없음. 실제 서버 tick·프레임·move 송신 간격을 기록한다.
3. 모든 군에 동일한 rate 설정 후보를 적용해 비용-품질 곡선을 만든다. 부족하면 5/10/20/30Hz 등의 Actor 설정을 pilot에서 탐색한다. owner move cadence는 주 비교에서 공통으로 유지한다.
4. 비용 구간이 겹치는 예산 지점을 선택하고 설정을 동결한다. 비교 허용 폭은 사전에 ±5%로 정한다. 예산 충족률도 보고한다.
5. 최종 결과가 좋은 Trial만 비용 기준으로 골라내지 않는다. 동결된 설정에서 발생한 예산 초과도 포함해 보고한다.
6. 실제 비용이 겹치지 않는 군은 '동일 비용 비교 불가'로 표시하고 Pareto 곡선으로 제시한다. padding, 손실 강제 증가, 숨겨진 correction 비활성화로 비용을 맞추지 않는다.

관측자 화면만 좋아지고 owner uplink 비용이 크게 늘면 성공으로 단정하지 않는다. 동일 비용은 '동일 측정 구간/허용 폭 내 비용'이라는 운용 정의이며 비트 단위 완전 동일을 주장하지 않는다.

## 12. 실험 장면과 실행 매트릭스

### 장면

| ID | 장면 | 목적 |
|---|---|---|
| S0 | 장애물 없는 평지 | 정상 동작·요청 밀림·부작용 기준 |
| S1 | 뒤쪽 수직 벽, 공간 충분 | 불필요한 동작 변경 여부 |
| S2 | 뒤쪽 벽, BackLong보다 공간 부족 | 거리 제한과 후보 선택 |
| S3 | 거의 벽에 붙은 상태 | Short/InPlace/WallHit fallback |
| S4 | 벽에 비스듬한 피격 | 옆 미끄러짐과 yaw 안정성 |
| S5 | 두 벽의 코너 | 다중 blocking hit·막힘 복귀 |
| S6 | 연속 피격/중간 취소 | ID/revision·타이머·재시뮬레이션 |
| X1 | 움직이는 문·다른 Character | 후속 동적 장애물 확장 |

S0~S3는 최초 구현의 필수 장면이다. 벽 간격은 임의 고정값뿐 아니라 후보의 원래 이동 길이 L에 대한 비율(예: 0.25L/0.5L/1.2L)로 정의한다. 초기 Capsule overlap은 별도 robustness 조건으로 구분한다.

### 네트워크

기존 프로필을 재사용한다: N0=(0ms,0,0%), N1=(25ms,±5ms,1%), N2=(50ms,±10ms,1%), N3=(100ms,±20ms,5%), N4=(250ms,0,10%). 지연은 각 방향의 에뮬레이션 설정이며 RTT 그 자체가 아니다. 적용한 incoming/outgoing 설정과 실제 관측값을 기록한다. 주 실험은 두 연결에 같은 프로필을 적용하고, 비대칭 네트워크는 후속 조건으로 둔다.

- 기능 확인: U/E-S × S0~S3 × N0/N2 × 조건당 3회.
- 주 평가: 5개 군 × S0~S3 × N0/N2/N3 × 20회 = 1,200 episodes.
- 견고성: 5개 군 × S4~S6 × N0/N2/N4 × 10회 = 450 episodes.
- 20회는 초기 계획값이다. pilot 분산으로 필요한 반복 수를 최종 고정하고 그 이후 조건을 바꾸지 않는다.
- episode는 초기 12초 고정(안정화 2초, 피격/복구 구간, 기록 tail 포함)으로 잡고 pilot에서 가장 긴 정상 후보도 끝나는지 확인한다. 비용 측정 구간·피격 횟수는 모든 군에서 동일하다.
- Trial/episode 순서를 seed로 섞고 군별 실행 순서를 균형 있게 배치한다. 네트워크 난수까지 완전 동일하게 재현된다고 가정하지 않는다.

## 13. 측정 구현

### 로그 schema

공통 키는 RunId, TrialId, ScenarioId/Revision, ActorId, Generation, ReactionId, PlanRevision, Role이다. frame/time 단위 사건을 별도 키 없이 합치지 않는다.

| 파일 | 기록 내용 |
|---|---|
| manifest.json | 엔진/빌드·에셋 hash, 충돌 프로필, 군, seed, 입력 잠금, tick/fps, 네트워크, 실행 완료·저장 품질 |
| reaction_events.csv | 명중 확정·계획 생성·수신·첫 적용·접촉·전환·해제·취소·종료 |
| reaction_candidates.csv | 후보별 특징·비용·탈락 이유·선택 결과·sweep 수·계산 시간 |
| movement_samples.csv | 서버·owner·observer의 Capsule/기준 보정 Mesh/속도/MovementMode/벽 거리 |
| rootmotion_samples.csv | Montage·Section·track time·play rate·추출 이동·실제 이동·action phase·원본/재생 여부 |
| contacts.csv | 접촉 상대·법선·blocking/initial penetration·step-up 시도·침투 해소·진입/해제 |
| corrections.csv | 보정 수신·적용·재시뮬레이션·SmoothCorrection·화면 이동을 구분 |
| connections.csv | 방향별 누적 bytes/packets와 구간 차분 |

### Root Motion 계측 경계

- 현재 OnRep_ReplicatedMovement만으로 수신을 세는 구조를 그대로 사용하지 않는다. Root Motion은 별도 복제·fixup 경로가 있다.
- 설치된 5.8 헤더의 `ACharacter::OnRep_RootMotion`과 `SimulatedRootMotionPositionFixup`은 virtual이 아니므로 단순 override를 전제로 설계하지 않는다. 먼저 Character의 네트워크 수신 전후 훅에서 `GetRepRootMotion()`의 변경을 관찰하고, CMC의 virtual SimulatedTick 전후에 fixup을 포함한 결과를 기록한다. 이 경우 이벤트 이름은 '관찰된 root 상태 변경'이며 실제 패킷 수신 횟수라고 부르지 않는다. 정확한 property 수신/내부 fixup 분리는 Networking Insights 또는 별도 엔진 계측 빌드로 검증한다. Super를 유지하고 비가상 함수 숨기기로 엔진 호출이 가로채진다고 가정하지 않는다.
- owner의 `OnClientCorrectionReceived`는 authoritative correction 도착 지점으로 계측한다. root-motion-specific correction RPC도 이 훅을 실제로 통과하는지 확인하고 빠지는 경로는 별도 기록한다.
- 보정 도착 시점의 위치 차이, 과거 saved move에 대한 서버 residual, replay 후 현재 위치 변화는 다른 값이다. 단일 'CorrectionDistance'로 혼합하지 않는다.
- observer는 SmoothCorrection/fixup과 Mesh 표시 이동을 기록한다. 이것을 owner correction RPC 횟수와 합치지 않는다.
- 재시뮬레이션 중 이동·접촉은 replay 태그로 분리한다. 원래 피격·damage·notify를 다시 발생시키거나 wall contact를 여러 실제 사건으로 세지 않는다.

### 주요 지표 정의

1. Capsule 관통: controlled wall에 대한 blocking overlap 비율/지속 시간. 가능하면 벽의 알려진 기하로 깊이를 계산하고 불가능하면 깊이를 만들어내지 않는다.
2. 시각적 관통: 머리·몸통 등의 bone proxy 침투율과 화면 영상. 이를 정밀한 전체 mesh 관통량이라고 부르지 않는다.
3. 비의도적 상승: 평지 기준 Capsule bottom 높이에서 후보가 명시적으로 허용한 수직 이동을 제외한 초과 높이의 peak와 지속 시간. 초기 판단값 5cm/0.1초는 pilot에서 동결한다.
4. 큰 Correction: owner에서 replay 후 현재 Capsule 위치 변화 또는 observer의 큰 보정 사건이 20cm를 넘는 비율. 역할/경로별로 따로 집계한다. 10/20/40cm 민감도 분석도 제시한다.
5. 시각적 튐: 프레임 사이 Mesh 이동에서 해당 구간의 정상 이동으로 설명되는 부분을 분리한 잔여 이동, 속도·가속도 변화, 영상.
6. 상태 정확도: 같은 실행의 서버 궤적과 역할별 Capsule/Mesh 오차. 선택된 다른 후보를 baseline 서버 궤적에 맞추어 오차를 계산하지 않는다.
7. 행동 품질: hit->실제 첫 반응 지연, 요청 대비 실제 knockback 거리/방향, Montage phase 차이, 회복 시간, InPlace/fallback 비율, 발 미끄러짐 proxy.
8. 효율: 실제 bytes/s·episode당 bytes·packets/s, planner/sweep CPU 평균/p95, 기록 오버헤드.

같은 PC 실행에서는 UTC wall clock으로 프로세스 간 궤적을 정렬하고 GameState server time도 보관한다. CMC client move timestamp는 별도 열이다. 다른 PC 실험은 동기화 오차를 먼저 측정하고 그보다 작은 차이를 정밀 효과로 해석하지 않는다.

### 기록 품질과 통계

- 완료·저장·연결·역할·asset hash·명중 횟수·샘플 범위·fps를 검사한다. 버전 불일치·연결 실패 등 기술적 무효 사유는 사전 정의한다.
- 관통·상승·회복 실패 자체는 방법의 실패 결과이며 invalid로 제거하지 않는다.
- 데이터 범위 부족 등 제외 수와 이유를 군별로 보고한다. extreme network에서 invalid 증가도 숨기지 않는다.
- 프레임을 독립 표본으로 세지 않는다. episode를 단위로 paired seed/장면 비교와 bootstrap 신뢰구간을 계산한다.
- 평균·p95·최악값·실패율을 함께 보고한다. 가중치/threshold tuning set과 최종 평가 set을 분리한다.
- 렌더링을 켠 owner/observer 영상을 같은 카메라로 저장한다. headless 기록만으로 화면 자연스러움을 결론내리지 않는다.

## 14. 단계별 구현·완료 조건

기간은 1인 기준 작업량 추정이며 에셋 준비·엔진 디버깅에 따라 달라진다. 각 단계는 완료 조건을 통과한 뒤 다음 단계로 간다.

| 단계 | 예상 | 구현/산출물 | 완료 조건 |
|---|---|---|---|
| R0 문제 재현·계측 설계 | 2~3일 | 새 맵·역할 배치·기본 Root Motion·영상·기록 skeleton | N0/N2에서 Capsule 상승/시각 상승/보정을 구분. 문제가 없으면 조건부터 재설계 |
| R1 후보·궤적·환경 검사 | 3~5일 | 후보 4개, bake, registry, sweep debug, 점수 Core | 장애물 없는 궤적과 실제 root 이동 오차 허용치 설정·통과, 불가능 후보/fallback 확인 |
| R2 공식 네트워크 기준선 | 4~6일 | U 모드, 두 클라이언트, RootMotion receipt·owner correction 로그 | 같은 Montage가 정상 실행. 늦은 수신/종료·관측자 표시·root correction 경로 확인 |
| R3 안전 실행·벽 반응 | 3~5일 | 직선 Warp, contact FSM, F-L | S0~S3에서 초기 겹침을 제외한 정상 충돌·복귀 확인, 매 틱 벽 밀기 방지 |
| R4 계획 공유·소유자 replay | 5~8일 | F-S/E-L/E-S, plan history, 필요한 saved move/packed data | 오래된/중복/지연 revision, 교체·취소·재시뮬레이션에서 행동 ID 유지와 정상 수렴 |
| R5 비교·비용 calibration | 3~4일 | 5개 군 검증, 예산 곡선, 기록 품질·지표 도구 | pilot에서 비용 overlap과 지표 민감도 확인, 설정·가설·성공 기준 동결 |
| R6 본 수집·견고성 | 4~6일 | 주 평가/robustness 원본·trace·영상 | 사전 정의한 반복 수·품질 검사 완료. 실패 결과도 보존 |
| R7 분석·보고 | 3~4일 | 신뢰구간·효과 분리·비용곡선·한계·재현 문서 | H1~H3를 각각 지지/기각/판단불가로 정리 |

전체 약 27~41 작업일이다. 동적 장애물·상대 Enemy 충돌은 별도 1~2주 이상을 잡고, 정적 벽 결과를 먼저 완성한다. R2/R4가 불안정하면 owner 실험을 생략하지 말고 범위를 줄여 원인을 해결한다.

### 필요한 검증

- 순수 계산 테스트: 후보 제약·정규화·tie-break·초기 침투·scale 범위.
- 계약 테스트: encode/decode·quantization 도입 시 오차·중복 ID/revision·문맥 변경·만료.
- 네트워크 통합: N0/N2에서 delayed start/phase, 중간 취소/새 피격, owner replay, observer Root Motion 종료.
- 통계·분석 테스트: 알려진 벽 관통/상승·중복 replay·counter reset 데이터를 넣어 올바른 지표·제외 여부 확인.
- 기존 실험 회귀: Lv_Jang의 Spline/Impulse/Detector/기록 기능이 기존처럼 동작하는지 수동 확인.

위 테스트는 신규 이동·프로토콜 기능 구현 시 필요한 항목이다. 이 계획 문서 작성 단계에서 기능 테스트를 실행했다는 뜻이 아니다.

## 15. 성공 판단과 중단·수정 기준

pilot 후 동결할 초기 실용 목표안:

- E-S가 F-S 또는 가장 강한 실용 기준선 대비, 동일 비용 구간에서 큰 correction episode 비율을 30% 이상 줄이는 것을 목표로 한다.
- capsule 관통/비의도적 상승을 악화시키지 않고, 의도한 knockback·시작 반응 지연·회복 품질을 사전 정한 허용 범위에서 유지한다.
- 위 30%는 선행논문 보장 수치가 아니라 연구 목표다. 신뢰구간·절대 감소량·기준선 발생률을 같이 제시한다.
- 주 지표가 이미 바닥에 가까우면 상대 감소율로 과장하지 않는다. 사건당 실용 차이가 없는 결과도 인정한다.
- F-L만으로 대부분 해결되면 '사전 검사·Warp로 충분, 후보 선택/계획 공유의 추가 효과는 제한적'이라고 결론낸다.
- 후보 선택만으로 좋아지고 공유는 차이가 없으면 H1만 지지한다. native Root Motion이 충분히 동기화한 정적 벽에서는 가능한 결과다.
- 서버 상승은 줄지만 owner correction이 늘면 시작 적용·replay 의존성을 재검토하고 성공으로 합치지 않는다.
- 비용 overlap을 만들지 못하면 동일 비용의 H3는 판단불가이며 곡선과 원인을 보고한다.

## 16. 최종 산출물과 첫 작업 묶음

최종 산출물은 UE 5.8 구현, 고정된 맵/에셋, 후보 registry와 bake/hash, 역할별 원본 로그, 연결 비용/trace, 재현 실행 스크립트, 효과 분리 보고서, owner/observer 비교 영상이다. 원본은 Saved에만 두지 말고 별도 배치 아카이브로 보관한다.

새 실행·분석·bake 검증 결과는 [팀 Saved 결과물 관리 규칙](../Workflow/Saved_결과물_관리.md)을 따른다. PowerShell은 `Scripts/NewSavedTask.ps1`로 작업/배치 README를 먼저 만들고 실행 명령·시각/시간대·파일 의미·완료 결과를 남긴다. 새 Python 생성기도 같은 구조의 README를 자동 작성한다. `Saved/Tasks/<실행ID>` 또는 `Saved/Experiments/<BatchId>` 아래에 모으며, 기존 결과를 옮기거나 덮어쓰지 않는다. 이 연구계획은 유지관리할 설계 소스이므로 docs/Research에 둔다.

첫 작업 묶음은 R0~R2다:

1. 사용 가능한 BackLong/BackShort/InPlace/WallHit 에셋과 Skeleton을 확인한다.
2. 새 Character/CMC와 L_ReactionWallLab을 만들고 서버 1·클라이언트 2 역할을 고정한다.
3. 기본 Montage 복제만으로 S0/S2와 N0/N2를 기록한다.
4. Capsule/Root Motion/phase/correction 로그를 연결하고 실제 문제가 발생하는지 확인한다.
5. 원인과 기준선이 확인된 뒤 R3의 Capsule 경로 검사·Warp·벽 반응을 구현한다.

계획 상태에서 런타임 코드·기존 맵·설정·사용자의 미커밋 변경은 수정하지 않았다.
