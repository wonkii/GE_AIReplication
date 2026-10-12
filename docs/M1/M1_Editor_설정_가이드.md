# M1 Spline 이동: Editor 설정 가이드

> M1은 수동 시작이다. 자동화 테스트·에셋 생성 스크립트는 추가하지 않는다. 아래 절차로 Blueprint와 맵을 직접 설정한다.

## 1. 책임과 설정 위치

| 클래스 | 책임 | Editor에서 설정할 항목 |
|---|---|---|
| `SharpEnemyCharacter` | Capsule·Mesh·CMC, 기본 이동 복제, 역할 진단 | Mesh·Animation·충돌, CMC 물리 설정, ActorId, Replication, 진단 표시 |
| `SharpEnemyAIController` | 서버의 추종 준비·시작·중단·재개 | Enemy의 AI Controller Class로 지정 |
| `SharpSplinePath` | 목표 경로 형상과 식별자 | Spline 점·Tangent·Point Type, PathId, Revision, 경로 검증 허용치 |
| `SharpSplineFollowerComponent` | 서버에서만 이동 입력 생성, 이동 후 실제 진행도 관측 | 속도·look-ahead·방향·끝점 감속·정체 제한·공중 입력 |
| `SharpExperimentManager` | 실험 식별자·참조 연결, 수동 Trial 제어 | Settings, Enemy, Path, Mode, Baseline Profile, 시작 높이·방향 |

모든 이동 수치는 초기 기본값이며 Editor에서 바꿀 수 있다. 실행 구간의 위치는 CMC가 결정한다. Spline을 이동 플랫폼처럼 매 틱 좌표에 적용하지 않는다.

## 2. Blueprint와 맵 설정

### Enemy

1. C++ 빌드 후 Content Browser에서 `SharpEnemyCharacter`를 부모로 Blueprint를 만든다. 권장 위치는 `/Game/SharpMovementExperiment/Blueprints/BP_SharpEnemy`다.
2. Mesh Component에 표시할 Skeletal Mesh와 Animation Blueprint를 지정한다. Capsule 반경·반높이와 Mesh 상대 위치·회전을 모델에 맞춘다. Skeletal Mesh 물리 시뮬레이션과 Root Motion은 첫 기준선에서 사용하지 않는다.
3. Class Defaults의 `AI Controller Class`는 `SharpEnemyAIController`, `Auto Possess AI`는 `Placed in World or Spawned`로 둔다. 플레이어 Auto Possess는 끈다.
4. `Use Controller Rotation Yaw`를 끄고, Character Movement의 `Orient Rotation to Movement`를 켜면 이동 방향을 따라 회전한다. 이 값은 실험에 맞춰 수정할 수 있다.
5. `Replicates`, `Replicate Movement`를 켜고 `Only Relevant to Owner`를 끈다. 첫 단일 AI 실험은 `Always Relevant`를 켜고 `Net Dormancy`를 `Never`로 설정한다.
6. `SharpMovementExperiment` 맵에 Enemy 한 개를 배치하고 인스턴스의 `Actor Id`를 `Enemy01`처럼 지정한다. 같은 월드에서 중복되지 않아야 한다.

### Spline 경로

1. `SharpSplinePath`를 부모로 `BP_SharpSplinePath`를 만든 뒤 맵에 배치한다. C++ Actor를 직접 배치해도 된다.
2. 인스턴스의 `Path Id`를 `Straight01`, `Path Revision`을 `1`로 지정한다. 경로를 바꿨다면 Revision도 직접 올린다.
3. Spline Component를 선택하고 점을 편집한다. 직선 기준선은 두 점을 약 1,200 cm 간격으로 놓고 Point Type을 Linear로 설정한다.
4. 점의 World Z를 바닥 표면 높이에 맞춘다. 경로는 Capsule 중심이 아닌 바닥 목표선이다. Manager가 Capsule 반높이와 시작 높이 여유를 더해 초기 배치한다.
5. 곡선은 점을 추가하고 Curve Point Type과 Tangent를 조절한다. 모든 점과 Tangent는 수평이어야 한다. `Closed Loop`는 끈다.
6. M1에서는 교차·자기 교차·루프·서로 가깝게 평행한 구간·입체 경로를 피한다. 전체 경로와 Enemy Capsule이 바닥 범위 안에 들어가도록 한다.

### Manager

기존 맵의 `BP_SharpExperimentManager` 인스턴스를 선택한다.

| 항목 | 첫 실행 권장 설정 |
|---|---|
| Settings | 기존 `DA_SharpExperimentSettings` |
| Configured Mode | `Default` |
| Configured Run Id | 원하는 이름, 예: `M1-straight-01`; 빈 값은 서버 GUID |
| Configured Trial Id | `1` |
| Enemy | 배치한 `BP_SharpEnemy` 인스턴스 |
| Path | 배치한 `BP_SharpSplinePath` 인스턴스 |
| Baseline Profile | `EngineDefault` |
| Start Height Offset | 바닥 위 추가 여유, 기본 5 cm |
| Align Rotation to Path | 시작 방향 정렬 사용 여부 |

Enemy와 Path는 클래스가 아니라 **맵 인스턴스**를 연결한다. 하나의 Enemy에 Manager를 둘 이상 연결하지 않는다. BeginPlay는 이동을 자동 시작하지 않는다.

## 3. 추종 Component의 조절값

Enemy Blueprint에서 `SplineFollower` Component를 선택하고 `Sharp Follow → Settings`를 펼친다.

| 설정 | 기본값 | 조절 목적 |
|---|---:|---|
| Target Speed | 300 cm/s | CMC Max Walk Speed를 실행 중 제어하는 목표 속도 |
| Look Ahead Distance | 100 cm | 앞쪽 목표점을 얼마나 멀리 볼지; 크면 곡선·코너를 깎을 수 있음 |
| Reverse Direction | 꺼짐 | 켜면 Spline 마지막 점에서 첫 점으로 이동 |
| Acceptance Radius | 30 cm | 끝점 XY 거리의 완료 허용치 |
| Stop Speed Tolerance | 5 cm/s | 실제 속도의 완료 허용치 |
| End Slowdown Distance | 200 cm | 끝점 감속 시작 거리, Acceptance Radius보다 커야 함 |
| Minimum Approach Speed | 10 cm/s | 끝점 접근 중 최소 목표 속도, Target Speed 이하 |
| Blocked Timeout Seconds | 3 s | 진행이 없을 때 실패 처리, 0은 비활성 |
| Minimum Progress Distance | 1 cm | 정체 시간 초기화에 필요한 진행 거리 |
| Projection Slack Distance | 30 cm | 실제 위치의 Spline 투영 진행도 변화 허용 여유 |
| Projection Speed Multiplier | 2 | 실제 속도·틱 간격에 따른 진행도 변화 허용 배수 |
| Allow Input While Falling | 꺼짐 | 낙하 중 평면 이동 입력 허용; CMC Air Control도 별도 영향 |

중단·재개는 현재 위치에서 다시 추종한다. 중단은 신규 입력을 멈추며 CMC의 기존 속도·마찰·중력은 계속 적용된다. 순간 정지 기능과 다르다.

`Target Speed`는 실행 중 `Max Walk Speed`를 덮어쓰고 종료·리셋 시 기존 값으로 복구한다. 다른 CMC 수치는 코드에서 강제하지 않는다. `Max Acceleration`, `Braking Deceleration Walking`, `Ground Friction`, `Braking Friction Factor`, `Rotation Rate`, `Air Control`, `Gravity Scale`, `Network Smoothing Mode`는 Character Movement의 Details에서 직접 조절한다. 제동을 없애면 끝점에서 정상 정지하기 어려울 수 있다.

실행 중 경로 형상·Transform·추종 방향을 편집하지 않는다. 실행을 끝낸 뒤 경로와 설정을 수정하고 다시 준비한다. 준비 후 Path Actor Transform 변경은 실패로 처리한다.

## 4. 수동 시작과 반복 실행

### 가장 간단한 설정 확인

1. 실험 맵을 연다.
2. `Simulate` 또는 서버 인스턴스를 선택할 수 있는 PIE를 실행한다.
3. PIE 실행 인스턴스의 Manager를 선택한다. 필요하면 `F8`로 플레이어 조작에서 빠져나와 World Outliner에서 선택한다.
4. Details의 `Sharp Experiment → Trial` 버튼을 사용한다.

| 버튼 | 동작 |
|---|---|
| Prepare Trial | 현재 TrialId로 설정 검증·시작점 배치·Ready 상태 준비 |
| Start Trial | Ready에서 이동 시작; 최초 Idle에서는 준비 후 시작 |
| Suspend Trial | 신규 이동 입력 중단 |
| Resume Trial | 현재 실제 위치에서 추종 재개 |
| Reset Trial | 기존 추종 초기화, TrialId 증가, 시작점 배치 후 Ready; 다시 Start 필요 |

편집 중인 원본 맵의 버튼은 이동을 실행하지 않는다. **BeginPlay가 완료된 서버 또는 Standalone 인스턴스**에서만 동작한다. 클라이언트 인스턴스의 버튼도 이동을 시작하지 않는다.

`Trial Status`가 Failed라면 Output Log의 이유를 확인하고 수정한 뒤 `Prepare Trial` 또는 `Reset Trial`을 사용한다. Completed에서 같은 TrialId로 다시 실행하려면 Prepare, 새 TrialId로 실행하려면 Reset을 선택한다.

### 서버와 두 클라이언트 관찰

이번 첫 확인은 사용자와 합의한 **Listen Server + 클라이언트 2개** 구성이다.

1. Play 버튼 옆 설정/Advanced Settings에서 `Net Mode = Play as Listen Server`, `Number of Players = 3`, `Run Under One Process = 켜짐`으로 둔다.
2. 서버와 나머지 두 클라이언트를 실행한다. 서버 창에서 `F8`로 빠져나온 뒤 서버 PIE 월드의 Manager 인스턴스를 선택한다.
3. Details의 `Prepare Trial`, `Start Trial` 버튼을 누른다. Output Log에서 Manager가 Server 역할인지 확인한다.
4. 두 Client 창에서 Enemy를 관찰한다. 클라이언트 창의 플레이어 캐릭터와 실험 Enemy의 역할은 다르므로 `ActorId`로 구분한다.

Dedicated Server의 수동 제어 경로는 다음에 연결한다. 화면 없는 서버를 클라이언트 Details 버튼으로 조작할 수 있다고 가정하지 않는다. 이번 Listen Server 확인은 Dedicated Server 멀티프로세스 검증을 대체하지 않는다. 이후 클라이언트 시작 UI가 필요하다면 플레이어가 소유한 PlayerController의 서버 RPC를 사용하고 Enemy/Manager의 소유권은 서버에 유지한다.

## 5. 복제 빈도와 진단 설정

`Baseline Profile = EngineDefault`는 Enemy Blueprint의 실제 복제 빈도와 smoothing 설정을 유지한다. 여기서 “기본”은 현재 Blueprint에 설정된 값을 뜻하며 Blueprint에서 수동으로 바꿨다면 엔진 초기값과 다를 수 있다.

`ControlledLowRate`를 선택하면 서버에서 Settings 에셋의 `Normal Net Update Frequency`를 적용한다. 기존 최소 갱신 빈도가 이 값보다 크면 함께 낮춘다. 별도의 적응형 빈도 설정은 변경하지 않는다. 현재 M5 전송 정책이 연결되어 모든 Mode를 지원한다. [전체 기능·설정 목록](../Architecture/SharpMovement_구현_파일_및_설정.md)을 참고한다.

Enemy의 `Sharp Enemy → Debug`에서 역할 표시와 진단 로그를 켜고 주기를 조절한다. `DebugLabel` TextRender Component의 상대 위치·회전·글자 크기·색은 Editor에서 직접 설정한다. 모델 위에 배치하면 확인하기 쉽다. 전용 서버에서는 텍스트를 표시하지 않는다.

Output Log에서 `LogSharpExperiment`, `LogSharpEnemy`로 검색한다. 서버에는 `ROLE_Authority`, Client에는 `ROLE_SimulatedProxy`가 기대된다. 로그에는 RunId·TrialId·Mode·PathId·ActorId·위치·속도·Movement Mode·Mesh 위치가 포함된다. Spline 진행도는 서버만 계산하며 클라이언트의 Follower 수치는 측정값으로 사용하지 않는다. 이것은 낮은 빈도 진단 로그이며 M3의 정밀 Recorder가 아니다.

## 6. 직접 확인할 최소 항목

- Enemy 한 개가 서버 및 두 Client에 동일 ActorId로 표시되는지 확인한다.
- 직선·완만한 곡선 각각에서 이동과 끝점 정지가 가능한지 확인한다.
- Suspend 후 관성·제동이 유지되고 Resume이 현재 위치에서 동작하는지 확인한다.
- Reset 후 TrialId가 증가하고 원래 시작점에서 다시 출발하는지 확인한다.
- PathId·ActorId 누락/중복, 미연결 참조, 닫힌 Spline 등의 설정 오류가 로그에 표시되는지 확인한다.
- 클라이언트 버튼으로 서버 이동이 시작되지 않는지 확인한다.

Impulse·급회전·급정지 사건, Detector·Snapshot·예측기·매 틱 Recorder는 이번 M1 범위에 포함하지 않는다. Spline의 모서리만으로 즉시 Velocity가 꺾이는 것을 보장하지 않으며 실제 궤적은 CMC 설정에 따라 달라진다.
