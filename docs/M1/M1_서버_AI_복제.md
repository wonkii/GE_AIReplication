# M1. 서버 권한 Enemy AI와 기본 이동 복제

> 상태: 개발 예정. [전체 아키텍처](../Architecture/Unreal_Sharp_Movement_마일스톤_구현계획.md) · 선행: [M0](../M0/M0_실험_설정.md) · 다음: [M2](../M2/M2_Impulse_재현.md)

기존 `Variant_Combat/AI/CombatEnemy.*`는 참고 코드다. 전용 Enemy의 서버·클라이언트 실행은 아직 검증되지 않았다.

## 담당 파일과 폴더

아래 경로는 저장소 루트 기준이다. 파일이 기존 단계에서 생성되었다면 이 단계에서는 확장한다.

| 경로 | 역할 |
|---|---|
| `Source/GE_AIReplication/SharpMovement/Enemy/SharpEnemyCharacter.h/.cpp` | 서버 권한 ACharacter, CMC, 이동 복제 |
| `Source/GE_AIReplication/SharpMovement/Enemy/SharpEnemyAIController.h/.cpp` | 서버 AI 이동 명령 |
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
