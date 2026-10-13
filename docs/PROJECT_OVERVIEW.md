# AI Replication 프로젝트 구성

기존 `GE_AIReplication` 프로젝트에 AI 이동 예측·복제 연구를 위한 모듈 경계를 구성했다. 인계서의 가칭 프로젝트로 이름을 바꾸지 않고, 기존 템플릿과 에셋 참조를 유지한다. 프로젝트 엔진 설정은 UE 5.8이다.

현재 Enemy 이동은 개별 Spline 기반으로 제어한다. [다중 Enemy 설정과 수동 확인](M1/M1_MultiEnemy_Spline_설정.md), [파일 책임과 확장 경계](Architecture/SharpMovement_책임분리.md)를 참고한다. 기존 맵은 Spline 배치와 Manager 연결을 수동으로 설정해야 한다.

Impulse·Detector·Recorder·클라이언트 Mesh 예측·그룹 반복·통합 보고서는 현재 다중 Enemy 구조에 연결되어 있다. 설정과 사용 순서는 [실험 기능 안내](Architecture/SharpMovement_실험기능.md)를 참고한다.

## 시스템 구분

| 모듈 | 역할 |
|---|---|
| `AIReplicationCore` | Actor·Overlap·통신에 의존하지 않는 이동 샘플 급변 판정 |
| `AIReplicationNet` | 이동 사건 Snapshot 데이터 계약 |
| `GE_AIReplication` | 인계서의 Lab 역할. 기존 캐릭터·AI·UI와 실험 환경을 통합 |

의존성은 `GE_AIReplication → AIReplicationNet → AIReplicationCore`이며, 통합 모듈은 Core도 직접 참조한다. Core는 Actor·통신·UI에 의존하지 않는다.

통합 모듈의 공개 헤더가 Core의 판정 설정과 Net의 Snapshot 타입을 사용하므로 두 모듈은 공개 의존성이다. 파일 기록·JSON과 Editor 배치 지원은 비공개 의존성으로 유지한다. 복제와 화면 보정은 통합 모듈의 역할별 컴포넌트가 담당한다.

## Git 관리

- 빌드 산출물·캐시·IDE 개인 설정·생성된 솔루션과 로컬 설정은 제외한다. 공유 `Config`, `Build` 리소스와 `.vsconfig`는 추적 가능하다.
- 소스·문서는 LF, Unreal 설정·프로젝트 파일은 CRLF로 줄바꿈을 통일한다.
- `.uasset`·`.umap`은 위치에 관계없이 Git LFS와 잠금 대상이다. 일반 이미지·모델·음원 원본도 지정 확장자에 LFS를 적용한다.
- `Content` 전체를 LFS로 처리하던 규칙을 확장자 기준으로 바꿔, 그 안의 문서·텍스트도 일반 Git으로 관리할 수 있다. 팀원은 Git LFS를 설치해야 하며 에셋 편집 전 잠금을 사용하는 것이 좋다.


서버·클라이언트 전용 패키징 타깃, CI와 LFS 공동 잠금 검증은 별도 작업이다. 현재 실행 도구는 Unreal Editor 실행 파일의 서버·게임 모드를 사용한다. 자동화 테스트는 이번 기능 구현에 추가하지 않았다.

구성 기준: [Epic 모듈 문서](https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-modules)
[Git 속성 문서](https://git-scm.com/docs/gitattributes).
