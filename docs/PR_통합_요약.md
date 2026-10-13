# PR 요약: 다중 Enemy 급변 실험 통합

SCB의 [PR #3](https://github.com/wonkii/GE_AIReplication/pull/3) 네트워크 정책과 YHJ의 [PR #4](https://github.com/wonkii/GE_AIReplication/pull/4) 실행·분석 도구를 기반으로, JWJ의 다중 Spline 이동과 Impulse → Detector → Snapshot → 클라이언트 Mesh 예측 → 기록·보고서를 연결했다. 기존 CMC 이동 복제를 유지하면서 같은 시나리오에서 7개 모드를 비교할 수 있다.

범위는 `integration/jwj`의 PR #3·#4 병합 이후 통합 변경이다. 아래는 병합된 PR의 원래 범위와 이후 통합 작업을 구분한 요약이다.

## 구현 내용과 근거

| 영역 | 이번 통합 내용 | 근거 문서 | 주요 구현 파일 |
|---|---|---|---|
| 다중 Enemy 이동 | 고정 X축 왕복을 개별 Spline 추종으로 교체. Enemy별 식별자·상태와 전체/개별 준비·시작·정지·리셋 지원 | [Spline 설정](M1/M1_MultiEnemy_Spline_설정.md) | [경로](../Source/GE_AIReplication/SharpMovement/Scenario/SharpSplinePath.cpp), [추종](../Source/GE_AIReplication/SharpMovement/Movement/SharpSplineFollowerComponent.cpp), [제어](../Source/GE_AIReplication/SharpMovement/Experiment/SharpExperimentManagerControl.cpp) |
| Impulse·Detector | 서버 충격과 정답 기록을 추가. Detector는 Overlap 정보 없이 실제 CMC 이동 결과로 급변 판정 | [기능 안내 §2·4](Architecture/SharpMovement_실험기능.md) | [충격](../Source/GE_AIReplication/SharpMovement/Scenario/SharpImpulseVolume.cpp), [순수 판정](../Source/AIReplicationCore/Private/Detector/SharpMovementClassifier.cpp), [서버 관측](../Source/GE_AIReplication/SharpMovement/Detector/SharpMovementDetectorComponent.cpp) |
| 네트워크 정책 | PR #3의 7개 정책에 실제 Detector 연결. 실행 구간을 검증해 Snapshot을 전달하고 종료 시 빈도 복원 | [기능 안내 §3](Architecture/SharpMovement_실험기능.md) | [정책](../Source/GE_AIReplication/SharpMovement/Net/SharpUpdatePolicy.cpp), [브리지](../Source/GE_AIReplication/SharpMovement/Net/SharpDetectorNetworkBridge.cpp), [Snapshot 채널](../Source/GE_AIReplication/SharpMovement/Net/SharpSnapshotChannelComponent.cpp), [전송 계약](../Source/AIReplicationNet/Public/SharpMoveSnapshot.h) |
| 시각 예측 | PR #3의 서버 전송 경로에 클라이언트 예측 추가. Simulated Proxy의 Mesh만 예측하고 최신 이동·충돌·만료 시 복귀 | [기능 안내 §4](Architecture/SharpMovement_실험기능.md) | [예측](../Source/GE_AIReplication/SharpMovement/Prediction/SharpVisualPredictorComponent.cpp), [CMC 계측](../Source/GE_AIReplication/SharpMovement/Movement/SharpCharacterMovementComponent.cpp) |
| 기록·반복·보고서 | PR #4 도구에 native Recorder 연결. 실행 구간별 CSV, 연결 비용, 반복/모드 순서, 기록 품질·비용 비교 보고서 지원 | [기록 실행·해석](Architecture/SharpMovement_기록파일_실행과해석.md), [분석 안내](../Scripts/Analysis/README.md) | [Recorder](../Source/GE_AIReplication/SharpMovement/Measurement/SharpMovementRecorderComponent.cpp), [반복](../Source/GE_AIReplication/SharpMovement/Experiment/SharpExperimentManagerRepeat.cpp), [실행](../Scripts/RunSharpTrial.ps1), [수집](../Scripts/Analysis/collect_recordings.py), [분석](../Scripts/Analysis/sharp_metrics.py), [보고서](../Scripts/Analysis/native_reports.py) |

파일 확장 시 역할 경계는 [책임 분리](Architecture/SharpMovement_책임분리.md)를 따른다. M0~M8 문서의 일부 `개발 예정` 표시는 초기 계획이므로 현재 구현 상태는 위 기능 안내를 기준으로 확인한다.

## 확인한 범위

기존 [검증 기록](Architecture/SharpMovement_실험기능.md)에는 Editor·게임 Win64 Development 빌드 통과가 남아 있다. [2026-10-13 실행 기록](Architecture/SharpMovement_기록파일_실행과해석.md)에서는 `Lv_Jang`의 Enemy 3개, 서버 1개·클라이언트 2개(N0/N2)로 EventOnly와 EventPredictor를 각 1회 실행했다.

- 두 모드 합계 Impulse 6건 모두 실제 변화 기록·감지, 18개 역할별 manifest 정상 완료, 두 Trial 기록 품질 Eligible=true.
- EventPredictor의 각 클라이언트에서 예측 시작·종료 각 5회 확인. 추가 착지 감지 2건도 포함한다.
- 연결 전체 송신 비용 차이는 약 0.36%. 렌더링 없는 짧은 기능 확인이므로 화면 품질이나 예측의 성능 우위는 후속 반복 실험으로 검증해야 한다. 이번 통합에 자동화 테스트는 추가하지 않았다.

## 에디터 설정과 테스트 순서

1. **빌드·재시작:** UE 5.8을 사용한다. 에디터를 닫고 `GE_AIReplicationEditor / Development Editor / Win64`를 빌드한 뒤 프로젝트를 다시 연다. Native Component 변경은 Live Coding만으로 반영하지 않는다. `BP_SharpEnemy`를 Compile/Save하고 SplineFollower·Experiment·Detector·Recorder·VisualPredictor·SnapshotChannel을 확인한다.
2. **레벨·연결:** `/Game/JWJ/Lv_Jang`을 연다. Manager는 1개, Settings에는 `DA_SharpExperimentSettings`, Paths에는 사용할 Spline 인스턴스를 연결한다. PathId와 양수 ExperimentActorId는 각각 고유하게 하고, 각 Path의 Assigned Enemy에 서로 다른 Enemy 인스턴스를 지정한다. 시작점은 바닥 높이에 맞추고 `Set Enemy At Start`로 배치한다.
3. **Enemy·충격:** Enemy의 Replicates·Replicate Movement를 켜고 AI Controller=`SharpEnemyAIController`, Auto Possess AI=`Placed in World or Spawned`로 둔다. 각 경로 중간에 Sharp Impulse Volume을 배치하거나 기존 것을 확인한다. VolumeId는 고유하게, 시작 Capsule과는 겹치지 않게 한다. Enemy Capsule의 Generate Overlap Events와 Volume의 Query Only·Pawn Overlap을 켠다.
4. **공통 설정:** Data Asset에서 Recording → Enabled=true, Flush Interval Seconds=0.5, Post Trial Observation Seconds=0.5로 둔다. 비교 기준은 Use Controlled Low Rate=true, Normal=10Hz, High=60Hz, Impulse=(0,700,250), 속도 변화 임계값=300, 방향 임계값=45도로 맞춘다. Prediction → Enable Prediction=true, EventOnly Uses Custom Correction=false를 사용한다.
5. **실행 설정:** Manager에서 Configured Mode=EventOnly, Configured Run Id는 비움, Prepare On Begin Play=true, Auto Start Trial=true, Auto Start Delay Seconds=3, Expected Client Count=1, Auto Repeat=false, Trial Duration Seconds=20으로 둔다. PingPong은 시간 제한으로 종료하며 Once는 먼저 자연 완료할 수 있다.
6. **서버·클라이언트:** Play의 Multiplayer Options에서 Number of Players=2, Net Mode=`Play As Listen Server`로 설정한다. 서버 창과 별도 클라이언트 창에서 Play를 시작하고 세 Enemy의 이동·충격·완료를 확인한다. 수동 시작은 Auto Start Trial을 끄고 **실행 중 서버 월드의 Manager**에서 Prepare All → Start All을 누른다.
7. **저장·재실행:** Completed 후 최소 1초 기다리고 Play를 종료한다. `Saved/SharpExperiments/Native/`의 각 역할별 manifest에서 Healthy=true, State/Outcome=Completed를 확인한다. 같은 배치·설정으로 Configured Mode만 EventPredictor로 바꾸어 다시 실행하고 클라이언트 예측을 확인한다. 수동 Stop으로 끝낸 기록은 완료 Trial 비교에서 제외된다.
8. **보고서:** Python 3.10 이상으로 아래 명령을 실행하고 출력 폴더의 `report.html`을 연다. RecordingQuality의 Eligible/Reasons, 충격·감지 대응, 예측 시작/종료, Mesh 오차와 비용을 순서대로 확인한다. 같은 PC의 기록은 `--clock wall`을 사용한다.

```powershell
python Scripts/Analysis/sharp_metrics.py Saved/SharpExperiments --native --clock wall --out Saved/Reports/manual
```

독립 서버·클라이언트 2개와 N0/N2 조건 재현은 [실행 명령](Architecture/SharpMovement_기록파일_실행과해석.md)을 사용한다. 에디터 버튼으로 보고서를 만들려면 Sharp Movement Report Actor의 PythonExecutable·Recording Directories·Output Directory를 지정하고 Generate Report → Open Report를 누른다. 준비가 실패하면 Manager/Path의 Last Command Error에서 중복 ID·바닥·충돌·연결을 먼저 확인한다.

## 작업자별 다음 단계

기존 PR·브랜치 담당 범위를 기준으로 한 후속 작업 제안이다. 우선 세 작업자가 위 기능 확인을 재현한 뒤 [M7 견고성](M7/M7_견고성.md) → [M8 최종 평가](M8/M8_최종_평가.md)로 진행한다.

| 작업자 | 다음 할 일 | 완료 산출물 |
|---|---|---|
| JWJ / wonkii — 통합·시나리오 | 직선/곡선, Once/PingPong, 벽 충돌·연속 충격·다중 Enemy 시나리오를 고정하고 렌더링 상태에서 예측 복귀를 확인한다. 착지 등 라벨 없는 급변은 Detector/정답 범위를 정리하고 환경 변경 시 Scenario Revision을 올린다. | 재현 가능한 맵·설정, 충돌/착지/만료별 복구 결과와 비교 영상 |
| SCB / fkEn575 — 네트워크 | 실제 Detector로 SingleShot·Burst·Adaptive를 검증한다. 연속 사건·손실·오래된 Snapshot에서 최신 상태 복귀와 빈도 복원을 확인하고 Networking Insights로 Enemy/Snapshot 비용을 분리한다. | 모드별 실제 갱신 간격·bytes/s, 누락/중복/복원 검증 표 |
| YHJ / YHJ-grass — 실행·분석 | 고정한 시나리오에서 7개 모드×N0~N4를 반복 실행한다. 동일 PC는 wall 시계로 정렬하고 미완료·기록 부족 Trial을 제외한다. EventOnly/EventPredictor의 같은 측정 비용 비교와 평균·최악값·변동 폭을 정리한다. | 반복 실행 원본, 기록 품질·비용 비교 보고서, M8 결과 문서 |

진행 순서는 **시나리오·라벨 고정 → 정책/복구 검증 → 반복 수집·동일 비용 비교**다. 연결 전체 비용과 Enemy 전용 비용을 구분하고, 착지를 단순 오탐으로 집계하지 않는다.
