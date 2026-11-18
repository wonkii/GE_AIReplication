# 08. 관절 추출·PELT 구간 분석 개발 계획과 테스트

2026-11-08 KST. 지정한 Animation Sequence/Montage의 관절 데이터를 오프라인으로 추출하고, ruptures/PELT로 나눈 구간을 I1 후보 Library와 I2 Warp 구간 설계에 재사용한다. 이 문서의 A0~A5는 이번 구현 범위이며, 이후 실제 후보 선택·Warp 적용·네트워크 검증은 별도 완료 관문이다.

## 1. 책임과 데이터 흐름

```text
AnimAnalysis.Extract → export.json
  UE 에디터: AnimPose로 전체 관절과 엔진 Root Motion 추출
AnimAnalysis.Analyze → analysis.json + CSV + report.html
  Python: 특징 계산 → ruptures.Pelt → 운동학적 구간 → Warp 후보/거부 이유
AnimAnalysis.Build → library.json + UHitWallReactionLibrary
  I1: 궤적·구간·원본 식별값 / I2: 검토 전 Warp 후보 구간
AnimAnalysis.Validate → validation-*.json
AnimAnalysis.Preview → 브라우저에서 관절·구간·근거 검토
```

| 구현 위치 | 책임 |
|---|---|
| `Source/GEAnimationAnalysisEditor` | Editor 전용 추출 API와 디버그 명령. 에디터 의존성을 Runtime에 추가하지 않음 |
| `Scripts/AnimationAnalysis/motion_analysis.py` | UE와 독립적인 수치 계산·PELT·검증·Warp 후보 필터 |
| `Scripts/AnimationAnalysis/anim_analysis.py` | 명령 실행, 작업 README, 단계별 입출력, DataAsset 변환 |
| `Scripts/AnimationAnalysis/motion_report.py` | 외부 서버/CDN 없는 HTML 검토 화면 |
| `Scripts/AnimationAnalysis/manny_profile.json` | Bone 그룹·발·PELT·접지·Warp 임계값 설정 |
| `Combat/Reaction/HitWallReactionLibrary` | I1/I2에서 읽을 수 있는 Runtime 자료형과 검토 여부를 확인하는 조회 함수 |

기존 `ReactionEnvironmentComponent`, `ReactionWarpComponent`, `HitReactionComponent`의 실행 정책과 복제 경로는 이번 분석 도구가 변경하지 않는다. 기존 문서의 `HitWallTrajectoryBaker` 역할 중 원본 궤적 추출을 이번 Editor 모듈이 담당한다.

## 2. 준비

1. 변경한 소스로 `GE_AIReplicationEditor Win64 Development`를 빌드한다. 새 Editor 모듈이므로 기존 에디터를 재시작한다.
2. 프로젝트 루트 PowerShell에서 한 번 실행한다.

```powershell
./Scripts/SetupAnimationAnalysis.ps1
```

UE 5.8에 포함된 Python 3.11로 numpy 1.26.4, scipy 1.13.1, ruptures 1.1.10을 설치한다. 설치 대상은 README가 있는 새 `Saved/Tasks/*-anim-analysis-environment-*/packages`이며 전역 Python/엔진 패키지는 수정하지 않는다. 명령은 requirements 해시가 일치하는 최신 성공 환경을 찾는다. 첫 설치에는 인터넷이 필요하다. 이 폴더는 분석 도구를 쓰는 동안 보관한다.

3. 에디터의 **Output Log 하단 명령 종류를 Cmd**로 선택한다. `AnimAnalysis.*`는 Python 코드가 아니다. 일반 에디터에서 실행하며, 패키지 게임/서버의 런타임 콘솔에는 등록하지 않는다.

```text
AnimAnalysis.Help
AnimAnalysis.SelfTest
```

## 3. 단계별 구현·검증 관문

| 단계 | 구현 및 산출물 | 디버그 명령 | 통과 조건 |
|---|---|---|---|
| A0 분석 환경 | 고정 버전 의존성·수치 테스트 | `AnimAnalysis.SelfTest` | 알려진 경계, 정지, Quaternion 부호, 속도/시간, 오류 입력 테스트 PASS |
| A1 관절 추출 | 전체 Bone 포즈·Root 궤적·시간 매핑·출처 | `AnimAnalysis.Extract <asset>` | 첫/끝 샘플 포함, Bone 누락/NaN 없음, 단조 시간, 에셋 해시 기록 |
| A2 특징·분리 | 속도·각속도·접지 proxy·PELT | `AnimAnalysis.Analyze latest` | 구간이 빈틈/중복 없이 전체 클립을 덮음. 경계 이유와 설정 기록 |
| A3 재사용 자료 | JSON 및 UE DataAsset 계약 | `AnimAnalysis.Build latest` | 궤적/구간 시간 보존, UE 자료형 변환, reviewed=false 유지 |
| A4 검사·시각 검토 | 검증 결과, HTML 포즈/그래프/히트맵 | `AnimAnalysis.Validate latest` / `AnimAnalysis.Preview latest` | 원본과 분석 해시 일치, 경계·포즈 직접 확인 가능 |
| A5 실제 에셋 통합 | 같은 디버그 명령으로 한 번에 실행 | `AnimAnalysis.Run <asset>` | Extract→Analyze→Build→Validate PASS |

### A1: 관절 추출

```text
AnimAnalysis.Extract /Game/Combat/Animations/AM_HitReaction_Front Mesh=/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple Hz=60 Rate=1
AnimAnalysis.Validate latest
```

- 모든 mesh Bone의 부모 인덱스, component 위치, Root 기준 위치, 부모 기준 위치/회전/scale을 저장한다.
- 위치 cm, 시간 s, Quaternion xyzw. `component`는 애니메이션 에셋 좌표이며 게임 월드 좌표가 아니다.
- `time`은 선택 구간 시작부터의 재생 시간, `asset_time`은 Montage/Sequence 시간, `sequence_time`과 `source_frame`은 연결된 Sequence 시간/프레임이다. 보간 프레임은 실수로 기록한다.
- Raw 포즈를 선택한 Mesh에 평가한다. Root Lock을 무시해 원본 Root 움직임을 유지한다. 엔진 Root Motion 추출 궤적은 별도 저장한다.
- 마지막 프레임은 길이가 60Hz에 정확히 나누어지지 않아도 포함한다. 속도는 실제 시간 차로 계산한다.

V1 지원: 동일 Skeleton의 비가산 Sequence, 또는 **1 Slot·1 Sequence Segment·양수 속도·반복 없음**인 Montage. `Section=이름`은 그 Section 하나만 추출하며 다음 Section으로 분기하지 않는다. Section 미지정 시 연결 없는 단일 Section을 요구한다. RateScale/Segment 속도/사용자 Rate를 구분한다. 복수 Slot/Segment, TimeStretch, 가산 애니메이션은 명시적으로 거부한다. 분석 시간은 60초, 샘플은 10~240Hz, 총 Bone 샘플 수는 200만 이하로 제한한다.

RBF 비용은 메모리 사용량이 샘플 수의 제곱으로 증가하므로 4,096개 시간 구간까지만 허용한다. 더 긴 클립은 `Model=l2`, 짧은 Section 또는 낮은 추출 Hz를 사용한다.

### A2: PELT 분석

```text
AnimAnalysis.Analyze latest Model=rbf Penalty=3 MinDuration=0.1
AnimAnalysis.Preview latest
```

같은 추출을 다른 설정으로 재분석하면 원본을 복사한 새 작업 폴더가 생기므로 이전 보고서를 보존한다. `latest` 대신 Output Log에 표시된 작업 폴더를 따옴표로 묶어 지정할 수 있다.

```text
AnimAnalysis.Analyze "C:/.../Saved/Tasks/원본작업폴더" Model=l2 Penalty=1
```

N개 포즈에서 N−1개 시간 구간 특징을 만든다. Root 방향별 속도/각속도, 설정 가능한 신체 부위별 평균 속도/각속도, 발 접지 proxy를 사용한다. cm/s와 rad/s의 고정 스케일을 설정에 맞춰 정규화하고 짧은 이동 평균 후 `ruptures.Pelt(jump=1)`을 적용한다. PELT가 반환하는 마지막 N은 종료점이며 별도 경계로 중복 생성하지 않는다. 지나치게 짧거나 일정한 신호는 한 구간으로 유지한다.

분류는 `root_travel`, `articulation`, `low_motion`이다. 자동으로 공격 판정/피격 의미를 확정하지 않는다. 발 접지는 에셋 좌표의 낮은 높이와 속도에 기반한 proxy이며 실제 충돌 검사가 아니다. 다른 Skeleton은 `Profile=설정파일경로`로 그룹·발 이름을 지정한다. 필수 그룹 전체 또는 발이 없으면 실패하며 일부 누락은 경고한다.

### A3: I1 후보 Library와 I2 Warp 초안

```text
AnimAnalysis.Build latest
```

기본 명령은 `library.json`을 만들고 메모리의 `UHitWallReactionLibrary`로 변환해 확인한다. Content Browser에 보관할 때만 새 에셋 경로를 지정한다.

```text
AnimAnalysis.Build latest Asset=/Game/Combat/Analysis/DA_HitReaction_Front_PELT
```

같은 이름의 기존 에셋은 덮어쓰지 않는다. 원본 Sequence/Montage, Notify, Section도 자동 편집하지 않는다.

I1은 후보별 시간 궤적, 원본 Animation/Section, SourceHash/AnalysisHash, Phases를 읽을 수 있다. I2는 `ProposedWarpWindows`의 `AssetStartSeconds`/`AssetEndSeconds`를 Montage 시간으로 사용할 수 있다. 재생 속도가 달라지면 베이크 설정을 맞추어 다시 분석해야 한다.

Warp 후보는 Root Motion 활성화, 최소 평면 이동, 고정 회전, 수직/측방 변위 한도, 역행 없음 조건을 만족하는 이동 구간에서 생성한다. 현재 Warp 구현의 기본 측방 허용치는 0.1cm이며 프로필도 같은 값에서 시작한다. 후보가 0개이면 거부 이유를 출력한다. 허용치를 통과했다고 환경 충돌이나 발 미끄러짐이 안전하다는 뜻은 아니다.

Library의 `FindCandidate`는 기본적으로 `bReviewed=true`인 후보만 반환한다. 이번 생성 결과는 항상 false다. 검토 전에 데이터를 보려면 명시적으로 `bRequireReviewed=false`로 조회한다. 이 값은 런타임 안전 검사를 대체하지 않는다.

### A4: 사용자가 직접 확인할 항목

```text
AnimAnalysis.Validate latest
AnimAnalysis.Preview latest
```

2026-11-09 보완: Windows의 Preview는 설치된 Whale, Edge, Chrome, Firefox 순으로 브라우저를 찾아 직접 실행한다. HTML 연결 프로그램이 Visual Studio여도 브라우저에서 연다. 수정 전부터 켜 둔 에디터에서는 Python 모듈이 캐시되므로 에디터를 재시작한 뒤 명령을 실행한다. 수동으로 열 때는 브라우저에서 Ctrl+O를 누르고 해당 작업 폴더의 report.html을 선택한다.

1. 슬라이더/재생 버튼으로 프레임을 확인한다. X–Z/Y–Z 투영을 바꿔 상체와 하체 움직임을 본다.
2. Root 속도 그래프의 PELT 경계와 관절 히트맵이 실제 움직임 변화에 대응하는지 확인한다.
3. 구간별 시작/끝의 재생 시간과 에셋 시간을 확인하고 경계 근거를 읽는다.
4. 녹색 Warp 후보가 있다면 같은 구간을 에디터 원본 애니메이션에서 확인한다. 후보가 없으면 거부 이유를 본다.
5. 의미상 경계의 정확도는 직접 표기한 기준 경계와 비교해야 한다. 자동 테스트 PASS는 자연스러움 평가 완료를 뜻하지 않는다.

### A5: 한 번에 실행 / 반복 검증

```text
AnimAnalysis.Run /Game/Combat/Animations/AM_HitReaction_Front
AnimAnalysis.Preview latest
```

```powershell
./Scripts/TestAnimationAnalysis.ps1
```

PowerShell 검사는 별도 숨김 에디터에서 동일한 콘솔 명령을 실행한다. 숫자 테스트, 실제 피격 Montage 추출/분리/Library, 부모 계층으로 재구성한 component/Root-relative 좌표 오차(<0.001cm), 원본 Sequence와 Montage 결과 일치, Rate=2의 시간/거리, 잘못된 Section/에셋 거부, 미검토 Library 조회 거부를 검사한다. `-CustomConfig=AnimationAnalysis`로 테스트 프로세스에만 `Config/Custom/AnimationAnalysis/DefaultGame.ini`를 적용한다. 이는 Toolset이 활성화한 GameFeatures에 필요한 GameFeatureData 스캔 규칙을 제공하며, 일반 게임 설정을 바꾸거나 오류를 무시하지 않는다.

## 4. 이후 I1/I2 실제 적용 관문

| 후속 단계 | 실제로 수행할 테스트 | 완료 조건 |
|---|---|---|
| I1 후보 등록 | 같은 Skeleton의 Long/Short/InPlace별 분석 후 후보 데이터 병합·태그·허용 범위 설정 | 원본 hash 일치, 중복 ID 없음, 무장애 재생과 궤적 오차 기준 통과 |
| I2 Window 검토 | 원본을 복제한 실험 Montage에 검토된 구간 적용, Warp 전후 궤적 기록 | 전체 경로 sweep, 발 미끄러짐·종료 포즈·과도한 압축/회전 확인 |
| 실행 연결 | 기존 ReactionWarpComponent/Planner가 검토된 후보와 시간 구간 사용 | 부적합 후보 fallback, 기본 CMC 충돌 유지 |
| 네트워크 | 서버·피격 소유자·관찰자에서 같은 계획과 재생 구간 비교 | 권위 판정, 시간 매핑, 교체/취소/replay 회귀 통과 |

이번 구현은 위 실행 연결을 완료했다고 주장하지 않는다. 특히 원본 에셋 분석은 AnimBP/블렌딩/IK/실제 지면/벽 접촉을 포함하지 않는다. 실제 게임 적용을 시작하면 기존 멀티플레이 검증 규칙을 적용한다.

## 5. 산출물 보관과 재현

매 실행은 `Saved/Tasks/<시각>-joint-analysis-<고유값>/README.md`부터 만든다. 원본, 분석, 라이브러리, 설정, 검증 파일을 함께 보관한다. 원본과 에셋 파일 해시가 다르면 재추출해야 한다. 작업 폴더는 Git으로 공유되지 않으므로 필요한 결과는 팀 공유 위치에 폴더 전체로 복사한다.

`report.html`은 단독으로 열어 볼 수 있지만, 재분석을 위해서는 export.json과 설정도 필요하다. 필수 숫자 검사와 함께 사람이 표기한 경계·에디터 영상 검토 결과를 후속 실험 기록에 남긴다.

## 6. 구현 검증 결과 (2026-11-08 KST)

- Editor 및 Game의 Win64 Development 빌드 통과. Editor 전용 추출 모듈은 Runtime 의존성에 추가하지 않았다.
- 수치 테스트 15개 통과. 위치 변화 없이 관절만 회전하는 경우의 분류와 RBF 메모리 제한도 포함한다.
- 실제 `AM_HitReaction_Front`: 0.75초, 89 Bone, 46 포즈, 기본 RBF 설정에서 3구간. 측방 이동 허용치 초과로 Warp 후보는 0개이며 거부 이유를 기록한다.
- 실제 Sequence/Montage 포즈 일치, Rate=2 시간/거리, 부모 계층 좌표 재구성, 오류 입력 거부, 미검토 후보 조회 차단, 명시적인 초안 조회, 단계별 콘솔 명령, 이전 분석을 보존하는 재분석을 통과했다.
- `Preview`의 보고서 URI 전달과 HTML의 프레임 탐색·투영축 변경·재생·마지막 프레임 동작을 검사했다. HTML 로직 검사는 `test_report.js`이며 Node가 설치된 환경에서 `node Scripts/AnimationAnalysis/test_report.js <report.html>`로 재실행할 수 있다. 기본 분석 실행에는 Node가 필요 없다.
- 브라우저 자동 접근 도구의 창 식별 오류로 실제 화면 배치의 육안 검증은 수행하지 못했다. 에디터의 최종 블렌딩/IK와 애니메이션 자연스러움은 사용자가 보고서와 원본 재생을 비교해 검토해야 한다.
- DataAsset은 메모리 내 생성·조회·시간 데이터 변환까지 검사했다. 선택적인 `Asset=/Game/...` 디스크 저장 경로는 아직 자동 통합 검사 범위에 포함하지 않는다.
- 복제/전투 실행을 변경하지 않아 이번 도구 검사에서 멀티플레이를 실행하지 않았다. I1/I2 런타임 연결 시 별도 검증이 필요하다.

상세 원본과 실행 로그는 `Saved/Tasks/20261108-094207382-joint-segmentation-development-4d092bc3/README.md`의 연결 목록을 따른다. 실패했던 초기 빌드·환경 설치·commandlet 실행도 보존한다. 최종 통합 테스트는 `20261108-100717358-joint-analysis-smoke-a4e40feb`에서 종료 코드 0과 PASS를 확인했다.

## 7. 기술 근거

- [Epic UAnimPoseExtensions](https://dev.epicgames.com/documentation/unreal-engine/API/Editor/AnimationBlueprintLibrary/UAnimPoseExtensions): 시각별 포즈 평가. 실제 함수 형식은 설치 UE 5.8 헤더로 확인했다.
- [ruptures PELT](https://centre-borelli.github.io/ruptures-docs/code-reference/detection/pelt-reference/): model/min_size/jump/penalty 기반 오프라인 변화점 탐지.
- [ruptures 저장소](https://github.com/deepcharles/ruptures): BSD-2-Clause, 패키지를 의존성으로 사용하며 소스를 복사하지 않는다.

PELT와 운동학 특징의 결합, 구간 라벨·Warp 후보 규칙은 프로젝트 구현이며 논문이 이 피격 에셋의 의미 구간이나 Warp 안전성을 보장하는 것은 아니다.
