# SharpMovement 마일스톤별 레벨 배치 및 수동 테스트

2026-10-11 · UE 5.8 · 현재 M0~M8 구현 기준

이 문서는 에디터에서 직접 배치하고 PIE로 확인하는 절차다. 자동화 테스트는 사용하지 않는다. 아래 수치는 첫 실행용 예시이며, Editor Details와 Data Asset에서 조절한다. Blueprint 생성·맵 배치·실제 PIE 검증은 사용자가 이 절차에 따라 수행해야 한다.

## 1. 공통 실험 레벨 만들기

기존 `Content/SharpMovementExperiment/Maps/SharpMovementExperiment` 맵을 사용하거나 별도의 평면 테스트 맵을 만든다. 기존 맵에는 M0 에셋이 있지만 Enemy·Path·Volume 연결까지 완료되어 있다고 가정하지 않는다.

### 에셋과 Actor

Content Browser의 C++ 클래스에서 아래 부모로 Blueprint를 만든다. 기존 Enemy Blueprint를 재사용한다면 부모 클래스와 상속된 Components를 먼저 확인한다.

| 에셋/Actor | C++ 부모 | 레벨 배치 | 연결할 항목 |
|---|---|---|---|
| `DA_SharpExperimentSettings` | `SharpExperimentSettings` Data Asset | 배치하지 않음 | Manager의 Settings |
| `BP_SharpExperimentManager` | `SharpExperimentManager` | 1개 | Settings, Enemy, Path, Impulse Volumes |
| `BP_SharpEnemy` | `SharpEnemyCharacter` | 1개 | Actor Id, Mesh, Anim Class, Follower 설정 |
| `BP_SharpPath` | `SharpSplinePath` | 1개 | Path Id, Path Revision, Spline 포인트 |
| `BP_SharpImpulseVolume` | `SharpImpulseVolume` | M2부터 1개 | Volume Id, TriggerBox, Impulse Settings |
| `SharpMovementReport` 또는 이를 상속한 BP | `SharpMovementReport` | M3부터 선택적으로 1개 | 종료된 서버/클라이언트 기록 폴더 |
| 바닥·조명·PlayerStart | 기존 에디터 Actor | 필요량 | 바닥 충돌, 플레이어 관찰 위치 |

Manager와 Report의 월드 위치는 실험 동작에 영향을 주지 않는다. Detector·Recorder·UpdatePolicy·VisualPredictor는 Enemy에 이미 포함되므로 별도 Actor나 Component를 추가하지 않는다.

### 최초 배치 예시

바닥 상면을 월드 `Z=0`으로 맞추고, 진행 방향을 `+X`로 잡는다. 좌표와 크기 단위는 cm다.

| 대상 | 예시 |
|---|---|
| 바닥 | X `-500~4500`, Y `-1500~1500` 이상을 덮는 평면. Pawn을 Block하는 충돌 필요 |
| Path | Actor 위치 `(0,0,0)`, 회전 0, Scale 1 |
| Spline | 포인트 0 `(0,0,0)`, 포인트 1 `(3000,0,0)`, 직선, Closed Loop 꺼짐 |
| Enemy | 시작점 근처. 실제 시작 Capsule 위치는 Prepare 때 Manager가 맞춤 |
| Impulse Volume | M2에서 중심 `(1500,0,100)`, TriggerBox Box Extent `(75,150,150)`로 시작 |
| PlayerStart | 예: `(1500,-1000,100)`. 플레이어를 경로 밖에 두고 Enemy 쪽을 바라봄 |

Spline 포인트는 **바닥 높이**다. Capsule 중심 높이로 올리지 않는다. Manager가 `Capsule Half Height + Start Height Offset`을 더해 Enemy를 준비한다. 최초에는 수평의 열린 Spline을 사용하고, 실험 준비 후에는 Spline·Volume 위치나 크기를 변경하지 않는다.

카메라는 기존 플레이어 카메라로 관찰해도 된다. 고정 화면이 필요하면 CameraActor를 배치하고 Level Blueprint에서 로컬 PlayerController에 `Set View Target with Blend`를 호출한다. Enemy를 플레이어가 Possess하면 서버 AI 추종 조건이 깨지므로 별도의 관찰 플레이어를 사용한다. 이 구현에는 자동 추적 카메라가 포함되어 있지 않다.

### Enemy와 Manager 공통 설정

| 대상 | 최초 설정 |
|---|---|
| Enemy | Actor Id = `Enemy_01`, Replicates/Replicate Movement 켜짐 |
| Enemy AI | AI Controller Class = `SharpEnemyAIController`, Auto Possess AI = `Placed in World or Spawned`, Auto Possess Player = Disabled |
| Enemy Mesh | Skeletal Mesh와 호환되는 Anim Class 지정. 최초에는 in-place 애니메이션, Simulate Physics 꺼짐 |
| Enemy Character Movement | 상속된 컴포넌트가 `SharpCharacterMovementComponent`인지 확인. 마찰·제동·회전·중력은 Details에서 지정 |
| Enemy SplineFollower | Target Speed = 300, Look Ahead Distance = 100, Acceptance Radius = 30, Stop Speed Tolerance = 5, End Slowdown Distance = 200 |
| Path | Path Id = `Path_01`, Path Revision = 1 |
| Manager | Settings = DA, Enemy = 레벨의 Enemy, Path = 레벨의 Path |
| Manager 실행 | Configured Mode = Default, Auto Start Trial 켜짐, Auto Start Delay Seconds = 3~5, Auto Repeat 꺼짐 |
| Manager 초기 위치 | Start Height Offset = 5, Align Rotation To Path 켜짐 |

Actor Id와 Volume Id는 각각 월드에서 유일하게 지정한다. 다른 Path Actor를 만들면 다른 Path Id를 지정한다. 같은 Path Actor를 여러 Manager가 참조하는 것은 가능하다. 동일 Enemy나 Volume을 여러 Manager가 공유하지 않는다.

기존 Blueprint는 C++ Editor 빌드 후 에디터를 다시 열고 Compile/Save한다. 이번 구현은 이미 빌드된 C++ 기본 Component를 사용하므로 BP에서 동일 기능을 중복 추가하거나 Tick에서 Enemy/Mesh 위치를 별도로 이동시키지 않는다. 이 Spline 추종에는 NavMesh와 Behavior Tree가 필요하지 않다.

### 공통 PIE 설정

Play 드롭다운 → Advanced Settings 또는 Editor Preferences → Level Editor → Play에서 설정한다.

| 설정 | 최초 값 |
|---|---|
| Play Net Mode | **Play As Client** |
| Number of Players / Play Number of Clients | 1. 두 클라이언트 수신 비교가 필요하면 2 |
| Run Under One Process | 켜짐 |
| Show Server Debug Drawing by Default | 켜짐 |
| Enable Network Emulation | 최초에는 꺼짐 |

Play As Client는 백그라운드 Dedicated Server를 실행한다. 별도의 서버 창이 없어도 서버 월드가 존재한다. 1 Client만으로도 서버 권한·클라이언트 복제 확인이 가능하다. [Epic 공식 PIE 안내](https://dev.epicgames.com/documentation/en-us/unreal-engine/play-in-editor-multiplayer-options-in-unreal-engine)

Manager의 Trial 버튼은 **실행 중인 서버 월드**에서만 유효하다. 편집 중인 원본 레벨이나 Client 인스턴스에서 누르면 실행되지 않는다. 첫 실험은 Auto Start Trial로 진행하는 것이 편하다. 수동 Suspend/Resume은 Listen Server의 서버 인스턴스에서 확인하거나, 서버 Blueprint가 Manager 함수를 호출하도록 연결한다. Client 키 입력만으로 서버 Manager가 제어되는 RPC는 구현하지 않았다.

`Configured Mode`는 PIE 시작 전에 바꾼다. 런타임에 이 값만 수정해도 진행 중인 Identity.Mode가 바뀌지는 않는다. Reset Trial은 TrialId를 증가시키고 Ready까지 준비하며, 수동 실행에서는 이후 Start Trial이 필요하다.

## 2. M0 — 설정과 실험 식별자

**배치:** Manager 1개와 Settings Data Asset 연결부터 확인한다. Enemy/Path를 아직 배치하지 않았다면 Auto Start Trial은 끈다.

**설정:** Configured Run Id는 빈 값 또는 `M0_Check`, Configured Trial Id = 1, Configured Mode = Default.

**수동 확인:**

1. Play As Client로 PIE를 실행한다.
2. Output Log에서 `LogSharpExperiment`의 서버 시작 정보와 Identity를 확인한다.
3. Client Manager의 복제된 Identity가 서버와 같은 RunId·TrialId·Mode인지 확인한다.
4. Run Id를 비우고 PIE를 다시 시작하면 새 RunId가 생성되는지 확인한다.

**통과 기준:** 서버와 클라이언트의 실험 식별자가 일치한다. M0 단독에서는 이동·CSV 생성까지 요구하지 않는다.

연관 파일: [SharpExperimentManager](../../Source/GE_AIReplication/SharpMovement/Experiment/SharpExperimentManager.h), [SharpExperimentSettings](../../Source/GE_AIReplication/SharpMovement/Experiment/SharpExperimentSettings.h), [SharpExperimentTypes](../../Source/GE_AIReplication/SharpMovement/Experiment/SharpExperimentTypes.h).

## 3. M1 — 서버 Spline 이동과 완료

**배치:** 공통 예시의 바닥·Enemy·Path를 추가하고 Manager의 Enemy/Path를 연결한다. Impulse Volumes 배열은 비운다.

**설정:** 공통 Enemy/Follower 설정 적용. 처음에는 Baseline Profile = EngineDefault, Configured Mode = Default, Auto Start Trial 켜짐.

**수동 확인:**

1. 시작 대기 후 Enemy가 Spline 시작점으로 준비되고 +X 방향으로 이동하는지 본다.
2. 서버 FollowState가 Following이고 Client Enemy가 SimulatedProxy인지 DebugLabel/로그로 확인한다.
3. 끝점 근처에서 감속하고 정지한 뒤 Trial Status가 Completed가 되는지 확인한다.
4. 수동 제어가 필요한 경우 서버에서 Suspend Trial → Resume Trial을 호출한다. Suspend는 신규 추종 입력을 중단하며 마찰·중력 등 물리는 계속된다.
5. 서버에서 Reset Trial → Start Trial로 새 TrialId와 시작점 복귀를 확인한다.

**통과 기준:** 서버만 추종 입력을 만들고 클라이언트는 복제된 이동을 표시한다. 끝점에 도착해 저속 조건까지 만족한 뒤 완료된다. NavMesh 없이 동작해야 한다.

연관 파일: [SharpEnemyCharacter](../../Source/GE_AIReplication/SharpMovement/Enemy/SharpEnemyCharacter.h), [SharpEnemyAIController](../../Source/GE_AIReplication/SharpMovement/Enemy/SharpEnemyAIController.h), [SharpSplineFollowerComponent](../../Source/GE_AIReplication/SharpMovement/Movement/SharpSplineFollowerComponent.h), [SharpSplinePath](../../Source/GE_AIReplication/SharpMovement/Scenario/SharpSplinePath.h).

## 4. M2 — Impulse와 서버 움직임 관찰

**배치:** 경로 중간에 Impulse Volume을 놓고 Manager의 Impulse Volumes 배열에 **레벨의 Volume 인스턴스**를 추가한다. 시작 Capsule과 겹치지 않게 한다.

**설정:**

| 대상 | 최초 예시 |
|---|---|
| Volume | Volume Id = `Side_01`, Direction Space = PathTangent |
| Impulse | `(0,700,0)`으로 횡방향 관찰 시작. 기본값 `(0,300,0)`보다 큰 예시이며 CMC 결과를 보고 조절 |
| 적용 | Velocity Change 켜짐, Trigger Policy = OncePerTrial, Follow Policy = Continue |
| TriggerBox | Query Only, Pawn = Overlap, Generate Overlap Events 켜짐 |
| Enemy Capsule | Pawn Object Type, Query 충돌 및 Generate Overlap Events 켜짐. Volume 채널을 Ignore하지 않음 |
| Enemy MovementRecorder | Draw Server Capsule, Draw Client Capsule, Draw Velocity, Draw Impulse Events 켜짐 |
| Data Asset Recording | Write Visual Logger 켜짐, Post Trial Observation Seconds = 0.5 |

PathTangent의 X는 진행 전방, Y는 우측, Z는 위쪽이다. 실제 Impulse는 **Volume의 설정**을 읽는다. Data Asset의 기존 `Scenario → Impulse` 필드는 이번 Volume 실험에 사용하지 않는다.

**수동 확인:**

1. PIE Client 화면에서 서버 Capsule(기본 주황), Client Capsule(청록), Enemy Mesh를 함께 본다. 서버 Tint 설정에 따라 색은 달라질 수 있다.
2. Enemy가 Box에 진입할 때 횡방향 속도 변화·사건 도형을 확인한다.
3. 서버 Volume의 Trigger Count가 1인지, `events.csv`에 해당 사건이 남는지 확인한다.
4. Visual Logger 창에서 Record 후 실행하거나 Auto Record Visual Logger To File을 켜서 서버 Enemy의 사건·상태를 확인한다.
5. 다음 Trial에서 Follow Policy = SuspendInput, Input Suspend Seconds = 0.25로 바꾸고 입력 중단 후 복귀를 비교한다.

**통과 기준:** 등록된 대상 Enemy의 서버 Capsule Overlap에서만 Impulse를 호출한다. 서버 이동 샘플과 사건이 기록되고 Client 화면에서 서버 위치와 표시 Mesh를 구분할 수 있다.

서버 도형 전달은 **같은 에디터 프로세스의 Dedicated Server → PIE Client** 경로다. 별도 프로세스·패키징 실행에서는 서버 CSV/Visual Logger로 관찰한다. `p.NetShowCorrections 1`과 `p.NetCorrectionLifetime 4`는 Player 보정 발생 시 표시용이며, Enemy의 지속적인 서버 위치 표시는 이 Recorder 도형을 사용한다.

연관 파일: [SharpImpulseVolume](../../Source/GE_AIReplication/SharpMovement/Scenario/SharpImpulseVolume.h), [SharpImpulseTypes](../../Source/GE_AIReplication/SharpMovement/Scenario/SharpImpulseTypes.h), [SharpMovementRecorder](../../Source/GE_AIReplication/SharpMovement/Measurement/SharpMovementRecorder.h).

## 5. M3 — 기본 복제의 오차 기록

**배치:** M2 레벨을 유지한다. 결과를 즉시 확인하려면 SharpMovementReport Actor를 추가한다. Report의 공간 위치는 무관하다.

**설정:** Manager Baseline Profile = ControlledLowRate, Data Asset Normal Net Update Frequency = 10, Replicate Movement Timestamps = true. Configured Mode = Default. 도형·CMC·카메라·기록 설정은 이후 비교 모드에서도 동일하게 유지한다.

**수동 확인:**

1. Volume 없는 직선 Control과 Volume 하나의 실험을 별도로 실행한다.
2. Trial이 Completed가 된 뒤 후행 기록이 끝날 때까지 기다린다. `Finishing Recording = false`를 확인하고, Client 로그도 닫힐 시간을 준 뒤 PIE를 종료한다.
3. `Saved/SharpMovement/<RunId>/Trial_<TrialId>/` 아래에서 같은 ActorId의 서버 폴더와 `Client_...` 폴더를 찾는다. Output Log/Recorder Output Directory에서 실제 경로를 확인할 수 있다.
4. 서버 `server_samples.csv`, `events.csv`, Client `client_samples.csv`, 양쪽 `diagnostics.csv`, `connections.csv`, `manifest.json`을 확인한다.
5. 편집 월드의 Report에 Server Recording Directory와 Client Recording Directory를 지정하고 Generate Report를 누른다.
6. Last Output Directory의 `report.html`을 브라우저로 열어 Capsule 오차와 Mesh 오차를 비교한다.

**통과 기준:** 종료된 동일 RunId·TrialId·ActorId의 로그가 연결되고, 시간 정렬된 오차와 coverage가 생성된다. 시각은 GameState의 추정 서버 시각이며, 측정할 수 없는 구간·첫 수신은 null로 남을 수 있다.

연관 파일: [SharpMovementRecorder](../../Source/GE_AIReplication/SharpMovement/Measurement/SharpMovementRecorder.h), [SharpCharacterMovementComponent](../../Source/GE_AIReplication/SharpMovement/Movement/SharpCharacterMovementComponent.h), [SharpMovementMetrics/Report](../../Source/GE_AIReplication/SharpMovement/Measurement/SharpMovementMetrics.h).

## 6. M4 — 급변 Detector

**배치:** 추가 Actor 없음. Enemy의 MovementDetector를 사용한다.

**설정:** Data Asset Detection에서 Velocity Change Threshold = 300, Direction Change Threshold Degrees = 45, Minimum Direction Speed = 50, Acceleration Threshold = 0, Detector Release Ratio = 0.5, Quiet Release Seconds/Minimum Risk Seconds/Retrigger Seconds = 각각 0.1로 시작한다.

**수동 확인:**

1. Volume 없는 직선 이동을 실행해 `DetectorSample`과 정상 구간의 검출 여부를 확인한다.
2. 횡방향 Impulse를 실행하고 `events.csv`의 관측된 속도 변화와 `diagnostics.csv`의 `DetectorEvent`, `RiskStart`, `RiskEnd`를 비교한다.
3. 설정을 바꿔 재실행할 때 실제 CMC 속도 차이가 임계값을 넘는지 먼저 확인한다. Impulse 입력값과 실제 이동 샘플의 Δv가 같다고 가정하지 않는다.
4. 별도의 곡선 Path 또는 정상 정지 Control을 실행해 방향 변화·정지 검출을 확인한다.
5. Report에서 TP/FP/FN과 Classifier 비용을 확인한다.

**통과 기준:** 서버 CMC 샘플만으로 사건을 판정하고, Risk 상태가 안정 구간에서 해제된다. 정상 급회전은 이 Volume 기준 보고서에서 FP가 될 수 있으므로 Detector가 반드시 0건이어야 하는 것은 아니다.

연관 파일: [SharpMovementClassifier](../../Source/AIReplicationCore/Public/Detector/SharpMovementClassifier.h), [SharpDetectorConfig](../../Source/AIReplicationCore/Public/Detector/SharpDetectorConfig.h), [SharpMovementDetectorComponent](../../Source/GE_AIReplication/SharpMovement/Detector/SharpMovementDetectorComponent.h).

## 7. M5 — 전송 모드 비교

**배치:** 추가 Actor 없음. 같은 레벨·Impulse·Detector를 유지한다.

**설정:** ControlledLowRate, Normal Frequency = 10, High Frequency = 60, Burst Seconds = 0.15. 우선 아래 모드를 한 번씩 PIE를 다시 실행하여 확인한다. EventOnly Uses Custom Correction은 꺼둔다.

| Configured Mode | 확인할 동작 |
|---|---|
| Default | 준비된 기준선의 CMC 복제 |
| Adaptive | 엔진 adaptive 빈도 사용·Trial 종료 후 전역 값 복원 |
| AlwaysHigh | 실행 중 High Frequency 적용 |
| SingleShot | Detector 사건마다 ForceNetUpdateRequest |
| Burst | 사건 후 잠시 고빈도, 이후 BurstEnd 및 기준선 복원 |
| EventOnly | SnapshotPublished/Received + ForceNetUpdateRequest, 기본 CMC 표시 |

**수동 확인:** 모드별 서버 진단과 Client `MovementReceived` 간격, 연결 bytes/s, M3와 동일 절차로 만든 오차 보고서를 비교한다. ForceNetUpdateRequest와 실제 수신을 구분한다. Adaptive는 동적인 정책이므로 항상 AlwaysHigh보다 빈번히 전송된다고 가정하지 않는다.

**통과 기준:** 선택한 모드의 정책이 적용되고 종료/Reset 때 원래 설정으로 복구된다. 전송 요청이 있다고 즉시 수신되거나 화면 오차가 반드시 줄어드는 것은 아니다.

연관 파일: [SharpUpdatePolicy](../../Source/GE_AIReplication/SharpMovement/Net/SharpUpdatePolicy.h), [SharpMoveSnapshot](../../Source/AIReplicationNet/Public/SharpMoveSnapshot.h), [SharpEnemyCharacter](../../Source/GE_AIReplication/SharpMovement/Enemy/SharpEnemyCharacter.h).

## 8. M6 — Client Mesh 예측과 복구

**배치:** 추가 Actor 없음. 평면·장애물 없는 M2 레벨로 시작한다.

**설정:** Configured Mode = EventPredictor, Enable Prediction = true, Maximum Prediction Seconds = 0.15, Correction Seconds = 0.1, Maximum Visual Offset = 150, Prediction Blend Seconds = 0.03, Collision Guard = true, Collision Channel = Visibility. Enable Airborne Model은 최초에는 꺼둔다.

**수동 확인:**

1. 횡방향 Impulse를 실행한다. 서버·Client Capsule과 Mesh 표시를 함께 본다.
2. Client `diagnostics.csv`의 SnapshotReceived → PredictionStart → PredictionStop을 확인한다. 다음 서버 상태·시간 제한 등에 따라 짧게 끝날 수 있다.
3. PredictionStop의 Detail 및 VisualPredictor의 Last Stop Reason을 확인한다.
4. M3 방식으로 report.html을 생성하여 EventOnly와 Mesh 오차·회복을 비교한다.
5. 추가 대조군으로 EventOnly + Event Only Uses Custom Correction = true를 실행한다. 예측 없이 동일한 커스텀 보정 경로를 비교할 수 있다.

**통과 기준:** 유효한 최신 사건에서만 예측하며, 서버의 더 새로운 상태나 제한 조건에서 멈추고 표시 Mesh를 기준 상태로 수렴시킨다. Predictor가 Client Capsule·CMC Velocity를 직접 변경하지 않아야 한다. 모든 사건에서 개선이 보장되는 것은 아니며 마찰·AI 재가속이 강하면 짧은 예측도 틀릴 수 있다.

PredictionStart가 없다면 DetectorEvent, SnapshotPublished, SnapshotReceived 순으로 확인한다. Snapshot이 도착할 때 이미 0.15초 이상 오래되었거나 더 새로운 이동 상태가 적용됐다면 예측하지 않는 것이 정상이다. 처음에는 네트워크 에뮬레이션 없이 확인한다. Root Motion·래그돌·움직이는 바닥은 최초 평가에서 제외한다.

연관 파일: [SharpVisualPredictorComponent](../../Source/GE_AIReplication/SharpMovement/Prediction/SharpVisualPredictorComponent.h), [SharpVisualPredictionSettings](../../Source/GE_AIReplication/SharpMovement/Prediction/SharpVisualPredictionSettings.h), [SharpCharacterMovementComponent](../../Source/GE_AIReplication/SharpMovement/Movement/SharpCharacterMovementComponent.h).

## 9. M7 — 예외 상황과 네트워크 조건

각 상황은 PIE를 종료하고 별도 레벨/설정으로 실행한다. 최초에는 한 조건씩 추가한다.

| 상황 | 배치/설정 | 수동 확인 |
|---|---|---|
| 연속 사건 | 서로 다른 Volume Id의 Box 2~3개를 순서대로 배치하고 모두 Manager에 등록 | 새 사건이 기존 예측을 갱신하는지. Snapshot은 최신 상태 복제이므로 모든 중간 사건의 수신을 요구하지 않음 |
| 재진입 | Trigger Policy = OncePerEntry, Rearm Delay 조절 | Box 퇴장 후 재진입 때만 재발동. Cooldown 중 들어간 후 내부에서 기다려도 자동 발동하지 않음 |
| 벽 | 측면 Impulse 방향에 벽 배치. Pawn 및 Predictor의 Visibility 채널을 Block | 서버 충돌과 PredictionStop Collision/복구 확인. 벽으로 진행이 막혀 Trial이 Failed라면 기능 안전성 확인과 성공 성능 비교를 구분 |
| 착지 | Z 성분이 있는 Impulse, 예: `(0,700,250)` | Falling→Walking 및 오래된 모델 중단. 이후 Enable Airborne Model을 켠 별도 실험과 비교 |
| 정상 회전/정지 | 곡선 Spline + 빈 Volume 배열, 서버 Suspend/Resume | 검출·보정의 정상 움직임 대조군 확인 |
| 여러 AI | Enemy마다 고유 Actor Id, 개별 Manager/Volume. 경로를 옆으로 떨어뜨려 서로 충돌하지 않게 배치 | Enemy별 사건·로그 분리. 같은 프로세스의 동시에 실행되는 Manager는 공통 전송 모드 사용 |

### 지연·변동·손실 예시

PIE Advanced Settings의 Network Emulation을 사용한다. 메뉴가 보이지 않으면 클라이언트 수를 2로 늘린 뒤 확인하고, 비교 실험 전체에서 클라이언트 수를 동일하게 유지한다. Emulation Target = **Server Only**, Profile = Custom, Incoming traffic = 0으로 두고 서버 Outgoing traffic만 바꿔 서버→Client 경로를 우선 관찰한다.

| 별도 Trial 조건 | Outgoing Minimum/Maximum Latency | Outgoing Packet Loss |
|---|---|---|
| 정상 | Network Emulation 꺼짐 | 0% |
| 일정 지연 | 50 / 50 ms | 0% |
| 지연 변동 | 30 / 80 ms | 0% |
| 지연 + 손실 | 30 / 80 ms | 2% |

Editor의 Incoming/Outgoing 설정은 수신/송신 각각에 적용된다. 양쪽에 설정하면 지연이 더해질 수 있으므로 위 값 하나를 RTT로 해석하지 않는다. 모드 비교 중 같은 에뮬레이션 값을 유지하고 Data Asset의 Network Condition Notes에도 조건을 적는다. 실제 Driver 설정은 manifest의 NetworkEmulation에 기록된다. [Epic 공식 네트워크 에뮬레이션 안내](https://dev.epicgames.com/documentation/ko-kr/unreal-engine/using-network-emulation-in-unreal-engine)

콘솔을 쓰려면 별도 조건으로 `NetEmulation.PktLagMin 30`, `NetEmulation.PktLagMax 80`, `NetEmulation.PktLoss 2`를 적용할 수 있다. Client 콘솔에만 입력했다고 서버 송신 설정이 바뀌었다고 가정하지 말고 서버 Driver의 Manifest를 확인한다. UE 5.8에서 `PktJitter`는 `PktLag`보다 먼저 처리되므로 `PktLag 100 + PktJitter 20`을 단순히 100±20ms로 해석하지 않는다. 최초에는 위 Editor 설정처럼 Min/Max 방식 하나만 사용한다. 종료 후 Editor Network Emulation을 끄고 콘솔 설정도 적용한 월드에서 `NetEmulation.Off`로 해제한다.

**통과 기준:** 지연·손실·연속 사건에서도 과거 상태를 무한 적용하지 않고 제한 조건에 따라 복구한다. 오래된 Snapshot 거부나 중간 Sequence 생략은 허용되며, Sequence 차이만으로 패킷 손실을 확정하지 않는다.

연관 파일: [SharpImpulseVolume](../../Source/GE_AIReplication/SharpMovement/Scenario/SharpImpulseVolume.h), [SharpVisualPredictorComponent](../../Source/GE_AIReplication/SharpMovement/Prediction/SharpVisualPredictorComponent.h), [SharpUpdatePolicy](../../Source/GE_AIReplication/SharpMovement/Net/SharpUpdatePolicy.h), [SharpMovementRecorder](../../Source/GE_AIReplication/SharpMovement/Measurement/SharpMovementRecorder.h).

## 10. M8 — 반복 실행과 최종 비교

**배치:** 공통 레벨과 Report Actor를 사용한다. 모든 모드가 같은 바닥·Path·Volume·CMC·AI 수·Client 수·FPS·네트워크 조건에서 실행되도록 유지한다.

**설정:** Manager Auto Start Trial/Auto Repeat 켜짐, Repeat Count = 10, Repeat Delay Seconds = 1, Auto Start Delay Seconds = 3~5. Mode Sequence에 Default, Adaptive, AlwaysHigh, SingleShot, Burst, EventOnly, EventPredictor를 순서대로 등록한다. 10회는 **모드별 횟수**이므로 이 구성은 총 70 Trial이다. 먼저 Repeat Count = 2로 실행 흐름을 확인해도 된다.

**수동 확인:**

1. PIE를 시작하고 각 Trial의 완료·후행 기록 종료·Reset·TrialId 증가·다음 실행을 확인한다. Failed가 발생하면 자동 반복이 멈추므로 실패 이유를 해결한다.
2. 전체 완료 후 PIE를 종료한다.
3. Report Actor에서 같은 RunId·TrialId·ActorId의 서버/Client 기록 폴더를 한 쌍씩 지정한다. Client가 여러 개면 같은 관찰 Client를 선택한다.
4. Generate Report를 눌러 개별 report.json/report.html을 생성한다. 필요하면 해당 Report Actor를 복제해 입력 경로를 보관한다.
5. Comparison Reports 배열에 **report.json을 포함하는 폴더**를 등록하고 Compare Reports를 누른다. 같은 폴더를 중복 등록하지 않는다.
6. comparison.json에서 같은 조건의 쌍, Minimum Comparison Coverage(기본 0.8), Budget Tolerance Percent(기본 5%) 범위 여부와 반복 통계를 확인한다. comparison.html에서 bytes/s와 오차·수신 지연을 함께 본다.
7. 영상에서 Mesh 후퇴·충돌·회복을 확인하고 수치와 대조한다. 동일 전송량 예산에 해당하지 않는 모드 쌍까지 같은 비용의 개선으로 해석하지 않는다.

EventOnly + Custom Correction 대조군은 Mode Sequence에 독립 모드로 들어있지 않다. Event Only Uses Custom Correction을 켠 별도 실행으로 확보한다. `-SharpMode` 실행 인자가 있으면 Mode Sequence보다 우선하므로 순회 실험에서는 제거한다.

Actor별 비용이 필요하면 Networking Insights에서 같은 실험 구간을 측정한 뒤 Actor Cost Csv를 선택적으로 지정한다. 형식은 다음과 같다.

```csv
RunId,TrialId,ActorId,ActorBytesPerSecond,SnapshotBytesPerSecond,RpcBytesPerSecond
```

실측 행을 직접 준비해야 하며 raw trace를 자동 변환하지 않는다. Actor 총량에 Snapshot/RPC를 다시 더하지 않는다. 미지정 시 연결 전체 incoming bytes/s로 비교하므로 이를 Enemy 하나의 비용이라고 해석하지 않는다. 여러 AI의 연결 통계도 같은 연결이면 중복 합산하지 않는다.

**통과 기준:** 비교 가능한 성공 Trial의 보고서·반복 통계가 생성되고, 동일 조건과 실제 비용 범위를 확인한 뒤 모드별 오차·회복의 차이를 평가할 수 있다. 개별 실패 Trial 보고서는 원인 분석에 사용할 수 있지만 성공 모드 비교에는 포함하지 않는다.

연관 파일: [SharpExperimentManager](../../Source/GE_AIReplication/SharpMovement/Experiment/SharpExperimentManager.h), [SharpMovementMetrics/Report](../../Source/GE_AIReplication/SharpMovement/Measurement/SharpMovementMetrics.h), [SharpMovementRecorder](../../Source/GE_AIReplication/SharpMovement/Measurement/SharpMovementRecorder.h).

## 11. 막혔을 때 확인 순서

| 증상 | 먼저 확인할 곳 |
|---|---|
| Enemy가 움직이지 않음 | Manager Settings/Enemy/Path 참조 → Actor/Path Id → Auto Start Trial → 서버 AI Possession → TrialStatus 실패 이유 → 바닥 충돌 |
| Prepare/Start 버튼 무반응 | 실행 중인 서버 인스턴스인지. 원본 편집 월드·Client에서는 Manager 제어 불가 |
| 시작하자마자 실패 | 닫힌/비수평/너무 짧은 Spline, ID 중복, Volume 시작 Capsule 겹침, 잘못된 DA 설정 |
| Impulse 없음 | Manager Volume 배열 → 서버 Armed/Trigger Count → Capsule/Box 충돌 → 진입 시점. Mesh Overlap은 대상이 아님 |
| 서버 도형 안 보임 | Draw Server Capsule → Play As Client → Run Under One Process → Show Server Debug Drawing by Default |
| Mesh가 안 보임 | Enemy Mesh/Anim Class/상대 위치 설정. C++ 클래스에 외형 에셋을 하드코딩하지 않았음 |
| Client 기록 없음 | 실제 네트워크 Client인지 → Enemy relevancy/Replicates → 시작 메타데이터 수신 → GameState → Recorder 출력 오류. 준비 대기를 늘려 Client 연결 시간 확보 |
| 예측 안 켜짐 | EventPredictor → DetectorEvent → SnapshotPublished/Received → SnapshotRejected 이유 → 시간 제한/MovementMode/Enable Prediction |
| Report 실패 | 개별 기록 폴더와 manifest 선택 여부, Identity 일치, 파일 종료/오류, 샘플 수, Last Error |
| 모드 비교 누락 | Completed 여부, 같은 시나리오/측정 조건, coverage, 예산 단위. 동일 비용 여부는 comparison.json에서 별도로 확인 |

기능별 전체 파일 목록과 간단 설정은 [구현 파일 및 설정](SharpMovement_구현_파일_및_설정.md), 실측 결과 기입 위치는 [실험 결과](SharpMovement_실험결과.md)를 참고한다.
