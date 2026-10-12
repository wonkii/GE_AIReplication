# M3 에디터 설정

| 대상 | 설정 |
|---|---|
| Manager | 우선 **Configured Mode = Default** |
| 비교 기준 | 비교 실험에는 **Baseline Profile = ControlledLowRate**와 Data Asset **Normal Net Update Frequency** 지정. 엔진 그대로 관찰할 때는 **EngineDefault** |
| Data Asset | **Replicate Movement Timestamps = true**를 모든 비교 모드에 동일하게 적용 |
| Recording | **Output Subdirectory / Flush Interval Seconds / Connection Sample Seconds / Post Trial Observation Seconds** 지정 |
| 클라이언트 | 게임 카메라로 Mesh 관찰. 보간 회전·위치와 충돌용 Capsule은 각각 CSV로 자동 기록 |

클라이언트 기록은 Enemy의 Trial 메타데이터 수신 후 자동 시작합니다. 같은 RunId·TrialId·ActorId의 서버 폴더와 클라이언트 폴더를 M8 Report에 연결합니다. 첫 Trial 안내가 오기 전의 클라이언트 구간은 측정할 수 없습니다.

서버 시각은 클라이언트 GameState의 추정 시각입니다. 별도 매 프레임 Ground Truth RPC는 사용하지 않습니다. Timestamp 필드의 추가 전송 비용은 모든 기준선에 포함됩니다.

[전체 기능–파일 목록](../Architecture/SharpMovement_구현_파일_및_설정.md)

CMC의 기본 timestamp는 마지막 **위치/회전 변화** 시각입니다. 위치·회전 변화 없는 속도/모드 사건의 첫 기본 상태 수신은 이 시각만으로 확정하지 않으며 null로 남을 수 있습니다. raw 복제 위치·속도는 MovementReceived 진단에 함께 기록합니다. 같은 시각의 Snapshot과 기본 이동 상태는 예측 시작을 허용하고, 더 새로운 기본 상태가 이미 적용된 Snapshot을 제외합니다.
