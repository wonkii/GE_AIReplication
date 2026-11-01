param(
    [string]$EngineRoot = 'C:/Program Files/Epic Games/UE_5.8',
    [int]$Port = 7789
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$engineExe = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$projectFile = Join-Path $projectRoot 'GE_AIReplication.uproject'
$taskDir = & "$PSScriptRoot/NewSavedTask.ps1" -TaskName 'hitwall-smoke' `
    -Description '실제 Montage/Notify/Box/Root Motion 및 서버 RPC 검증' `
    -Command 'Scripts/TestHitWall.ps1 (Editor 빌드 후 실행)' `
    -Artifacts @('*.log: 각 프로세스 로그', 'summary.json: 판정 결과')

function Start-TestProcess([string[]]$ExtraArguments, [string]$LogName) {
    $processArgs = @('"' + $projectFile + '"') + $ExtraArguments + @('-unattended', '-nullrhi', '-nosplash', '-nosound', ('-abslog="' + (Join-Path $taskDir $LogName) + '"'))
    Start-Process -FilePath $engineExe -ArgumentList $processArgs -WindowStyle Hidden -PassThru
}
function Wait-TestProcess($Process) {
    if (!$Process.WaitForExit(120000)) {
        Stop-Process -Id $Process.Id
        throw 'Unreal test exceeded 120 seconds.'
    }
}
$results = [ordered]@{}
try {
    foreach ($case in @('wall', 'free', 'queue')) {
        $arguments = @('/Game/RootMotionHitWall/Maps/L_HitWallBaseline', '-game', '-HitWallSmoke', '-UseFixedTimeStep', '-FPS=60')
        if ($case -eq 'free') { $arguments += '-HitWallNoWall' }
        if ($case -eq 'queue') { $arguments += '-HitWallQueue' }
        $testProcess = Start-TestProcess $arguments "$case.log"
        Wait-TestProcess $testProcess
        $logText = Get-Content -LiteralPath (Join-Path $taskDir "$case.log") -Raw
        $results[$case] = $logText -match '\[HitWallSmoke\] PASS'
        if (!$results[$case]) { throw "Failed test: $case" }
        if ($case -eq 'wall' -and $logText -notmatch 'EnemyFinal=X=7[0-4]\.') { throw 'Wall did not constrain capsule at expected X.' }
        if ($case -eq 'free' -and $logText -notmatch 'EnemyFinal=X=1[0-9][0-9]\.') { throw 'Unobstructed root motion was not observed.' }
    }
    $serverProcess = Start-TestProcess @('/Game/RootMotionHitWall/Maps/L_HitWallBaseline', '-server', "-port=$Port") 'server.log'
    try {
        $ready = $false
        for ($attempt = 0; $attempt -lt 60; $attempt++) {
            $serverLog = Join-Path $taskDir 'server.log'
            if ((Test-Path -LiteralPath $serverLog) -and (Select-String -LiteralPath $serverLog -Pattern 'Load map complete' -Quiet)) { $ready = $true; break }
            Start-Sleep -Milliseconds 500
        }
        if (!$ready) { throw 'Dedicated server did not become ready.' }
        $clientProcess = Start-TestProcess @("127.0.0.1:$Port", '-game', '-HitWallNetworkSmoke') 'client.log'
        Wait-TestProcess $clientProcess
        $clientLog = Get-Content -LiteralPath (Join-Path $taskDir 'client.log') -Raw
        $serverLogText = Get-Content -LiteralPath $serverLog -Raw
        $results['network'] = ($clientLog -match '\[HitWallSmoke\] PASS') -and ([regex]::Matches($serverLogText, '\[HitWall\] Hit attacker=').Count -eq 3)
        if (!$results['network']) { throw 'Dedicated server/client attack or reaction test failed.' }
    } finally {
        if (!$serverProcess.HasExited) { Stop-Process -Id $serverProcess.Id }
    }
} finally {
    $results | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskDir 'summary.json') -Encoding UTF8
    Add-Content -LiteralPath (Join-Path $taskDir 'README.md') -Value ("`n검증 결과: " + ($results | ConvertTo-Json -Compress) + "`n로그는 기능 확인 근거로 보관합니다.") -Encoding UTF8
    Write-Output $taskDir
}
