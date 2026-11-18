param(
    [string]$EngineRoot = 'C:/Program Files/Epic Games/UE_5.8',
    [int]$Port = 7789
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$engineExe = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$projectFile = Join-Path $projectRoot 'GE_AIReplication.uproject'
$taskDir = & "$PSScriptRoot/NewSavedTask.ps1" -TaskName 'jang-combat-smoke' `
    -Description 'Lv_Jang 전투 회귀 및 서버 권한 4방향 회전/후퇴, Dedicated Server와 공격/관찰 Client 동기화 검증' `
    -Command 'Scripts/TestJangCombat.ps1 (Editor 빌드 및 ConfigureJangCombat.py 실행 후)' `
    -Artifacts @('standalone.log: 단일 실행 통합 검증', 'server.log / client.log / observer.log: 서버와 두 클라이언트의 피격 복제 검증', 'summary.json: 최종 판정')

function Start-TestProcess([string[]]$ExtraArguments, [string]$LogName) {
    $processArgs = @('"' + $projectFile + '"') + $ExtraArguments + @('-unattended', '-nullrhi', '-nosplash', '-nosound', ('-abslog="' + (Join-Path $taskDir $LogName) + '"'))
    Start-Process -FilePath $engineExe -ArgumentList $processArgs -WindowStyle Hidden -PassThru
}
function Wait-TestProcess($Process) {
    $deadline = [DateTime]::UtcNow.AddSeconds(100)
    while (!$Process.WaitForExit(1000)) {
        if ([DateTime]::UtcNow -gt $deadline) { Stop-Process -Id $Process.Id; throw 'Unreal validation exceeded 100 seconds.' }
    }
    if ($Process.ExitCode -ne 0) { throw "Unreal exited with code $($Process.ExitCode). See task logs." }
}
$results = [ordered]@{}
try {
    $standalone = Start-TestProcess @('/Game/JWJ/Lv_Jang', '-game', '-SharpMode=EventOnly', '-SCCProvider=None', '-ExecCmds="t.MaxFPS 60,Combat.Smoke Server"') 'standalone.log'
    Wait-TestProcess $standalone
    $results['standalone'] = (Get-Content -LiteralPath (Join-Path $taskDir 'standalone.log') -Raw) -match '\[JangCombatSmoke\] PASS Server'
    if (!$results['standalone']) { throw 'Standalone validation did not report PASS.' }
    $server = Start-TestProcess @('/Game/JWJ/Lv_Jang', '-server', "-port=$Port", '-SharpMode=EventPredictor', '-SCCProvider=None', '-ExecCmds="t.MaxFPS 60,Combat.Smoke Server"') 'server.log'
    $client = $null
    $observer = $null
    try {
        $ready = $false
        for ($attempt = 0; $attempt -lt 60; $attempt++) {
            $serverLog = Join-Path $taskDir 'server.log'
            if ((Test-Path -LiteralPath $serverLog) -and (Select-String -LiteralPath $serverLog -Pattern 'Load map complete' -Quiet)) { $ready = $true; break }
            if ($server.HasExited) { throw 'Server exited before accepting clients.' }
            Start-Sleep -Milliseconds 500
        }
        if (!$ready) { throw 'Dedicated server did not become ready.' }
        $client = Start-TestProcess @("127.0.0.1:$Port", '-game', '-ExecCmds="t.MaxFPS 60,Combat.Smoke Client"') 'client.log'
        # Let the attacking client claim the first player slot used by the server fixture.
        for ($attempt = 0; $attempt -lt 60; $attempt++) {
            $clientLog = Join-Path $taskDir 'client.log'
            if ((Test-Path -LiteralPath $clientLog) -and (Select-String -LiteralPath $clientLog -Pattern 'Load map complete' -Quiet)) { break }
            if ($client.HasExited) { throw 'Attacking client exited before observer launch.' }
            Start-Sleep -Milliseconds 250
        }
        $observer = Start-TestProcess @("127.0.0.1:$Port", '-game', '-ExecCmds="t.MaxFPS 60,Combat.Smoke Observer"') 'observer.log'
        Wait-TestProcess $server
        $serverText = Get-Content -LiteralPath $serverLog -Raw
        $clientText = Get-Content -LiteralPath (Join-Path $taskDir 'client.log') -Raw
        $observerText = Get-Content -LiteralPath (Join-Path $taskDir 'observer.log') -Raw
        $results['network'] = $serverText -match '\[JangCombatSmoke\] PASS Server' -and $clientText -match '\[JangCombatSmoke\] PASS Client' -and $clientText -notmatch '\[JangCombatSmoke\] FAIL'
        $results['directions'] = $clientText -match '\[JangCombatSmoke\] PASS FourDirections' -and $observerText -match '\[JangCombatSmoke\] PASS FourDirections' -and $observerText -notmatch '\[JangCombatSmoke\] FAIL'
        $results['presentation'] = $clientText -match '\[JangCombatSmoke\] PASS DirectionalPresentation' -and $observerText -match '\[JangCombatSmoke\] PASS DirectionalPresentation'
        if (!$results['network']) { throw 'Dedicated server/client integration validation failed.' }
        if (!$results['directions']) { throw 'Four-direction replication to both clients failed.' }
        if (!$results['presentation']) { throw 'Facing and retreat movement were not observed in all directions on both clients.' }
    } finally {
        if ($client -and !$client.HasExited) { Stop-Process -Id $client.Id }
        if ($observer -and !$observer.HasExited) { Stop-Process -Id $observer.Id }
        if (!$server.HasExited) { Stop-Process -Id $server.Id }
    }
} finally {
    $results | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskDir 'summary.json') -Encoding UTF8
    Add-Content -LiteralPath (Join-Path $taskDir 'README.md') -Value ("`n결과: " + ($results | ConvertTo-Json -Compress) + "`n클라이언트는 서버 검증이 끝난 뒤 종료합니다. 기록 근거로 README와 로그를 함께 보관합니다.") -Encoding UTF8
    Write-Output $taskDir
}
