# 본인 캐릭터 러버 밴딩 연구 — 1단계 Baseline

## 범위 및 보존 원칙
- 기존 `Lv_Rubberband`, BP, `Rubberband/`, `rb.*`, 원격 분석 스크립트 및 설정은 변경하지 않는다.
- 새 레벨 `/Game/YHJ/LocalRubberband/Lv_LocalRubberband`를 기존 레벨의 템플릿으로 생성한다. 기존 캐릭터와 GameMode BP를 복제한 후 신규 C++ 부모 및 기본 Pawn을 지정한다.
- 새 `LocalRubberband/` 클래스, `lrb.*` 명령어, `Saved/Experiments/LocalRubberband/`, 별도 분석 스크립트. 저수준 CSV writer(FSharpRecordingFiles) 및 공통 기반 캐릭터만 재사용한다.
- UE의 서버 권위 이동, 예측, 보정, 미승인 입력 재실행은 수정하지 않는다. 완화 알고리즘은 아직 추가하지 않는다.

## 설치 / 에디터 설정 (UE 5.8)
1. 압축을 새 폴더에 해제한다. 기존 작업 트리에 적용할 경우 새 파일만 복사한다. 원격 프로젝트 원본은 보존한다.
2. `GE_AIReplication.uproject` 프로젝트 파일을 생성한 뒤 IDE에서 `GE_AIReplicationEditor` Development Editor 빌드. 에디터를 재시작한다. 기존 프로젝트 요구 플러그인과 SDK는 동일하다.
3. 에디터에서 모든 기존 변경을 저장하고 PIE를 종료한다. Python Editor Script Plugin(기존 uproject에 활성화됨)과 Editor Scripting Utilities 사용 가능 여부를 확인한다. Output Log의 Python 모드에서 다음을 실행한다.
   ```python
   import unreal, os
   exec(compile(open(os.path.join(unreal.Paths.project_dir(), 'Scripts/SetupLocalRubberband.py'), encoding='utf-8').read(), 'SetupLocalRubberband.py', 'exec'))
   ```
   또는 File > Execute Python Script에서 `Scripts/SetupLocalRubberband.py` 선택. 스크립트는 새 경로에만 작성하며 기존 대상이 있으면 덮어쓰지 않는다.
4. 새 레벨을 열고 World Settings > GameMode Override가 `BP_LocalRubberbandGameMode`, Default Pawn이 `BP_LocalRubberbandCharacter`인지 확인한다. 새 캐릭터의 부모가 `ALocalRubberbandCharacter`, CMC가 `ULocalRubberbandMovementComponent`, InputScript가 `ULocalRubberbandInputScriptComponent`인지 확인한다. 기존 레벨과 지형, PlayerStart, Mesh, AnimBP, InputAction, Capsule 크기, CMC 기본값, SpringArm/Camera 설정을 비교한다. 기존 BP에서 입력 패턴과 핵심 이동 값을 복사하도록 스크립트를 작성했다. BP 재부모화 시 엔진이 일부 상속 컴포넌트 설정을 초기화할 수 있으므로 차이는 신규 BP에서만 복원한다.
5. **스크립트 실패 시** 원본을 수정하지 말고 Content Browser에서 기존 BP 두 개를 Duplicate하여 새 폴더에 이름을 지정하고, 새 캐릭터의 Class Settings > Parent Class를 LocalRubberbandCharacter로 변경하여 Compile. CMC/스크립트 타입이 잘못된 경우 신규 native class에서 새 BP를 생성해 기존 Mesh, AnimBP, 입력, 이동, 카메라 설정을 복사한다. 기존 레벨을 열고 File > Save Current Level As로 신규 경로에 저장한 뒤 신규 레벨에서만 GameMode Override 지정. 부분 생성된 신규 에셋은 검토 후 삭제하거나 수동 완료한다. 기존 맵/Blueprint에 대해서는 절대 재부모화나 저장 변경을 수행하지 않는다.
6. 이 단계는 PIE 전용이다. 패키징은 새 맵이 실제로 만들어진 후 별도로 MapsToCook에 추가한다(기존 Config는 변경하지 않음).

## 실험 실행
- PIE: Play As Client, Run Under One Process, Number of Players=2(기존 연구와 동일한 실행 부담을 유지하려는 경우). 두 번째 클라이언트는 입력하지 않는다. 최소 구성은 1명. Dedicated server가 같은 에디터 프로세스에 있어야 한다. 에디터 기본 네트워크 에뮬레이션을 비활성화하고, 기존 실험과 동일한 FPS/Tick/초기 시작 위치를 확인한다.
- 새 캐릭터가 정상 스폰되고 PlayerState가 동기화된 뒤, 움직일 클라이언트 창에서만 `~` 콘솔 명령을 실행한다. 키보드/마우스 입력을 추가하지 않는다. 기존 `rb.*` 명령은 사용하지 않는다.

| 조건 | 단방향 지연(ms) | 손실(%) |
|---|---:|---:|
| N0 | 0 | 0 |
| L100 | 100 | 0 |
| L200 | 200 | 0 |
| L300 | 300 | 0 |
| J50-150 | 50–150 | 0 |
| J100-300 | 100–300 | 0 |
| L200-P3 | 200 | 3 |

`In` = 서버 → 소유 클라이언트(기존 원격 실험과 수신 방향 동일), `Out` = 소유 클라이언트 → 서버, `Both` = 양방향 각각 적용(RTT에 더해지는 지연은 달라짐). UE 수신/송신 에뮬레이션 코드 경로가 다르므로 별도 조건으로 처리한다. 지정 수치는 설정값이며 실측 RTT/지터가 아니다. 손실 난수 시드는 통제하지 않는다.

### 순서
1. `lrb.Run N0 In 1` — 정상 기준선 및 로그 생성 확인.
2. `p.NetShowCorrections 1` (선택) — UE 보정 시각화.
3. `lrb.Run N0 In 1 200` — 1초 후 로컬 위치만 +X로 200cm 옮기는 합성 교정 실험. 50~600cm, N0 전용. 주입 이벤트를 별도로 기록하며 실제 교정 발생은 로그에서 확인한다. 입력 재전송 및 서버 수용 정책에 따라 보정이 없을 수도 있다. 합성 주입 자체는 러버 밴딩이 아니므로 해당 프레임을 성과로 세지 않는다.
4. `lrb.Batch N0,L100,L200,L300,J50-150,J100-300,L200-P3 In 3` — 기존과 같은 7조건, 기본 3루프. 우선 수신 장애만 비교.
5. 동일 목록을 `Out`, `Both`로 각각 실행한다. `lrb.Stop`은 해당 클라이언트에서 취소하고 원래 네트워크 설정 복원.
6. 반복 실험은 fresh PIE로 같은 시작 위치에서 수행하는 편이 가장 엄밀하다. Batch는 기존 ReturnHome 패턴을 사용하므로 각 조건 사이에 초기 위치를 강제 리셋하지 않는다(조건별 초기 위치 기록). 순서 효과를 피하려면 단일 `lrb.Run`으로 조건 순서를 교차시키고 반복한다.

이동 스크립트 기본 단계는 기존과 동일: 2초 직진, 1초 정지, 1.5초 90도, 1.5초 반전, 0.5초 정지, 0.4초 지그재그 6회, 1.2초 전방 점프, 1초 정지, 최대 8초 ReturnHome, 1초 Rest. ReturnHome은 도착하면 조기 종료한다. Batch는 1.5초 안정화, 0.5초 기록 선행, 1초 꼬리 기록, 1초 조건 간 휴지를 둔다.

## 데이터 / 분석
`Saved/Experiments/LocalRubberband/<BatchId>/<Condition>/` 아래 조건 메타데이터, local/server 샘플, local 교정 콜백, local replay 그룹, 입력 단계 로그, 종료 상태가 생성된다. PlayerId로 동일 캐릭터만 추적하며 다른 캐릭터의 분석은 하지 않는다.
- `local_samples.csv`: Capsule, 원본 Mesh 위치, 기본 Mesh 오프셋 제거 Visual 위치, 속도, 이동 모드, 실제 PlayerCameraManager 위치·회전(월드 tick 시점 캐시, 렌더링 정확한 프레임은 아님), Frame/HostTime/추정 ServerTime.
- `local_corrections.csv`: native OnClientCorrectionReceived 시점의 위치와 서버 교정 목표, move timestamp, base-relative 표시, 서버 모드, saved move 수. 서버 목표는 과거 입력 시간 또는 base-relative 좌표일 수 있어 현재 위치와 단순 거리 비교하지 않는다.
- `local_replays.csv`: 첫 콜백 전 위치, replay 직전 위치, replay 이후 위치, 실제 순위치 변화 `NetShiftCm`, 연결되는 콜백 ID 구간. 여러 교정이 한 replay에 합쳐질 수 있다. 이 값은 Capsule의 변화이며 화면상 실제 후퇴 거리와 동일하지 않다.
- `local_script.csv`: 입력 단계와 합성 오차 주입 시점/성공 여부. 합성 조건은 별도 이름으로 저장한다.
- `*_status.txt`: Completed + WriterHealthy=1 확인. 미완료 또는 샘플 부족은 제외한다. `local_replays.csv`가 빈 경우에도 교정 콜백 자체가 없을 수 있으므로 오류로 단정하지 않는다.

```bash
python Scripts/Analysis/local_rubberband_metrics.py Saved/Experiments/LocalRubberband --output Saved/Experiments/LocalRubberband/local_summary.csv
```
분석은 교정 콜백 수, replay 그룹 수, 미연결 콜백 수, 순위치 변화 P95/max, Capsule/Visual/Camera의 인접 샘플 이동 P95/max를 계산한다. 인접 샘플 이동은 정상 이동과 카메라 조작까지 포함하며 곧바로 러버 밴딩 횟수로 해석할 수 없다. 실제 교정 이벤트 주변과 합성 주입 시점을 분리한 영상/프레임 분석은 후속 단계이다. `HostTime`은 동일 PC의 단조 시계, `ServerTime`은 추정 시간이다. 서로 다른 시점의 서버/클라 위치를 단순 비교하지 않는다.

## 검증 상태 / 다음 단계
- 생성물은 소스와 에디터 생성 스크립트이다. UE 5.8 빌드, 에디터 에셋 생성, 실제 네트워크 주입 및 서버 교정, 화면 프레임 재현은 이 환경에서 검증할 수 없었다. 새 `.umap`/`.uasset` 바이너리는 에디터에서 생성해야 한다.
- 정상 조건에서 로그와 PlayerId, 스크립트 단계, 카메라 유효성 확인 → 합성 오차에서 native 콜백과 replay 기록이 나타나는지 확인 → 실제 조건 배치 실행 → 보정 시점 전후 영상/시각적 지표 검증 → 완화 설계.
- 기준선에서 러버 밴딩이 없으면 해당 조건은 정상적인 결과다. 특정 조건에서 보정이 없다면 지연만으로는 로컬 예측 오류를 유발하지 않았을 수 있다. 필요한 경우 이전 RecoveryLab의 짧은 연속 손실을 신규 연구에 별도로 추가한다. 이번 단계에서는 추가 완화 알고리즘 또는 연속 손실 실험을 구현하지 않았다.
