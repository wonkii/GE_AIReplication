# 다중 Enemy 급변 실험 기능

현재 `integration/jwj`의 경로·실행 구조에 Impulse, Detector, Recorder, Visual Predictor, 반복 실행과 보고서를 연결한 사용 안내다.

실제 `Lv_Jang` 기록 결과, Editor에서 기록하는 순서와 CSV 열 해석은 [기록파일 실행과 해석](SharpMovement_기록파일_실행과해석.md)에 정리했다.

## 1. 레벨 설정

기존 설정을 유지한다.

1. Manager는 레벨에 하나만 배치한다.
2. `Manager.Paths`에 각 Spline을 등록한다.
3. 각 Path의 `Assigned Enemy`, 고유한 `PathId`를 지정한다.
4. Enemy마다 양수이며 고유한 `ExperimentActorId`를 지정한다.
5. `Manager.Settings`에 기존 `SharpExperimentSettings` Data Asset을 연결한다.
6. Enemy의 AI Controller는 `SharpEnemyAIController`, Auto Possess AI는 `Placed in World or Spawned`를 사용한다.

Enemy의 새 Detector, Recorder, VisualPredictor는 기본 컴포넌트로 생성된다. 별도로 추가하거나 Blueprint에서 Tick을 연결할 필요가 없다. CharacterMovement도 계측 가능한 하위 클래스로 기본 생성되며 엔진 이동 처리는 유지한다.

## 2. Impulse 구역 추가

1. Place Actors에서 **Sharp Impulse Volume**을 찾아 이동 경로 중간에 배치한다. 이 네이티브 클래스로 Blueprint를 만들어 사용해도 된다.
2. `VolumeId`를 `Impulse01`처럼 고유하게 지정한다.
3. `TriggerBox` 크기를 조절해 Enemy의 Capsule이 실제로 통과하게 한다. 시작 위치의 Capsule과 겹치면 준비가 거부된다.
4. 필요하면 `Manager`를 지정한다. 비워 두면 현재 월드의 유효하게 준비된 Enemy를 대상으로 한다.
5. Enemy Capsule의 Generate Overlap Events를 켠다. Volume은 Query Only, Pawn Overlap을 유지한다.

하나의 Volume을 여러 Enemy가 지나도 발동 여부와 대기 시간은 Enemy별로 관리한다.

| 설정 | 의미 |
|---|---|
| Use Experiment Impulse | 켜면 준비 당시 Data Asset의 `Impulse` 사용. 끄면 Volume의 `ImpulseVector` 사용 |
| Direction Space / World | 월드 축 기준 |
| Direction Space / Volume Local | Volume 회전 기준 |
| Direction Space / Path Tangent | 해당 Enemy의 경로 진행·오른쪽·위 방향 기준. 왕복 중 반대 진행도 반영 |
| Velocity Change | 질량을 적용하지 않고 속도 변화로 충격 적용 |
| Once Per Execution | 해당 Enemy의 준비/리셋 Generation마다 한 번 |
| Once Per Entry | 나갔다가 다시 들어올 때마다 발동. Rearm Delay 적용 |
| Follow / Continue | 경로 이동 입력 계속 |
| Follow / Suspend Input | 지정 시간 동안 입력만 중단하고 같은 실행 구간에서 자동 재개 |
| Is Sharp Event | Detector 평가용 시나리오 정답 라벨. 작은 대조 자극은 끄고 사용 |

Impulse는 서버에서만 적용한다. 클라이언트에 충격을 다시 적용하지 않는다. 수동 정지·리셋·실패·수동 중단은 충격 재개 타이머를 취소한다. 실행 중 Volume 형상이나 ID를 바꾸면 재진입 시 실패 처리하고 리셋을 요구한다. Volume 설정값은 준비 시 복사한다.

## 3. 네트워크 정책 설정

Data Asset의 **Use Controlled Low Rate**를 켜면 기본 Actor 설정을 보관하고 실험 동안 Normal 빈도를 적용한다. Adaptive는 High를 최대 빈도, Normal을 최소 빈도로 사용한다. 정지/완료/해제 시 원래 값으로 복원한다.

기존 동작을 유지하기 위해 기본값은 꺼져 있다. Actor 기본 빈도가 100Hz인데 High가 60Hz라면 Burst는 빈도를 낮추는 설정이므로 시작이 거부된다. 비교 실험에서는 예를 들어 Controlled Low Rate를 켜고 Normal=10, High=60으로 설정한다. 표시된 빈도는 실제 패킷 전송 횟수가 아니다.

| 모드 | 실제 동작 |
|---|---|
| Default | 기본 CMC 복제. Controlled Low Rate를 선택했다면 그 공통 기준 사용 |
| Adaptive | 엔진 적응형 업데이트 빈도 |
| AlwaysHigh | NetUpdateFrequency와 MinNetUpdateFrequency를 High로 설정 |
| SingleShot | 실제 Detector 사건마다 ForceNetUpdate 요청 |
| Burst | 사건마다 High 빈도와 즉시 업데이트 요청. Burst Seconds 이후 복원, 연속 사건은 기간 연장 |
| EventOnly | 실제 이동 결과 Snapshot 전송, 기본 화면 보정 사용 |
| EventPredictor | EventOnly와 같은 서버 Snapshot을 사용하고 클라이언트 Mesh 예측 적용 |

Adaptive 단독 실행은 `net.UseAdaptiveNetUpdateFrequency=1`이 필요하다. 기존 `RunSharpTrial.ps1`은 이를 설정한다. 같은 프로세스에서 Manager의 Mode Sequence를 사용하면 `Manage Adaptive For Mode Sequence`가 모드에 따라 전역 값을 설정하고 종료 시 값과 우선순위를 복원한다. 같은 프로세스의 다른 실험 월드와 전역 설정을 공유하므로 비교 실험은 서버 프로세스 하나당 실험 월드 하나로 실행한다.

수동 실행의 Configured Mode는 PIE/게임 시작 전에 선택한다. 실행 중 바꿀 모드 순서는 Mode Sequence를 사용한다.

모든 모드에서 서버 이동 시각 복제를 활성화한다. 이 계측에 필요한 공통 복제 비용은 엔진 기본 Character와 차이가 있을 수 있다.

## 4. Detector와 화면 예측

Detector는 서버 CMC 이동 결과의 속도·방향·선택적 가속도/MovementMode 변화를 비교한다. Volume 진입 정보를 판정에 사용하지 않는다. 해제 임계값, 조용한 시간, 최소 Risk 시간, 재발동 간격, 최대 샘플 간격을 설정할 수 있다. 임계값과 관련 설정은 실험 준비 당시 값으로 고정한다.

Visual Predictor는 Simulated Proxy에서만 Mesh 위치를 바꾼다. Capsule 위치, CMC 속도, 서버 권한 상태를 수정하지 않는다.

- 지상에서는 일정 속도 모델을 사용한다.
- 공중 모델을 켜면 서버 중력과 속도로 짧은 낙하/상승을 예측한다. 새 설정의 기본값은 켜짐이다.
- 최신 기본 이동, MovementMode 변경, 예측 시간 만료, 거리 제한, 충돌에 따라 예측을 끝낸다.
- Prediction Blend Seconds로 예측 진입을, Correction Seconds로 기본 위치 복귀를 조절한다.
- 예측/복귀 중에는 기본 Mesh smoothing과 중복 적용되지 않게 조정한다.
- EventOnly Uses Custom Correction은 선택 기능이다. 기본값은 꺼져 있어 일반 EventOnly는 엔진 smoothing을 유지한다.

예측 효과는 지연, 충돌, 최신 기본 이동 도착 시각, GameState의 서버 시계 추정에 따라 달라진다. 이 구현만으로 개선 효과가 보장되지는 않으며 실제 기록으로 확인해야 한다.

## 5. 반복 실행

Manager의 기존 준비·시작·정지·리셋 버튼과 Path의 개별 버튼을 사용한다. 그룹 자동 반복은 다음 설정을 사용한다.

| 설정 | 의미 |
|---|---|
| Auto Repeat | 그룹 완료 뒤 다음 Trial 준비 및 시작 |
| Repeat Count | 모드마다 실행할 횟수. 첫 실행 포함 |
| Mode Sequence | 실행할 모드 순서. 비어 있으면 현재 모드 반복 |
| Repeat Delay Seconds | 종료 후 관찰이 끝난 뒤 추가 대기 |
| Trial Duration Seconds | 실행 최대 시간. 0이면 모든 경로의 자연 완료를 기다림 |

`PingPong`은 계속 왕복하므로 자동 반복 실험에는 Trial Duration Seconds를 지정한다. Once 경로는 지정 시간보다 일찍 끝날 수 있다. 그룹의 한 Enemy가 실패하면 남은 Enemy를 정지하고 자동 반복을 중단한다.

개별 버튼이나 수동 중단은 그룹 자동 제어를 취소한다. 다시 그룹 Start를 누르면 새 반복 순서를 시작한다. 수동 중단한 구간은 완료된 Trial로 공정 비교에 포함되지 않는다.

`RunSharpTrial.ps1`은 `SharpDurationSec=MeasureSec`를 넘기고 종료 후 기록 저장 대기 시간을 둔다. 프로세스별 반복 실행 방식은 그대로 유지한다.

## 6. 기록 파일

기본 출력은 `Saved/SharpExperiments/Native`다. `-SharpOutDir=<directory>`를 넘기면 그 아래 `Native`에 기록한다. `-SharpClientId=1` 같은 ID를 지정할 수 있다.

```text
Native/<RunId와 해시>/Trial_<id>/
  Actor_<id>/Generation_<id>/Segment_<id>/<Server 또는 Client, PID, World>/
    manifest.json
    server_samples.csv              # 서버 이동 결과마다
    client_<id>_samples.csv          # 클라이언트 프레임마다
    ground_truth.csv                # 충격 호출과 실제 관찰, 정답 라벨
    detector_samples.csv            # 판정 관찰과 계산 비용
    detector.csv                    # 사건과 Risk 해제
    diagnostics.csv                 # 제어, 수신, 보정, 예측 종료 등
  _connections/<Role>/Part_<id>/connections.csv
```

해당 역할에서 사건이 없으면 일부 파일은 생성되지 않는다. 파일은 역할과 Enemy/Generation/Segment별로 분리해 여러 기록자가 같은 CSV에 쓰지 않는다. 동일 실행 경로에 기존 파일이 있으면 이어 붙이지 않고 오류를 기록한다.

서버 샘플은 실제 이동 업데이트 이후, 클라이언트 샘플은 CMC와 Predictor 이후에 기록한다. Mesh 오차용 좌표는 평소 Capsule-Mesh 간격을 제거하고 원본 Mesh 위치도 함께 보관한다. 기록은 버퍼 한도 또는 Flush Interval에 맞춰 저장하며 완료 뒤 Post Trial Observation Seconds 동안 계속된다.

manifest에는 준비 당시 설정, 경로, Volume, CMC 설정과 실행 상태를 남긴다. Impulse 호출은 실제 이동 변화 시각과 구분한다. 변화가 관찰되지 않거나 취소된 사건은 상태를 명시하고 급변 품질 평가에서 제외한다.

CMC가 누적 충격을 소비하는 `AppliedTime`도 기록한다. 이동 도중 발생한 Overlap에서 충격을 예약했을 때, 적용 전의 같은 이동 업데이트를 충격 결과로 오인하지 않도록 한다. 이 시점은 Detector의 판정 입력으로 사용하지 않는다.

네트워크 비용은 Manager에서 한 번 기록한다. 엔진 연결 전체의 송수신 바이트·패킷 카운터이며 Enemy별 비용이나 NIC 바이트로 해석하면 안 된다. Enemy별 전송량은 기존 Networking Insights Trace 도구로 별도 측정한다.

Draw Debug를 켜면 Capsule·속도·Impulse 화살표와 Enemy 식별 정보를 표시한다. Visual Logger 옵션은 동시에 기록하는 Enemy 수를 관리해 기록 상태를 켜고 마지막 기록 종료 시 원래 상태로 돌린다.

## 7. 분석과 보고서

기존 Python 표준 라이브러리 분석 도구를 사용한다.

```powershell
python Scripts/Analysis/sharp_metrics.py Saved/SharpExperiments --native --clock wall
```

외부 실행 스크립트 결과는 해당 Batch 디렉터리를 입력한다.

```powershell
python Scripts/Analysis/sharp_metrics.py Saved/Experiments/<BatchId> --native --out Saved/Reports/<BatchId>
```

같은 PC의 프로세스는 `wall`로 정렬한다. 별도 PC는 동기화된 시계가 확보되어 있지 않다면 `server`를 사용하고 GameState 추정 시계의 영향을 고려한다.

Standalone은 서버 역할 샘플만 만든다. 화면 오차 분석에는 Simulated Proxy를 갖는 별도 클라이언트가 필요하다.

`--native`는 원본 파일을 바꾸지 않고 `_collected`에 기존 CSV 양식으로 모은다. 기존 로그 분석은 `--native` 없이 계속 사용할 수 있다. 원본 Recorder 스키마를 새 분석 코드와 별개로 유지하지 않는다.

주요 결과:

- `report.html`, `report.json`, `report.md`: 전체 결과. HTML 표 검색 가능.
- `errors.csv`, `trials.csv`, `events.csv`: 실행 구간별 위치 오차·복구·수신 지연.
- `detector_trials.csv`, `detector_events.csv`, `detector_costs.csv`: 정확도·감지 지연·Classifier 계산 비용.
- `presentation_diagnostics.csv`: Snapshot 누락 번호와 예측 종료 이유, 실제 SmoothCorrection 통계.
- `connection_costs.csv`: 연결 전체 비용.
- `recording_quality.csv`: 완료 여부, 기록 범위와 비교 제외 이유.
- `comparison.csv`: 같은 시나리오와 시계의 실행 비교. 비용 차이가 허용 범위 이내인지 표시.

native 기본 최대 서버 보간 간격은 0.1초다. Generation/Segment가 다르거나 샘플 공백이 큰 구간을 이어서 보간하지 않는다. 기록 범위가 기본 80% 미만이거나 완료되지 않은 실행, 측정 비용이 없는 실행은 공정 비교에서 제외한다. `WithinBudget=false`는 같은 비용에서의 개선 결과로 해석하지 않는다.

경로 외의 바닥·벽 등 레벨 충돌 환경을 바꾸면 Data Asset의 **Scenario Revision**도 올린다. 보고서는 전체 레벨의 모든 충돌 형상 변경을 자동으로 판별하지 않는다.

Detector Precision/FP는 Ground Truth에 기록된 시나리오 라벨과의 대응 기준이다. 착지·충돌 등 라벨 없는 실제 급변도 `FP_Other/UnmatchedDetections`에 포함될 수 있으므로 판정 근거를 함께 확인한다.

Editor에서는 **Sharp Movement Report** Actor를 배치하고 Generate Report / Compare Reports / Open Report 버튼을 사용할 수 있다. PythonExecutable에 Python 실행 파일 경로를 지정한다. 입력 디렉터리가 비어 있으면 기본 기록 폴더를 사용한다. 이 버튼은 기존 Python 분석기를 호출하므로 Python 3.10 이상이 필요하다.

## 8. 코드 책임

| 영역 | 책임 |
|---|---|
| AIReplicationCore/Detector | UObject·Overlap·복제에 의존하지 않는 판정 계산 |
| Scenario/SharpImpulseVolume | 공간 진입, Enemy별 발동 조건, 실제 서버 충격 |
| Detector/SharpMovementDetectorComponent | 실제 CMC 샘플 수집, 사건 번호 발급, 정책 연결 |
| Experiment/SharpEnemyExperimentComponent | 실행 수명, 설정 고정, Impulse 재개 타이머 취소 |
| Net | 업데이트 정책과 실행 구간이 검증된 Snapshot 전달 |
| Prediction | 클라이언트 Mesh 예측·복귀와 소유권 복원 |
| Movement/SharpCharacterMovementComponent | 엔진 이동 유지, 실제 보정 계측과 smoothing 조정 |
| Measurement | 샘플·사건·진단·설정 파일과 연결 비용 기록 |
| ManagerRepeat | 그룹 완료·시간 제한·반복 순서·Adaptive 설정 수명 |
| Scripts/Analysis | 양식 수집, 계산, 품질 확인, 비용 비교와 보고서 |

자동화 테스트는 추가하지 않았다. 빌드와 정적 연결 검토는 실제 네트워크 환경에서의 개선 효과 검증을 대신하지 않는다.

## 9. 이번 구현의 확인 범위

- Editor·게임 Win64 Development 빌드 통과.
- 기존 `Lv_Jang`을 저장 없이 열어 세 Enemy의 새 기본 컴포넌트와 CharacterMovement 클래스, Path01/02/03 연결 확인.
- 기존 레벨의 서버 1개·클라이언트 1개 실행에서 세 Enemy 시작·완료, CSV와 HTML/JSON 보고서 생성 확인.
- 별도 임시 레벨에서 세 Enemy가 각자 충격 구역을 통과하도록 실행. EventOnly와 EventPredictor 두 Trial의 충격 총 6건을 모두 기록·감지하고 Snapshot 수신 확인.
- EventPredictor Trial의 세 Enemy에서 PredictionStart와 NewServerMovement에 따른 PredictionStop 확인.
- 충격 입력 중단의 자동 재개가 Segment를 유지하고, 모드 전환 시 TrialId와 Generation이 증가하는 것 확인.
- 임시 레벨과 데이터 에셋은 Content에서 정리하고 실행 기록만 Saved에 보관.

위 실행은 같은 PC의 짧은 기능 확인이며 렌더링 없이 수행했다. 여러 지연·손실 조건에서의 화면 품질 개선이나 통계적 성능 우위를 입증하는 결과는 아니다. 자동화 테스트 코드는 추가하거나 실행하지 않았다.
