# SharpMovement 구현 파일 및 에디터 설정

M0~M8 계획의 C++ 실행 경로를 구현했습니다. 에디터에서 지정할 경로·에셋·장면·충돌·수치는 코드의 고정 참조 대신 Data Asset, Actor 및 컴포넌트 프로퍼티로 노출합니다. `.uasset`·`.umap`의 배치와 Blueprint 설정은 아래 안내에 따라 에디터에서 지정합니다.

자동화 테스트는 추가하거나 실행하지 않았습니다. UE 5.8 C++ 컴파일·링크 확인과 실제 네트워크 실험 결과는 별개입니다. 이 구현만으로 예측 효과·전송량 개선이 입증된 것은 아닙니다.

## 기능별 파일

경로는 저장소 루트 기준입니다. 헤더와 구현 파일을 각각 연결했습니다.

| 단계 | 기능 | 파일 |
|---|---|---|
| M0 | 모드·RunId·TrialId·클라이언트 Trial 메타데이터 | [SharpExperimentTypes.h](../../Source/GE_AIReplication/SharpMovement/Experiment/SharpExperimentTypes.h) |
| M0 | 에디터 Data Asset 설정·입력 검증 | [SharpExperimentSettings.h](../../Source/GE_AIReplication/SharpMovement/Experiment/SharpExperimentSettings.h) · [SharpExperimentSettings.cpp](../../Source/GE_AIReplication/SharpMovement/Experiment/SharpExperimentSettings.cpp) |
| M0 / M1 / M8 | 서버 Trial 제어·Reset·자동 시작·모드별 반복 | [SharpExperimentManager.h](../../Source/GE_AIReplication/SharpMovement/Experiment/SharpExperimentManager.h) · [SharpExperimentManager.cpp](../../Source/GE_AIReplication/SharpMovement/Experiment/SharpExperimentManager.cpp) |
| M1 | Character 권한·CMC·Snapshot 복제·수신 관찰 | [SharpEnemyCharacter.h](../../Source/GE_AIReplication/SharpMovement/Enemy/SharpEnemyCharacter.h) · [SharpEnemyCharacter.cpp](../../Source/GE_AIReplication/SharpMovement/Enemy/SharpEnemyCharacter.cpp) |
| M1 | 서버 AIController 소유·추종 명령 | [SharpEnemyAIController.h](../../Source/GE_AIReplication/SharpMovement/Enemy/SharpEnemyAIController.h) · [SharpEnemyAIController.cpp](../../Source/GE_AIReplication/SharpMovement/Enemy/SharpEnemyAIController.cpp) |
| M1 | 열린 수평 Spline 기하·투영·경로 검증 | [SharpSplinePath.h](../../Source/GE_AIReplication/SharpMovement/Scenario/SharpSplinePath.h) · [SharpSplinePath.cpp](../../Source/GE_AIReplication/SharpMovement/Scenario/SharpSplinePath.cpp) |
| M1 | CMC보다 먼저 입력 생성·도착·중단·재개·Prepare 설정 고정 | [SharpSplineFollowerComponent.h](../../Source/GE_AIReplication/SharpMovement/Movement/SharpSplineFollowerComponent.h) · [SharpSplineFollowerComponent.cpp](../../Source/GE_AIReplication/SharpMovement/Movement/SharpSplineFollowerComponent.cpp) · [SharpSplineMovementTypes.h](../../Source/GE_AIReplication/SharpMovement/Movement/SharpSplineMovementTypes.h) · [SharpSplineMovementTypes.cpp](../../Source/GE_AIReplication/SharpMovement/Movement/SharpSplineMovementTypes.cpp) |
| M2 | Impulse 좌표계·반복·입력 중단 설정 | [SharpImpulseTypes.h](../../Source/GE_AIReplication/SharpMovement/Scenario/SharpImpulseTypes.h) · [SharpImpulseTypes.cpp](../../Source/GE_AIReplication/SharpMovement/Scenario/SharpImpulseTypes.cpp) |
| M2 | 서버 Capsule Overlap·Impulse 적용·Trial별 Arm/Disarm | [SharpImpulseVolume.h](../../Source/GE_AIReplication/SharpMovement/Scenario/SharpImpulseVolume.h) · [SharpImpulseVolume.cpp](../../Source/GE_AIReplication/SharpMovement/Scenario/SharpImpulseVolume.cpp) |
| M2 / M3 | 기록·서버/클라이언트 도형·Visual Logger 설정 | [SharpMovementRecordTypes.h](../../Source/GE_AIReplication/SharpMovement/Measurement/SharpMovementRecordTypes.h) · [SharpMovementRecordTypes.cpp](../../Source/GE_AIReplication/SharpMovement/Measurement/SharpMovementRecordTypes.cpp) |
| M2 / M3 / M8 | 서버 궤적·Impulse 사건·클라이언트 Capsule/Mesh·진단·연결 통계·manifest | [SharpMovementRecorder.h](../../Source/GE_AIReplication/SharpMovement/Measurement/SharpMovementRecorder.h) · [SharpMovementRecorder.cpp](../../Source/GE_AIReplication/SharpMovement/Measurement/SharpMovementRecorder.cpp) |
| M3 / M6 | CMC SmoothCorrection 계측·내장 smoothing과 Mesh 예측 간섭 처리 | [SharpCharacterMovementComponent.h](../../Source/GE_AIReplication/SharpMovement/Movement/SharpCharacterMovementComponent.h) · [SharpCharacterMovementComponent.cpp](../../Source/GE_AIReplication/SharpMovement/Movement/SharpCharacterMovementComponent.cpp) |
| M3 / M8 | 시간 정렬·첫 수신 전후 오차·회복·탐지·반복/예산 비교 보고서·HTML 그래프 | [SharpMovementMetrics.h](../../Source/GE_AIReplication/SharpMovement/Measurement/SharpMovementMetrics.h) · [SharpMovementMetrics.cpp](../../Source/GE_AIReplication/SharpMovement/Measurement/SharpMovementMetrics.cpp) |
| M4 | Actor·Volume과 독립적인 이동 샘플 | [SharpMovementSample.h](../../Source/AIReplicationCore/Public/Detector/SharpMovementSample.h) |
| M4 | Δv·방향각·가속도·해제·재발동 설정 | [SharpDetectorConfig.h](../../Source/AIReplicationCore/Public/Detector/SharpDetectorConfig.h) |
| M4 | 순수 Classifier·Risk 상태·불연속 시각 처리 | [SharpMovementClassifier.h](../../Source/AIReplicationCore/Public/Detector/SharpMovementClassifier.h) · [SharpMovementClassifier.cpp](../../Source/AIReplicationCore/Private/Detector/SharpMovementClassifier.cpp) |
| M4 | 서버 이동 이후 샘플링·Detector 이벤트·판정 실행 비용 | [SharpMovementDetectorComponent.h](../../Source/GE_AIReplication/SharpMovement/Detector/SharpMovementDetectorComponent.h) · [SharpMovementDetectorComponent.cpp](../../Source/GE_AIReplication/SharpMovement/Detector/SharpMovementDetectorComponent.cpp) |
| M5 / M7 | 최신 급변 Snapshot·identity·uint32 순번 wrap 비교 | [SharpMoveSnapshot.h](../../Source/AIReplicationNet/Public/SharpMoveSnapshot.h) |
| M5 | Default / Adaptive / AlwaysHigh / SingleShot / Burst / EventOnly / EventPredictor 전송 정책·원래 설정 복원 | [SharpUpdatePolicy.h](../../Source/GE_AIReplication/SharpMovement/Net/SharpUpdatePolicy.h) · [SharpUpdatePolicy.cpp](../../Source/GE_AIReplication/SharpMovement/Net/SharpUpdatePolicy.cpp) |
| M6 | 예측·수렴·충돌 보호의 에디터 설정 | [SharpVisualPredictionSettings.h](../../Source/GE_AIReplication/SharpMovement/Prediction/SharpVisualPredictionSettings.h) |
| M6 / M7 | Simulated Proxy Mesh 예측·순번/시각 검증·메타데이터 대기·충돌/착지/상태 변경/시간 초과 복구 | [SharpVisualPredictorComponent.h](../../Source/GE_AIReplication/SharpMovement/Prediction/SharpVisualPredictorComponent.h) · [SharpVisualPredictorComponent.cpp](../../Source/GE_AIReplication/SharpMovement/Prediction/SharpVisualPredictorComponent.cpp) |
| 공통 | 공개 데이터 타입의 모듈 의존성 | [AIReplicationNet.Build.cs](../../Source/AIReplicationNet/AIReplicationNet.Build.cs) · [GE_AIReplication.Build.cs](../../Source/GE_AIReplication/GE_AIReplication.Build.cs) |

## 단계별 설정 문서

실제 Actor 배치·참조 연결·PIE 실행·통과 기준은 [마일스톤별 레벨 배치 및 수동 테스트](SharpMovement_마일스톤별_레벨_배치_및_수동_테스트.md)를 순서대로 따른다.

| 단계 | 간단 설정 |
|---|---|
| M0 | [M0_Editor_설정.md](../M0/M0_Editor_설정.md) |
| M1 | [M1_Editor_설정.md](../M1/M1_Editor_설정.md) |
| M2 | [M2_Editor_설정.md](../M2/M2_Editor_설정.md) |
| M3 | [M3_Editor_설정.md](../M3/M3_Editor_설정.md) |
| M4 | [M4_Editor_설정.md](../M4/M4_Editor_설정.md) |
| M5 | [M5_Editor_설정.md](../M5/M5_Editor_설정.md) |
| M6 | [M6_Editor_설정.md](../M6/M6_Editor_설정.md) |
| M7 | [M7_Editor_설정.md](../M7/M7_Editor_설정.md) |
| M8 | [M8_Editor_설정.md](../M8/M8_Editor_설정.md) |

## 실행과 기록 연결

1. Enemy·Path·Manager·선택적인 Impulse Volume을 배치하고 참조를 연결합니다. 기존 Enemy Blueprint는 부모 클래스와 상속된 Character Movement가 새 C++ 클래스인지 확인하고 재컴파일합니다.
2. 서버에서 Prepare Trial → Start Trial을 호출하거나 Auto Start Trial을 켭니다. 클라이언트는 복제된 Enemy 메타데이터로 자동 기록합니다.
3. 동일 RunId·TrialId·ActorId의 종료된 서버 폴더와 클라이언트 폴더를 SharpMovementReport에 지정한 뒤 Generate Report를 호출합니다.
4. 모드별 보고서를 Comparison Reports에 등록하고 Compare Reports로 실측 예산 범위 및 반복 변동을 확인합니다.

| 기록 | 내용 |
|---|---|
| 서버 `server_samples.csv` | CMC 업데이트 후 시각·Capsule·Velocity·Mode·기준 Mesh |
| 서버 `events.csv` | Volume 사건·호출 전 속도·실제 속도 변화가 관찰된 샘플·미관측/취소 상태 |
| 클라이언트 `client_samples.csv` | 표시 Mesh·Capsule·시각·Mode·최근 복제 시각·예측 여부·offset |
| 양쪽 `diagnostics.csv` | Detector·Risk·ForceNetUpdate 요청·Snapshot 발행/수신·CMC 수신/보정·예측 시작/종료 |
| 양쪽 `connections.csv` | 연결 누적 bytes/packets 차이를 실제 wall time으로 나눈 값. Actor별 비용이 아님 |
| 양쪽 `manifest.json` | 식별자·엔진·역할·설정·실제 Network Emulation·출력 종료 상태 |
| 보고서 `aligned_errors.csv` | 같은 추정 서버 시각으로 정렬한 Capsule/Mesh 오차·프레임 이동·시각 잔차·가속도 |
| 보고서 `report.json` | 오차 peak/mean/면적·수신 전후 오차·회복·TP/FP/FN·Classifier 비용·수신 간격·네트워크 비용 |
| 그래프 `report.html / comparison.html` | 시간별 오차·실측 전송량 대비 오차/추정 수신 지연. hover·지표 선택 지원 |
| 비교 `comparison.json` | 동일 조건과 예산 단위의 모드 쌍, 허용 전송량 차이, 반복 평균/범위/표준편차 |

CSV는 `Saved/<Output Subdirectory>/<RunId>/Trial_<TrialId>/<프로세스·월드·GUID>/`에 저장됩니다. 클라이언트 폴더명은 Client로 시작합니다. 여러 AI/클라이언트가 있어도 개별 출력 폴더를 사용하며, 파일 identity를 검사해 다른 기록의 혼합을 거부합니다.

## 비교에서 확인할 사항

- Default도 측정용 이동 timestamp·Trial 메타데이터를 전송합니다. 모든 모드에 같은 측정 설정을 사용해야 합니다. ForceNetUpdate 요청 시각과 실제 수신 시각은 구분합니다.
- GameState 시각은 동기화 추정치입니다. 시계 역행·표본 공백·서버 범위 밖 구간을 제외하고 coverage를 제공합니다. 수신 지연은 추정값이며 정밀한 one-way latency가 아닙니다.
- 회복은 이벤트 윈도우에서 최대 오차 이후 threshold 이하 상태가 hold 시간 지속된 경우만 계산합니다. 다음 사건·기록 종료로 관찰 시간이 부족하면 RecoveryCensored로 남깁니다.
- 같은 서버 이동 샘플에 소비된 여러 Impulse는 관측 가능한 상태 하나로 묶어 Detector를 일대일 대응합니다. Observed는 실제 Velocity 변화의 관찰이며 Impulse만의 인과 효과를 분리한 값은 아닙니다.
- 정상 급회전·정지는 Volume 사건 기준 평가에서는 오탐으로 집계되는 대조군입니다. 실제 게임에서 의미 있는 급변인지의 판단과 이 실험의 TP/FP 정의는 구분합니다.
- 표시 프레임 이동량에는 정상 움직임이 포함됩니다. VisualResidualStep에는 예측·시계 오차도 포함됩니다. CMCCapsuleCorrection은 Capsule 보정 요청 거리이며 Mesh 후퇴 거리와 같지 않습니다.
- Actor·Snapshot·RPC 비용은 Networking Insights 실측값을 선택적으로 가져옵니다. 연결 전체 bytes/s를 Enemy 비용으로 치환하지 않습니다.
- EventOnly와 EventPredictor의 Snapshot 전송 코드는 동일합니다. EventOnly + Custom Correction 대조군으로 보정 방식 변경과 궤적 예측을 구분합니다.
- Sequential Mode Sequence와 반복은 실험 실행 기능이며 자동화 테스트가 아닙니다. 성능 결론과 영상은 에디터 실행 이후 [실험 결과 문서](SharpMovement_실험결과.md)에 작성합니다.

CMC의 기본 timestamp는 마지막 **위치/회전 변화** 시각입니다. 위치·회전 변화 없는 속도/모드 사건의 첫 기본 상태 수신은 이 시각만으로 확정하지 않으며 null로 남을 수 있습니다. raw 복제 위치·속도는 MovementReceived 진단에 함께 기록합니다. 같은 시각의 Snapshot과 기본 이동 상태는 예측 시작을 허용하고, 더 새로운 기본 상태가 이미 적용된 Snapshot을 제외합니다.

## 확인 범위

2026-10-11, 설치된 UE 5.8 기준:

- `GE_AIReplicationEditor Win64 Development`: 헤더 생성·C++ 컴파일·모듈 링크 성공.
- `GE_AIReplication Win64 Development`: 일반 게임 타깃 컴파일·링크 성공.
- HTML 그래프 스크립트 문법과 설정 문서의 파일 연결 확인.
- 자동화 테스트·PIE 실측·영상·Actor trace 비용 측정은 수행하지 않음. Blueprint 재컴파일 및 맵 배치는 단계별 설정 안내에 따라 에디터에서 수행.
