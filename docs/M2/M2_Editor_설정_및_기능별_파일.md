# M2 Editor 설정 및 기능별 파일

> 2026-10-11 · UE 5.8 · [전체 개발계획서](M2_서버_관찰_기반_전체_개발계획서.md) · [M1 Editor 가이드](../M1/M1_Editor_설정_가이드.md)

## 1. 구현 범위

서버 전용 Impulse Volume, 서버 이동 Recorder, 서버/클라이언트 Capsule·속도 도형, Visual Logger 연동, Trial 자동 시작·반복·리셋을 구현했다. CMC 기본 복제를 유지한다. Detector, 사건 Snapshot 전송, 시각 예측과 선택 사항인 Gameplay Debugger Category는 추가하지 않았다.

자동화 테스트는 요청에 따라 작성·실행하지 않는다. Editor C++ 빌드와 소스 검토로 확인하며 실제 Overlap·PIE 도형 전달·기록은 아래 수동 확인 절차를 따른다.

이번 변경은 소스·문서에 한정한다. 기존 사용자 에셋을 수정하지 않았고 새 Volume Blueprint와 맵 배치는 **Editor에서 직접 설정**한다. Actor 참조, 에셋/맵 경로, Box 크기, 카메라, CMC 설정을 코드에서 지정하지 않는다.

## 2. 기능별 연관 파일

아래 `.h` 링크에 대응하는 같은 이름의 `.cpp`가 구현 파일이다.

| 기능 | 파일 | 책임 |
|---|---|---|
| Impulse 설정 | [SharpImpulseTypes.h](../../Source/GE_AIReplication/SharpMovement/Scenario/SharpImpulseTypes.h) | 방향·재발동·입력 정책과 유효성 검사 |
| Volume | [SharpImpulseVolume.h](../../Source/GE_AIReplication/SharpMovement/Scenario/SharpImpulseVolume.h) | Capsule Overlap, 서버 권한, 중복 방지, CMC AddImpulse |
| 기록·관찰 계약 | [SharpMovementRecordTypes.h](../../Source/GE_AIReplication/SharpMovement/Measurement/SharpMovementRecordTypes.h) | 조절 가능한 파일/관찰 설정, 사건 구조체 |
| Recorder·도형 | [SharpMovementRecorder.h](../../Source/GE_AIReplication/SharpMovement/Measurement/SharpMovementRecorder.h) | 이동·사건 CSV, Manifest, Visual Logger, 디버그 그리기 |
| Enemy 통합 | [SharpEnemyCharacter.h](../../Source/GE_AIReplication/SharpMovement/Enemy/SharpEnemyCharacter.h) | MovementRecorder 기본 Component와 Visual Logger Snapshot |
| Trial 수명 | [SharpExperimentManager.h](../../Source/GE_AIReplication/SharpMovement/Experiment/SharpExperimentManager.h) | Volume 목록, 자동 시작/반복, 입력 복귀, 후행 기록·실패 |
| Data Asset | [SharpExperimentSettings.h](../../Source/GE_AIReplication/SharpMovement/Experiment/SharpExperimentSettings.h) | M2 Recording 설정과 검증 |
| 기존 이동 활용 | [SharpSplineFollowerComponent.cpp](../../Source/GE_AIReplication/SharpMovement/Movement/SharpSplineFollowerComponent.cpp), [SharpEnemyAIController.cpp](../../Source/GE_AIReplication/SharpMovement/Enemy/SharpEnemyAIController.cpp) | 기존 Suspend/Resume·이동 델리게이트 활용; 이번 M2에서 수정 없음 |
| 모듈 의존성 | [GE_AIReplication.Build.cs](../../Source/GE_AIReplication/GE_AIReplication.Build.cs) | Json·JsonUtilities 비공개 의존성 |

## 3. Blueprint와 맵 연결

1. C++ Editor 빌드 후 에디터를 다시 열어 새 Component·UPROPERTY를 로드한다. 구조 변경을 Live Coding에만 맡기지 않는다.
2. 기존 실험 Enemy/BP_TestEnemy가 `ASharpEnemyCharacter`를 상속하는지 확인한다. Components에 **MovementRecorder**가 나타나야 한다. M1 AIController/Follower/복제 설정은 유지한다.
3. `ASharpImpulseVolume` 부모의 Blueprint를 만든다. 권장 경로는 `Content/SharpMovementExperiment/Blueprints/BP_SharpImpulseVolume`이며 다른 폴더도 가능하다.
4. Volume을 맵의 직선 경로 중간에 배치하고 **TriggerBox의 Box Extent·위치·회전**을 조절한다. 시작 Capsule과 끝점 감속 구간을 피한다.
5. Manager의 `Sharp Experiment → M2 Setup → Impulse Volumes` 배열에 **맵의 Volume 인스턴스**를 추가한다.
6. Manager의 Enemy/Path/Settings 참조는 M1처럼 연결하고 Configured Mode는 Default로 둔다.

Volume은 대상 Enemy/Path를 Manager에서 가져온다. 별도의 중복 참조 설정은 없다. 빈 배열은 정상 이동 Control 실험이다. 각 VolumeId는 월드에서 유일해야 하며, 같은 Volume을 여러 Manager가 공유하면 준비를 거부한다.

## 4. Volume 설정

Volume Details의 `Sharp Impulse → Settings`를 펼친다.

| 항목 | 초기값 | 조절 목적 |
|---|---|---|
| Volume Id | 비어 있음 | 직접 `Side_01` 같은 유일한 ID 지정 |
| Impulse Vector | `(0, 300, 0)` | 선택한 방향 기준의 입력 벡터 |
| Direction Space | World | World / VolumeLocal / PathTangent |
| Velocity Change | 켜짐 | 질량에 무관한 Δv; 꺼짐은 CMC Mass 영향 |
| Trigger Policy | OncePerTrial | Trial당 한 번 / 퇴장 후 재진입마다 한 번 |
| Rearm Delay | 0.25초 | 첫 발동 후 최소 사건 간격 |
| Follow Policy | Continue | Spline 입력 유지 / SuspendInput으로 일시 중단 |
| Input Suspend Seconds | 0.25초 | 입력 중단 후 Resume까지 시간 |
| Replicates | 꺼짐 | Volume 자체 복제 비활성 유지 |

PathTangent는 X=추종 전방, Y=우측, Z=상향이며 Reverse Direction을 반영한다. VolumeLocal은 회전만 적용하고 Scale로 세기를 바꾸지 않는다. 설정은 Prepare 때 고정되므로 변경 후 Reset/Prepare한다. 준비 후 Volume 위치·회전·크기·ID 변경은 실패로 처리한다.

TriggerBox 설정은 **Collision Enabled=Query Only**, **Pawn Response=Overlap**, **Generate Overlap Events=켜짐**이다. Box Extent는 양수로 지정한다. Enemy Capsule도 Pawn Object Type, Query 충돌, Generate Overlap Events가 필요하며 Volume Object Channel을 Ignore하면 안 된다. Mesh와 플레이어의 Overlap은 발동 대상에서 제외한다. 클라이언트 월드의 Volume 충돌은 비활성화한다.

Armed/Trigger Count는 런타임 확인용이다. 준비·리셋 중 발동하지 않고 Start 후 활성화된다. OncePerEntry에서 Cooldown 중 진입하면 내부에서 대기해도 자동 발동하지 않으며, 다시 나가서 들어와야 한다.

## 5. Enemy 관찰 설정

Enemy Blueprint의 **MovementRecorder**를 선택하고 `Sharp Observation → Observation`을 펼친다.

| 항목 | 초기값 | 의미 |
|---|---|---|
| Draw Server Capsule | 꺼짐 | 서버 실제 Capsule 표시 |
| Draw Client Capsule | 꺼짐 | Client 로컬 Capsule; 화면의 Mesh 위치와 구분 |
| Draw Velocity | 켜짐 | Capsule을 표시하는 역할에서 속도 화살표 추가 |
| Draw Impulse Events | 켜짐 | 사건 구·Impulse 화살표 |
| Server / Client / Event Color | 주황 / 청록 / 자홍 | 서버 Tint 적용 시 실제 표시색은 달라짐 |
| Shape Lifetime Seconds | 0.05초 | 이동 도형 수명 |
| Event Lifetime Seconds | 0.5초 | 사건 도형 수명 |
| Arrow Time Scale | 0.2초 | Velocity·Impulse 벡터의 표시 배율 |
| Arrow Head Size | 20cm | 화살촉 크기 |
| Event Radius | 12cm | 사건 구와 Visual Logger 위치 표시 크기 |
| Event Sphere Segments | 12 | 구의 분할 수, 4~64 |
| Line Thickness | 1 | 선 두께 |

최초 관찰은 Draw Server Capsule을 켠다. 필요하면 Draw Client Capsule도 켠다. 도형 표시를 꺼도 CSV 기록은 유지된다. 서버 관찰 설정은 Trial 시작 때 고정되므로 Trial 사이에서 조절한다.

## 6. Settings Data Asset 기록 설정

연결한 `DA_SharpExperimentSettings`의 **M2 Recording → Recording**에서 조절한다.

| 항목 | 초기값 | 의미 |
|---|---|---|
| Output Subdirectory | SharpMovement | Project Saved 아래 상대 폴더; 절대 경로와 `.`/`..` 금지 |
| Flush Interval Seconds | 1초 | 버퍼를 디스크에 저장하는 주기 |
| Maximum Buffered Rows | 4096 | CSV 합산 행이 이 값에 도달하면 조기 Flush |
| Event Observation Seconds | 0.5초 | 호출 후 속도 변화 관찰 창 |
| Observable Velocity Change | 1cm/s | 호출 직전 속도와의 차이가 이 값보다 크면 Observed |
| Maximum Pending Events | 128 | 동시 미완료 사건 제한; 초과 시 실패 |
| Post Trial Observation Seconds | 0.5초 | Completed/Failed 후 후행 관찰 시간 |
| Write Visual Logger | 켜짐 | 서버 Enemy Visual Logger 호출 |
| Auto Record Visual Logger To File | 꺼짐 | 엔진 공통 Visual Logger 파일 기록 자동 활성화 |
| Network Condition Notes | Network emulation disabled | 실제 지연·손실·FPS·환경 메모 |

Post Trial Observation Seconds를 Event Observation Seconds 이상으로 두면 마지막 사건을 충분히 관찰하기 쉽다. 짧으면 종료 시 Pending 사건이 Cancelled로 남을 수 있다.

기존 Data Asset의 `Scenario → Impulse`는 초기 프로젝트 예약 필드다. **M2는 Volume의 Impulse Vector를 사용한다.** Detector/Network/Prediction 필드는 후속 단계에서 사용한다.

CMC의 가속·마찰·제동·질량·중력·AirControl·RotationRate·Smoothing과 Actor 복제 빈도는 기존 Details에서 직접 설정한다. M2가 임의로 변경하지 않는다. M1의 ControlledLowRate 프로필과 Follower의 MaxWalkSpeed 제어는 기존 동작을 유지한다.

## 7. Manager 자동 시작·반복

`Sharp Experiment → Automation`에서 조절한다.

| 항목 | 초기값 | 동작 |
|---|---|---|
| Auto Start Trial | 꺼짐 | 서버 BeginPlay 다음 틱에서 Prepare 후 자동 Start |
| Auto Start Delay Seconds | 3초 | 최초 시작·각 반복에서 Prepare 후 대기 |
| Auto Repeat | 꺼짐 | 성공적으로 완료하고 기록이 닫힌 Trial만 반복 |
| Repeat Count | 10 | 같은 PIE 세션의 완료 Trial 목표 수, 최초 1회 포함 |
| Repeat Delay Seconds | 1초 | 후행 기록 종료 후 Reset까지 대기 |

Dedicated Server에서 Auto Start Trial을 켜고 카메라를 준비할 시간을 확보한다. 고정 대기는 Client 연결 완료를 보장하지 않으므로 늦게 연결되면 시간을 늘린다. 실패 시 자동 반복을 중단한다. 새 반복 묶음은 PIE를 다시 실행한다.

수동 버튼은 기존처럼 서버 PIE 인스턴스에서 사용한다. SuspendInput 중 수동 Suspend를 누르면 자동 복귀 예약을 취소해 수동 Resume을 기다린다. Reset·실패·EndPlay도 예약을 취소한다. Suspend는 누적 입력을 소비하고 신규 입력을 중단하며 기존 속도·마찰·중력은 유지한다.

Completed/Failed 후 Finishing Recording이 켜진 동안은 후행 관찰 중이다. 마지막 이동 콜백의 모든 구독자가 끝난 이후 기록을 닫는다.

## 8. 서버 도형을 Client 화면에 표시

1. Play Advanced Settings: **Play as Client**, Players 2, **Run Under One Process=켜짐**.
2. **Show Server Debug Drawing by Default=켜짐**. Tint Color/Strength 조절.
3. Enemy MovementRecorder의 Draw Server Capsule=켜짐.
4. Manager의 Auto Start Trial을 켜고 PIE 실행.
5. Client에서 Enemy Mesh와 서버 Capsule·속도 화살표를 함께 관찰.

설치 UE 5.8의 DrawDebugCapsule/DirectionalArrow는 Dedicated Server에서 **같은 Editor 프로세스의 PIE Client 월드**로 그리기를 전달한다. Client의 ServerDrawDebug 표시 플래그가 켜져 있어야 한다. 별도 프로세스·패키징 Client에는 이 경로가 적용되지 않으므로 서버 기록을 사용한다.

Listen Server에서는 Players 3으로 서버 화면과 Client 두 개를 분리한다. 서버 도형은 서버 화면에서 보고 필요하면 Eject로 에디터 관찰 카메라를 사용한다. 카메라는 기존 플레이어나 맵의 CameraActor/View Target으로 설정하며 Enemy를 Possess하지 않는다.

`p.NetShowCorrections 1`, `p.NetCorrectionLifetime 4`는 Player 위치 보정이 발생할 때 표시한다. Enemy의 Simulated Proxy 보정에는 `p.NetVisualizeSimulatedCorrections 1`도 참고할 수 있다. 지속적인 서버 Capsule 관찰과 용도가 다르다.

## 9. 기록 읽기

Output Log의 `LogSharpRecording` 또는 MovementRecorder의 Output Directory에서 실제 출력 경로를 확인한다.

```text
Saved/<OutputSubdirectory>/<RunId>/Trial_<TrialId>/<ProcessId>_<World>_<GUID>/
  manifest.json
  server_samples.csv
  events.csv
```

같은 TrialId로 재실행해도 GUID가 달라 덮어쓰지 않는다. Manifest는 엔진·역할·월드·ID, CMC/복제/Follower/관찰 설정, Volume 설정·배치와 실행 결과를 저장한다. 가능한 네트워크 콘솔 변수만 자동 저장하므로 PIE 에뮬레이션 설정은 Network Condition Notes에도 기입한다.

samples에는 초기 상태와 매 OnCharacterMovementUpdated 결과가 있다. DeltaSeconds=0인 초기 행은 실제 이동 행과 구분한다. 같은 프레임에 여러 행이 있을 수 있으므로 Frame/RecordOrdinal/SampleId를 사용한다.

events 해석:

- OverlapTime/CallTime/CallCompleteTime: Overlap과 호출의 서버 월드 시각.
- FirstPostCallSampleTime: 호출 뒤 첫 이동 결과. 누적 Impulse 소비 완료를 보장하지 않는다.
- ActualVelocityChangeTime: 관찰 창에서 속도 차이가 허용치를 넘은 첫 샘플 시각. 미관찰은 빈 칸.
- Status: Observed / NoObservableChange / Cancelled. 이동 업데이트 해상도의 관찰이다.

속도 변화에는 가속·제동·중력·충돌도 포함된다. Observed는 내부 Impulse 적용 시점을 증명하거나 독립적인 급변 Detector 판정이 아니다. Control 로그와 비교하고 M4 Detector에 사건 데이터를 공급하지 않는다.

Visual Logger를 수동 사용하면 Editor에서 열고 Record를 활성화한 뒤 Trial을 시작한다. 서버 Enemy와 사건 텍스트의 EventId를 기준으로 확인한다. 자동 파일 기록은 엔진 기본 경로를 사용하며 CSV 폴더에 직접 파일을 생성하지 않는다. 엔진 Output Log에서 저장 경로를 확인한다. 여러 Recorder의 자동 기록은 마지막 Recorder 종료까지 유지한다. 기존 활성화 상태는 종료 후 복구하므로 수동 기록이 이미 켜져 있었다면 직접 저장/종료한다. Trial 중 엔진 공통 Record 설정을 수동 전환하지 않는다.

## 10. 수동 확인 순서

1. Volume 없는 Control에서 이동·끝점 정지와 CSV 생성 확인.
2. Side `(0, 300, 0)` 하나로 Trigger Count=1, EventId와 전후 속도 확인.
3. Reset 후 TrialId 증가·Volume 재활성화·이전 Timer 취소 확인.
4. OncePerTrial 중복 방지와 OncePerEntry의 퇴장·재진입·Cooldown 확인.
5. Capsule·Mesh 동시 겹침에도 호출은 한 번이고 Client에는 AddImpulse가 실행되지 않는지 확인.
6. Continue와 SuspendInput을 별도 Trial에서 비교. Falling의 AirControl/AllowInputWhileFalling도 기록.
7. Completed/Failed 후 마지막 샘플·Manifest·미완료 사건 상태 확인.
8. Dedicated Server+같은 프로세스 Client에서 도형 확인. 별도 프로세스에서는 서버 기록 확인.

첫 실행은 네트워크 에뮬레이션 없이 시작한다. 이 목록은 Editor 수동 확인용이며 자동화 테스트를 추가하지 않는다.

## 11. 이번 개발의 확인 결과

- UE 5.8 `GE_AIReplicationEditor / Win64 / Development` 최종 컴파일·링크 성공.
- 신규 M2 소스의 형식과 문서의 로컬 링크 확인 완료.
- 자동화 테스트 작성·실행 생략.
- 새 Volume Blueprint 생성, 기존 맵 배치 변경, 실제 PIE 실행은 이번 코드 작업에서 수행하지 않음. 위 설정·수동 확인 순서를 적용해야 함.

빌드 결과는 로컬 `Saved/M2_build.log`에 남긴다. 제한된 실행 환경에서는 엔진 빌드 도구의 볼륨 조회가 실패해 허용된 별도 실행 환경으로 컴파일했다. 프로젝트 런타임 설정은 변경하지 않았다.
