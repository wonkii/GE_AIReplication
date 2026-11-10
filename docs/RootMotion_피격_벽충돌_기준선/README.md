# Root Motion 피격·벽 충돌: 현재 구현과 연구 개발 계획

기준일: **2026-11-02 KST**. 공격·피격은 기존 `Lv_Jang`의 Player/Sharp Enemy에 통합했다. 공통 Combat 컴포넌트·피격 인터페이스와 Sharp 어댑터를 사용하며 전용 Character·레벨·BP는 제거했다. 실행·확장은 [04](04_공격피격_실행.md)를 따른다. 이 폴더는 정량 연구 계획의 진입점이며 Legacy는 변경 이력으로 참조한다.

## 1. 무엇을 개발하는가

**플레이어가 정면에서 공격했을 때, 피격 캐릭터가 주변 공간에 맞는 Root Motion 동작을 실행하고 서버·클라이언트가 같은 피격 행동 계획을 따르도록 개발한다.** 벽 관통, 비의도적 상승, 큰 위치 보정을 실제 네트워크 비용과 함께 검증하는 것이 최종 목표다.

첫 단계는 **Player → 서버 Enemy → 뒤쪽 정적 벽**의 기본 CMC 기준선이다. 이후 안전 목표와 벽 반응, 환경별 후보 선택, 행동 계획 공유를 추가한다. 소유 플레이어 피격까지 확장해야 Autonomous Proxy의 correction·replay에 관한 결론을 낼 수 있다.

```text
현재 기능
  좌클릭 → 서버 공격 승인 → Notify/Socket Box 명중 → Enemy 피격 Montage
                                                     ↓
다음 작업                                      기본 CMC로 벽 충돌
  Trial·계측·영상 → 기본 기준선 확보
                       ↓
  안전 경로/목표·벽 반응 → 환경별 피격 후보 선택
                       ↓
  서버 행동 계획 공유 → 소유 플레이어 replay → 동일 비용 비교
```

## 2. 현재 어디까지 구현되었는가

| 항목 | 상태 | 근거와 남은 일 |
|---|---|---|
| Montage Notify별 다음 1타 입력·01→02→03 연결 | 구현됨 | ComboInputWindow/MeleeAttackComponent. 구간 밖 입력은 폐기, 정량 측정은 1타만 사용 |
| Notify State·Socket Box·서버 명중 판정 | 구현됨 | 공통 피격 인터페이스, overlap/sweep, 중복 타격 방지, 차폐 검사와 방향 무관 접촉 |
| Enemy 단일 Root Motion 피격 | 구현됨 | HitReactionComponent, ReactionId 중복 방지·상태 복제 |
| 기존 Lv_Jang 통합·공통 애니메이션 생성 | 구현됨 | ConfigureJangCombat.py, CombatAssetSetup, SharpCombatAdapterComponent |
| Lv_Jang 기능 검사 | TestJangCombat.ps1 | 현재 통합 검사 결과는 작업별 Saved README/summary로 확인. 이전 01:22 KST 전용 레벨 기록과 구분 |
| 반복 Trial·자동 완료/tail·정량 Recorder | 미구현 | 가장 먼저 개발할 항목 |
| 안전 목표·Motion Warping·벽 반응 전환 | 미구현 | 기준선 이후 추가. 플러그인/모듈 의존성도 아직 없음 |
| 환경별 다중 후보 선택·행동 계획 공유 | 미구현 | 논문 원리의 프로젝트 적용안 |
| 소유 플레이어 피격·saved move 연계 | 미구현 | AI 관찰 실험과 별도 검증 필요 |
| N0/N2 정량 비교·동일 bytes/s 비교 | 미수집 | 기능 검사 통과와 구분 |

**중요한 용어 정리:** 기존 문서의 “긴 피격”은 고정된 후방 이동 후보를 뜻하는 목표 명칭이다. [현재 실행 문서](04_공격피격_실행.md)의 구현은 길이 **0.75초인 단일 Montage**다. 장시간 피격이 준비되었다는 뜻이 아니다. 무장애 후방 최대 변위 L과 자연 종료 시간 T를 pilot에서 확정하고, 벽 충돌 관찰 시간이 부족하면 실험용 후보를 교체한 뒤 모든 조건에서 고정한다.

## 3. 어떤 논문을 어떻게 참고하는가

| 참고 자료 | 개발에 연결할 부분 | 적용 시점 |
|---|---|---|
| [Environment-aware Motion Matching, 2025](https://arxiv.org/html/2510.22632v1) | 공간을 고려한 피격 후보 평가·선택 | 기준선과 안전 실행 이후 |
| [A Physics-Aware Dead Reckoning Technique…, 2012](https://mural.maynoothuniversity.ie/id/eprint/4164/1/SMcL_ISSC2012_physics.pdf) | 접촉 진입·유지·해제 기록, 후속 접촉 기반 전송 실험 | 계측부터 참고, Burst는 별도 후속 비교 |
| [Predictive Dead Reckoning for Online Peer-to-Peer Games, 2024](https://ece.uwaterloo.ca/~sl2smith/papers/2023TOG-Predictive_Dead_Reckoning.pdf) | 이동·충돌·표시 복구를 구분하는 구조와 평가 | 계측·실행·분석 설계 |
| [Epic CMC](https://dev.epicgames.com/documentation/en-us/unreal-engine/understanding-networked-movement-in-the-character-movement-component-for-unreal-engine), [Motion Warping](https://dev.epicgames.com/documentation/en-us/unreal-engine/motion-warping-in-unreal-engine) | 엔진 이동·복제 기반, 제한된 목표 정렬 | 현재 기반과 안전 실행 |

논문의 직접 재현과 이번 프로젝트의 응용 설계를 구분한다. 특히 **서버 행동 계획 공유와 owner replay 통합은 이번 개발의 검증 대상**이며, 위 논문들이 UE Root Motion 피격에서 효과를 입증한 것으로 서술하지 않는다. 논문별 서지·읽을 절·적용 범위는 [05](05_참고논문과개발연결.md)에 정리했다.

## 4. 읽는 순서

| 문서 | 답하는 질문 |
|---|---|
| [01. 현재 구현과 기준선 개발](01_구현계획.md) | 실제로 있는 코드와 다음에 작성할 코드는 무엇인가? |
| [02. 맵과 애니메이션 설정](02_맵과애니메이션설정.md) | 에셋·벽 거리·서버 tick·실행 조건을 어떻게 고정하는가? |
| [03. 측정과 완료 기준](03_측정과완료기준.md) | 관통·상승·보정을 어떻게 분리 측정하는가? |
| [04. 공격·피격 실행](04_공격피격_실행.md) | 현재 구현을 어떻게 실행하고 조정하는가? |
| [05. 참고논문과 개발 연결](05_참고논문과개발연결.md) | 어떤 논문의 어느 원리를 가져오는가? |
| [06. 후속 개선 상세 설계](06_후속개선_상세설계.md) | 안전 실행·후보 선택·계획 공유를 어떻게 구현하는가? |
| [07. 단계별 작업과 평가](07_단계별작업과평가.md) | 어떤 순서로 개발하고 무엇을 통과해야 완료인가? |
| [08. 관절 추출·PELT 개발과 테스트](08_관절추출_PELT_개발계획과테스트.md) | 지정 애니메이션을 분석하고 I1/I2 초안을 디버그 명령으로 어떻게 확인하는가? |
| [09. 다구간 Warp 구현 명세와 검증](09_다구간_Warp_구현명세와검증.md) | 구간 정책·연속 보정·계획 공유의 요구사항은 무엇인가? |
| [10. 다구간 Warp 테스트 설정](10_다구간_Warp_테스트설정.md) | 구현된 모드·정책·비교 명령을 어떻게 직접 시험하는가? |
| [11. 다중 벽 자동 순회 개발계획](11_다중벽_자동순회_개발계획.md) | YHJ의 여섯 벽·경사 지점에서 모드별 피격을 자동 관찰하려면 무엇을 개발해야 하는가? |
| [12. 다중 벽 순회 실행](12_다중벽_순회_실행.md) | 실제 구현된 명령을 어떤 순서로 입력하고 원본/보정을 관찰하는가? |

기준선 작업은 01→02→03→04, 연구 전체 이해는 README→05→06→07 순서가 적합하다.

## 5. 첫 완료 목표와 운영 규칙

다음 완료 목표는 **반복 가능한 기본 CMC 기준선과 원본 CSV·영상 확보**다. 무장애에서 정상 Root Motion을 확인하고, 벽 거리 4조건 × 네트워크 2조건의 결과를 수집한다. 문제가 없으면 0건으로 보고하고 가설을 재검토한다.

기준선에서는 고정 피격 후보 1개, 정적 벽, 기본 CMC 충돌·Root Motion 복제·smoothing을 유지한다. Impulse·래그돌·Spline·커스텀 등속 Predictor는 섞지 않는다. 실제 화면 품질은 렌더링 영상으로 확인한다.

이 폴더는 Git으로 관리할 설계 소스다. 새 빌드 로그·검사 결과·실험 원본·분석 보고서는 [Saved 관리 규칙](../Legacy/Workflow/Saved_결과물_관리.md)에 따라 작업별 README를 먼저 만들고 저장한다. 기존 소스·에셋·Saved 결과는 임의로 이동하거나 덮어쓰지 않는다.
