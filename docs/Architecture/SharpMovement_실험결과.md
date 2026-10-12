# SharpMovement 실험 결과 기록

현재는 C++ 구현 단계이며 실험 결과는 미측정입니다. 자동화 테스트는 실행하지 않았습니다. 아래 항목은 에디터에서 수집한 기록과 Networking Insights trace를 근거로 채웁니다.

| 실행 | Mode | 장면·Impulse·AI 수·네트워크 조건 | 서버/클라이언트 기록 | 보고서 | Trace |
|---|---|---|---|---|---|
| 미측정 | 미측정 | 미측정 | 미측정 | 미측정 | 미측정 |

확인할 값: 첫 상태 수신 추정 지연, 수신 전후 Capsule/Mesh 오차, peak/mean/면적, 회복 및 censoring, 표시 프레임 이동·잔차, 탐지 지연·Precision/Recall, Classifier 비용, 연결/Actor/Snapshot/RPC 실측 비용, 반복 변동과 worst case.

모드 비교는 comparison.json의 동일 시나리오·예산 단위, WithinBudgetTolerance, 표본 coverage를 확인합니다. EventOnly·동일 Custom Correction 대조군·EventPredictor의 영상도 함께 남깁니다. 실측 전에는 어느 모드가 우수한지 판단하지 않습니다.

[구현 파일과 에디터 설정](SharpMovement_구현_파일_및_설정.md)
