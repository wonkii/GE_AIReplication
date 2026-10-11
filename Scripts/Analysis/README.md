# 실험 실행·분석 스크립트

> 상태: 임시안. 관련 단계: M3(Metrics), M4(탐지 평가), M7·M8(네트워크 조건, 반복 실행, 모드 비교) · 작성: 염형주

실험을 같은 조건으로 반복 실행하고, Recorder가 남긴 CSV에서 오차와 회복 시간 같은 지표를 계산하는 스크립트다. CSV 형식은 아직 Recorder가 없어서 임시로 정했고, Recorder 구현에 맞춰 고친다.

## 1. 파일 구성

| 파일 | 역할 |
|---|---|
| `Scripts/RunSharpTrial.ps1` | 서버 1개와 클라이언트 여러 개 실행, 네트워크 조건 적용, 반복 실행, 로그 검사 |
| `Scripts/Analysis/sharp_metrics.py` | Recorder CSV 분석, 모드·네트워크 조건별 요약표 생성 |
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
3. 접속 대기, 워밍업, 측정 시간(기본 10초, 5초, 60초)만큼 기다린다.
4. 클라이언트, 서버 순서로 창을 닫는다. 15초 안에 꺼지지 않으면 강제로 종료한다.
5. 서버와 클라이언트 로그의 RunId, TrialId, Mode가 같은지 검사하고 결과를 `run_meta.json`의 `Valid`에 남긴다.

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

## 4. 분석 스크립트

```bash
python Scripts/Analysis/sharp_metrics.py Saved/Experiments/<BatchId>
python Scripts/Analysis/sharp_metrics.py Saved/Experiments --out results --window 1.5
```

입력 폴더 아래에서 `server_samples.csv`가 있는 폴더를 모두 찾아 각각 Trial 하나로 분석한다.

### 4.1 계산 방식

오차는 클라이언트 샘플의 시각에 맞춰 서버 위치를 선형 보간한 뒤 두 위치 사이의 3D 거리로 계산한다.

사건 시각 t0는 `ActualVelocityChangeTime`을 쓰고, 이 값이 없으면 `ImpulseCallTime`을 쓴다. 사건 구간은 t0부터 `--window`(기본 1초)까지이며, 같은 Actor의 다음 사건이 먼저 오면 거기서 자른다.

첫 수신은 사건 이후의 상태가 클라이언트에 처음 도착한 시각이다. 오차를 이 시각 전과 후로 나눠서 따로 계산한다 (M3 5번).

회복 시간은 오차가 사건 전 0.5초 평균보다 10cm 이상 크지 않은 상태로 0.1초 유지되기 시작한 시점까지의 시간이다. 지연이 있으면 Simulated Proxy는 평소에도 속도 × 지연만큼 뒤처지기 때문에, 0cm를 기준으로 하면 회복을 판정할 수 없다.

보정 거리는 프레임 사이 Mesh 이동 중 실제 서버 이동으로 설명되지 않는 부분이다. 이 값이 `--snap-threshold`(기본 20cm)를 넘은 횟수를 스냅 횟수로 센다.

### 4.2 결과 파일

결과는 `<입력 폴더>/_analysis/`에 저장된다.

| 파일 | 내용 |
|---|---|
| `report.md` | 모드·네트워크 조건별 요약표, Detector 평가표 |
| `summary.csv` | 모드와 네트워크 조건별 평균, 표준편차, 최댓값 |
| `events.csv` | 사건 1건 × 클라이언트 × Actor 단위 지표 |
| `trials.csv` | Trial × 클라이언트 × Actor 단위 전체 구간 지표 |
| `detector_trials.csv`, `detector_events.csv` | 탐지 지연, TP·FN·FP, Precision·Recall |

### 4.3 주요 지표

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

### 4.4 가짜 데이터로 확인하기

```bash
python Scripts/Analysis/make_synthetic_run.py --out Saved/_synthetic
python Scripts/Analysis/sharp_metrics.py Saved/_synthetic
```

단순한 모델로 만든 데이터라 수치 자체는 의미가 없고, 분석 코드가 제대로 도는지 확인하는 용도다. 정상이라면 AlwaysHigh, Burst, SingleShot의 첫 수신 시간과 최대 오차가 Default보다 작게 나온다.

## 5. 한계와 남은 결정 사항

- 전송량(bytes/s)은 이 스크립트에서 계산하지 않는다. Networking Insights(`-NetTrace`)로 확인한다.
- 클라이언트의 `GetServerWorldTimeSeconds()`는 추정값이라 지연이 크면 시각이 어긋나고, 그만큼(속도 × 시각 차이) 오차 값에 섞인다.
- `RxStateTime`을 기록하지 않으면 첫 수신이 사건 전 상태일 수 있다. 이 경우 `events.csv`의 `FirstUpdateExact`가 0으로 표시된다.
- Metrics를 구현계획대로 C++(`SharpMovementMetrics`)로 옮길지, Python 후처리로 둘지 정해야 한다.
- CSV 열 이름은 Recorder 구현에 맞춰 확정해야 한다.
