# M1 에디터 설정

| 대상 | 설정 |
|---|---|
| Enemy Blueprint | 부모 `SharpEnemyCharacter`, Mesh·애니메이션·Mesh 상대 위치/회전 지정. 기존 Blueprint는 재컴파일 |
| Enemy | **Actor Id**를 월드 내 유일하게 지정. **Replicates / Replicate Movement** 켜기 |
| AI | `SharpEnemyAIController`, **Auto Possess AI = Placed in World or Spawned** |
| Path | `SharpSplinePath`의 **Path Id / Path Revision**, 열린 수평 Spline의 점·접선 편집. 시작점은 바닥 높이 |
| Manager | **Enemy / Path**, **Start Height Offset**, **Align Rotation To Path** 지정 |
| SplineFollower | **Target Speed / Look Ahead Distance / Acceptance Radius / End Slowdown Distance / Blocked Timeout** 조절 |
| CMC | 가속·제동·마찰·회전·Air Control·Gravity Scale·Mass는 Enemy의 Character Movement에서 설정 |

서버 PIE Manager의 **Prepare Trial → Start Trial**로 실행합니다. 자동 시작은 **Auto Start Trial / Auto Start Delay Seconds**로 설정합니다. 추종 설정은 Prepare에서 고정되므로 바꾼 뒤 다시 Prepare합니다. NavMesh·Behavior Tree는 이 Spline 입력 추종 경로에 필요하지 않습니다.

[전체 기능–파일 목록](../Architecture/SharpMovement_구현_파일_및_설정.md)
