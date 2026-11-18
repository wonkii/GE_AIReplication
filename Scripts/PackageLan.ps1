param(
    [string]$EngineRoot = 'C:/Program Files/Epic Games/UE_5.8',
    [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $projectRoot 'GE_AIReplication.uproject'
$uat = Join-Path $EngineRoot 'Engine/Build/BatchFiles/RunUAT.bat'
if (-not (Test-Path -LiteralPath $uat)) { throw "Unreal Automation Tool not found: $uat" }
$OutputDirectory = & (Join-Path $PSScriptRoot 'NewSavedTask.ps1') -TaskName 'LAN-Package' `
    -Description 'Lv_Jang Windows Development LAN 패키징' -Command 'Scripts/PackageLan.ps1 (UE BuildCookRun, /Game/JWJ/Lv_Jang)' `
    -OutputDirectory $OutputDirectory -Artifacts @('Windows/: 배포할 전체 패키지 폴더. PlayLAN.cmd로 시작', 'package.log: 빌드/쿠킹/패키징 결과. ExitCode=0 확인', 'UAT-*/: 이번 실행의 AutomationTool 상세 로그')
$previousUatLogFolder = $env:uebp_LogFolder
$previousUatFinalLogFolder = $env:uebp_FinalLogFolder
# UAT clears its log directory. Give each invocation a new task-owned directory.
$env:uebp_LogFolder = Join-Path $OutputDirectory ('UAT-' + [guid]::NewGuid().ToString('N').Substring(0,8))
$env:uebp_FinalLogFolder = $env:uebp_LogFolder
try {
& $uat BuildCookRun "-project=$projectFile" -noP4 -platform=Win64 -clientconfig=Development `
    -build -cook -stage -pak -package -archive "-archivedirectory=$OutputDirectory" `
    '-map=/Game/JWJ/Lv_Jang+/Game/ThirdPerson/Lvl_ThirdPerson+/Game/SharpMovementExperiment/Maps/SharpMovementExperiment' `
    '-ubtargs=-NoUBA -NoXGE -MaxParallelActions=2' -unattended -utf8output 2>&1 | Tee-Object -FilePath (Join-Path $OutputDirectory 'package.log')
$packageExitCode = $LASTEXITCODE
}
finally {
    $env:uebp_LogFolder = $previousUatLogFolder
    $env:uebp_FinalLogFolder = $previousUatFinalLogFolder
}
Add-Content -LiteralPath (Join-Path $OutputDirectory 'README.md') -Value "`n실행 결과: BuildCookRun ExitCode=$packageExitCode (0이면 성공)." -Encoding UTF8
if ($packageExitCode -ne 0) { throw "LAN packaging failed (exit $packageExitCode)" }
$windowsDirectory = Join-Path $OutputDirectory 'Windows'
@'
@echo off
start "" /D "%~dp0" "%~dp0GE_AIReplication.exe" -windowed -ResX=1280 -ResY=720 -ExecCmds="t.MaxFPS 60" %*
'@ | Set-Content -LiteralPath (Join-Path $windowsDirectory 'PlayLAN.cmd') -Encoding ascii
@'
# Lv_Jang LAN 멀티플레이

이 Windows 폴더 전체를 각 PC에 복사하고 PlayLAN.cmd를 실행하세요.
기본 레벨: /Game/JWJ/Lv_Jang. 실행 파일 하나로 Host와 Client를 모두 지원합니다.

1. Host: 'Host: 서버 열기'를 누르고 표시된 IPv4:포트를 공유하세요.
2. Host: '서버 접속'을 눌러 플레이를 시작하세요.
3. Client: Host 주소 입력 → 'LAN에서 주소 확인' → '서버 접속'.
4. 나머지 Client도 같은 순서로 참가하세요. LAN 세션 공개 연결 수는 8입니다.
5. Esc로 메뉴를 열어 '서버 종료 / 나가기'. Host 종료 시 Client 연결도 종료됩니다.

같은 LAN이 필요합니다. Windows 방화벽에서 게임의 사설 네트워크 통신을 허용하세요.
기본 게임 UDP 포트는 7777, LAN 검색 UDP 포트는 14001입니다.
VPN/가상 어댑터가 잘못 선택되면 PlayLAN.cmd -MULTIHOME=자신의LAN_IP 로 실행하세요.

PlayLAN.cmd는 1280x720 창 모드, 최대 60FPS로 시작합니다.
플레이어 이동 복제는 최대 60Hz/최소 30Hz 설정이며 실제 전송률은 부하에 따라 달라집니다.
Exponential 스무딩: Client 위치 0.06초, Host에서 보는 원격 플레이어 위치 0.05초.
이 설정은 AI 실험 정책과 별개이며 모든 환경의 끊김 제거를 보장하지는 않습니다.
'@ | Set-Content -LiteralPath (Join-Path $windowsDirectory 'LAN-사용법.md') -Encoding UTF8
Add-Content -LiteralPath (Join-Path $OutputDirectory 'README.md') -Value "`n배포: Windows 폴더 전체 복사 후 PlayLAN.cmd 실행. Windows/LAN-사용법.md 참고. 기본 맵=/Game/JWJ/Lv_Jang." -Encoding UTF8
Write-Output "LAN package: $OutputDirectory (copy the entire Windows folder to each PC)"
