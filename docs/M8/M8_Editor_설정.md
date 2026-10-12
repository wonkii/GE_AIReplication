# M8 에디터 설정

| 대상 | 설정 |
|---|---|
| 반복 | Manager **Auto Start Trial / Auto Repeat / Repeat Count / Repeat Delay Seconds** |
| 모드 순회 | **Mode Sequence**에 비교할 모드를 순서대로 등록. Repeat Count는 각 모드별 횟수. 빈 배열은 Configured Mode 반복 |
| Report Actor | `SharpMovementReport` 배치. **Server Recording Directory / Client Recording Directory**에 종료된 기록 폴더 지정 |
| Metrics | **Maximum Interpolation Gap / Event Window Seconds / Detector Match Seconds / Timestamp Tolerance Seconds / Recovery Error / Recovery Hold Seconds** 지정 |
| 보고서 출력 | **Report Output Directory** 지정 후 **Generate Report**. 빈 경로는 Saved/SharpMovementReports의 새 폴더 |
| 모드 비교 | **Comparison Reports**에 report.json이 있는 폴더들 지정. **Budget Tolerance Percent / Minimum Comparison Coverage** 지정 후 **Compare Reports** |
| Actor별 비용 | Networking Insights에서 Actor/Snapshot/RPC 비용을 같은 시간 구간으로 확인하고 **Actor Cost Csv**에 아래 형식의 CSV 지정(선택) |

```csv
RunId,TrialId,ActorId,ActorBytesPerSecond,SnapshotBytesPerSecond,RpcBytesPerSecond
```

값은 trace에서 실측하여 입력합니다. ActorBytesPerSecond는 Actor 총량이며 Snapshot·RPC를 다시 더하지 않습니다. 각 보고서 식별자에 맞는 행이 정확히 하나여야 합니다. UE의 raw trace를 이 CSV로 자동 변환하는 기능은 포함하지 않습니다.

출력은 `aligned_errors.csv`, `report.json`, `comparison.json`입니다. **Write Html Charts**를 켜면 `report.html`(시간별 오차)과 `comparison.html`(실측 bytes/s 대비 오차·수신 지연)을 함께 만듭니다. 브라우저에서 열고 hover·지표 선택으로 값을 확인합니다. 연결 전체 bytes/s·packets/s는 실제 누적 카운터 차이와 wall time으로 계산합니다. Actor trace가 없으면 연결 전체 incoming bytes/s로 비교하며 Actor 비용으로 표시하지 않습니다. 여러 AI의 기록에서 동일 연결 수치를 다시 합산하지 않습니다.

같은 시나리오·평가 설정·예산 단위인 보고서만 비교합니다. 예산 오차 허용범위 내의 쌍과 충분한 표본 coverage를 확인합니다. 반복 요약은 평균·최소·최대·표준편차를 제공합니다. 실제 수신 전후 지표와 회복 실패는 null/RecoveryCensored로 구분합니다. 종료된 실패 Trial도 개별 보고서는 만들 수 있지만 성공 모드 비교에는 포함하지 않습니다.

[전체 기능–파일 목록](../Architecture/SharpMovement_구현_파일_및_설정.md)

연결 통계는 엔진 NetConnection 카운터(패킷 overhead 포함) 기준입니다. 송신 에뮬레이션에서 버린 패킷도 outgoing 카운터에 포함될 수 있으며, OS/NIC 계측값은 아닙니다. 장면의 벽·바닥·장애물 및 실행 조건은 비교 중 동일하게 유지하고 Network Condition Notes에도 기록합니다.
