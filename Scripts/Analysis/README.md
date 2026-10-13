# 실험 실행·분석 스크립트

> native Recorder가 기존 분석 CSV 양식에 연결되어 있다. 아래 로그 변환 안내는 기존 저빈도 기록 분석에도 사용할 수 있다.

현재 다중 Enemy 구현의 설정·기록·반복·보고서 안내는 [실험 기능 안내](../../docs/Architecture/SharpMovement_실험기능.md)를 참고한다.

```powershell
python Scripts/Analysis/sharp_metrics.py Saved/SharpExperiments --native --clock wall
```

`--native`는 Enemy별 원본 기록을 `_collected`로 모은 뒤 분석한다. `report.html`과 `report.json`, 정밀 사건·Detector·예측 진단·연결 비용·조건 비교 결과를 함께 생성한다. 같은 PC에서는 wall, 별도 PC에서는 시계 동기화 상황을 고려해 server를 선택한다.

다중 Spline 리팩터링 이후 이동 로그에 Generation/Segment/PathId/PathRevision/State를 추가한다. 로그 변환기는 이 열과 `control_events.csv`를 보존하며, 분석기는 RunId/TrialId/ActorId/Generation/Segment 실행 키 안에서만 보간한다. 결과의 ActorId는 기존 식별자를 유지하고 ExecutionKey에 실행 구간을 표시한다. Generation이 없는 기존 CSV는 기존 방식으로 처리한다. 상세 설정은 [다중 Enemy 가이드](../../docs/M1/M1_MultiEnemy_Spline_설정.md)를 참고한다.

실험을 같은 조건으로 반복 실행하고, Recorder가 남긴 CSV에서 오차와 회복 시간 같은 지표를 계산하는 스크립트다. CSV 형식은 아직 Recorder가 없어서 임시로 정했고, Recorder 구현에 맞춰 고친다.

## 1. 파일 구성

| 파일 | 역할 |
|---|---|
| `Scripts/RunSharpTrial.ps1` | 서버 1개와 클라이언트 여러 개 실행, 네트워크 조건 적용, 반복 실행, 로그 검사 |
| `Scripts/Analysis/sharp_metrics.py` | Recorder CSV 분석, 모드·네트워크 조건별 요약표 생성 |
| `Scripts/Analysis/convert_logs.py` | Recorder가 생기기 전까지 SharpEnemyCharacter 로그를 CSV로 변환 |
| `Scripts/Analysis/make_synthetic_run.py` | 분석 스크립트 확인용 가짜 데이터 생성 (실험값 아님) |

Python 3.8 이상이면 따로 설치할 패키지는 없다. PowerShell 스크립트는 Windows 기본인 5.1 기준으로 작성했다.

## 2. 실행 스크립트

```powershell
# 명령만 확인하고 실제로 실행하지는 않음
.\Scripts\RunSharpTrial.ps1 -Mode Default -DryRun

# 세 모드를 5번씩 반복. 클라이언트 1은 N0, 클라이언트 2는 N2 (기본값)
.\Scripts\RunSharpTrial.ps1 -Mode Default,Burst,AlwaysHigh -Trials (1..5)

# 클라이언트 2의 네트워크 조건을 직접 지정
.\Scripts\RunSharpTrial.ps1 -Mode Burst -ClientProfiles N0,Custom -LagMs 80 -JitterMs 15 -LossPct 2

# 서버의 Networking Insights 트레이스도 같이 저장
.\Scripts\RunSharpTrial.ps1 -Mode Default -NetTrace
```

### 2.1 실행 순서

1. M0의 실행 인자(`-SharpRunId`, `-SharpTrialId`, `-SharpMode`)에 결과 폴더(`-SharpOutDir`)를 더해 서버를 실행한다.
2. 서버 로그에 `LogSharpExperiment ... Role=Server`가 찍히면 클라이언트를 실행한다.
3. 서버에 `SharpAutoStart=1`, `SharpExpectedClients=N`, `SharpStartDelay=WarmupSec`를 전달한다. 서버는 대상 준비와 클라이언트 PlayerController 연결을 확인한 뒤 워밍업한다. 실행기는 `[Sharp][Group] Started` 로그를 기다린 후 측정 시간(기본 60초)을 센다. 시작하지 못하면 해당 실행을 무효로 기록한다.
4. 클라이언트, 서버 순서로 창을 닫는다. 15초 안에 꺼지지 않으면 강제로 종료한다.
5. 로그를 검사하고 결과를 `run_meta.json`의 `Valid`에 남긴다. 하나라도 걸리면 `Valid`는 False가 되고 이유가 `Issues`에 들어간다.
   - 서버와 클라이언트의 RunId, TrialId, Mode가 같은지
   - 서버의 모든 Enemy에 요청한 모드의 전송 정책이 적용됐는지 (`[M5][PolicyBind] ... InitSuccess=1`)
   - 각 클라이언트에 Enemy가 서버와 같은 수만큼 복제됐는지 (`[M1][Enemy] ... Role=SimulatedProxy`)
   - 진단용 가짜 사건 Actor(`SharpPolicyDiagnosticActor`)가 꺼져 있는지

Adaptive 모드는 엔진 전역 설정 `net.UseAdaptiveNetUpdateFrequency`가 켜져 있어야 동작하고, 다른 모드는 꺼져 있어야 결과가 섞이지 않는다. 그래서 서버에 `-ini:Engine:[ConsoleVariables]:net.UseAdaptiveNetUpdateFrequency=`로 Adaptive일 때 1, 나머지는 0을 넘긴다. 이 방식이 실제로 적용되는지는 Adaptive로 한 번 실행해서 `Valid`가 True인지(정책 적용 거부 사유 `AdaptiveGlobalDisabled`가 없는지)로 확인해야 한다.

`-NetTrace`를 쓰면 `<RunId>/network_cost_inputs.csv`에 Trial·클라이언트별로 실행 조건을 미리 채운 줄이 생긴다. `Tools/summarize_network_cost.py`의 입력 양식이며, 빈 칸(구간, 패킷 수, 비트 수)은 Networking Insights에서 `server.utrace`를 열어 직접 채운다. 빈 칸이 남아 있으면 그 도구가 오류를 낸다.

### 2.2 네트워크 프로필

네트워크 조건은 클라이언트에만 건다. 서버 기록이 네트워크 조건의 영향을 받지 않게 하기 위해서다. 수신과 송신 양쪽에 지연 범위 `L-J ~ L+J`, 손실률 `P`를 같은 값으로 준다.

| ID | 편도 지연 L | 지터 ±J | 손실 P | RTT 근사 |
|---|---|---|---|---|
| N0 | 0 | 0 | 0% | 0 |
| N1 | 25ms | 5ms | 1% | 50ms |
| N2 | 50ms | 10ms | 1% | 100ms |
| N3 | 100ms | 20ms | 5% | 200ms |
| N4 | 250ms | 0 | 10% | 500ms |
| Custom | `-LagMs` | `-JitterMs` | `-LossPct` | |

### 2.3 결과 폴더

같은 모드를 반복하면 RunId는 같고 TrialId만 달라진다.

```text
Saved/Experiments/<BatchId>/
└─ <BatchId>-<Mode>/            RunId
   ├─ network_cost_inputs.csv   -NetTrace를 쓸 때만. 비용 도구 입력 양식 (2.1)
   └─ T<TrialId>/
      ├─ run_meta.json           실행 조건, 클라이언트별 네트워크 프로필, 검사 결과
      ├─ server.log, client_1.log, client_2.log
      ├─ server.utrace           -NetTrace를 쓸 때만
      └─ *.csv                   Recorder가 작성 (3장)
```

`Saved/`는 `.gitignore`에 들어 있어서 실험 결과는 커밋되지 않는다.

## 3. CSV 형식 (임시안)

- 단위는 cm, s를 쓴다.
- 시각은 서버와 클라이언트 모두 `GameState->GetServerWorldTimeSeconds()`로 기록한다 (구현계획 문서 3.2).
- 모든 파일에 공통 키 `RunId`, `TrialId`, `Mode`, `ActorId`를 넣는다.
- 파일은 `-SharpOutDir=`로 받은 폴더에 쓰고, 이 인자가 없으면 `Saved/Experiments/<RunId>/T<TrialId>/`에 쓴다.

### 3.1 server_samples.csv

서버에서 이동 틱마다, Actor별로 한 줄씩 남긴다 (M3 1번).

| 열 | 내용 |
|---|---|
| `RunId`, `TrialId`, `Mode`, `ActorId` | 공통 키 |
| `ServerTime` | 서버 시각 |
| `PosX/Y/Z` | Actor 위치 (Capsule 중심) |
| `VelX/Y/Z` | CMC Velocity |
| `MovementMode` | Walking, Falling 등 |

### 3.2 client_\<ClientId\>_samples.csv

클라이언트에서 프레임마다, Actor별로 한 줄씩 남긴다 (M3 2번).

| 열 | 내용 |
|---|---|
| `RunId`, `TrialId`, `Mode`, `ActorId` | 공통 키 |
| `ClientId` | 커맨드라인 `-SharpClientId=` 값 |
| `ServerTime` | 그 프레임의 `GetServerWorldTimeSeconds()` |
| `CapsuleX/Y/Z` | 복제된 Capsule 위치 (`GetActorLocation()`) |
| `MeshX/Y/Z` | 화면에 보이는 위치. Mesh 월드 위치에서 Mesh 기본 상대 위치를 뺀 값이며, smoothing이 없으면 Capsule 위치와 같다 |
| `UpdateCount` | 이 Actor의 이동 상태를 받은 누적 횟수 (`OnRep_ReplicatedMovement` 호출 수) |
| `FrameMs` | 선택. 그 프레임의 시간(ms). 클라이언트 프레임 저하 확인용 |
| `RxStateTime` | 선택. 마지막으로 받은 이동 상태의 서버 시각. `ACharacter::GetReplicatedServerLastTransformUpdateTimeStamp()`를 쓸 수 있는지 확인이 필요하다 (서버 월드 시각과 기준이 같은지) |

### 3.3 ground_truth.csv

서버에서 Impulse Volume 사건마다 한 줄씩 남긴다 (M2 3·4번).

| 열 | 내용 |
|---|---|
| `RunId`, `TrialId`, `ActorId` | 공통 키 |
| `EventId` | 사건 번호 |
| `Kind` | 사건 종류 (Side, Back, Forward, Up, GentleTurn 등) |
| `IsSharp` | 급변 사건이면 1, 오탐 확인용 대조군(정상 회전·감속)이면 0 |
| `VolumeOverlapTime` | Overlap 감지 시각 |
| `ImpulseCallTime` | `AddImpulse` 호출 시각 |
| `ActualVelocityChangeTime` | 이동 업데이트 후 실제 속도 변화가 관측된 시각 |
| `ImpulseX/Y/Z` | Impulse 벡터 |

### 3.4 detector.csv

서버에서 Detector가 판정할 때마다 한 줄씩 남긴다. 열은 M4 문서의 Detector 출력 형식을 그대로 따른다.

| 열 | 내용 |
|---|---|
| `RunId`, `TrialId`, `ActorId` | 공통 키 |
| `SequenceId`, `ObservedTime` | 판정 번호, 관측 시각 |
| `PosX/Y/Z`, `PostVelX/Y/Z`, `MovementMode` | 판정 시점의 위치, 사후 Velocity, 이동 모드 |
| `DeltaV`, `DeltaThetaDeg`, `Reason` | 판정 근거 |
| `RiskState` | `Enter` 또는 `Exit`. 분석에는 `Enter`만 쓴다 |

### 3.5 Recorder에 필요한 것

- 파일은 1초 정도마다 flush한다. 스크립트가 강제 종료하는 경우에도 마지막 1초 정도만 잃게 하기 위해서다.
- 측정 시간이 끝나면 서버가 스스로 종료하는 기능(예: `-SharpDurationSec=`)이 있으면 좋다. 지금은 스크립트가 창을 닫아서 종료시킨다.

## 4. 로그 변환 (Recorder가 생기기 전까지)

SharpEnemyCharacter(PR #3)는 측정값을 CSV가 아니라 로그로 남긴다. `convert_logs.py`가 이 로그를 3장의 CSV 형식으로 바꿔서 분석 스크립트에 넘길 수 있게 한다.

```bash
python Scripts/Analysis/convert_logs.py Saved/Experiments/<BatchId>
python Scripts/Analysis/sharp_metrics.py Saved/Experiments/<BatchId> --min-speed 100
```

| 로그 | 만드는 파일 |
|---|---|
| 서버 `[M2][CMC]` (0.5초마다, 큰 속도 변화가 있으면 추가) | `server_samples.csv` |
| 클라이언트 `[M3][ClientVisual]` (0.5초마다) | `client_<N>_samples.csv` |
| 클라이언트 `[M3][BaselineSummary]` (이동이 끝난 뒤 한 번) | `baseline_summary.csv` |

- Actor 구분은 `ExperimentActorId`를 쓰고, 0이면 Actor 이름을 쓴다.
- 화면 위치(Mesh)는 Mesh 위치에서 Capsule과 Mesh 사이의 평소 간격을 뺀 값이다. 평소 간격은 실행 전체의 중앙값으로 추정한다.
- 변환한 폴더에는 `converted_from_logs.txt`가 생기고, 분석 결과에 로그 변환 데이터라는 경고가 붙는다.
- Recorder가 만든 CSV가 이미 있으면 덮어쓰지 않는다 (`--force`로 덮어쓰기).
- 시각 기준은 `--clock`으로 고른다. `wall`(기본)은 로그 줄 앞의 기록 시각을, `server`는 로그의 `ServerTime`을 쓴다. 서버와 클라이언트가 같은 PC에서 돌면 모든 로그가 같은 시계로 찍히므로 `wall`이 실제 화면 어긋남을 그대로 보여준다. 클라이언트의 `ServerTime`은 서버 시각을 추정한 값이라 네트워크 지연만큼 같이 밀리고, 그러면 지연 때문에 생긴 오차가 상쇄돼서 보이지 않는다. 2026-10-12 실행에서 `server`로는 N0·N2 오차가 21cm로 같았지만, `wall`로는 11cm와 35cm로 지연(편도 50ms) 효과가 드러났다. 다른 PC끼리 돌릴 때만 `server`를 쓴다. 로그 시각을 읽을 수 없으면 자동으로 `server`를 쓴다.

로그가 0.5초 간격이라 직진 구간의 평균 오차 정도만 의미가 있다. Enemy가 10m를 이동하는 데 2초 정도라 이동 중 샘플이 몇 개 안 되고, 급변 순간의 최대 오차나 회복 시간은 잴 수 없다. 정지 구간을 빼려면 분석할 때 `--min-speed 100`처럼 서버 속력 기준을 준다.

## 5. 분석 스크립트

```bash
python Scripts/Analysis/sharp_metrics.py Saved/Experiments/<BatchId>
python Scripts/Analysis/sharp_metrics.py Saved/Experiments --out results --window 1.5
```

입력 폴더 아래에서 `server_samples.csv`가 있는 폴더를 모두 찾아 각각 Trial 하나로 분석한다.

### 5.1 계산 방식

오차는 클라이언트 샘플의 시각에 맞춰 서버 위치를 선형 보간한 뒤 두 위치 사이의 3D 거리로 계산한다.

사건 시각 t0는 `ActualVelocityChangeTime`을 쓰고, 이 값이 없으면 `ImpulseCallTime`을 쓴다. 사건 구간은 t0부터 `--window`(기본 1초)까지이며, 같은 Actor의 다음 사건이 먼저 오면 거기서 자른다.

첫 수신은 사건 이후의 상태가 클라이언트에 처음 도착한 시각이다. 오차를 이 시각 전과 후로 나눠서 따로 계산한다 (M3 5번).

회복 시간은 오차가 사건 전 0.5초 평균보다 10cm 이상 크지 않은 상태로 0.1초 유지되기 시작한 시점까지의 시간이다. 지연이 있으면 Simulated Proxy는 평소에도 속도 × 지연만큼 뒤처지기 때문에, 0cm를 기준으로 하면 회복을 판정할 수 없다.

보정 거리는 프레임 사이 Mesh 이동 중 실제 서버 이동으로 설명되지 않는 부분이다. 이 값이 `--snap-threshold`(기본 20cm)를 넘은 횟수를 스냅 횟수로 센다.

`--min-speed`를 주면 서버 속력이 그 값보다 낮은 샘플은 오차 계산에서 뺀다. 수신 횟수는 전체 구간 기준으로 센다.

시간 어긋남 진단은 클라이언트 위치가 서버 경로를 몇 초 늦게 따라가는지(`BestShiftSec`)를 찾고, 그만큼 당겨서 맞췄을 때 남는 오차(`CapMeanAfterShift`)를 함께 보여준다. 추정 지연이 넣은 지연보다 훨씬 크면 클라이언트의 서버 시각 추정이 어긋났거나 프레임이 떨어진 것이고, 당긴 뒤에도 오차가 크면 샘플 간격 문제나 실제 보정 오차다. 클라이언트 CSV에 `FrameMs`(프레임 시간)가 있으면 평균 25ms 또는 최대 200ms를 넘는 클라이언트에 경고를 붙인다.

### 5.2 결과 파일

결과는 `<입력 폴더>/_analysis/`에 저장된다.

| 파일 | 내용 |
|---|---|
| `report.md` | 모드·네트워크 조건별 요약표, Detector 평가표 |
| `summary.csv` | 모드와 네트워크 조건별 평균, 표준편차, 최댓값 |
| `events.csv` | 사건 1건 × 클라이언트 × Actor 단위 지표 |
| `trials.csv` | Trial × 클라이언트 × Actor 단위 전체 구간 지표 |
| `detector_trials.csv`, `detector_events.csv` | 탐지 지연, TP·FN·FP, Precision·Recall |

### 5.3 주요 지표

| 열 | 의미 | 관련 단계 |
|---|---|---|
| `CapPeak`, `MeshPeak` | 사건 구간 최대 오차 | M3 |
| `CapArea`, `MeshArea` | 오차의 시간 적분 (cm·s) | M3 |
| `CapTimeAbove` | 임계 오차(`--error-threshold`, 기본 50cm)를 넘은 시간 | M3 |
| `CapRecovery`, `MeshRecovery` | 회복 시간. 구간 안에 회복하지 못하면 빈 값 | M3 |
| `FirstUpdateDelay` | 사건 후 첫 상태가 도착하기까지의 시간 | M3, M5 |
| `CapPeakBeforeFirst`, `CapPeakAfterFirst` | 첫 수신 전과 후의 최대 오차 | M3 판단 게이트 |
| `MaxCorrection`, `SnapCount` | 한 번의 최대 보정 거리, 스냅 횟수 | M3 |
| `UpdatesPerSec` | 클라이언트가 초당 받은 이동 상태 횟수 | 비용 보조 지표 |
| `LatencyFromImpulse` | Impulse 호출부터 감지까지의 시간 | M4 |

### 5.4 가짜 데이터로 확인하기

```bash
python Scripts/Analysis/make_synthetic_run.py --out Saved/_synthetic
python Scripts/Analysis/sharp_metrics.py Saved/_synthetic
```

단순한 모델로 만든 데이터라 수치 자체는 의미가 없고, 분석 코드가 제대로 도는지 확인하는 용도다. 정상이라면 AlwaysHigh, Burst, SingleShot의 첫 수신 시간과 최대 오차가 Default보다 작게 나온다.

## 6. 한계와 남은 결정 사항

- 전송량(bytes/s)은 이 스크립트에서 계산하지 않는다. Networking Insights(`-NetTrace`)로 확인한다.
- 클라이언트의 `GetServerWorldTimeSeconds()`는 추정값이라 네트워크 지연만큼 같이 밀린다. 이 값으로 기록하면 지연 때문에 생긴 오차가 상쇄돼 보이지 않으므로, Recorder는 같은 PC의 공통 시계(예: `FPlatformTime::Seconds()` 또는 UTC 시각)로도 기록해야 한다. 3장의 시각 기준(`GetServerWorldTimeSeconds`)은 이에 맞춰 다시 정해야 한다.
- `RxStateTime`을 기록하지 않으면 첫 수신이 사건 전 상태일 수 있다. 이 경우 `events.csv`의 `FirstUpdateExact`가 0으로 표시된다.
- Metrics를 구현계획대로 C++(`SharpMovementMetrics`)로 옮길지, Python 후처리로 둘지 정해야 한다.
- CSV 열 이름은 Recorder 구현에 맞춰 확정해야 한다.
- 매 틱 기록하는 Recorder가 필요하다. 다른 파일을 건드리지 않도록 `Measurement/` 폴더에 World Subsystem으로 만드는 방안을 제안한다 (맵이나 Enemy를 수정하지 않고 모든 Enemy를 찾아서 기록).
- 서버가 초당 약 30번만 패킷을 보내고 있다 (`stat net`의 In Packets). `NetServerMaxTickRate`를 올리지 않으면 AlwaysHigh·Burst의 60Hz 설정이 실제로는 30Hz로 동작한다.
