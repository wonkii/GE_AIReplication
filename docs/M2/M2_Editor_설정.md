# M2 에디터 설정 요약

| 대상 | 설정 |
|---|---|
| Volume | `SharpImpulseVolume` 배치. 유일한 **Volume Id**, Box 범위·위치·회전 지정 |
| 충돌 | Volume **Query Only / Pawn Overlap / Generate Overlap Events**, Enemy Capsule **Pawn / Generate Overlap Events** |
| Volume Settings | **Impulse Vector / Direction Space / Velocity Change / Trigger Policy / Rearm Delay** 지정 |
| 입력 중단 | **Follow Policy = Suspend Input**이면 **Input Suspend Seconds** 지정 |
| Manager | **Impulse Volumes**에 해당 Volume만 등록. 빈 목록은 정상 이동 대조군 |
| 기록·관찰 | Data Asset **Recording**에서 출력·flush·관찰 tail 설정. Enemy **MovementRecorder → Observation**에서 Capsule·Velocity·Impulse 표시 설정 |

실제 Impulse는 Volume의 설정입니다. Data Asset의 기존 **Impulse** 필드는 M0 메타데이터용으로 남아 있으며 Volume을 덮어쓰지 않습니다. 시작 Capsule과 Volume을 겹치지 않게 배치하고, Prepare 이후 Volume을 이동했다면 다시 Prepare합니다.

같은 프로세스 Dedicated Server PIE의 서버 도형은 클라이언트 뷰포트 **Show → Developer → Server Draw Debug**로 관찰합니다. 별도 프로세스의 서버 관찰은 **Visual Logger**를 사용합니다. 상세 설명은 [기존 M2 가이드](M2_Editor_설정_및_기능별_파일.md)에 있습니다.

[전체 기능–파일 목록](../Architecture/SharpMovement_구현_파일_및_설정.md)
