# 10. 다구간 Warp 실행과 테스트 설정

2026-11-09 구현. 자연스러움은 사용자가 직접 평가한다. 서버 AI 피격용 구현이며, 소유 플레이어의 prediction/replay는 미지원으로 명시적 fallback한다. 이번 작업에서 실제 멀티플레이 실행 요청은 승인되지 않았으므로 복제 경로의 실환경 검증은 미완료다.

## 가장 빠른 수동 테스트

수치·충돌·복제 동작의 기술 검증과 사용자가 화면으로 판단하는 자연스러움 평가는 별도다. 아래 수동 명령이나 48회 자동 비교를 사용자가 반드시 다시 수행해야 하는 것은 아니다. 직접 모드를 바꾸거나 결과를 재현하고 싶을 때 사용한다.

**이 문서의 모든 콘솔 명령은 한 줄이 명령 하나다. 한 줄을 붙여넣고 Enter를 누른 뒤 다음 줄을 입력한다. 코드 블록 전체를 여러 줄로 한꺼번에 붙여넣지 않는다. 긴 명령이 화면에서 줄바꿈되어 보여도 실제로는 한 줄 전체를 복사한다. PowerShell 예제도 실행할 항목 한 줄씩 입력하며 `#` 주석은 입력하지 않아도 된다.**

1. UE 5.8 Editor를 재시작해 새 C++ 타입을 반영하고 `Lv_Jang`을 연다.
2. 가장 간단한 화면 확인은 PIE 1명으로 가능하다. Client가 보는 결과도 함께 확인하려면 아래의 Listen Server + Client 구성을 사용한다. 플레이어와 가까운 적 뒤에 정적 벽을 두고, 적과 플레이어가 지상에 있게 한다.
3. Play를 누르고 **게임 화면 안을 클릭**한다. **Esc 바로 아래·숫자 1 왼쪽의 백틱/틸드 키**(백틱과 물결표가 표시된 키)를 눌러 게임 콘솔을 연다. 보통 Shift 없이 그 키를 누르면 된다. 열리지 않으면 영문 입력 상태로 바꾸고 `Edit > Project Settings > Engine > Input > Console > Console Keys`를 확인한다. 이 프로젝트의 `Config/DefaultInput.ini` 기본 키는 `Tilde`다. 같은 키를 반복해 누르면 콘솔이 확장되거나 닫힌다.
4. **Standalone 또는 Listen Server 게임 창의 콘솔**에서 다음을 위에서부터 **한 줄씩 입력하고 매번 Enter**를 누른다. 현재 `Mode`와 `Compare` 구현은 명시적인 authority 게임 월드를 요구하므로 Editor Output Log의 일반 Editor 월드나 Client 창에서 입력하면 거부한다. 이는 현재 프로젝트 디버그 명령의 구현 제한이며, Client 화면 관찰을 금지하는 Unreal의 제약은 아니다.

```text
ReactionWarp.Mode MultiPhase AllowDraft=1 Library=/Game/Combat/Analysis/DA_HitReaction_Front_PELT Policy=/Game/Combat/Analysis/DA_ReactionWarp_Front_Draft
hitwall.Probe 1
hitwall.Hit
```

첫째 줄은 다음 피격에 적용할 모드/정책 설정, 둘째 줄은 기록 활성화(화면 관찰만 할 경우 선택), 셋째 줄은 가까운 적에게 피격 1회를 발생시킨다. 첫째 줄을 입력한 직후에는 아직 피격이 재생되지 않는다.

`DA_ReactionWarp_Front_Draft`는 이번에 새로 만든 별도 정책 Asset이다. 원본 Animation·기존 Library·BP 기본값은 저장하거나 덮어쓰지 않았다. `Mode` 설정은 다음 피격부터 적용되며 진행 중인 계획은 바꾸지 않는다. 정책과 Library 참조는 계획에 포함되어 관찰 클라이언트가 같은 cooked 데이터를 읽는다.

동일 조건의 자동 비교가 필요하면 위 설정 뒤에 **다음 한 줄을 입력하고 Enter**를 누른다. 한 번의 피격만 볼 때는 실행할 필요가 없다.

```text
ReactionWarp.Compare gaps=250,80,40,10 modes=Off,Uniform,MiddleOnly,MultiPhase reps=3 AllowDraft=1
```

적의 시작 위치에서 플레이어 반대 방향으로 벽을 탐색한다. 시작 위치와 벽 사이에 최대 gap보다 충분한 여유를 둔다. 위 명령은 **4개 거리 × 4개 모드 × 3회 = 48회**를 실행한다. 종료 또는 `hitwall.Stop` 시 원래 모드·draft 설정·배우 위치/회전·probe/label/FPS를 복원한다. 비교 중에는 캐릭터를 조작하지 않는다. `gap`은 Capsule 표면과 벽 사이 거리이고 `BudgetCm`은 허용 Root 투영 거리이므로 같은 값이 아니다.

## Client 화면 관찰과 Editor의 서버 상태 확인

Play 버튼 옆 옵션/Advanced Settings에서 다음을 설정한다.

- `Number of Players`: **2**
- `Net Mode` / `Play Net Mode`: **Play As Listen Server**
- `Run Under One Process`: **켜기** (같은 Editor에서 서버/Client PIE 월드를 선택하려는 경우)
- 실행 표시 방식: `New Editor Window (PIE)` 또는 `Selected Viewport`

실행 후 창 제목의 NetMode로 Listen Server와 Client 창을 구분한다. **위 설정/피격 명령은 Listen Server 창에서 한 줄씩 입력하고, 애니메이션은 Client 창에서 관찰하면 된다.** Client는 서버가 정한 계획을 전달받으며 주변 환경을 다시 최적화하지 않는다. 관찰하려는 적이 Client 카메라에 보이도록 플레이어 위치/시점을 잡는다. 이 관찰 방식으로 플레이어 자신이 맞는 owner prediction까지 검증되는 것은 아니다.

실행 중 Editor로 돌아와 Outliner의 옵션 메뉴에서 **World > Choose World**에 표시되는 **서버 PIE 월드**를 고르고 해당 적을 선택하면 서버 쪽 Actor/Component를 살펴볼 수 있다. Client PIE 월드를 고르면 같은 적의 Client 인스턴스를 살펴볼 수 있다. Selected Viewport에서 마우스가 게임에 잡혀 있으면 `Shift+F1`로 마우스를 해제한다. 그 뷰포트에서 `F8` 또는 Eject를 사용하면 플레이어 조종을 벗어나 관찰할 수 있다.

Play 전의 일반 **Editor 월드**는 편집용 원본이며 실행 중인 서버 상태가 아니다. PIE를 실행하고 서버 **PIE 월드**를 선택하는 것이 핵심이다. 현재 Details에서 Warp의 Mode/Library/Policy 설정과 CMC의 노출된 값을 확인할 수 있지만, 서버 확정 계획 전체를 보여 주는 전용 GUI는 아직 없다. 결과/이유/hash는 Output Log의 `[ReactionWarpPeer]`, `[ReactionWarp]` 또는 CSV/보고서에서 확인한다. Client 창이나 일반 Editor Output Log에서 `Mode`/`Compare`를 실행해 서버로 전달하는 기능도 현재 구현에는 없다.

Play 전 Editor Output Log의 `Cmd`에서 가능한 작업은 `ReactionWarp.Validate`, `SelfTest`, `Plan`, `Preview` 등 원본·정책·오프라인 계획 진단이다. 실제 Capsule 충돌·Montage/CMC 실행·복제 관찰에는 PIE 같은 실행 월드가 필요하다.

근거: [Epic PIE 콘솔 설명](https://dev.epicgames.com/documentation/en-us/unreal-engine/playing-and-simulating-in-unreal-engine), [Epic PIE 멀티플레이 옵션](https://dev.epicgames.com/documentation/unreal-engine/play-in-editor-multiplayer-options-in-unreal-engine?lang=en-US). `Choose World` 및 F8 동작은 설치된 UE 5.8 Editor 소스에서도 확인했다.

| 모드 | 동작 |
|---|---|
| Off | 이동 보정 없이 기존 Root Motion과 CMC 충돌 |
| Uniform | 전체 시간에 같은 배율; 모든 정책 범위를 만족해야 실행 |
| MiddleOnly | 배열 가운데 구간만 허용하고 나머지 구간 전체를 정확히 1로 잠금 |
| MultiPhase | 각 구간의 잠금·범위·보존 비용을 반영해 연속 곡선을 계산 |
| Legacy | 기존 단일 Notify/전체 Scale 방식. 기존 `hitwall.Run ... warp=0,1` 의미 유지 |

너무 가까운 벽이나 MiddleOnly의 이동 용량 부족은 `InsufficientCompressionCapacity` fallback이 정상이다. 하한을 무시해 0으로 압축하지 않는다. fallback은 계획 성공/자연스러움 통과로 집계하지 않고, 기존 CMC 이동/충돌로 처리한다. 검토된 Short/InPlace 후보를 고르는 선택기는 이번 범위에 포함하지 않았다.

## Asset 설정과 조절 값

대상 Character에는 `HitReactionComponent`, `MotionWarpingComponent`(복제 꺼짐), `ReactionWarpComponent`, `ReactionEnvironmentComponent`가 있어야 한다. 기존 BP에 있는 구성은 그대로 사용한다. 지속 설정을 원하면 BP의 Reaction Warp에서 `Library`, `Policy`, `Mode`를 지정하고 `bEnableWarp`를 켠다. 패키징 시 Library/Policy/Animation을 cook 대상에 포함한다.

초기 정책은 실험용이며 승인되지 않았다.

| 항목 | 기본값 |
|---|---|
| 구간 시간 | 0~0.1 / 0.1~0.283333 / 0.283333~0.75초 |
| 구간별 MinScale / MaxScale | 0.5 / 1.0 |
| PreservationWeight | 3 / 1 / 3; 낮을수록 압축을 더 허용 |
| TransitionSeconds | 0.04초; 짧은 구간에서는 자동 제한 |
| LateralLimitCm | 5cm |
| SmoothnessWeight | 0.25 |

구간은 배열이므로 세 개 또는 의미 라벨에 종속되지 않는다. 상한은 16구간/64키이고 초과 시 거부한다. 잠금은 `bAllowWarp=false`로 지정한다. 경계 전환도 잠긴 구간에 침범하지 않는다.

`bSemanticReviewed`와 `bWarpPolicyReviewed`는 별도다. 기본 게임 실행은 모든 구간의 두 승인과 현재 `ApprovedPolicyHash`를 요구한다. 사용자가 정책을 검토하고 승인할 때 `ReactionWarpTools.policy_digest(policy)`의 현재 값을 `approved_policy_hash`에 기록할 수 있다. 정책·원본·분석 경계가 바뀌면 승인이 무효화된다. `AllowDraft=1`은 현재 PIE 세션의 실험 허용이며 Asset 승인을 바꾸지 않는다.

정책 Asset이 없는 다른 checkout에서는 Editor Output Log의 **Cmd**로 생성한다. 기존 Asset 경로는 거부하므로 새 이름을 사용한다.

```text
ReactionWarp.Migrate /Game/Combat/Analysis/DA_HitReaction_Front_PELT NewPolicy=/Game/Combat/Analysis/DA_ReactionWarp_Front_Draft
ReactionWarp.Validate /Game/Combat/Analysis/DA_HitReaction_Front_PELT
ReactionWarp.SelfTest
ReactionWarp.Plan Candidate=Front BudgetCm=100 Policy=Balanced
ReactionWarp.Plan Candidate=Front BudgetCm=100 Policy=Equal
ReactionWarp.Preview latest
```

`Front`는 기본 Library에서 `/Game/Combat/Animations/AM_HitReaction_Front`와 일치하는 유일한 후보 `Motion_43489b8f6672`에만 연결한다. 다른 후보는 실제 CandidateId와 Policy 경로를 지정한다. `Balanced`는 기본 정책, `Equal`은 동일 정책을 메모리에서 균등 비용으로 복사한 비교다. 승인 전 W0~W2는 DRAFT 진단이며 환경 sweep 통과를 의미하지 않는다.

현재 에셋 측정: 원래 투영 이동 **136.974cm**, 최대 측방 변위 **1.908cm / 0.20초**, 기본 MultiPhase 최소 용량 **68.487cm**. 실제 환경에서는 측방 포락선·벽 여유·전체 Capsule sweep가 추가된다. Blend/CMC로 실제 변위가 계획보다 작을 수 있으며 그 오차는 수치 solver 오차와 별도다.

## 자동 검증과 결과 읽기

프로젝트 루트 PowerShell:

```powershell
# 사전 조건: Editor 빌드, 08 분석 환경 설치 및 원본 분석 작업 보존
./Scripts/TestReactionWarp.ps1

# 원본/정책 검증 + 실제 단일 게임의 7개 시나리오 + 48회 비교/복원 검사
./Scripts/TestReactionWarp.ps1 -Standalone

# 필요 시 사용자가 실행: Dedicated Server + Client/Observer
./Scripts/TestReactionWarp.ps1 -Network
```

직접 Automation을 사용할 경우 테스트 이름은 `GE.ReactionWarp.Numerics`다. 수치 검증에는 1/2/3/5/16구간, 잠금, 짧은/0이동 구간, 잘못된 시간·NaN·hash·미검토 입력, 연속성, 30/60/120fps와 큰 delta, blend 가중치·마지막 delta·중간 시작을 포함한다.

결과는 `Saved/Tasks/*-reaction-warp-*/README.md`부터 확인한다.

| 파일 | 용도 |
|---|---|
| input.json / policy.json / plan.json | 입력·정책·계획 snapshot; 비교 실행은 input.json 안에 정책을 포함 |
| plan-*.json | 비교 trial별 서버 확정 계획, hash와 거부 사유 |
| planned_trajectory.csv | 공통 적분기로 평가한 원본·계획 궤적 |
| runtime_samples.csv | 실제 Capsule, 계획 위치/오차, pose 이후 ball 위치와 지면 trace 접촉 이동 |
| trials.csv | 예약된 모든 trial의 Completed/Fallback/Rejected/Failed/Cancelled |
| validation.json / summary.json | 원본 검증과 실행 결과·미검증 항목 |
| report.html | 연속 배율·계획/실제 궤적·구간·trial 비교 |

Peer별 전체 기록은 기존 `Saved/HitWallProbe/<session-role>/<actor>/`에 `runtime_samples.csv`, `plan-*.json`, 기존 frame/event CSV로 보관한다. 실제 plan/CMC 오차와 발의 trace 접촉 이동은 품질 판단을 돕는 측정이며 자연스러움 자동 판정이 아니다. 관절이 없거나 접촉 trace가 성립하지 않으면 해당 접촉 이동을 품질 증거로 사용하지 않는다.

`ReactionWarp.Preview <작업 폴더>` 또는 `latest`는 보고서를 만들고 기존 `report_browser.py`로 브라우저를 직접 연다. Editor 없이 재생성하려면:

```powershell
python ./Scripts/ReactionWarp/render_report.py '<작업 폴더>' --open
```

## Editor 없이 네 모드의 실제 이동을 재생하기

`Scripts/ReactionWarp/motion_comparison.py`는 기존 원본 Joint 분석과 모드 비교 실행 기록을 결합해 독립 HTML을 만든다. 게임 실행이나 PIE 콘솔 입력은 필요 없다. 생성된 `report.html`을 Chrome/Edge에서 열고 **재생**을 누른다. HTML에 데이터가 포함되어 있어 별도 서버도 필요 없다.

- 네 화면은 동일한 벽 간격, 고정 카메라, 공간 축척과 시간을 사용한다. 캐릭터 골격 자체가 뒤로 이동한다.
- 기본 **80cm / 1회 / 실측 이동**에서 비교하고, 0.25× 또는 0.5×로 늦추거나 시간 막대를 움직인다. 250·40·10cm와 세 번의 반복 기록도 선택할 수 있다.
- 현재 후퇴 거리, Off 대비 후퇴 감소, 벽 여유, 최종·최대 이동 거리를 cm로 표시한다. 벽 간격은 시작 Capsule 표면 기준이다.
- **실측 이동**은 기록된 Mesh 위치와 Capsule 이동·충돌 결과를 사용한다. **계획 이동**은 C++ `planned_trajectory.csv`를 사용하며 충돌·블렌딩을 재현하지 않는다.
- 전체 골격은 원본의 Root 상대 Joint 포즈에 실제 Mesh 이동을 한 번 더한 재구성이다. 게임에서 전신 Joint를 프레임마다 캡처한 결과가 아니며 AnimBP·IK·블렌드 차이가 남는다. 실측 발 위치는 별도 점으로 표시한다. Montage 종료 후에는 마지막 원본 포즈를 유지한다.
- Fallback은 성공한 모드별 보정으로 해석하지 않는다. 이 기록의 MiddleOnly 80cm 및 보정 모드의 40·10cm 조건은 원본 이동과 CMC 충돌 결과다. Fallback의 계획 재생은 표시하지 않는다.
- 사용한 실행 기록은 최종 블렌드 기준 코드 수정 전 기록이다. 최신 소스나 멀티플레이 복제를 검증한 결과는 아니다. 자연스러움은 직접 관찰한다.

현재 보존된 데이터로 다시 만들려면 **프로젝트 루트 PowerShell**에서 아래 **명령 한 줄 전체를 입력한 뒤 Enter 한 번**을 누른다. PIE 게임 콘솔에 입력하는 명령이 아니다.

```powershell
python ./Scripts/ReactionWarp/motion_comparison.py --compare "Saved/Tasks/20261109-043000-reaction-warp-A5255D544FF784FB64A405A377CB0FCE" --source "Saved/Tasks/20261108-100727397625-joint-analysis-ede958f1" --probe "Saved/HitWallProbe/20261109_132946_Server_27260_42a18919/BP_SharpEnemy_C_UAID_E89C25A81E8D960A03_1893138791"
```

출력된 새 `Saved/Tasks/*-warp-motion-replay-*/` 폴더의 `report.html`을 연다. `summary.csv`는 거리 수치, `replay.json`은 내장 재생 데이터, `provenance.json`은 입력 파일 해시, `validation.json`은 원본/계획 식별자·시각·위치 연결 검사다. 다른 기록에는 각 입력 경로를 변경한다. 동일 실행의 비교 폴더와 Probe Actor 폴더가 필요하며 불일치하면 생성기가 거부한다. 생성기와 HTML 템플릿은 게임 코드를 변경하지 않는다.

## 이번 작업 검증 범위와 확장 지점

- 최종 소스로 Editor/Game Development 빌드 통과. 수치 검사 897개와 W0~W2 원본/정책/보고서 통합 검증은 직전 검증 시점에 통과했다.
- 단일 게임의 무장애·가능한 압축·하한 부족·취소·후속 피격·시작 overlap·중간 장애물 7개 시나리오 통과.
- 48개 비교 trial 모두 기록되고 모드/배치 복원 검사 통과. 자연스러움은 사용자 평가로 남겼다.
- 그 이후 정확한 엔진 추출 delta로 blend 참조를 계산하는 보완과 회귀 2개(총 899개), 계획/실제 위치 계측·로컬 cooked 데이터 검사를 추가했다. 최종 실행 재검증 요청이 승인되지 않아 해당 변경 후 수치/게임 재실행은 미완료다. `-Standalone`으로 재검증할 수 있다.
- 실제 서버/관찰자 검증은 실행 요청이 승인되지 않아 미실행. `ReactionWarp.NetTest`도 owner replay를 PASS로 표시하지 않는다. 소유 플레이어는 `OwnerPredictionNotValidated` fallback.
- 고정 yaw/지상/평면/단일 비분기 Montage/Rate=1/정적 환경부터 지원한다. 회전·수직·역행·time stretch·다중 slot/segment·동적 장애물 재계획은 거부 또는 fallback.
- `ReactionWarpPolicy`는 분석과 분리된 확장 지점, `ReactionWarpPlan`은 순수 solver/공통 적분, `ReactionWarpComponent`는 환경 검사, `ReactionMultiPhaseWarpModifier`는 실행, Probe/Tools는 계측·진단을 담당한다.

소유 플레이어 지원을 확장하려면 먼저 CMC saved move에 계획 식별자/수명을 연결하고 correction/replay·늦은 수신·취소/교체 회귀 fixture를 추가한 뒤 해당 fallback 제한을 해제한다. 현재 AI 관찰 경로를 owner 검증으로 취급하지 않는다.
