#Requires -Version 7.0
[CmdletBinding()]
param(
    [ValidateSet('Pilot','FrequencyMatrix')][string]$Plan = 'Pilot',
    [ValidateRange(1,100)][int]$Blocks = 1,
    [string]$QualityPolicy,
    [string]$Editor = 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe',
    [string]$Project = (Join-Path $PSScriptRoot '../GE_AIReplication.uproject'),
    [string]$Map = '/Game/JWJ/Lv_Jang',
    [double]$X = -30, [double]$Y = 2420, [double]$Z = 2, [double]$Heading = 85.975,
    [ValidateRange(1024,65535)][int]$Port = 18777,
    [switch]$DryRun
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$Project = (Resolve-Path -LiteralPath $Project).Path
if (-not $DryRun -and -not (Test-Path -LiteralPath $Editor -PathType Leaf)) { throw "Editor not found: $Editor" }
if ($QualityPolicy) { $QualityPolicy = (Resolve-Path -LiteralPath $QualityPolicy).Path }
$root = & "$PSScriptRoot/NewSavedTask.ps1" -TaskName 'SprintCost' -Description 'Split-process sprint cost and input reconstruction' `
    -Command $MyInvocation.Line -Artifacts @('plan.csv — planned cells', 'trials — role logs, endpoint traces and raw CSVs',
        'attempts.csv — all attempts and outcomes', 'cost_pairs.csv — comparable blocks', 'break_even.csv — frequency estimates', 'results.md — batch report')
$policyPath = Join-Path $root 'quality_policy.json'
if ($QualityPolicy) { Copy-Item -LiteralPath $QualityPolicy -Destination $policyPath }
else {
    @{ MaxMissedTapPct = $null; MaxAbsReleaseErrorP95Ms = $null; MaxReconciliationP95Cm = $null } |
        ConvertTo-Json | Set-Content -Encoding utf8 -LiteralPath $policyPath
}
$batches = @(
    if ($Plan -eq 'Pilot') {
        [pscustomobject]@{ Jitter = 0; Loss = 0 }
        [pscustomobject]@{ Jitter = 200; Loss = 10 }
    } else {
        foreach ($jitter in 100,200,300) {
            foreach ($loss in 0,10,20) { [pscustomobject]@{ Jitter = $jitter; Loss = $loss } }
        }
    }
)
$schedules = @(
    [pscustomobject]@{ Name = 'Walk'; Rate = 0; Duty = 0.0 }
    [pscustomobject]@{ Name = 'Hold'; Rate = 0; Duty = 1.0 }
    foreach ($rate in 2,6,12,30) { [pscustomobject]@{ Name = 'Periodic'; Rate = $rate; Duty = 0.5 } }
    [pscustomobject]@{ Name = 'MixedTap'; Rate = 18; Duty = 0.26 }
)
if ($Plan -eq 'Pilot') { $schedules = @($schedules | Where-Object { $_.Name -in 'Walk','MixedTap' -or $_.Rate -eq 12 }) }
$variants = @('SeparateRpc','MoveData','MoveDataBit')

function Get-InputEdges($schedule) {
    if ($schedule.Name -eq 'MixedTap') {
        foreach ($offset in 0,20,40) {
            $index = 0
            foreach ($time in 2,2.1,2.2,2.3,8,13) {
                @{ Seconds = $offset + $time; Pressed = (($index++ % 2) -eq 0) }
            }
        }
    } elseif ($schedule.Rate -gt 0) {
        for ($i = 0; $i -lt $schedule.Rate; ++$i) {
            @{ Seconds = (60.0 / $schedule.Rate) * ($i + 0.5); Pressed = (($i % 2) -eq 0) }
        }
    }
}

$planned = @(foreach ($network in $batches) {
    foreach ($schedule in $schedules) {
        $networkName = "Jitter$($network.Jitter)Loss$($network.Loss)"
        $key = "$networkName-$($schedule.Name)-$($schedule.Rate)"
        $edges = @(Get-InputEdges $schedule)
        $inputHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData(
            [Text.Encoding]::UTF8.GetBytes((ConvertTo-Json -InputObject $edges -Depth 5 -Compress)))).Substring(0,16)
        for ($block = 1; $block -le $Blocks; ++$block) {
            for ($v = 0; $v -lt $variants.Count; ++$v) {
                $variant = $variants[($v + $block - 1) % $variants.Count]
                $id = [guid]::NewGuid().ToString('N')
                $trial = Join-Path $root "trials/$key/B$block-$variant-$($id.Substring(0,8))"
                [pscustomobject]@{ Path = $trial; RunId = $id; Variant = $variant; Scenario = $schedule.Name;
                    Frequency = $schedule.Rate; Duty = $schedule.Duty; Network = $networkName; JitterMs = $network.Jitter;
                    LossPct = $network.Loss; Block = $block; ConditionHash = "$key-$inputHash"; InputHash = $inputHash;
                    InputEdges = $edges }
            }
        }
    }
})
$planned | Select-Object Path,RunId,Variant,Scenario,Frequency,Duty,Network,JitterMs,LossPct,Block,ConditionHash,InputHash |
    Export-Csv -NoTypeInformation -Encoding utf8 -LiteralPath (Join-Path $root 'plan.csv')

function Start-Role($trial, [string]$role) {
    $destination = if ($role -eq 'server') { $Map } else { "127.0.0.1:$Port" }
    $mode = if ($role -eq 'server') { '-server' } else { '-game' }
    $log = Join-Path $trial "$role/Unreal.log"
    $result = Start-Process -FilePath $Editor -PassThru -WindowStyle Hidden -ArgumentList @(
        ('"' + $Project + '"'), $destination, $mode, "-port=$Port", '-NoLiveCoding', '-NoSound', '-NoVSync',
        '-Unattended', '-NoSplash', '-Windowed', '-ResX=640', '-ResY=480',
        ('"-abslog=' + $log + '"'), "-SprintCostRole=$role", ('"-SprintCostDirectory=' + $trial + '"'))
    return $result
}

function Wait-TrialFile($trial, $relative, $processes, [int]$timeout) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $target = Join-Path $trial $relative
    while (-not (Test-Path -LiteralPath $target)) {
        foreach ($role in 'server','owner','observer') {
            $errorFile = Join-Path $trial "$role/role_failed.txt"
            if (Test-Path -LiteralPath $errorFile) { throw "$role — $(Get-Content -Raw -LiteralPath $errorFile)" }
        }
        if (Test-Path -LiteralPath $target) { break }
        foreach ($process in $processes) {
            if ($process.HasExited -and -not (Test-Path -LiteralPath $target)) { throw "Process $($process.Id) exited with $($process.ExitCode) before $relative" }
        }
        if ($watch.Elapsed.TotalSeconds -gt $timeout) { throw "Timed out waiting for $relative" }
        Start-Sleep -Milliseconds 250
    }
}

function Wait-OwnedListener($server, [int]$port) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    while (-not (Get-NetUDPEndpoint -LocalPort $port -OwningProcess $server.Id -ErrorAction SilentlyContinue)) {
        if ($server.HasExited) { throw "Server exited with $($server.ExitCode) before opening UDP $port" }
        if ($watch.Elapsed.TotalSeconds -gt 120) { throw "Server did not listen on UDP $port" }
        Start-Sleep -Milliseconds 500
    }
}

$completed = 0
$failed = 0
try {
    if (-not $DryRun -and (Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue)) { throw "UDP port $Port is already in use." }
    foreach ($row in $planned) {
        $trial = $row.Path
        & "$PSScriptRoot/NewSavedTask.ps1" -OutputDirectory $trial -TaskName 'SprintCostCell' -Description $row.ConditionHash `
            -Command "UnrealEditor -SprintCostDirectory=$trial -SprintCostRole=<role>" -Artifacts @(
                'plan.json — assigned role configuration and input schedule', 'owner, server, observer — role capture and logs',
                'manifest.json — measured environment and connection windows', 'input_windows.csv, move_timeline.csv, correction_episodes.csv — diagnostics') | Out-Null
        foreach ($role in 'server','owner','observer') { New-Item -ItemType Directory -Path (Join-Path $trial $role) | Out-Null }
        $data = [ordered]@{ RunId = $row.RunId; Variant = $row.Variant; Scenario = $row.Scenario; Frequency = $row.Frequency;
            Duty = $row.Duty; Network = $row.Network; JitterMs = $row.JitterMs; LossPct = $row.LossPct; Block = $row.Block;
            ConditionHash = $row.ConditionHash; InputHash = $row.InputHash; InputEdges = $row.InputEdges; Map = $Map;
            X = $X; Y = $Y; Z = $Z; HeadingDegrees = $Heading; Port = $Port; Instrumentation = 'SplitProcess';
            OwnerLagMinMs = $(if ($row.JitterMs -or $row.LossPct) { 50 } else { 0 });
            OwnerLagMaxMs = $(if ($row.JitterMs -or $row.LossPct) { 50 + $row.JitterMs } else { 0 });
            IncomingFixedLagMs = $(if ($row.JitterMs -or $row.LossPct) { 50 } else { 0 });
            OwnerLossPercentPerDirection = $row.LossPct; InputSource = 'Synthetic';
            PlannedWindowSeconds = 60; TailSeconds = 10;
            QualityPolicy = (Get-Content -Raw -LiteralPath $policyPath | ConvertFrom-Json) }
        $data | ConvertTo-Json -Depth 8 | Set-Content -Encoding utf8 -LiteralPath (Join-Path $trial 'plan.json')
        $processes = [Collections.Generic.List[Diagnostics.Process]]::new()
        $status = 'Planned'
        $reason = ''
        try {
            if (-not $DryRun) {
                $server = Start-Role $trial 'server'
                $processes.Add($server)
                Wait-OwnedListener $server $Port
                $processes.Add((Start-Role $trial 'owner'))
                Wait-TrialFile $trial 'owner/ready.txt' $processes 120
                $processes.Add((Start-Role $trial 'observer'))
                Wait-TrialFile $trial 'complete.txt' $processes 300
                ++$completed
                $status = 'Completed'
            }
        } catch {
            ++$failed
            $status = 'Failed'
            $reason = $_.Exception.Message
            Write-Warning "$($row.ConditionHash) $($row.Variant) $reason"
        } finally {
            foreach ($process in $processes) {
                if (-not $process.HasExited -and -not $process.WaitForExit(3000)) { Stop-Process -InputObject $process -ErrorAction SilentlyContinue }
                $process.Dispose()
            }
            Add-Content -LiteralPath (Join-Path $trial 'README.md') -Value "`nOutcome: $status $reason"
            [pscustomobject]@{ Path = $trial; RunId = $row.RunId; Variant = $row.Variant; Block = $row.Block;
                ConditionHash = $row.ConditionHash; Outcome = $status; Detail = $reason } |
                Export-Csv -Append -NoTypeInformation -Encoding utf8 -LiteralPath (Join-Path $root 'attempts.csv')
        }
    }
    if (-not $DryRun) { & "$PSScriptRoot/CompareSprintTrials.ps1" -CostBatch $root }
} finally {
    Add-Content -LiteralPath (Join-Path $root 'README.md') -Value "`nOutcome: $completed complete, $failed failed, dry run $DryRun."
    Write-Output "Results: $root"
}
