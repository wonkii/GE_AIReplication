# M0 에디터 설정

| 대상 | 설정 |
|---|---|
| Data Asset | `SharpExperimentSettings` 기반 에셋을 Manager의 **Settings**에 지정 |
| Manager | **Configured Run Id**(빈 값은 GUID), **Configured Trial Id**, **Configured Mode** 지정 |
| 실행 | PIE **Play As Client**, 클라이언트 수 1 이상. 서버 동작을 별도 관찰하려면 Dedicated Server 사용 |
| 실험 맵 | 기존 실험 맵 또는 별도 맵에 Manager·Enemy·Path 배치. 기존 기본 맵 변경은 선택 사항 |

명령줄 `-SharpRunId`, `-SharpTrialId`, `-SharpMode`로 실행 값을 덮어쓸 수 있습니다. 유효한 `-SharpMode`를 지정하면 Mode Sequence는 사용하지 않습니다.

[전체 기능–파일 목록](../Architecture/SharpMovement_구현_파일_및_설정.md)
