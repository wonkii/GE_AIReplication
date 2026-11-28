# LocalRubberband — 원클릭 Baseline 실험

## 적용
현재 AIReplication.zip의 SCB 구조를 기준으로 한 신규 파일 3개만 제공한다. 프로젝트 루트에 압축 해제한다. 기존 C++ 소스, 맵, BP, 원격 실험, 기존 분석 스크립트는 바꾸지 않는다. `SetupLocalRubberband.py`를 다시 실행하지 않는다. 이미 빌드되어 있는 LocalRubberband C++ 클래스 및 Stage 2의 lrb.Run 명령이 필요하다. 이번 패치 자체에는 재빌드가 필요 없다.

## 처음 한 번 확인
- 에디터에서 Save All을 하고 PIE를 종료한다. Python Editor Script Plugin을 활성화한다. EditorLevelLibrary가 없으면 Editor Scripting Utilities를 활성화한 후 재시작한다.
- Play 옵션: Play As Client, Number of Players 2, Run Under One Process ON, Enable Network Emulation OFF, Spawn Player At Default Player Start. 기존과 동일한 FPS/서버 Tick 설정 유지. 자동 실험 동안 입력 및 Play 설정을 변경하지 않는다.
- 이 스크립트는 기존 PIE 설정을 사용한다. Python에서 설정 접근이 제한될 수 있으므로 위 옵션을 사전에 설정한다. 실행 시 실제 월드 구성(서버 1+클라이언트 2)을 검사하고 불일치 시 중단한다. 네트워크 에뮬레이션 설정은 읽을 수 있을 때만 검사한다.

## 실행
UE 에디터의 일반 편집 상태에서 File > Execute Python Script로 `Scripts/RunLocalRubberband.py`를 선택하거나 Output Log의 Cmd 모드에서:

```
py "C:/Unreal Projects/GE_AIReplication/Scripts/RunLocalRubberband.py"
```

자동으로 기존 `/Game/SCB/LocalRubberband/Lv_LocalRubberband`를 열고, Clean BP/CMC/InputScript 및 GameMode Default Pawn을 검사한다. 맵의 실제 스폰도 검사한다. 기존 에셋을 생성/복제/저장하지 않는다. 맵 전환 전에 미저장 파일이 있으면 중단한다. Slate post-tick 콜백으로 진행하므로 에디터를 막는 sleep 루프는 사용하지 않는다. 각 조건은 새 PIE 세션으로 실행하며 첫 번째 클라이언트 월드(이름 순 정렬)를 자동 선택하고 `lrb.Run`을 호출한다. 두 번째 클라이언트는 자동 입력하지 않는다. 각 조건의 새 로그 폴더 및 양측 완료 파일을 확인하고 종료 후 다음 세션을 시작한다. 맵의 PlayerStart 배정은 엔진에 맡기며 같은 초기 좌표를 강제로 보장하지 않는다. `InitialClientLocation`을 결과에 기록한다.

## 기본 계획 (15개 PIE 실행)
- Smoke: N0 In 1회(1루프)
- Synthetic: N0 In 1 200 (인위적인 200cm 위치 오차; 별도 분류)
- Common: N0,L100,L200,L300,J50-150,J100-300,L200-P3 각각 In 3루프 (7개)
- Burst: B500,B1000,B2000 각각 Out/Both 1루프 (6개, 별도 분류)

`RunLocalRubberband.py` 상단의 `INDEPENDENT_REPEATS`(기본 1), `COMMON_LOOPS`(기본 3), `INCLUDE_BURSTS`(기본 True)로 조절한다. `INDEPENDENT_REPEATS`는 Common과 Burst에만 적용한다. 반복 1회는 기능 점검이며 통계적 성능 검증이 아니다. 각 실험에서 보정이 없더라도 정상 완료로 취급한다. 실험을 강제로 성공 처리하지 않는다. 보정 콜백은 시각적 러버 밴딩 횟수가 아니다. 실제 패킷 도착 패턴/카메라 화면 녹화는 검증하지 않는다.

## 출력
- 원본: `Saved/Experiments/LocalRubberband/<기존 C++ BatchId>/<condition>`
- 분석: `Saved/LocalRubberbandAnalysis/Auto/<timestamp_id>/`
  - `plan.json`: 실행 계획
  - `runs.csv`: 기존 Stage 2 분석 + 단계/반복/초기 위치/플레이어 정보
  - `events.csv`: 보정 이벤트별 분석
  - `status.json`: COMPLETED/ABORTED/INCOMPLETE, 완료 수, 보정 관측 실행 수 및 원본 경로
- 같은 Auto 폴더 아래 `<timestamp_id>.zip`: 이번 실행의 분석 및 원본 로그만 수집한 업로드용 파일.

기존 원격 데이터와 이전 Local 실행은 분석 대상에 포함하지 않는다. 분석 시 Common/Synthetic/Burst를 구분한다. 에디터 종료 등으로 스크립트 자체가 종료되면 ZIP이 없을 수 있으나 이미 저장된 원본/중간 결과는 유지된다.

## 중단 및 오류
PIE를 Stop하면 실행 도중 자동화가 중단된다. 세션 사이에도 취소하려면 에디터 Output Log Cmd에서:
```
py import builtins; builtins._local_rubberband_one_click_runner.cancel_requested = True
```
자동화는 자신이 시작한 세션만 종료하며, 가능한 경우 `lrb.Stop`으로 네트워크 설정을 복구한다. 오류/타임아웃/미완료 로그가 있으면 다음 조건을 무리하게 시작하지 않는다. 실제 완료 상태와 에러를 Output Log에서 `[LRB Auto]`로 검색한다. 분석 모듈이 정상 완료로 판단하지 못한 경우 ABORTED로 종료하며 이전 결과를 보존한다. 자동 PIE 시작 및 대상 월드 명령 전달은 UE 5.8 API 사용을 전제로 한다. UE 실행 검증은 수행하지 않았으며, API가 다르게 작동하는 경우 로그를 통해 수정이 필요할 수 있다.

## 검증
Python 문법 검사 및 기존 Stage 2 테스트 6개 + 신규 모의 자동화 테스트 4개를 실행했다. 실제 에디터, C++ 빌드, PIE 및 시각적 러버 밴딩 재현은 미검증이다. 신규 테스트는 프로젝트 루트에서 다음과 같이 실행할 수 있다.
```
python -m unittest discover -s Scripts/Analysis -p 'test_local_rubberband_*.py'
```
