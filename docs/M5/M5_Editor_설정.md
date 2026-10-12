# M5 에디터 설정

| 대상 | 설정 |
|---|---|
| Manager | **Configured Mode**에 Default / Adaptive / AlwaysHigh / SingleShot / Burst / EventOnly / EventPredictor 선택 |
| 공통 기준선 | 모드 비교는 **ControlledLowRate**, 동일 **Normal Net Update Frequency** 권장 |
| Data Asset | **High Net Update Frequency**와 **Burst Seconds** 지정. High 값은 준비된 기준선보다 작을 수 없음 |
| Adaptive | 엔진의 process-wide adaptive 빈도 설정을 임시 적용. 종료·Reset·EndPlay 시 이전 값 복원 |
| 대조군 | **EventOnly** 기본값은 CMC 표시 유지. **Event Only Uses Custom Correction = false** |

SingleShot은 Detector 사건마다 ForceNetUpdate 한 번, Burst는 짧은 고빈도 구간, EventOnly/EventPredictor는 같은 Snapshot 갱신 및 ForceNetUpdate 경로를 사용합니다. ForceNetUpdate는 실제 즉시 전송이나 수신을 보장하지 않습니다.

동일 프로세스에서 동시에 실험하는 여러 Manager는 같은 adaptive 설정을 쓰는 모드로 맞춥니다. 다른 전역 설정을 요구하는 동시 Trial은 오류로 중단됩니다.

[전체 기능–파일 목록](../Architecture/SharpMovement_구현_파일_및_설정.md)
