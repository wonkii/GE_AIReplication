# M1. 서버 권한 Enemy AI와 기본 이동 복제

> 상태: 다중 Enemy Spline 코드 구현. 실제 이동은 수동 확인 필요. [설정 가이드](M1_MultiEnemy_Spline_설정.md) · [파일 책임](../Architecture/SharpMovement_책임분리.md) · 선행: [M0](../M0/M0_실험_설정.md) · 다음: [M2](../M2/M2_Impulse_재현.md)

기존 `Variant_Combat/AI/CombatEnemy.*`는 참고 코드다. 현재 전용 Enemy는 각자의 Spline을 서버 CMC 입력으로 추종하며 개별/전체 제어를 지원한다. 아래 내용은 원래 M1 목표이며 실제 설정은 위 가이드를 따른다. 서버·클라이언트 이동의 실행 검증은 별도로 필요하다.

## 담당 파일과 폴더

아래 경로는 저장소 루트 기준이다. 파일이 기존 단계에서 생성되었다면 이 단계에서는 확장한다.

| 경로 | 역할 |
|---|---|
| `Source/GE_AIReplication/SharpMovement/Enemy/SharpEnemyCharacter.h/.cpp` | 서버 권한 ACharacter, CMC, 이동 복제 |
| `Source/GE_AIReplication/SharpMovement/Enemy/SharpEnemyAIController.h/.cpp` | 서버 AI Possession과 소유 해제 |
| `Source/GE_AIReplication/SharpMovement/Movement/SharpSplineFollowerComponent.h/.cpp` | Enemy별 서버 이동 입력 |
| `Source/GE_AIReplication/SharpMovement/Experiment/SharpEnemyExperimentComponent.h/.cpp` | 개별 실행 상태와 정책 수명 |
| `Content/SharpMovementExperiment/Blueprints/BP_SharpEnemy.uasset` | 전용 Enemy 배치와 표시 설정 |

## 개발 및 검증 사항

**목표:** AI의 서버 이동과 클라이언트 Simulated Proxy 표시를 확인한다.

**구현 작업**

1. `ASharpEnemyCharacter`를 `ACharacter`에서 만들고 `bReplicates=true`, `bReplicateMovement=true`로 설정한다.
2. AIController가 서버에서만 동일한 시작점·목적지로 이동 명령을 내리게 한다. 첫 실험은 충돌이 없는 직선 이동이다.
3. Dedicated Server 1개와 Client 2개에서 Enemy의 Authority/Simulated Proxy 역할, 위치·속도·Movement Mode를 디버그 표시한다.
4. Actor relevancy와 dormancy 때문에 테스트 중 Enemy가 갱신 대상에서 빠지지 않는지 확인한다.

**산출물:** 재현 가능한 AI 이동 장면과 역할 검증 로그.

**완료 기준:** 서버의 한 AI가 두 클라이언트에 표시되며, 클라이언트가 AI 이동의 권한을 갖지 않는다.

공식 참고: [CMC의 역할별 이동 흐름](https://dev.epicgames.com/documentation/en-us/unreal-engine/understanding-networked-movement-in-the-character-movement-component-for-unreal-engine), [AI MoveTo](https://dev.epicgames.com/documentation/unreal-engine/BlueprintAPI/AI/AIMoveTo).
