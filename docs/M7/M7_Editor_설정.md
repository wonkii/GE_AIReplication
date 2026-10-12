# M7 에디터 설정

| 상황 | 설정 |
|---|---|
| 연속 사건 | 서로 가까운 Volume 여러 개 등록. 재진입은 Trigger Policy / Rearm Delay 조절 |
| 충돌·착지 | 벽의 충돌 채널을 Predictor sweep 채널과 맞추기. 상향 Impulse·중력·Air Control 설정 |
| 정상 회전 | 곡선 Spline + 빈 Impulse Volumes |
| 정지 | 서버 Manager **Suspend Trial / Resume Trial**로 입력 중단·재개. 제동 수치는 CMC에서 지정 |
| 여러 AI | Enemy마다 유일 Actor Id와 개별 Manager·Volume 목록 지정. 공통 모드 사용 |
| 지연·지터·손실 | PIE Advanced Settings의 **Network Emulation** 또는 서버/클라이언트 콘솔 설정 |

첫 실험은 Editor의 Network Emulation에서 **Server Only / Custom**, Incoming=0으로 두고 Outgoing Minimum/Maximum Latency를 30/80ms, Packet Loss를 2%로 지정합니다. 설정값은 RTT 전체가 아닙니다. 정상 조건으로 돌아갈 때 Network Emulation을 끕니다.

콘솔로 같은 송신 조건을 지정하려면 `NetEmulation.PktLagMin 30`, `NetEmulation.PktLagMax 80`, `NetEmulation.PktLoss 2`를 사용합니다. Client 콘솔만 조절했다고 서버 송신 설정이 바뀐 것은 아니므로 실제 Driver의 manifest NetworkEmulation을 확인합니다. UE 5.8의 `PktJitter`는 `PktLag`보다 먼저 처리하므로 `PktLag 100`과 `PktJitter 20`을 함께 지정한 값을 100±20ms로 해석하지 않습니다. 기존 PktLag/PktJitter 설정은 해제하고 한 방식으로 확인합니다. 콘솔 설정은 적용한 월드에서 `NetEmulation.Off`로 해제합니다.

SequenceId 중복·과거 상태 제외, 메타데이터 도착 대기, 다음 서버 상태·모드 변경·벽·시간·offset 한계 시 수렴이 코드에 포함됩니다. RepNotify는 최신 상태이므로 중간 사건 병합을 허용하며 매 틱 RPC를 사용하지 않습니다. 같은 이동 업데이트에 소비된 여러 Impulse는 보고서에서 하나의 관측 가능한 급변 상태로 묶습니다.

[전체 기능–파일 목록](../Architecture/SharpMovement_구현_파일_및_설정.md)

[레벨 배치와 조건별 수동 확인](../Architecture/SharpMovement_마일스톤별_레벨_배치_및_수동_테스트.md)
