# AI Replication 프로젝트 구성

기존 `GE_AIReplication` 프로젝트에 AI 이동 예측·복제 연구를 위한 모듈 경계를 구성했다. 인계서의 가칭 프로젝트로 이름을 바꾸지 않고, 기존 템플릿과 에셋 참조를 유지한다. 프로젝트 엔진 설정은 UE 5.8이다.

## 시스템 구분

| 모듈 | 역할 |
|---|---|
| `AIReplicationCore` | 공통 이동 상태, 예측 및 오차·갱신 정책 계산을 구현할 영역 |
| `AIReplicationNet` | 스냅샷 전송, 복제 및 수신 상태 재구성을 구현할 영역 |
| `GE_AIReplication` | 인계서의 Lab 역할. 기존 캐릭터·AI·UI와 실험 환경을 통합 |

의존성은 `GE_AIReplication → AIReplicationNet → AIReplicationCore`이며, 통합 모듈은 Core도 직접 참조한다. Core는 Actor·통신·UI에 의존하지 않는다.

이 현재 공개 인터페이스에 필요한 엔진 Core만 공개 의존성으로 두고, 프로젝트 모듈 간 연결은 비공개 의존성으로 시작한다. Net에 Actor나 복제 기능을 구현할 때 `CoreUObject`·`Engine` 등을 실제 사용 위치에 맞게 추가한다. 기존 통합 모듈의 AI·UI 의존성은 템플릿에서 사용하므로 유지한다.

## Git 관리

- 빌드 산출물·캐시·IDE 개인 설정·생성된 솔루션과 로컬 설정은 제외한다. 공유 `Config`, `Build` 리소스와 `.vsconfig`는 추적 가능하다.
- 소스·문서는 LF, Unreal 설정·프로젝트 파일은 CRLF로 줄바꿈을 통일한다.
- `.uasset`·`.umap`은 위치에 관계없이 Git LFS와 잠금 대상이다. 일반 이미지·모델·음원 원본도 지정 확장자에 LFS를 적용한다.
- `Content` 전체를 LFS로 처리하던 규칙을 확장자 기준으로 바꿔, 그 안의 문서·텍스트도 일반 Git으로 관리할 수 있다. 팀원은 Git LFS를 설치해야 하며 에셋 편집 전 잠금을 사용하는 것이 좋다.


서버·클라이언트 전용 타깃, 연구 기능, CI와 원격 저장소 설정은 후속 작업이다. LFS 원격 다운로드·공동 잠금 동작과 서버·클라이언트 실행은 이번 설정만으로 검증된 것이 아니다.

구성 기준: [Epic 모듈 문서](https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-modules)
[Git 속성 문서](https://git-scm.com/docs/gitattributes).
