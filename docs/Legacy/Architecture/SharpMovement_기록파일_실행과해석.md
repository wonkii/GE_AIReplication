# Lv_Jang 기록파일 실행과 해석

## 실제 실행 결과 — 2026-10-13

저장된 `/Game/JWJ/Lv_Jang`을 그대로 사용했다. Enemy, Spline, Impulse 자산은 이 확인 과정에서 저장하거나 변경하지 않았다.

- 서버 1개와 독립 클라이언트 2개를 같은 PC에서 실행했다.
- EventOnly, EventPredictor를 각각 1회 실행했다.
- 클라이언트 1: N0, 인위적인 지연·손실 없음.
- 클라이언트 2: N2, 양방향 각각 40–60ms 지연과 1% 손실 설정.
- 서버·클라이언트 프레임 제한 60FPS, 최대 실행 시간 20초, 종료 후 추가 대기 3초.
- 현재 경로는 모두 Once / Reverse Direction=true이다. 각 경로가 약 5–6초 만에 자연 완료되어, 기록도 종료 관찰 구간 후 닫힌다. 20초 내내 움직인 기록으로 해석하지 않는다.
- 렌더링 없이 게임 이동·복제·기록을 실행했다. 아래 Mesh 수치는 기록된 위치의 오차이며 육안 화면 검증이나 장시간 성능 평가 결과가 아니다.

분석에 사용한 최종 원본: `Saved/Experiments/RecordingReview-20261013-1910/`. Batch 이름은 식별자이며 실제 시작 시각은 EventOnly 18:52:31, EventPredictor 18:53:26 KST이다.

결과: `Saved/RecordingReview20261013/final_analysis/report.html`, `report.md`, `report.json`.

| 확인 항목 | EventOnly | EventPredictor |
|---|---:|---:|
| 정상 완료 manifest | 9 / 9 | 9 / 9 |
| 서버 위치 샘플, 세 Enemy 합계 | 1,049 | 1,047 |
| N0 클라이언트 샘플 | 1,049 | 1,051 |
| N2 클라이언트 샘플 | 1,047 | 1,054 |
| Impulse 실제 관찰 | 3 / 3 | 3 / 3 |
| Impulse에 대응한 Detector 감지 | 3 / 3 | 3 / 3 |
| 호출 → 감지 평균 | 17.0ms | 17.3ms |
| 최저 기록 범위 비율 | 98.54% | 98.86% |
| 기록 품질 검사 Eligible | true | true |

9개 manifest는 **Enemy 3개 × 서버·클라이언트 3개 역할**이다. 두 모드 합계 18개 모두 Healthy=true, State와 Outcome=Completed를 확인했다. 범위 비율은 클라이언트·Enemy 조합 중 가장 낮은 시간 범위 비율이다. 패킷 성공률이나 손실률이 아니다.

프로세스는 창이 없는 실행이라 마지막에 강제 종료되었다(`run_meta.json`의 ForcedKill=true). 그 전에 18개 Recorder가 관찰 구간을 끝내고 파일을 닫았으며, 최종 완료 상태와 저장 상태를 확인했다. ForcedKill만 보고 기록이 완전하다고 판단하면 안 된다.

### Enemy별 Impulse 연결

| Enemy ActorId | 경로 | 실제 통과한 Volume | 발동 횟수, 모드당 |
|---|---|---|---:|
| 1 | Path03, 곡선 | Impulse01 | 1 |
| 2 | Path02, 직선 | Impulse02 | 1 |
| 3 | Path01, 직선 | Impulse03 | 1 |

모든 사건은 `Status=Observed`, `IsSharp=1`, `VelocityChange=1`, 충격 `(0,700,250)`을 기록했다. 여섯 사건을 모은 `final_analysis/impulse_events.csv`에서 확인할 수 있다. 원본과 달리 이 파일의 ServerTime과 사건 시각은 각 실행의 시작 샘플을 0초로 맞춘 값이다.

예를 들어 EventPredictor / Enemy 2는 다음과 같다.

- 충격 호출: 실행 시작 샘플 이후 약 1.915초.
- CMC 적용 및 실제 속도 변화: 약 1.932초.
- 이전 속도: `(-300,0,0)`cm/s.
- 이후 속도: 약 `(-301.42,699.39,233.92)`cm/s.
- MovementMode가 Walking(1)에서 Falling(3)으로 바뀌었다.
- 같은 서버 이동 샘플에서 Detector가 감지했다. `LatencyFromActual=0`은 기록 샘플 기준이며, 물리적으로 연산 시간이 0이라는 뜻이 아니다.

수직 속도가 정확히 250이 아닌 것은 충격 적용 후 이동 결과에 중력 등이 반영되기 때문이다. 파일에는 단순 Overlap 호출뿐 아니라 실제 이동 변화도 기록되었다.

### 추가 감지와 클라이언트 해석

Impulse 라벨에 대응하지 않은 감지는 EventOnly 1건, EventPredictor 2건이다. 원본 이동 샘플을 대조하면 모두 **Falling(3) → Walking(1) 착지**이고 수직 속도가 약 -253~-258cm/s에서 0으로 바뀐다. 속도 변화량은 약 302~346cm/s로 현재 300 임계값을 넘었다. 따라서 보고서의 FP_Other를 모두 잘못된 탐지라고 해석하면 안 된다. 현재 Ground Truth는 Impulse만 라벨링한다.

EventPredictor의 각 클라이언트는 Snapshot 5건을 수신하고 예측 시작·종료를 각각 5회 기록했다. 종료 이유는 모두 NewServerMovement였다. 세 충격과 두 추가 착지 감지가 예측 경로까지 전달된 것이다.

| 모드 | 네트워크 | Capsule 평균 오차 | Mesh 평균 오차 |
|---|---|---:|---:|
| EventOnly | N0 | 2.51cm | 3.67cm |
| EventOnly | N2 | 14.74cm | 15.97cm |
| EventPredictor | N0 | 2.62cm | 3.94cm |
| EventPredictor | N2 | 14.41cm | 15.76cm |

위 수치는 세 Enemy의 실행별 평균을 평균한 값이다. N2에서는 약 50ms 뒤처짐이 추정되어 넣은 지연 조건과 대체로 맞는다. 지연 조건에서 오차가 커진다고 해서 Spline 연결 실패를 뜻하지 않는다.

서버의 두 연결 합산 송신량은 EventOnly 약 6,866B/s, EventPredictor 약 6,891B/s, 차이 약 0.36%로 기본 5% 비교 허용 범위 안이다. 이 비용은 플레이어와 관리 상태 등을 포함한 **엔진 연결 전체**이며 Enemy만의 비용이나 NIC 전송량이 아니다. 모드당 한 번의 짧은 실행이고 평균 차이도 작으므로 Predictor의 성능 우위를 결론내릴 근거는 부족하다. 연결·충격·감지·예측·기록의 동작을 확인한 결과다.

## 1. Editor에서 직접 기록하기

1. 수정된 Editor 빌드를 사용하고 `Lv_Jang`을 연다. 이미 Editor가 켜져 있었다면 종료 후 다시 연다.
2. Manager의 Settings에 연결된 `DA_SharpExperimentSettings`를 열어 **Recording → Enabled**를 켠다. 현재 저장된 값은 이미 켜져 있다. Flush Interval Seconds=0.5, Post Trial Observation Seconds=0.5를 우선 사용한다.
3. Manager에서 다음을 설정한다. 처음에는 한 번만 실행한다.

   | 항목 | 처음 사용할 값 |
   |---|---|
   | Configured Mode | EventOnly, 이후 EventPredictor로 별도 실행 |
   | Configured Run Id | 비워 두면 서버가 실행마다 새 이름 생성 |
   | Prepare On Begin Play | true |
   | Auto Start Trial | true |
   | Auto Start Delay Seconds | 3 |
   | Expected Client Count | 1, 아래 Listen Server + 플레이어 2 설정 기준 |
   | Auto Repeat | false |
   | Trial Duration Seconds | 20 |

4. Play 옵션 / Multiplayer Options에서 **Number of Players=2, Net Mode=Play As Listen Server**를 선택한다. 서버 창과 클라이언트 창을 함께 실행한다. Standalone은 서버 위치·Impulse 기록만 만들므로 클라이언트 오차 비교용으로는 이 구성을 사용한다.
5. Play를 누른다. 클라이언트 접속 후 3초가 지나면 전체 Enemy가 자동으로 출발한다. `Set Enemy At Start`는 위치 배치 기능이며 기록 시작 버튼이 아니다. Recorder는 실제 Running 상태부터 파일을 연다.
6. 세 Enemy가 충격 구역을 지나 경로를 완료하는 것을 관찰한다. 현재 Once 설정이면 자연 완료한다. PingPong 설정이면 계속 움직이므로 Trial Duration Seconds로 완료 시점을 정한다.
7. Completed 이후 **최소 1초** 기다린 뒤 Play를 종료한다. 설정에서 Post Trial Observation Seconds를 늘렸다면 그 시간에 여유를 더해 기다린다. 실행 직후 Reset하거나 Play를 종료하면 끝부분이 잘리거나 Completed가 아닌 상태가 기록될 수 있다.
8. 탐색기에서 프로젝트의 `Saved/SharpExperiments/Native/`를 연다. 실행 폴더 → Trial_1 → Actor_1/2/3 → Generation_1 → Segment_1 → Server 또는 Client 역할 폴더를 확인한다. PIE에서 `client_0_samples.csv`가 만들어지는 것은 기본 ClientId=0을 사용하기 때문이며, 역할 폴더의 월드 식별자로 구분된다.
9. 각 `manifest.json`에서 Healthy=true와 State/Outcome의 Completed를 확인한다. 충격과 감지는 서버 폴더, 클라이언트 위치는 Client 폴더에서 확인한다.

버튼으로 직접 시작하려면 Auto Start Trial을 끄고 Play 후 **실행 중인 서버 월드의 Manager**에서 Prepare All → Start All을 누른다. 시작할 때 자동 준비가 끝났다면 Start All부터 누르면 된다. 편집 월드에 있는 Manager나 클라이언트 Manager의 제어 버튼은 실행 권한이 없어 거부된다. 리셋하여 다시 시험하려면 기존 관찰 구간이 닫힌 뒤 Reset All → Start All을 누른다. 수동 Stop All은 정지한 기록을 남기지만 완료 실행 비교에서는 제외된다.

## 2. 같은 조건으로 서버·클라이언트를 실행하기

프로젝트 폴더에서 PowerShell을 연다. 아래는 이번 확인처럼 화면 렌더링 없이 실제 기록을 만드는 명령이다. `BatchId`는 짧고 매번 새로운 이름을 쓴다. 다음 명령은 새 실행을 생성한다.

```powershell
.\Scripts\RunSharpTrial.ps1 `
  -Map '/Game/JWJ/Lv_Jang' `
  -Mode EventOnly,EventPredictor `
  -Trials 1 `
  -BatchId ('record-' + (Get-Date -Format 'MMdd-HHmmss')) `
  -ClientProfiles N0,N2 `
  -Editor 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  -WarmupSec 3 -MeasureSec 20 -RecordingTailWaitSec 3 `
  -ExtraServerArgs '-nullrhi','-nosound','-nosplash','-ExecCmds=t.MaxFPS 60' `
  -ExtraClientArgs '-nullrhi','-nosound','-nosplash'
```

결과는 `Saved/Experiments/<실제 BatchId>/`에 생긴다. 스크립트는 접속을 기다려 전체 이동을 시작하고, 모드별 서버·클라이언트 프로세스를 따로 실행한다. 마지막 표의 Valid=true, Enemy 수=3, 각 클라이언트의 기록을 확인한다. 이번에 사용한 두 모드와 네트워크 조건을 재현할 때 저장된 맵 설정을 수정할 필요는 없다.

여러 번 비교하려면 `-Trials (1..10)`처럼 지정한다. 자연 완료가 가능한 Once 또는 시간 제한이 있는 PingPong을 사용한다. 임의로 긴 BatchId/SharpOutDir를 쓰면 Windows 폴더 생성 제한에 다시 걸릴 수 있으므로 짧은 이름을 유지한다.

## 3. 기록을 보고서로 바꾸기

Python 3.10 이상이 필요하다. 이 PC에서 확인한 Python 경로를 사용하면 다음처럼 실행할 수 있다. 일반 Python이 설치되어 있다면 `& $pythonExe`를 `python`으로 바꿔도 된다.

```powershell
$pythonExe = 'C:\Users\wonkii\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'

# Editor에서 만든 기본 기록 분석
& $pythonExe .\Scripts\Analysis\sharp_metrics.py `
  .\Saved\SharpExperiments --native --clock wall `
  --out .\Saved\Reports\manual-record

# 이번에 만든 최종 기록 분석
& $pythonExe .\Scripts\Analysis\sharp_metrics.py `
  .\Saved\Experiments\RecordingReview-20261013-1910 --native --clock wall `
  --out .\Saved\RecordingReview20261013\final_analysis
```

본인이 새로 실행한 Batch는 해당 Batch 폴더만 입력한다. 실패한 과거 실행까지 모두 한꺼번에 합치지 않는다. 같은 PC의 서버·클라이언트는 `--clock wall`을 사용한다. `--native`가 Enemy별 원본을 `_collected`에 모아 분석하며 원본 파일은 덮어쓰지 않는다.

분석이 끝나면 출력 폴더의 `report.html`을 브라우저로 연다. 먼저 RecordingQuality를 확인하고, 이어서 Impulse / Detector / 화면 오차 / 예측 진단을 본다. `Eligible=false`이면 Reasons를 확인한 뒤 비교 지표를 해석한다.

Editor의 Sharp Movement Report Actor를 사용해도 된다. Recording Directories 배열의 첫 항목에 분석할 기본 기록 또는 Batch 폴더, Output Directory에 결과 폴더, PythonExecutable에 위 실행 파일을 지정하고 Generate Report → Open Report를 누른다.

## 4. 파일과 열 읽는 순서

| 파일 | 확인할 내용 |
|---|---|
| manifest.json | Enemy·경로·모드·준비 당시 설정, Healthy, State, Outcome |
| server_samples.csv | 서버의 실제 위치·속도·MovementMode |
| ground_truth.csv | 충격 호출·적용·실제 변화, VolumeId, Status, IsSharp |
| detector.csv | 감지 사건 Enter와 Risk 해제 Exit. 모든 행이 새 사건인 것은 아님 |
| detector_samples.csv | 서버 샘플마다 판정값과 Classifier 계산 비용 |
| client_*_samples.csv | 클라이언트 Capsule·Mesh 위치, 수신 상태, Predicting |
| diagnostics.csv | Snapshot 수신, 예측 시작·종료 이유, 실제 CMC 보정 |
| _connections/**/connections.csv | 연결 전체 바이트·패킷 누적 카운터 |
| report.html / report.md | 해석하기 편한 요약 |
| events.csv | 충격별 클라이언트 최대 오차·첫 수신·회복 |
| detector_events.csv | 각 Impulse에 대한 TP/FN, 호출·실제 변화 기준 감지 지연 |
| recording_quality.csv | 기록 범위, 완료 여부, 비교 제외 사유 |
| comparison.csv | 조건이 같은 모드의 비용 차이와 WithinBudget |

공통 식별자는 `RunId + TrialId + ActorId + Generation + Segment`이다. EventId는 Enemy별로 증가하므로 서로 다른 Enemy의 EventId=1은 서로 다른 사건이다.

- 위치는 cm, 속도는 cm/s, 시간은 s이다.
- 원본 ServerTime은 서버 월드 시각이고 클라이언트에서는 GameState 추정 시각이다. 서로 직접 빼서 정밀 지연으로 해석하지 않는다.
- WallTime은 UTC ticks를 초로 바꾼 값이며 Unix timestamp가 아니다. 큰 숫자 자체를 실행 경과 시간으로 읽지 않는다.
- `_collected`의 ServerTime과 사건 시각은 선택한 시계로 정렬된다. `--clock wall`이면 해당 실행의 첫 서버 샘플 기준 상대 시각이고 NativeServerTime은 원본 시각이다.
- ImpulseCallTime은 요청, AppliedTime은 CMC가 충격을 소비한 시점, ActualVelocityChangeTime은 실제 속도 변화가 관찰된 샘플 시점이다. 처음 적용 전 샘플이 FirstSampleTime에 찍힐 수도 있다.
- Status=NotObserved/Cancelled/PendingLimit은 정상 관찰된 충격 결과로 취급하지 않는다.
- Predicting=0인 프레임도 정상이다. 예측은 사건 직후 짧게 켜졌다가 새 서버 이동이 도착하면 종료된다.

## 5. 이번 기록 확인에서 보완한 부분

- Native 실행 폴더 이름의 설명 부분을 줄여 실제 서버·클라이언트 출력 경로가 Windows 폴더 생성 제한에 걸리지 않게 했다. 전체 RunId는 파일 안에 남는다.
- 분석 도구는 Windows의 긴 경로 형식을 사용해 깊은 Native 폴더를 누락 없이 읽는다.
- 실행 스크립트가 Native 하위 파일을 재귀적으로 확인하고, 일부 Enemy/클라이언트 CSV 또는 manifest가 없거나 저장 실패·미완료 상태이면 실행 실패로 표시하도록 했다.
- Editor Development 빌드와 실제 레벨 실행으로 확인했다. 자동화 테스트는 추가하거나 실행하지 않았다.

`Saved` 결과는 Git 관리 대상이 아니다. 장기 보관하거나 다른 사람에게 전달할 때는 해당 Batch 원본 폴더와 보고서 폴더를 함께 보관한다.
