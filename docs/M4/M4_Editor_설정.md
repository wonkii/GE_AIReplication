# M4 에디터 설정

Data Asset의 **Detection**에서 다음 항목을 지정합니다.

| 항목 | 의미 |
|---|---|
| Velocity Change Threshold | Δv 판정 임계값, 0보다 커야 함 |
| Direction Change Threshold Degrees / Minimum Direction Speed | 방향각 임계값과 거의 정지한 상태의 각도 제외 기준 |
| Acceleration Threshold | Δv/Δt 추가 판정. 0이면 비활성 |
| Detector Release Ratio | 해제 임계값 비율. 0 초과, 1 미만 |
| Quiet Release Seconds / Minimum Risk Seconds | 안정 구간 확인 시간과 Risk 최소 유지 시간 |
| Retrigger Seconds / Maximum Detector Sample Gap | 연속 사건 간 대기 시간과 불연속 샘플 제외 간격 |
| Detect Movement Mode Changes | Movement Mode 변화도 사건으로 사용할지 선택 |

Detector는 모든 모드에서 서버 CMC 이동 결과만 읽습니다. Volume이 없는 직선·곡선·정지 대조군도 기록하여 오탐을 확인합니다. `diagnostics.csv`의 DetectorSample·DetectorEvent·RiskStart·RiskEnd를 사용합니다. 측정된 실행 비용은 순수 Classifier 호출 비용이며 파일 기록·전송 정책 비용을 포함하지 않습니다.

[전체 기능–파일 목록](../Architecture/SharpMovement_구현_파일_및_설정.md)
