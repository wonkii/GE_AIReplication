# LAN 서버 생성 및 명시적 접속

## 사용자 흐름

같은 Windows Development 패키지의 **Windows 폴더 전체**를 두 PC에 복사한다. Host/Client 전용 빌드를 별도로 만들 필요는 없다.

현재 배포 시작 레벨은 `/Game/JWJ/Lv_Jang`이다. `PlayLAN.cmd`를 실행하면 1280×720 창 모드, 최대 60FPS로 시작한다. 실행 파일을 직접 열어도 같은 레벨과 LAN 메뉴가 열린다.

1. Host에서 `Host: 서버 열기`를 누른다.
2. 표시된 `IPv4:포트`를 Client에게 알려준다. 주소 필드는 읽기 전용이며 복사할 수 있다.
3. Host는 이 시점에 게임 플레이에 입장하지 않는다. `서버 접속`을 눌러 로컬 플레이어를 생성한다.
4. Client는 받은 주소를 입력하고 `LAN에서 주소 확인`을 누른다.
5. 약 5초간 LAN 검색을 수행한다. 동일 프로젝트/프로토콜, 주소, 포트가 일치하고 빈자리가 있는 세션만 확인 성공으로 처리한다.
6. 확인 성공 후에만 `서버 접속`이 활성화된다. 이 버튼을 누르기 전에는 참가 요청이나 맵 이동이 없다.
7. 플레이 중 Esc로 메뉴를 표시하고 `서버 종료 / 나가기`를 누른다. Host 종료는 모든 Client 연결도 종료한다.

Host가 로컬 플레이에 입장하기 전에도 Client가 접속할 수 있다. 서버 월드는 서버를 여는 순간부터 실행되므로 AI 등 월드 로직도 실행된다. Host의 플레이어 Pawn 생성만 명시적인 접속까지 보류한다. 실험 시작/정지는 기존 실험 Manager가 담당한다.

## 네트워크 조건

- 같은 LAN에서 IPv4 UDP 통신이 가능해야 한다. 기본 게임 포트는 7777, NULL LAN 검색 포트는 14001이다.
- Windows 방화벽에서 해당 실행 파일의 사설 네트워크 통신을 허용한다. 이 구현은 방화벽 설정을 자동으로 바꾸지 않는다.
- 게스트 Wi-Fi, AP isolation, 다른 VLAN에서는 검색 브로드캐스트가 전달되지 않을 수 있다.
- Host에는 OnlineSubsystemNull이 실제 광고하는 주소 하나를 표시한다. VPN/가상 어댑터 때문에 잘못된 인터페이스가 선택되면 실행 인자 `-MULTIHOME=192.168.0.10`으로 올바른 로컬 IPv4를 지정한다.
- 포트를 바꾸려면 Host에 `-port=7780`을 전달하고 화면에 표시된 주소를 그대로 공유한다.
- 주소 확인은 LAN 세션 응답 확인이다. 이후 Host 종료나 방화벽 변경으로 실제 접속은 실패할 수 있으며, 이 경우 오류를 표시하고 메뉴로 복귀한다.
- 주소 수정 시 확인 결과를 폐기한다. 확인 결과는 30초 후 만료되며 다시 확인해야 한다.
- 현재 인터넷 등록, NAT traversal, 릴레이, DNS/IPv6 입력은 범위에 포함하지 않는다.

## 구현 구성

- `AIReplicationNet/Public/LanSessionSubsystem.h`: Blueprint API, 상태 및 상태 변경 이벤트.
- `AIReplicationNet/Private/LanSessionSubsystem.cpp`: NULL 세션 생성/검색/참가/삭제, 주소 검증, 네이티브 Slate 메뉴, 실패 복구.
- `GE_AIReplicationGameMode`: 독립 게임의 로컬 플레이어를 명시적 접속까지 보류. PIE의 기존 자동 플레이는 유지한다.
- 메뉴는 패키징 및 에디터의 **Standalone Game**에서 자동 표시한다. 기본 ThirdPerson GameMode를 상속하지 않는 다른 GameMode에는 동일한 입장 보류 처리를 별도로 연결해야 한다.
- 현재 시작 맵인 Lv_Jang에서 Listen NetDriver를 열므로 Host가 서버를 열 때 맵을 다시 로드하지 않는다. Client만 접속 버튼 이후 서버 월드로 이동한다.
- Subsystem은 각 프로세스에 로컬로 존재하며 복제되지 않는다. 실제 게임 상태는 기존 Actor 복제/RPC를 사용한다.
- 네트워크 작업은 Delegate 완료 이벤트로 연결하고 UI는 대기하지 않는다. NULL 구현은 일부 완료 이벤트를 같은 호출에서 즉시 전달할 수 있다. 맵 로딩 중 프레임 정지는 별개다.
- 20초 작업 제한, 중복 요청 제한, Delegate 해제, Network/TravelFailure 처리, 종료 후 로컬 맵 재시작을 제공한다. 세션 삭제 자체가 응답하지 않으면 재생성 경합을 피하려고 종료 대기 상태를 유지하고 재시작 안내를 표시한다.
- 새 맵/기능 도입 시 `GE_LAN_PROTOCOL` 또는 빌드 식별 정책을 함께 갱신한다.

## 패키징

프로젝트 루트에서 PowerShell로 실행한다.

```powershell
./Scripts/PackageLan.ps1
```

기본 출력은 `Saved/Tasks/<작업ID>/Windows`이며 실제 경로는 실행 종료 시 표시된다. 같은 작업 폴더에 README와 package.log, 실행별 `UAT-*` 상세 로그를 남긴다. Lv_Jang, ThirdPerson 및 SharpMovementExperiment 맵을 포함하며, 기본 시작 맵과 서버 기본 맵은 `/Game/JWJ/Lv_Jang`이다. 패키지에는 `PlayLAN.cmd`와 `LAN-사용법.md`가 포함된다. 실험 맵으로 시작하려면 실행 인자로 `/Game/SharpMovementExperiment/Maps/SharpMovementExperiment`를 지정한다.

## 플레이어 이동 설정

`GE_AIReplicationCharacter::BeginPlay`에서 Blueprint 기본값 로딩 후 플레이어 LAN 프로필을 적용한다. AI용 `Sharp` 전송 정책/예측기와 별개이며 CMC의 기본 입력 예측·서버 보정은 유지한다.

- 이동 복제: `NetUpdateFrequency=60`, `MinNetUpdateFrequency=30`. 실제 전송률을 보장하는 값은 아니다.
- `NetworkSmoothingMode=Exponential`.
- Client가 보는 원격 플레이어: 위치 0.06초, 회전 0.05초.
- Listen Server가 보는 원격 플레이어: 위치 0.05초, 회전 0.06초.
- 적용 결과는 `[LAN-Player]` 로그에 기록한다. PC 간 시계를 강제로 맞추는 별도 RPC는 추가하지 않는다.

상대가 늦게 따라오는 체감을 줄이기 위해 기존 Client 0.10초/Host 0.08초에서 위치 보간 시간을 줄였다. 초기 복제 중 만들어진 CMC 예측 데이터의 캐시에도 변경값을 반영한다. 보간 시간 감소는 지터를 더 드러낼 수 있으며 FPS 저하·패킷 손실을 해결하는 설정은 아니다. `stat unit`, `stat net`, `p.NetShowCorrections 1`로 실제 환경을 확인한다.

## 검증

주소 파서 자동 테스트: Unreal Automation의 `GE.LAN.AddressValidation`. 잘못된 IPv4, 빈 octet, 잘못된 포트, travel 옵션 삽입, multicast/broadcast 주소를 거부하는지 확인한다.

독립 프로세스 통합 테스트:

```powershell
./Scripts/TestLan.ps1
./Scripts/TestLan.ps1 -Packaged -Executable '실제 패키징 작업 폴더/Windows/GE_AIReplication.exe'
./Scripts/TestLan.ps1 -Packaged -Rendered -Executable '실제 패키징 작업 폴더/Windows/GE_AIReplication.exe'
```

Development 테스트 명령 `LAN.Smoke`는 버튼과 동일한 Subsystem API를 사용한다. 기본 맵이 Lv_Jang인지, 자동 입장이 없는지, Host의 명시적 플레이 시작, LAN 주소 확인 후 Client 접속, 각 프로세스에서 상대 플레이어의 수평 이동(10cm 초과)이 관측되는지, Client 종료 후 메뉴 복귀를 확인한다. `-ExpectedMap`으로 검사할 맵 경로를 지정할 수 있다. 기본 검증은 NullRHI이며 `-Rendered`는 실제 렌더러를 사용한다. 새 테스트 결과와 README는 `Saved/Tasks/<작업ID>`에 저장한다. 이 테스트는 **한 PC의 두 프로세스 검증**이며, 실제 두 PC의 LAN 브로드캐스트와 방화벽 통과 여부, 끊김 개선의 정량 측정은 별도로 확인한다. Shipping에는 개발 테스트 명령이 포함되지 않는다.

수동 확인 항목: 잘못된 주소 및 존재하지 않는 주소, 주소 변경/확인 만료, 연속 클릭, 서버 종료/재생성, Host 강제 종료 후 Client 복구, 실제 두 PC 접속, Korean 메뉴 표시 및 Esc 메뉴 토글.

### 2026-11-02 Lv_Jang 패키지 검증

- UE 5.8.3 Development Editor/Game 빌드와 Windows BuildCookRun 성공.
- Lv_Jang 에셋을 읽어 ThirdPerson GameMode/플레이어 클래스와 PlayerStart를 확인했다. 맵 에셋은 변경하지 않았다.
- 완성된 패키지에서 기본 맵 `/Game/JWJ/Lv_Jang`, Host=ListenServer, Client=Client 모드를 확인했다.
- NullRHI 및 D3D12 렌더링 두 프로세스 검사 모두 접속, 양방향 원격 플레이어 이동, Client 메뉴 복귀를 통과했다.
- `[LAN-Player]` 로그에서 60/30Hz 설정과 Exponential, 위치 보간 0.10/0.08초가 적용된 것을 확인했다.
- 결과/배포: `Saved/Tasks/20261102-001310125-LvJang-LAN-Package-0103350c/`. 각 검사 폴더는 작업 README에 연결했다.
- 실제 여러 PC와 방화벽 환경, 변경 전후 끊김의 정량 비교는 이번 자동 검사 범위에 포함하지 않는다.

### 2026-11-01 검증 결과

- UE 5.8 Development Editor 빌드 성공.
- `GE.LAN.AddressValidation` 자동 테스트 성공.
- Editor `-game` 독립 프로세스 Host/Client 통합 테스트 성공.
- Windows Development BuildCookRun 성공. 출력: `Saved/Packages/LAN/Windows`.
- 패키징 실행 파일 두 프로세스 통합 테스트 성공. Host 대기 중 Client의 접속도 확인했다. 로그: `Saved/Tests/LAN/20261101-221656`.
- 패키징 렌더링에서 한글 메뉴와 초기 접속 버튼 비활성 상태를 스크린샷으로 확인했다. 이미지: `Saved/Tests/LanMenu.png`.
- 실제 두 PC와 방화벽 환경, 마우스로 모든 버튼을 조작하는 수동 검증은 별도로 필요하다.
