# M6 에디터 설정

| 대상 | 설정 |
|---|---|
| Manager | **Configured Mode = EventPredictor** |
| Data Asset | **Replicate Movement Timestamps = true**, **Maximum Prediction Seconds / Correction Seconds** 지정 |
| Visual Prediction | **Enable Prediction / Maximum Visual Offset / Prediction Blend Seconds / Future Time Tolerance** 지정 |
| 충돌 보호 | **Collision Guard**, 벽이 Block하는 **Collision Channel**, **Collision Capsule Scale** 지정 |
| 낙하 모델 | **Enable Airborne Model**은 필요한 실험에서만 켜기 |
| 보정 대조군 | EventOnly에서 **Event Only Uses Custom Correction = true**로 예측 없는 동일 보정 경로 사용 |

`SharpCharacterMovementComponent`는 Enemy에 자동 장착됩니다. EventPredictor에서 내장 smoothing과 중복되지 않게 Mesh 위치만 관리하며 종료 시 원래 smoothing을 복원합니다. 클라이언트 Capsule·Velocity는 예측기가 수정하지 않습니다.

첫 실험은 평면·장애물 없는 경로와 in-place 애니메이션으로 구성합니다. Root Motion·래그돌·움직이는 바닥은 현재 모델의 평가 범위에 포함하지 않습니다. 등속 모델은 CMC 마찰이나 AI 재가속을 재현하지 않으므로 예측 시간과 offset을 짧게 제한합니다.

[전체 기능–파일 목록](../Architecture/SharpMovement_구현_파일_및_설정.md)
