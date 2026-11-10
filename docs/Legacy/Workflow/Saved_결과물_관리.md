# Saved 결과물 관리 — 팀 공통 규칙

## 적용 범위

팀원과 AI 도구가 앞으로 생성하는 개발·검증·실험 결과에 적용한다. 한 작업의 결과를 폴더 하나로 전달하면 생성 이유와 읽는 방법을 알 수 있어야 한다. 파일마다 별도의 md를 만들 필요는 없으며 작업 폴더의 `README.md` 하나에서 각 파일을 설명한다.

```text
Saved/Tasks/20261101-223000000-LAN-check-12ab34cd/
  README.md
  build.log
  Host.log
  Client.log
  screenshots/
```

기존 분류형 위치도 허용한다. 예: `Saved/Tests/LAN/<실행ID>/`, `Saved/Experiments/<BatchId>/`, Recorder의 역할별 기록 폴더. 하위 CSV 등은 상위 작업 README에서 묶어서 설명해도 된다.

## 필수 설명

README에는 작업명, 목적, 생성 도구/실행 명령, 생성 시각(시간대 포함), 주요 파일·하위 폴더별 의미와 열람 방법, 결과/미확인 사항, 보관·삭제 기준을 작성한다. 시작 시 생성하고 종료 후 성공/실패와 확인한 내용을 갱신한다. 생성기가 자동 기록하는 항목 이외의 판단은 작업자가 추가한다.

실험 CSV/manifest 원본은 보고서 재생성에 필요하므로 보고서와 함께 보관한다. 프로세스 번호, 로그 등 일회성 결과는 검토 후 정리할 수 있다. 민감한 실행 인자는 기록하기 전에 가린다. 이미 존재하는 README는 자동으로 덮어쓰지 않는다.

## 새 작업 시작

프로젝트 루트의 PowerShell에서:

```powershell
$task = ./Scripts/NewSavedTask.ps1 -TaskName 'LAN-check' `
    -Description 'Host와 Client의 LAN 접속 확인' `
    -Command 'Scripts/TestLan.ps1' `
    -Artifacts @('Host.log: Host 실행 기록', 'Client.log: Client 실행 기록')

# 이후 출력은 모두 $task 아래로 저장한다.
# 예: 빌드 명령 *> (Join-Path $task 'build.log')
```

반환값은 작업 폴더의 절대 경로다. 실행마다 시각과 고유값이 달라 기존 결과와 섞이지 않는다. 기존 도구의 지정된 출력 폴더에는 `-OutputDirectory`로 README를 추가할 수 있다. 반복 실행마다 새 작업 폴더를 지정하는 것이 기본이며 같은 폴더 재사용은 같은 작업을 이어갈 때만 허용한다.

`TestLan.ps1`은 실행별 폴더와 README를 자동 생성한다. `PackageLan.ps1`은 실행별 작업 폴더 안에 패키지와 `package.log`를 저장한다. `RunSharpTrial.ps1`은 배치 및 Trial 설명을 생성한다. 분석기와 Native Recorder도 출력 폴더에 설명을 남긴다.

## 엔진 자동 생성물과 기존 파일

언리얼 자체의 Logs, Autosaves, Config, 셰이더/빌드 캐시, Cook/Stage 임시 출력은 엔진 기본 동작을 유지한다. 이 규칙은 Saved 전체를 감시해서 파일을 옮기는 기능이 아니다. 수동 명령도 출력 경로를 작업 폴더로 지정해야 한다. 기존 결과는 자동 재배치하지 않는다.

`Saved` 내용은 `.gitignore`로 제외된다. 이 문서, 루트 `AGENTS.md`, 생성 스크립트는 Git으로 공유하고 결과물은 필요한 경우 README를 포함한 작업 폴더 전체를 별도 공유한다. 팀에 규칙을 배포하려면 이 파일들을 함께 커밋한다.
