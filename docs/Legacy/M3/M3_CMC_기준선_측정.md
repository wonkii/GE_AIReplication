# M3. 기본 CMC의 오차와 Rubber Banding 측정

> 상태: 개발 예정. [전체 아키텍처](../Architecture/Unreal_Sharp_Movement_마일스톤_구현계획.md) · 선행: [M2](../M2/M2_Impulse_재현.md) · 다음: [M4](../M4/M4_Detector.md)

기본 CMC의 오차와 시각적 보정이 실제로 나타나는 조건을 확정한 뒤 다음 단계를 평가한다.

## 담당 파일과 폴더

아래 경로는 저장소 루트 기준이다. 파일이 기존 단계에서 생성되었다면 이 단계에서는 확장한다.

| 경로 | 역할 |
|---|---|
| `Source/GE_AIReplication/SharpMovement/Measurement/SharpMovementRecorder.h/.cpp` | 서버 궤적과 클라이언트 Capsule·Mesh 샘플 기록 |
| `Source/GE_AIReplication/SharpMovement/Measurement/SharpMovementMetrics.h/.cpp` | 위치 오차·회복 시간·보정 거리 계산 |
| `Source/GE_AIReplication/SharpMovement/Enemy/SharpEnemyCharacter.h/.cpp` | 필요한 이동·복제 관찰 지점 연결 |

## 개발 및 검증 사항

**목표:** 제안 기능이 해결할 문제가 실제로 존재하는 조건을 찾는다.

**구현 작업**

1. 서버 매 이동 틱에 시각·위치·Velocity·Movement Mode를 기록한다.
2. 클라이언트에 같은 시각의 Capsule 위치와 **화면에 보이는 Mesh 위치**를 각각 기록한다. 위치 보정의 시작/종료와 최대 한 번의 표시 이동량도 기록한다.
3. C++에서 필요 시 `OnRep_ReplicatedMovement`와 `SmoothCorrection`을 관찰용으로 확장하고, `Super` 호출을 유지한다. 실전 전송 시점과 Actor별 비트 수는 Networking Insights에서 검증한다.
4. 네트워크 에뮬레이션 없이 시작하고, 이후 지연·지터·손실을 차례로 추가한다. 에뮬레이션 설정은 각 실행 로그에 남긴다.
5. **첫 급변 상태를 받기 전**의 오차와 **첫 수신 이후**의 오차를 따로 계산한다.

**측정값:** 위치 오차 peak/평균/면적, 회복 시간, 표시 Mesh의 보정 거리, 첫 상태 도착 시간, AI별 bytes/s·packets/s.

**판단 게이트:** 첫 수신 이후 CMC가 이미 충분히 정확하면 M6의 복잡한 예측기 대신 M5의 `SingleShot` 개선을 우선 평가한다. 문제가 거의 없다면 업데이트 빈도·네트워크 조건·Impulse 범위를 조절해 **문제 발생 조건**을 먼저 확정한다.

공식 참고: [Network Smoothing](https://dev.epicgames.com/documentation/en-us/unreal-engine/understanding-networked-movement-in-the-character-movement-component-for-unreal-engine), [Networking Insights](https://dev.epicgames.com/documentation/en-us/unreal-engine/networking-insights-in-unreal-engine), [Network Emulation](https://dev.epicgames.com/documentation/unreal-engine/using-network-emulation-in-unreal-engine).
