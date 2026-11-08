# Client 콘솔에서 Enemy 제어

`GE_AIReplicationPlayerController`를 상속한 PlayerController에서 사용하는 개발용 명령이다. 현재 ThirdPerson PlayerController는 기존 클래스 리다이렉트를 통해 이 클래스를 사용한다. 실제 구현은 `Source/GE_AIReplication/SharpMovement/Debug/SharpExperimentDebugCommands.cpp`에 있다.

## 적용

1. PIE를 종료하고 에디터를 닫은 뒤 `GE_AIReplicationEditor / Win64 / Development`를 빌드하고 다시 연다. PlayerController에 RPC와 Exec 선언이 추가되므로 처음 적용할 때 Live Coding만 사용하지 않는다.
2. `Lv_Jang`을 저장한다. Manager는 하나만 두고, `Paths`에 실험할 경로를 등록한다. 명령은 등록된 전체 Enemy에 적용되므로 두 Enemy만 실험하려면 두 경로만 등록한다.
3. 두 경로의 `Assigned Enemy`, 고유한 Path Id/Actor Id를 확인한다. 같은 구간에서 마주 달리게 하려면 한 Enemy의 `SplineFollower → Settings → Reverse Direction`을 켜고 출발 캡슐은 서로 떨어뜨린다. 충돌을 보려면 Capsule의 Pawn 응답을 Block으로 유지한다.
4. 수동 실험에서는 Manager의 `Auto Start Trial`과 `Auto Repeat`를 끈다. Listen Server와 Client 또는 Dedicated Server와 Client를 실행한다. Development/Debug 빌드에서만 동작하며 Shipping/Test에서는 서버도 요청을 거부한다.

## Client 창 명령

Client 게임 창에 포커스를 주고 `~`로 콘솔을 연다. 아래 명령은 한 줄씩 입력한다.

| 명령 | 동작 |
|---|---|
| `SharpPrepare` | 서버에서 전체 검증 후 출발점 배치, Ready로 준비. 이동은 시작하지 않음 |
| `SharpStart` | Ready에서 시작하거나 Stopped에서 현재 위치로부터 재개 |
| `SharpStop` | 전체 Enemy 입력·속도·누적 힘 정리, 자동 시작/반복 취소 |
| `SharpReset` | 전체 정지 후 새 Trial로 출발점 재배치, Ready. 별도로 Start 필요 |
| `SharpStatus` | 서버의 상태 집계와 각 Enemy의 ActorId, 경로, 상태, 속도, 위치, 실패 사유 조회 |

첫 실행:

```text
SharpPrepare
SharpStatus
SharpStart
```

정지 및 현재 위치에서 재개:

```text
SharpStop
SharpStart
```

완료/실패 후 처음부터 재실험:

```text
SharpReset
SharpStart
```

Client의 소유 PlayerController → Reliable Server RPC → 기존 ExperimentManager 전체 제어 순서로 실행한다. 결과는 요청한 Client의 콘솔과 `LogSharpExperimentDebug` 로그로 돌아온다. Client가 로컬 Enemy 위치를 직접 변경하지 않으며 기존 서버 검증을 그대로 사용한다. 네트워크 왕복 후 결과가 도착하므로 명령별 응답을 확인한다. 상태 조회의 위치·속도는 서버 관측값이고 Client 렌더링 위치와 다를 수 있다.

`SharpStart OK`는 서버가 시작 요청을 수락했다는 뜻이다. 이후 충돌로 정체되면 `SharpStatus`에서 Failed와 `No path progress within Blocked Timeout`을 확인할 수 있다. 기본 제한은 3초이며 계속 충돌 상태를 관찰하려면 실행 전에 두 Enemy의 `Blocked Timeout Seconds`를 0으로 설정한다.

`Start capsules overlap`은 두 출발 캡슐 중첩으로 준비가 거절된 것이다. 출발점을 분리하거나 한쪽 Reverse Direction을 수정한 뒤 다시 준비한다. `Command not recognized`라면 새 빌드 반영 여부와 GameMode의 PlayerController가 `GE_AIReplicationPlayerController`의 자식인지 확인한다.

## 수동 네트워크 확인

1. Client에서 `SharpPrepare` 후 응답의 Ready 개수가 등록한 Enemy 수와 일치하고 아직 이동하지 않는지 확인한다.
2. `SharpStart` 후 Running 응답과 Client에서 복제된 이동·충돌을 확인한다.
3. 정체 시간 제한 전에 `SharpStop`을 입력하고 Stopped 응답 및 정지를 확인한다.
4. `SharpReset`으로 Ready를 확인하고 다시 출발시킨다. 충돌로 Failed가 되었다면 Reset 후 Start한다.
5. 출발 캡슐이 겹치는 배치에서는 `SharpPrepare`의 거부 사유가 Client 콘솔까지 전달되는지 확인한다.

이 절차는 수동 확인 방법이며 네트워크 실행 검증 결과를 의미하지 않는다.
