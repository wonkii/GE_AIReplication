param([string]$EngineRoot = 'C:/Program Files/Epic Games/UE_5.8')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$task = & "$PSScriptRoot/NewSavedTask.ps1" -TaskName 'joint-analysis-smoke' `
    -Description '디버그 명령을 통한 관절 추출/PELT/Library 단계별 통합 검증' `
    -Command 'Scripts/TestAnimationAnalysis.ps1 (Editor build and SetupAnimationAnalysis first)' `
    -Artifacts @('unreal.log: AnimAnalysis 명령 실행 기록', 'summary.json: 실행 결과; 실제 분석은 로그의 별도 task 폴더')
$success = $false
try {
    $exe = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
    $arguments = @('"' + (Join-Path $root 'GE_AIReplication.uproject') + '"', '-run=pythonscript',
        '-script="' + (Join-Path $PSScriptRoot 'AnimationAnalysis/smoke_unreal.py') + '"',
        '-unattended', '-nullrhi', '-nosplash', '-nosound', '-SCCProvider=None', '-CustomConfig=AnimationAnalysis',
        '-abslog="' + (Join-Path $task 'unreal.log') + '"')
    $process = Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if (!$process.WaitForExit(300000)) { Stop-Process -Id $process.Id; throw 'Animation analysis smoke exceeded 300 seconds.' }
    $text = Get-Content -LiteralPath (Join-Path $task 'unreal.log') -Raw
    $success = $process.ExitCode -eq 0 -and $text.Contains('[AnimAnalysis] PASS Integration:')
    if (!$success) { throw "Integration failed. See $task/unreal.log" }
} finally {
    @{ passed=$success; exit_code=$process.ExitCode } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $task 'summary.json') -Encoding UTF8
    Add-Content -LiteralPath (Join-Path $task 'README.md') -Value "`n결과: passed=$success. 로그와 연결된 원본 분석 작업 폴더를 함께 보관하세요." -Encoding UTF8
    Write-Output $task
}
