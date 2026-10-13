# SharpMovement의 파일 책임과 확장 지점

## 경계

아래 경로는 `Source/GE_AIReplication/SharpMovement` 기준이다.

| 파일 | 책임 |
|---|---|
| Enemy/SharpEnemyCharacter | 물리 Character, Native Component 구성, 기본 이동 RepNotify 연결 |
| Enemy/SharpEnemyAIController | AI Possession과 소유 해제 통지 |
| Scenario/SharpSplinePath | Spline 형상·식별자·AssignedEnemy·출발 설정 |
| Scenario/SharpSplinePathValidation.cpp | 준비 시 투영 모호성 검사 |
| Scenario/SharpSplinePathControl.cpp | Details 버튼, Editor 배치와 Undo, Manager 명령 전달 |
| Scenario/SharpEnemyPlacement | 공통 출발 Transform·바닥·Capsule 검증 및 배치 |
| Movement/SharpSplineMovementTypes | 추종 설정·상태·순회 방식 |
| Movement/SharpSplineFollowerComponent | CMC 이전 입력, 이동 후 진행도, 도착·반환·정체 |
| Experiment/SharpEnemyExperimentComponent | 개별 문맥, 정책 수명, 상태 복제·종료 정리 |
| Experiment/SharpExperimentTypes | Identity·Status·Summary 계약 |
| Experiment/SharpExperimentValidation | 전체/개별 읽기 전용 사전 검증 |
| Experiment/SharpExperimentManager.cpp | 전체 Identity·시작 설정·복제·Actor 수명 |
| Experiment/SharpExperimentManagerControl.cpp | 전체/개별 명령 조정과 집계 |
| Experiment/SharpExperimentManagerAutomation.cpp | 준비 대기·접속 조건·워밍업·시작 타이머 |
| Experiment/SharpExperimentManagerRepeat.cpp | 그룹 시간 제한·반복 순서·Adaptive 전역 설정 수명 |
| Measurement/SharpMovementDiagnosticsComponent | 서버/클라이언트 로그, 수신·보정 통계 |
| Measurement/SharpMovementRecorderComponent | 실행 구간별 실제 샘플·사건·진단 기록과 종료 후 관찰 |
| Measurement/SharpMovementRecorderManifest.cpp | 준비 당시 설정과 실행 결과 메타데이터 |
| Measurement/SharpRecordingFiles | CSV 버퍼·저장·역할별 파일 경로 |
| Measurement/SharpConnectionRecorderComponent | Manager 단위 연결 전체 비용 기록 |
| Measurement/SharpMovementReport | Editor에서 기존 Python 분석기 호출 |
| Scenario/SharpImpulseVolume | 공간 진입, Enemy별 충격과 발동 제한 |
| Detector/SharpMovementDetectorComponent | 실제 CMC 결과 판정, 사건 번호와 정책 연결 |
| Prediction/SharpVisualPredictorComponent | Simulated Proxy의 Mesh 예측·복귀 |
| Movement/SharpCharacterMovementComponent | 기본 이동 유지, 충격 적용·SmoothCorrection 계측 |
| Net/SharpUpdatePolicy | 기존 전송 정책·사전 검증·Burst 타이머 |
| Net/SharpSnapshotChannelComponent | 보조 Snapshot 게시·복제·수신 검증 |
| Net/SharpDetectorNetworkBridge | 외부 Detector 사건과 현재 정책의 연결 |

AIReplicationNet의 SharpMoveSnapshot은 페이로드만 정의한다. Actor 제어·정책·CMC 설정을 Net 모듈에 섞지 않는다.

## 흐름과 불변 조건

`Details/서버 Blueprint → Manager → Enemy Experiment → Follower → CMC`

`CMC 결과 → Follower 진행도 + Diagnostics + Recorder + Detector`

`Detector → Bridge → Policy 또는 SnapshotChannel → 클라이언트 VisualPredictor`

`Status 복제 → 클라이언트 기록/진단/예측 문맥 갱신 + Snapshot 재검토`

- 연결 설정 원본은 Path.AssignedEnemy다. Manager에는 Path 목록만 저장한다.
- 서버 준비 시 문맥과 설정을 확정한다. DataAsset은 복사하고 Follower 설정은 값으로 복사한다.
- 한 월드에 Manager 하나, 한 Enemy에 Path 하나다. 개별 명령은 대상별 준비/Controller 검증을 수행한다.
- Session/Manager를 통해 제어한다. Status를 직접 수정하거나 Follower만 Reset하지 않는다.
- 서버 Tick은 Session 검증 → Follower 입력 → CMC 순서다. 클라이언트는 추종 입력을 생성하지 않는다.
- 일반 수동 이동에서 개별 완료·실패는 다른 Enemy를 종료하지 않는다. 자동 반복/시간 제한으로 시작한 비교 실험은 참여 Enemy의 실패 시 나머지를 정지한다. 전체 명령 사전 검증 실패는 아무도 시작시키지 않는다.
- 전체 준비는 배치 위치와 충돌을 모두 검사한 뒤 적용한다. 예상하지 못한 적용 실패는 그룹을 종료하지만 이미 적용된 위치를 이전 위치로 롤백하는 트랜잭션은 아니다.
- 전체 Reset은 TrialId, 개별 새 준비/Reset은 Generation, 수동 시작/재개는 Segment를 구분한다. Impulse 입력 중단의 자동 재개는 같은 Segment를 유지한다. 이 번호를 조용히 wrap하지 않는다.
- SequenceId는 실제 Detector가 발급하는 Enemy 수명 번호다. 정책/채널은 사건을 생성하지 않는다.
- Snapshot과 Status의 RepNotify 순서를 가정하지 않는다. 미래 세대 Snapshot은 문맥 도착 후 재검토한다.
- Stop/Release/완료/실패/EndPlay에서 정책의 빈도 변경을 복원한다. 개별 정책은 전역 Adaptive CVar를 변경하지 않는다.
- 제어용 ForceNetUpdate는 Detector 정책 요청이나 실제 패킷 수와 구분한다.
- Editor 배치는 Undo 트랜잭션을 사용한다. 실행 제어는 서버/GameWorld/BeginPlay 조건을 확인한다.

## 확장 위치

| 확장 | 수정할 경계 |
|---|---|
| 닫힌/경사/입체 경로 | Path 검증, Placement, Follower 진행도 정책 |
| 장애물 회피나 새로운 추종 알고리즘 | Follower |
| 새로운 외력 시나리오 | Scenario와 Session, 준비된 문맥 확인 |
| 새 기록 형식이나 측정 지표 | Recorder, Scripts/Analysis의 수집·분석 경계 |
| 새 시각 예측 모델 | Snapshot 계약과 별도 Predictor |
| 게임 화면 버튼 | 소유 PlayerController Server RPC → Manager |
| 반복 종료 조건 확장 | ManagerRepeat, 전체 종료 집계 → Reset/Prepare/Start |

기존 정수 ActorId·Policy API·로그 태그를 유지했다. Wonjunjang의 기능은 현재 다중 Enemy 수명과 CSV 양식에 맞춰 통합했다. Snapshot 필드와 Native Component 구성이 추가되므로 이전 빌드 서버와 새 빌드 클라이언트를 혼용하지 않는다. 사용 방법은 [실험 기능 안내](SharpMovement_실험기능.md)를 참고한다.

에디터 배치와 수동 확인은 [설정 가이드](../M1/M1_MultiEnemy_Spline_설정.md)를 따른다. 자동화 테스트는 범위에서 제외한다.
