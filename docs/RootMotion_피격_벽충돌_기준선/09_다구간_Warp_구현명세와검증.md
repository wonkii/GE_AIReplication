# 09. 다구간 Warp 구현 명세와 검증

작성: 2026-11-09. 상태: **구현 기준 명세. 다구간 정책·solver·Modifier·계획 복제 전달·진단/비교 명령이 구현되었다. 실제 실행/테스트 설정과 완료·미검증 범위는 [10. 다구간 Warp 테스트 설정](10_다구간_Warp_테스트설정.md)을 따른다. 자연스러움은 사용자 평가, 실제 네트워크/owner replay는 별도 관문으로 남는다.**

## 1. 목표와 설계 판단

PELT로 나눈 초기반동·주된 후퇴·후퇴 감속/회복 구간을 모두 이동 보정 대상으로 지원한다. 환경의 이동 가능 거리에 맞춰 구간별 보정량을 배분하고, 경계에서 갑자기 속도가 달라지지 않도록 연결한다. 사용자는 PIE에서 같은 Montage의 원본·전체 동일 배율·두 번째 구간만 보정·다구간 보정을 비교한다.

**이 방향은 구현 구조로 적절하다.** 두 번째 구간에만 압축을 몰면 그 구간에서 과도한 발 미끄러짐이 생길 수 있고, 첫째·셋째 구간의 이동만으로도 남은 공간을 초과할 수 있다. 다만 세 구간에 분배하면 항상 자연스러워진다는 주장은 하지 않는다. 초기 충격감과 회복 발 디딤을 훼손할 수도 있으므로 비교 실험으로 승인한다.

모든 구간을 보정할 수 있다는 것은 매번 모든 구간을 강하게 보정한다는 뜻이 아니다. 구간마다 허용 배율·보존 비용·잠금 여부를 두고 필요한 만큼만 조절한다. 구간 개수는 배열로 처리하며 3개나 특정 의미 라벨을 코드에 고정하지 않는다. 원본 Montage를 새로 베이크하는 기능이 아니라, 원본 포즈/시간을 유지하며 재생 중 Root Motion을 보정하는 기능이다. IK·포즈·재생 시간 보정은 후속 확장이다.

## 2. 확인한 현황과 근거

기준 자료: `Saved/Tasks/20261108-100727397625-joint-analysis-ede958f1/analysis.json` 및 `export.json`. 대상은 `/Game/Combat/Animations/AM_HitReaction_Front`, 0.75초, 60Hz, Rate=1이다. 다음 구현 시작 시 해시와 최신 결과를 다시 검증한다.

| 구간 ID | 에셋 시간 | 샘플 경계 | 평균 Root 속도 | 사람이 검토할 의미 |
|---|---|---|---|---|
| 0 | 0~0.1초 | 0~6 | 196.5cm/s | 초기반동 |
| 1 | 0.1~0.283333초 | 6~17 | 271.5cm/s | 주된 후퇴 |
| 2 | 0.283333~0.75초 | 17~45 | 145.1cm/s | 후퇴 감속·회복 |

자동 라벨은 셋 모두 `root_travel`, `reviewed=false`다. 의미 라벨은 사용자 해석이며 알고리즘의 의미 인식 결과가 아니다. 셋째 구간의 이동 경로 길이는 평균 속도×시간 기준 약 67.7cm로, 정지 구간이 아니다. 경로 길이와 목표 방향으로의 투영 거리는 구분한다.

Warp 후보는 측방 이동이 전체 궤적의 직선 허용치 0.1cm를 초과해서 0개다. `motion_analysis.py::warp_windows`는 전체 궤적을 먼저 검사하므로 개별 구간의 부적합까지 확정한 결과는 아니다. 이 거부를 지우거나 `reviewed`만 바꿔 통과시키지 않는다.

| 현재 코드 | 확인 사항 | 필요한 변경 |
|---|---|---|
| `Combat/Reaction/HitWallReactionLibrary.h` | 궤적·Phases·ProposedWarpWindows·단일 reviewed | 구간별 정책과 검토 상태 분리 |
| `Combat/ReactionWarpComponent.cpp` | 단일 Notify 설정, 직선·고정 회전 검사, 단일 PlanarScale | Library 기반 다구간 계획 및 전체 경로 검사 |
| `Combat/HitReactionComponent.cpp` | Montage 끝까지 Scale 적용, 서버 State 전달 | 시간 곡선 계획 적용·중도 합류·정리 |
| `Combat/ReactionPlanarWarpModifier.*` | Scale 상속, 마지막 delta 수명 보존 | 새 다구간 Modifier 추가, 기존 방식은 비교용 유지 |
| `Combat/Probe/HitWallProbeSubsystem.*` | gap/warp 비교 실행 | 모드·정책·구간 배율·품질 지표 기록 |

Epic은 Motion Warping의 시간 창과 Scale/Skew Warp를 제공한다. 여기서 제안하는 다구간 비용 최적화와 연속 배율은 프로젝트의 확장 설계이며 기본 기능이 자동으로 해결하지 않는다. [Epic Motion Warping](https://dev.epicgames.com/documentation/unreal-engine/motion-warping-in-unreal-engine), [Scale API](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/RootMotionModifier_Scale).

## 3. 첫 구현 범위

- 단일 비분기 Montage, 고정 재생 속도 1, 지상·정적 환경·고정 yaw·평면 Root Motion부터 지원한다. 다른 속도/Section/회전 경로는 지원 여부를 명시하고 미지원이면 사유와 함께 거부한다.
- 세 구간 각각 잠금/해제와 배율 범위·보존 비용을 설정한다. 배율은 첫 구현에서 0~1 범위의 압축만 허용한다. 늘리기·역방향·회전 보정은 지원하지 않는다.
- 무장애에서는 배율 1로 원본을 보존한다. 너무 가까운 벽에서 자연스러움 하한을 무시하며 0까지 압축하지 않는다. 검토된 Short/InPlace 후보가 있으면 전환하고, 없으면 기존 CMC 충돌 경로로 명시적으로 fallback한다. fallback은 자연스러움 통과로 집계하지 않는다.
- 목표 에셋은 작은 측방 이동 때문에 현재 거부된다. 첫 구현의 필수 관문으로 실제 측방량을 측정하고, **고정 yaw·단조 전진·제한된 측방 변위를 갖는 평면 경로**까지 지원한다. 측방 변위를 없애 직선으로 투영하지 않고 원본 XY delta를 함께 스케일한다. 명시적 측방 한도와 전체 Capsule sweep를 모두 통과해야 한다.
- 큰 곡선/역행/수직 이동/회전/동적 장애물 재계획은 후속 확장으로 남긴다. 새 장애물에 대한 CMC 충돌은 계속 유지한다.

## 4. 데이터 계약

다음 이름은 제안이며 구현 시 기존 스타일에 맞출 수 있다. 의미와 검증 규칙은 유지한다.

**Library 구간 정책 `FReactionPhaseWarpPolicy`**

- `PhaseId`, `AssetStartSeconds`, `AssetEndSeconds`, `SemanticLabel`.
- `bAllowWarp`, `MinScale`, `MaxScale`, `PreservationWeight`, `TransitionSeconds`.
- `bSemanticReviewed`, `bWarpPolicyReviewed`, 검토자 메모/정책 버전. 기존 후보 reviewed는 호환성을 유지하되 의미 검토만으로 실행 승인하지 않는다.
- `SourceHash`, `AnalysisHash`, `PolicyHash`를 연결한다. 정책은 분석 원본과 분리해 PELT 재분석 시 사람 검토가 덮어써지지 않게 한다. 경계/원본 변경 시 기존 정책 승인을 무효화한다.

예시 시작값은 실험용이다: 각 구간 범위 0.5~1.0, 보존 비용 초기반동 3 / 후퇴 1 / 회복 3. 모두 1인 균등 비용도 비교한다. 0.5는 자연스러움이 입증된 하한이 아니며 실험으로 조정한다. 잠긴 구간은 전체 구간에서 정확히 1이어야 한다. 전환이 잠금 영역에 침범하지 않게 제약한다.

**서버 계획 `FReactionWarpPlan`**

- SchemaVersion, ReactionId, CandidateId, PolicyHash, Source/Analysis 식별자, Montage, PlayRate, ServerStartTime.
- 결과 상태 `Unmodified / Planned / Fallback` 및 구체적인 이유.
- 시간별 연속 배율 키, 구간별 원래/계획 이동량, 목표/도달 가능 거리, 전체 경로 검사 결과.
- 고정 순서의 작은 키 배열을 직렬화한다. 첫 구현 상한 예: 16구간·64키. 상한 초과는 명시적으로 거부하며 잘라내지 않는다. 전체 Bone 포즈와 대형 분석 JSON은 복제하지 않는다.
- 런타임에서 파일 SHA를 매 피격 계산하지 않는다. Editor/빌드 검증으로 원본과 bake의 일치를 확인하고 cooked 식별자와 계획 버전을 비교한다. 클라이언트 불일치는 로컬 재최적화 없이 실패 사유를 남긴다.

데이터 누락·미검토·NaN·겹침·역전 시간·범위 오류는 계획 생성 전에 차단한다. v1 Library를 암묵적으로 승인하지 않는다. 명시적 마이그레이션/검증 도구를 제공한다.

## 5. 보정량 계산과 연속성

### 5.1 동일하게 쓰는 궤적 표현

추출된 누적 Root 위치를 r(t), 단조 후퇴의 기준 방향을 d라 한다. 고정 yaw 조건에서 평면 속도에 연속 배율 s(t)를 적용한다.

```text
qXY(t) = integral[0,t] s(u) * rXY'(u) du
qZ(t)  = rZ(t)                 // Z는 보정하지 않음
회전   = 원본 회전             // 첫 구현에서는 고정 회전만 지원
```

`q(t)=s(t)*r(t)`로 계산하면 배율이 바뀔 때 위치가 튀므로 사용하지 않는다. 구간별 상수 배율 Modifier를 단순히 이어 붙이는 방식도 경계 속도 불연속을 만들 수 있어 채택하지 않는다.

시간 키의 배율을 선형 보간해 s(t)를 연속으로 만든다. 전환 키를 구간 경계 양쪽에 배치하고 짧은 구간에서는 길이를 제한한다. 원본 r(t)는 샘플 사이 선형으로 정의한다. 각 겹치는 시간 조각에서 선형 s(t)×상수 r'(t)를 적분하면 정확한 누적 q(t)를 얻는다. 평가 함수 하나를 계획·sweep·실행 테스트에 공유한다. 이후 가속도 품질이 부족하면 C1 곡선으로 확장하되 동일한 적분/검증 계약을 유지한다.

### 5.2 환경 거리와 목적함수

기존 안전 거리 탐색을 사용하되 측방 포락선을 반영한 보수적 탐색으로 시작한다. 최종 목표 방향 거리 B는 Capsule 표면 거리·WallClearance를 일관되게 반영한다. 시작 겹침은 별도 실패로 처리한다.

시간 키 배율 벡터 x에 대해 다음 작은 볼록 문제를 푼다. 계수는 실제 적분으로 만들고 전환 부분의 이동량도 포함한다.

```text
min  sum_k w_k * (1 - x_k)^2 + lambda * sum_k (x[k+1] - x[k])^2
s.t. 각 시간 영역의 MinScale <= s(t) <= MaxScale
     잠긴 구간의 s(t) = 1
     dot(qXY(T), d) = B
```

엄격히 양수인 비용, 고정 키 순서·허용 오차·반복 상한을 가진 bounded equality QP solver를 C++ 순수 함수로 구현한다. 외부 Python/ruptures는 런타임에 필요하지 않다. 최적화 실패/수치 오류를 성공으로 취급하지 않는다. 잠금·범위 제약으로 B가 불가능하면 `InsufficientCompressionCapacity`로 fallback한다. 경계 전환으로 가능한 최소 이동량도 달라지므로 단순히 구간 거리×최소 배율만으로 가능 여부를 확정하지 않는다.

낮은 PreservationWeight일수록 압축을 더 허용하지만 강제 우선순위는 아니다. 모든 구간에 공간 부족을 분배할 수 있고, 임의 개수 구간에 같은 계산을 적용한다. s=1인 무장애 결과는 정확히 원본이어야 한다.

### 5.3 전체 경로 검사

종점만 검사하지 않는다. q(t)를 월드 좌표로 변환한 뒤 시작 overlap과 전 구간 Capsule sweep를 수행한다. 시간 샘플 간 곡선 오차를 줄이도록 길이/곡률 기반 세분화 또는 오차 상한만큼 보수적 팽창을 적용한다. 이산 sweep를 연속 경로의 절대적 증명으로 표현하지 않는다.

측방 이동·구석 벽·초기 구간에서 충돌하면 제한된 횟수로 더 보수적인 B를 선택해 재계산/재검사한다. 작은 B가 반드시 더 안전하다고 가정하지 않으며 매 결과를 검사한다. 반복 상한 도달 시 fallback한다. 실제 블렌딩·IK·CMC 결과는 사전 계획과 다를 수 있으므로 런타임 계측과 CMC 충돌을 유지한다.

## 6. 실행과 네트워크 연결

1. `ReactionWarpComponent`가 승인된 후보/정책을 조회하고 환경 거리→계획→전체 경로 검사를 수행한다. 새 모드에서는 Library가 창의 근거다. 기존 `ExpectedOneWindow` 검사는 Legacy 모드에만 남긴다.
2. `FCombatReactionState`에 서버가 확정한 계획을 전달한다. 클라이언트는 주변 환경을 다시 조회하거나 배율을 재계산하지 않는다.
3. 새 `ReactionMultiPhaseWarpModifier` 한 개가 전체 보정 곡선을 담당한다. 기존 Skew Warp/Scale과 같은 Root Motion에 중복 적용하지 않도록 활성 Modifier 소유권을 검사한다.
4. 한 프레임이 여러 경계를 넘어가도 `[PreviousAssetTime, CurrentAssetTime]` 전체의 적분 차이를 사용한다. 현재 프레임의 배율 하나만 delta에 곱하지 않는다. 늦은 수신은 현재 시간에서 이어 재생하고 누락된 과거 변위를 순간 적용하지 않는다.
5. UE 5.8 설치 소스에서 `ProcessRootMotion` 입력 delta의 공간, blend weight, 호출 시점, 마지막 프레임 수명을 먼저 확인한다. 원본 추출 대비 보정 delta를 어느 단계에 반영해야 blend가 한 번만 적용되는지 통합 테스트로 고정한다. 누적 q를 월드 위치로 강제 SetActorLocation하거나 raw delta로 무조건 치환하지 않는다.
6. 취소·교체·종료·EndPlay·중복 State·늦은 OnRep에서 Modifier 제거/재적용이 정확히 한 번 이루어져야 한다. 기존 마지막 delta 처리 의도를 보존한다.
7. CMC가 이동/충돌/위치 복구를 담당한다. 서버 시작 시간과 Montage asset time을 명확히 매핑한다. Rate=1 외에는 검증 전 거부한다.

서버 AI 피격+관찰자 검증으로 소유 플레이어의 prediction/replay까지 검증됐다고 결론내리지 않는다. owner 피격 fixture와 correction/replay 시험은 별도 완료 관문이다.

## 7. 구현 단계와 디버그 명령 계약

**아래 표는 설계 당시의 명령 계약이다. 현재 구현된 명령·정확한 인자·미지원 범위는 [10](10_다구간_Warp_테스트설정.md) 및 `ReactionWarp.Help`를 참조한다.**

| 단계 | 작업 | 신규 명령 예 | 통과 조건 |
|---|---|---|---|
| W0 | 원본/정책 검사·마이그레이션·측방량 계측 | `ReactionWarp.Validate /Game/Combat/Analysis/DA_HitReaction_Front_PELT` | 최신 hash, 정책 미승인 사유, 측방 최대값/시점, 구간 용량 출력 |
| W1 | 순수 solver·적분·임의 구간 처리 | `ReactionWarp.SelfTest` | 아래 수치 테스트 PASS |
| W2 | 환경 독립 계획 비교와 HTML | `ReactionWarp.Plan Candidate=Front BudgetCm=80 Policy=Balanced` | 배율 키·구간 거리·가능 여부·경계 그래프 생성 |
| W3 | 단일 플레이 실행 연결 | `ReactionWarp.Mode MultiPhase` 다음 `hitwall.Hit` | 실제 궤적/구간 배율/계획 해시 기록 |
| W4 | 환경 비교 자동 실행 | `ReactionWarp.Compare gaps=250,80,40,10 modes=Off,Uniform,MiddleOnly,MultiPhase reps=3` | 48개 계획된 trial과 성공/실패/fallback 개별 기록 |
| W5 | 네트워크/수명 회귀 | `ReactionWarp.NetTest` | 실제 구성한 peer별 동일 계획과 종료/취소 증거. fixture 없으면 BLOCKED |
| W6 | 결과 검토 | `ReactionWarp.Preview latest` | HTML을 실제 브라우저로 열고 trial/모드/구간별 비교 |

`Candidate=Front`는 새 fixture의 별칭이다. W0에서 실제 CandidateId와 연결해 출력하고 자동으로 잘못된 후보를 선택하지 않는다. `BudgetCm`는 허용 Root 투영 거리이고 Compare의 gap은 벽과 Capsule 표면 사이 거리다. 둘을 같은 숫자로 취급하지 않는다.

`ReactionWarp.Mode`는 다음 피격부터 적용하고 실행 중 계획은 바꾸지 않는다. Compare는 종료/취소 시 기존 모드·배우 배치·콘솔 설정을 복원한다. PIE world/authority를 명시하고 실패 trial을 누락하지 않는다. 기존 `hitwall.Run ... warp=0,1` 의미는 바꾸지 않는다.

W0~W2는 정책 승인 전에도 명시적 draft로 실행 가능하다. W3 이후 draft 시험은 `AllowDraft=1`을 해당 실험 세션에 한해 허용하고 화면·로그에 DRAFT를 표시한다. 기본 게임 조회의 검토 제한을 전역 해제하지 않는다. 실험 성공/사용자 검토 후 정책을 별도로 승인한다.

## 8. 검증 명세

### 수치/계획 테스트

- 1/2/3/5구간, 길이가 짧은 구간, 0 이동 구간, 모두 잠김, 혼합 잠금, 경계 전환 중첩, 잘못된 시간·NaN·hash·미검토 입력.
- B=원래 거리이면 모든 배율 1. 가능한 B에서는 목표 투영 오차 0.1cm 이하. 불가능한 B와 반복 상한은 명확한 실패.
- 개별 구간 잠금은 전체 구간 배율 1을 보장한다. 해제된 첫째/둘째/셋째 모두 압축 가능한 사례를 각각 검증한다.
- 같은 계획을 30/60/120fps 및 큰 delta로 평가해 최종 계획 변위 차이 0.1cm 이하. 한 tick에서 모든 경계를 통과해도 동일하다.
- 배율 경계 좌우 차이 1e-4 이하, 위치 누적 연속. 원본 자체의 가속도 변화를 오류로 오인하지 않고 보정으로 추가된 변화량을 기록한다.
- 곡선 측방 이동, 코너, 시작 overlap, 중간 충돌/끝점 안전, 조건 범위 밖 회전·수직·역행을 검증한다.
- 실제 delta/계획 비교는 blend-in/out, 마지막 프레임, 취소, 중간 시작을 포함한다. 수치 허용값은 고정 yaw의 계획 검사용이며 실제 CMC/블렌딩 오차와 분리한다.

### 실제 환경과 자연스러움

네 가지 모드의 정의를 고정한다: Off=보정 없음, Uniform=전체 동일 배율, MiddleOnly=첫째/셋째 잠금, MultiPhase=전체 허용 정책. 같은 시작 포즈·벽·피격 방향·원본·속도를 사용한다. MiddleOnly가 공간을 맞출 수 없으면 실패로 기록하며 배율을 몰래 범위 밖으로 내리지 않는다.

PIE 관측 순서: 무장애→여유 있는 벽→중간 거리→매우 가까운 벽→비스듬한 벽/코너. 기본 자동 간격 외에 실제 압축 가능 최소/최대 거리 안팎을 추가한다. 과도하게 가까운 조건은 자연스러운 보정 성공보다 올바른 fallback을 검증하는 경우가 될 수 있다.

사용자는 정면/측면 및 저속 재생 영상에서 다음을 확인한다.

| 확인 사항 | 기록/판정 |
|---|---|
| 초기 충격감 | 첫 구간에서 피격이 약해 보이거나 몸이 뜨는지, 1~5점+메모 |
| 발 미끄러짐 | 월드 foot 위치·접지 상태·접지 구간 누적 이동 cm, 동일 조건 모드 비교 |
| 경계 연결 | 0.1초/0.283333초 부근 속도·가속도 변화와 육안 끊김 |
| 회복 | 마지막 발 디딤, 종료 포즈, locomotion 복귀 시 튐 |
| 환경 | Capsule 침투 여부, 계획/실제 경로, CMC로 잘린 이동과 상승량 |
| 네트워크 | 서버/owner/observer의 계획 hash·Montage 시간·복구량·취소 처리 |

오프라인 contact proxy만으로 실제 접지를 판정하지 않는다. 실제 pose 평가 이후의 foot 위치와 바닥 trace/기존 접지 신호를 함께 기록한다. 시간/상태를 맞춰 비교하고 Capsule 이동 자체를 발 미끄러짐으로 측정하지 않는다. 수치 미끄러짐과 사용자 품질 평가는 별도 항목이다.

자연스러움 승인에는 적어도 압축이 가능한 두 환경에서 사용자가 MultiPhase와 다른 모드를 비교한 기록이 필요하다. 성공을 미리 가정하지 않는다. MultiPhase가 열등하면 가중치/하한/전환 길이를 조정하거나 후보를 교체한다. IK를 자동으로 추가하여 문제를 숨기지 않는다.

### 산출물

각 실행에 `Saved/Tasks/<timestamp>-reaction-warp-*/`를 만들고 README, 입력 snapshot/hash, policy.json, plan.json, planned_trajectory.csv, runtime_samples.csv, trials.csv, validation.json, report.html을 보관한다. trial은 ReactionId·모드·gap·peer 역할·성공/거부/fallback을 연결한다. 보고서는 구간별 s(t), 원본/계획/실제 경로, 속도와 발 미끄러짐, 정책과 거부 이유를 보여 준다. 실제 게임 영상을 기록하지 않았다면 화면 미검증으로 표시한다. Preview는 기존 `report_browser.py`의 브라우저 직접 실행을 재사용한다.

## 9. 완료와 다음 구현 인계

완료는 다음을 모두 충족한 경우다.

1. 모든 구간에 대해 잠금/범위/비용을 조절할 수 있고, 3구간 외에도 동작한다.
2. 목표 에셋의 측방 거부 원인을 수치로 설명하고 새 지원 조건에서 실제 전체 경로 검사를 통과시키거나 정확히 fallback한다.
3. W0~W4/W6를 사용자가 명령으로 재현하고 비교 보고서를 열 수 있다. W5의 완료 여부와 남은 owner 검증을 별도 명시한다.
4. Editor/Game 빌드, 의미 있는 수치/통합 테스트, 취소/교체/늦은 수신 회귀가 통과한다. 자연스러움은 사용자 기록으로 승인한다.
5. 원본 Animation/사용자의 기존 DataAsset을 덮어쓰지 않는다. 새 정책 Asset 또는 명시적 마이그레이션 결과를 만든다.

다음 작업자는 [08 분석 도구](08_관절추출_PELT_개발계획과테스트.md)와 본 문서를 읽고 W0부터 진행한다. 현재 staged된 `DA_HitReaction_Front_PELT`와 다른 dirty 에셋을 보존한다. 첫 변경은 데이터 계약·순수 계획 함수·테스트이며, 그 검증 뒤 Modifier/복제 경로를 연결한다. 본 문서 작성 시 런타임 코드는 수정하지 않았고 다구간 보정의 실제 자연스러움 테스트도 아직 수행하지 않았다.
