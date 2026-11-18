param(
    [string]$EngineRoot = 'C:/Program Files/Epic Games/UE_5.8',
    [switch]$Network,
    [switch]$Standalone,
    [int]$Port = 7791
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$task = & "$PSScriptRoot/NewSavedTask.ps1" -TaskName 'reaction-warp-test' `
    -Description 'W0-W2 source/policy/solver 검증; 선택적으로 실제 서버/관찰 클라이언트 통합 검증' `
    -Command "Scripts/TestReactionWarp.ps1 -Network:$Network" `
    -Artifacts @('offline.log: source/migration/plan report checks','server.log / client.log / observer.log: optional real peers','summary.json: results; owner replay is BLOCKED')
$exe = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$processes = @()
function Start-WarpProcess([string[]]$Extra, [string]$LogName) {
    $arguments = @('"' + (Join-Path $root 'GE_AIReplication.uproject') + '"') + $Extra + @('-unattended', '-nullrhi', '-nosplash', '-nosound', '-SCCProvider=None', '-CustomConfig=AnimationAnalysis', ('-abslog="' + (Join-Path $task $LogName) + '"'))
    $arguments | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $task ($LogName + '.arguments.json')) -Encoding UTF8
    Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
}
function Wait-WarpProcess($Process, [int]$Seconds=180) {
    $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
    while (!$Process.WaitForExit(500)) {
        if ([DateTime]::UtcNow -gt $deadline) { throw 'Warp test timed out; inspect logs.' }
    }
    if ($Process.ExitCode -ne 0) { throw "Unreal exit code $($Process.ExitCode). See $task" }
}
$results = [ordered]@{ offline=$false; network='not run'; owner='BLOCKED: prediction/replay fixture unavailable'; naturalness='user evaluation' }
try {
    $offline = Start-WarpProcess @('-run=pythonscript', ('-script="' + (Join-Path $PSScriptRoot 'ReactionWarp/smoke_unreal.py') + '"')) 'offline.log'
    $processes += $offline
    Wait-WarpProcess $offline
    $results.offline = (Get-Content -LiteralPath (Join-Path $task 'offline.log') -Raw).Contains('[ReactionWarp] PASS Integration')
    if (!$results.offline) { throw 'Offline integration did not report PASS.' }
    if ($Standalone) {
        $single = Start-WarpProcess @('/Game/JWJ/Lv_Jang', '-game', '-ExecCmds="t.MaxFPS 60,hitwall.Probe 1,ReactionWarp.Smoke Server Compare"') 'standalone.log'
        $processes += $single
        Wait-WarpProcess $single
        $results.standalone = (Get-Content -LiteralPath (Join-Path $task 'standalone.log') -Raw).Contains('[ReactionWarpSmoke] PASS Server')
        if (!$results.standalone) { throw 'Standalone fixture did not report PASS.' }
    }
    if ($Network) {
        $server = Start-WarpProcess @('/Game/JWJ/Lv_Jang', '-server', "-port=$Port", '-SharpMode=EventPredictor', '-ExecCmds="t.MaxFPS 60,hitwall.Probe 1,ReactionWarp.Smoke Server"') 'server.log'
        $processes += $server
        $ready = $false
        for ($attempt=0; $attempt -lt 80; $attempt++) {
            $log = Join-Path $task 'server.log'
            if ((Test-Path -LiteralPath $log) -and (Select-String -LiteralPath $log -Pattern 'Load map complete' -Quiet)) { $ready=$true; break }
            if ($server.HasExited) { throw 'Server exited before map loaded.' }
            Start-Sleep -Milliseconds 500
        }
        if (!$ready) { throw 'Server did not become ready.' }
        $client = Start-WarpProcess @("127.0.0.1:$Port", '-game', '-SharpClientId=warp-client', '-ExecCmds="t.MaxFPS 60,hitwall.Probe 1,ReactionWarp.Smoke Client"') 'client.log'
        $processes += $client
        $observer = Start-WarpProcess @("127.0.0.1:$Port", '-game', '-SharpClientId=warp-observer', '-ExecCmds="t.MaxFPS 60,hitwall.Probe 1,ReactionWarp.Smoke Observer"') 'observer.log'
        $processes += $observer
        Wait-WarpProcess $server
        $texts = @{}
        foreach ($role in @('server','client','observer')) { $texts[$role] = Get-Content -LiteralPath (Join-Path $task "$role.log") -Raw }
        foreach ($role in @('server','client','observer')) {
            if ($texts[$role] -notmatch '\[ReactionWarpSmoke\] PASS' -or $texts[$role] -match '\[ReactionWarpSmoke\] FAIL|PlanRejected=') { throw "$role verification failed." }
        }
        $serverHashes = @([regex]::Matches($texts.server, '\[ReactionWarpPeer\].*Result=1.*Plan=([A-F0-9]+)') | ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique)
        if (!$serverHashes.Count) { throw 'No compressed server plan was executed.' }
        foreach ($role in @('client','observer')) {
            foreach ($hash in $serverHashes) { if (!$texts[$role].Contains("Plan=$hash")) { throw "$role missed plan $hash" } }
        }
        $results.network = 'PASS: server + two simulated AI observers, identical planned hashes, natural completion/cancel/replacement'
    }
} finally {
    foreach ($process in $processes) { if (!$process.HasExited) { Stop-Process -Id $process.Id } }
    $results | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $task 'summary.json') -Encoding UTF8
    Add-Content -LiteralPath (Join-Path $task 'README.md') -Value ("`n결과: " + ($results | ConvertTo-Json -Compress) + "`nRaw logs and linked diagnostic tasks should be retained together.") -Encoding UTF8
    Write-Output $task
}
