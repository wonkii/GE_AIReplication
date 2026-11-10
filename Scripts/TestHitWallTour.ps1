param(
    [string]$EngineRoot = 'C:/Program Files/Epic Games/UE_5.8',
    [switch]$Rendered
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$task = & "$PSScriptRoot/NewSavedTask.ps1" -TaskName 'hitwall-tour-test' `
    -Description 'YHJ 여섯 지점 Off/MultiPhase 12 trial 및 취소/완료 상태 복원 검사' `
    -Command "Scripts/TestHitWallTour.ps1 -Rendered:$Rendered" `
    -Artifacts @('tour.log: 실제 맵 실행과 PASS/FAIL', 'summary.json: 실행 결과와 한계')
$exe = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$renderFlag = if ($Rendered) { '-RenderOffscreen' } else { '-nullrhi' }
$projectArgument = '"' + (Join-Path $root 'GE_AIReplication.uproject') + '"'
$arguments = @($projectArgument,
    '/Game/YHJ/Lv_HitWallTest', '-game', '-unattended', $renderFlag, '-DisablePython',
    '-nosound', '-nosplash', '-CustomConfig=AnimationAnalysis',
    '-ExecCmds="t.IdleWhenNotForeground 0,ReactionWarp.TourSmoke"',
    ('-abslog="' + (Join-Path $task 'tour.log') + '"'))
$process = $null
$passed = $false
try {
    $process = Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if (!$process.WaitForExit(150000)) { throw 'Tour timeout; inspect tour.log' }
    $log = Get-Content -LiteralPath (Join-Path $task 'tour.log') -Raw
    $passed = $process.ExitCode -eq 0 -and $log.Contains('[HitWallTourSmoke] PASS')
    if (!$passed) { throw "Tour test failed (exit=$($process.ExitCode)); inspect $task" }
} finally {
    if ($process -and !$process.HasExited) { Stop-Process -Id $process.Id }
    @{ passed=$passed; rendered=[bool]$Rendered; naturalness='user evaluation'; network='not tested; replication unchanged' } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $task 'summary.json') -Encoding utf8
    Add-Content -LiteralPath (Join-Path $task 'README.md') -Value "`nOutcome: passed=$passed. Retain logs and linked ReactionWarp task folders. Naturalness is user evaluation."
    Write-Output $task
}
