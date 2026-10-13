# 여러 Enemy의 Spline 설정과 수동 확인

## 적용 범위

현재 `integration/jwj`의 네트워크 정책과 정수 `ExperimentActorId`를 유지하면서 Wonjunjang의 거리 기반 Spline 추종을 통합했다. X축 1,000cm 자동 왕복 입력은 제거했다. 기존 `.umap`과 `.uasset` 배치는 자동으로 변경하지 않는다.

Editor를 종료하고 빌드한 후 다시 열어 Native Component 변경을 반영한다. Live Coding만으로 적용하지 않는다. 기존 Enemy Blueprint를 열어 Compile/Save하고 `SplineFollower`, `Experiment`, `Diagnostics`, `SnapshotChannel`, `SharpUpdatePolicy` 컴포넌트를 확인한다.

## 레벨 배치

1. 기존 `BP_SharpEnemy`를 세 개 배치하고 `Experiment Actor Id`를 1, 2, 3처럼 고유한 양수로 설정한다.
2. AI Controller Class는 `SharpEnemyAIController`, Auto Possess AI는 `Placed in World or Spawned`로 둔다. Replicates와 Replicate Movement를 켠다. 이동 방향으로 회전하려면 Use Controller Rotation Yaw를 끄고 CMC의 Orient Rotation To Movement를 켠다. 첫 확인에서는 Root Motion과 Mesh 물리 시뮬레이션을 끈다.
3. Place Actors의 `SharpSplinePath`를 세 개 배치한다. 필요하면 이 클래스를 부모로 Blueprint를 만든다. Spline의 점을 편집하고 Path Id를 `Path01`, `Path02`, `Path03`, Revision을 1로 설정한다.
4. Spline의 World Z를 바닥 표면 높이에 맞춘다. 열린 수평 경로와 양수 균일 Scale을 사용한다. 첫 경로는 두 Linear 점, 나머지는 완만한 Curve로 만든다.
5. 각 Spline의 `Assigned Enemy`에 서로 다른 **배치 인스턴스**를 연결한다. 경로가 유효하면 Enemy가 출발점으로 이동한다. 오류는 `Last Command Error`에 표시된다.
6. 연결 후 경로나 출발 설정을 바꾸면 `Set Enemy At Start`로 재배치한다. 배치는 Undo/Redo 대상이다.
7. Manager는 레벨에 한 개만 둔다. 기존 `BP_SharpExperimentManager`를 사용할 수 있다. Settings에 기존 DataAsset, Paths 배열에 세 Spline 인스턴스를 연결한다.
8. 첫 확인에서는 경로를 충분히 떨어뜨린다. Enemy 충돌·회피·장애물은 실제 추종 결과에 영향을 준다. 코드가 기존 충돌 설정을 자동으로 변경하지 않는다.

경로 Actor를 복제하면 Path Id와 Assigned Enemy도 함께 복사된다. 복제한 경로는 각각 고유한 Path Id와 담당 Enemy로 변경해야 한다. 예를 들어 Path01/Path02/Path03을 사용한다. 중복 오류에는 실제 중복 값과 두 경로의 이름이 표시된다.

모든 대상은 같은 월드에 있어야 한다. World Partition에서는 실험 Actor가 모두 로드되도록 배치/스트리밍을 설정한다. 동적으로 생성한 Path도 서버의 Paths에 등록 후 준비할 수 있다. 클라이언트는 이동을 위해 Path를 추종하지 않는다.

## 경로와 배치 조건

- Spline은 바닥 기준선이다. 실제 Enemy 중심 높이는 Capsule 반높이와 Start Height Offset을 더해서 계산한다.
- 정방향은 첫 점, Follower의 Reverse Direction이 켜져 있으면 마지막 점이 출발점이다. 시작 회전은 진행 접선의 Yaw에 맞춘다.
- 시작점 위/아래 50cm의 Visibility trace로 바닥을 확인한다. 바닥 높이 차이가 10cm보다 크거나 걸을 수 없는 경사면이면 거부된다. 바닥은 Visibility를 차단해야 한다.
- 시작 Capsule의 장애물 충돌 및 전체 배치 후 Enemy 간 Capsule 충돌을 검사한다. 막힌 위치를 임의로 밀어내어 성공시키지 않는다.
- 자기 교차·되돌아가는 형상·가까운 비인접 구간을 투영 모호성 검사로 제한한다. Minimum Projection Separation 기본값은 60cm다. 최대 5cm 오차의 Polyline 근사 검사이며 수학적으로 정확한 곡선 교차 증명은 아니다.
- 준비 검사는 최대 2,048 Polyline 점을 지원한다. 매우 복잡한 경로는 단순화한다. 다른 Path끼리의 교차는 허용하지만 그 위치에서 Enemy가 충돌할 수 있다.

## 버튼과 상태

PIE/Simulate 또는 Standalone을 실행하고 **서버 월드의 실행 인스턴스**를 선택한다. 필요하면 F8로 조작에서 빠져나온다. 편집 원본과 클라이언트 인스턴스의 실행 버튼은 서버를 제어하지 않는다.

| 위치 | 버튼 | 동작 |
|---|---|---|
| Spline | Set Enemy At Start | 편집 중 위치 배치; 실행 서버에서는 해당 Enemy의 새 실행 준비 |
| Spline | Start Enemy | Ready에서 출발, Stopped에서 현재 위치로부터 재개 |
| Spline | Stop Enemy | 해당 Enemy의 입력·속도·누적 힘 정리, 정책 복원 |
| Spline | Reset Enemy | 해당 Generation 증가, 출발점 배치 후 Ready |
| Manager | Prepare All | 전체 사전 검증 후 새 Generation으로 전체 준비 |
| Manager | Start All | 전체 사전 검증 후 같은 서버 프레임에서 시작 |
| Manager | Stop All | 준비된 전체 대상 정지 및 자동 시작 취소 |
| Manager | Reset All | TrialId와 각 Generation 증가, 재배치 후 Ready |
| Manager | Suspend / Resume Trial | 실험용 입력 중단/재개; 관성·중력·외력 유지 |

기존 PrepareTrial/StartTrial/ResetTrial 이름은 전체 제어의 별칭이다. 기본 상태는 `Idle → Ready → Running → Completed`이며 정지는 Stopped, 입력 중단은 Suspended, 오류는 Failed다. Reset 후 자동 출발하지 않는다. Completed/Failed를 재실행하려면 Reset한다. 이미 전부 Running이면 Start All은 중복 실행하지 않는다. 개별 제어로 상태가 섞였다면 전체 동시 출발 전에 Reset All을 사용한다.

Stop은 공중 위치 고정이 아니다. 속도를 지워도 이후 중력과 외력이 작용할 수 있다. 실행 중 경로/연결/식별자가 바뀌거나 AI Possession이 사라지면 해당 실행을 실패 처리한다. 설정은 준비 시 복사하며 변경을 적용하려면 다시 준비한다.

Reset은 대상의 이동을 먼저 정지한다. 출발점이 막혀 사전 검증이 실패하면 위치와 실행 식별자를 유지하고 오류를 표시한다. 문제를 수정한 뒤 다시 Reset한다.

## 이동 설정

Enemy의 `SplineFollower → Settings`에서 조절한다. 기본값은 Target Speed 300cm/s, Look Ahead 100cm, Acceptance Radius 30cm, End Slowdown 200cm, Blocked Timeout 3초다.

- Once: 끝점 근처에서 CMC로 감속하고 속도 허용치까지 내려가면 완료한다.
- PingPong: 같은 도착 조건을 만족한 뒤 방향을 반전한다. 반환점은 각 Spline의 실제 길이다.
- Blocked Timeout=0은 정체 시간 제한을 해제한다.
- 큰 Look Ahead, 높은 속도, 낮은 가속도는 코너를 깎는 원인이 될 수 있다. 실제 위치를 Spline에 매 Tick 강제로 붙이지 않는다.
- Max Walk Speed는 실행 중 제어하고 정지/완료/실패/Release에서 복원한다. 마찰·제동·Air Control 등은 기존 CMC 설정을 사용한다.
- Burst의 High 빈도는 실제 평상시 빈도보다 높아야 한다. BP가 100Hz, Settings High가 60Hz라면 기존 정책처럼 거부된다. 실험 설정에서 직접 맞춘다.

## 자동 실행 연결

Prepare On Begin Play는 기본 켜짐, Auto Start Trial은 기본 꺼짐이다. 준비는 이동을 시작하지 않는다. BeginPlay/Controller 준비를 확인하며 Preparation Timeout Seconds 기본 30초 안에 준비되지 않으면 오류를 남긴다.

| 서버 옵션 | 의미 |
|---|---|
| `-SharpAutoStart=1` | 명시적인 자동 시작 |
| `-SharpExpectedClients=2` | 두 원격 PlayerController가 서버에 생성될 때까지 대기 |
| `-SharpStartDelay=5` | 접속 조건 충족 후 5초 워밍업 |

PlayerController 확인은 클라이언트 렌더링/에셋 로딩 완료 응답은 아니다. 관련성·스트리밍으로 실험 Actor가 빠지지 않는지 확인한다.

RunSharpTrial.ps1은 위 옵션을 전달하고 `[Sharp][Group] Started`를 기다린 뒤 측정 시간을 센다. 수동 버튼은 준비/자동 시작 타이머를 취소한다. Dedicated Server를 게임 화면에서 제어하는 Widget/PlayerController RPC는 이번 범위에 추가하지 않았다.

## 로그와 Detector 계약

Diagnostics가 기존 M2 CMC 및 M3 ClientVisual/MovementOnRep/BaselineSummary 태그를 유지한다. 로그 간격 기본값은 0.5초이며 매 Tick CSV Recorder를 대체하지 않는다. 준비/Reset의 Teleport는 일반 이동 샘플에서 제외한다. 완료 요약은 서버의 Completed 상태를 기준으로 한다.

RunId/TrialId/ActorId/Generation/Segment/PathId/PathRevision/State를 기록한다. Generation은 새 준비/Reset, Segment는 시작/재개마다 바뀐다. 분석은 실행 키 안에서만 보간하며 변환기는 `control_events.csv`도 만든다. Generation 열이 없는 기존 CSV는 예전 ActorId 기준으로 읽는다.

외부 Detector는 관찰 순간 Snapshot.Generation과 Snapshot.Segment에 Enemy의 Experiment.Status 값을 복사하고 SequenceId를 Enemy 수명 동안 단조 증가시켜야 한다. 대기 중이던 사건에 새 실행 정보를 덮어쓰지 않는다. Bridge는 Running 상태, 세대·구간과 최신 Segment 시작 시각을 확인한다. 수신 시 해당 문맥이 아직 도착하지 않았다면 문맥 복제 후 다시 검토한다. EventOnly/EventPredictor의 서버 게시 규칙은 같고 수신은 관찰만 수행한다. 실제 Detector나 시각 Predictor를 새로 구현한 것은 아니다.

## 수동 확인 절차

1. 세 경로의 Assigned Enemy, 배치 위치, Undo/Redo, 저장 후 재열기를 확인한다.
2. 서버에서 Prepare All/Start All을 누르고 Summary 및 각 Enemy의 Experiment.Status를 확인한다.
3. 한 Enemy만 Stop/Start/Reset하고 다른 Enemy의 상태·Generation이 유지되는지 확인한다.
4. Stop All/Reset All을 반복하고 위치·속도·방향·진행도·TrialId를 확인한다.
5. Once 완료와 PingPong 반환을 확인한다. 벽에 막힌 Enemy만 실패해야 한다.
6. 중복 연결, 막힌 출발점, Possession 상실, 실행 중 경로 변경의 오류 표시를 확인한다.
7. Listen Server + 두 클라이언트에서 같은 ActorId의 이동을 관찰한다. 클라이언트 버튼은 서버 상태를 변경하지 않아야 한다.
8. Dedicated Server 자동 시작과 지연/손실 조건은 별도로 확인한다.

자동화 테스트는 추가하거나 실행하지 않는다. 이 문서는 확인 절차이며 실행 결과를 의미하지 않는다.
