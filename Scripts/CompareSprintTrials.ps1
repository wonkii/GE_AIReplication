[CmdletBinding(DefaultParameterSetName='Existing')]
param(
    [Parameter(Mandatory, ParameterSetName='Existing')]
    [string[]]$RunFolders,
    [Parameter(Mandatory, ParameterSetName='SplitProcess')]
    [string]$CostBatch
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
$variants = @('SeparateRpc', 'MoveData')

function Get-Median([double[]]$samples) {
    $sorted = @($samples | Sort-Object)
    $middle = [int][Math]::Floor($sorted.Count / 2)
    if ($sorted.Count % 2) { return $sorted[$middle] }
    return ($sorted[$middle - 1] + $sorted[$middle]) / 2.0
}

function Get-BootstrapInterval([double[]]$samples) {
    if ($samples.Count -lt 2) { return @($null, $null) }
    $random = [Random]::new(42)
    $draw = [double[]]::new($samples.Count)
    $medians = [double[]]::new(2000)
    for ($iteration = 0; $iteration -lt $medians.Count; $iteration++) {
        for ($index = 0; $index -lt $draw.Count; $index++) {
            $draw[$index] = $samples[$random.Next($samples.Count)]
        }
        $medians[$iteration] = Get-Median $draw
    }
    [Array]::Sort($medians)
    return @($medians[49], $medians[1949])
}

function Get-P95([double[]]$samples) {
    if ($samples.Count -eq 0) { return $null }
    $ordered = @($samples | Sort-Object)
    return $ordered[[int][Math]::Ceiling(.95 * $ordered.Count) - 1]
}

function Export-SplitProcessComparison([string]$batch) {
    $rows = @(Import-Csv -LiteralPath (Join-Path $batch 'attempts.csv') | Where-Object Outcome -eq 'Completed')
    $inputs = @(foreach ($row in $rows) {
        $m = Get-Content -Raw -LiteralPath (Join-Path $row.Path 'manifest.json') | ConvertFrom-Json
        $up = $m.OwnerEndpoint
        $down = $m.ServerEndpoint
        $profile = Import-Csv -LiteralPath (Join-Path $row.Path 'profile.csv') | Where-Object Phase -eq 'Total'
        $corrections = @(Import-Csv -LiteralPath (Join-Path $row.Path 'correction_episodes.csv') | Where-Object InCostWindow -eq '1')
        $moves = @(Import-Csv -LiteralPath (Join-Path $row.Path 'move_timeline.csv') | Where-Object { $_.InCostWindow -eq '1' -and $_.PositionErrorCm })
        $remote = @(Import-Csv -LiteralPath (Join-Path $row.Path 'observer/events.csv') | Where-Object {
            $_.Event -eq 'RemoteSample' -and [double]$_.PlatformSeconds -ge $up.WindowStart -and [double]$_.PlatformSeconds -lt $up.WindowEnd
        })
        $taps = @(Import-Csv -LiteralPath (Join-Path $row.Path 'input_windows.csv') | Where-Object PlannedPressRow)
        $seconds = $up.WindowEnd - $up.WindowStart
        $deltaUp = $up.SentBytes / $seconds
        $deltaDown = $down.SentBytes / ($down.WindowEnd - $down.WindowStart)
        $missed = @($taps | Where-Object { $_.Outcome -ne 'Overlap' -or -not $_.ReleaseErrorMs }).Count
        $missedRate = if ($taps.Count) { 100.0 * $missed / $taps.Count }
        $releaseP95 = Get-P95 @($taps | Where-Object ReleaseErrorMs | ForEach-Object { [math]::Abs([double]$_.ReleaseErrorMs) })
        $reconciled = @($corrections | Where-Object ReconciliationCm)
        $reconciliation = Get-P95 @($reconciled | ForEach-Object ReconciliationCm)
        $policy = $m.QualityPolicy
        $quality = if ($profile.Dropped -ne '0' -or ($corrections.Count -gt 0 -and -not $reconciled.Count)) { 'Incomplete' }
            elseif ($null -eq $policy.MaxMissedTapPct -or $null -eq $policy.MaxAbsReleaseErrorP95Ms -or $null -eq $policy.MaxReconciliationP95Cm) { 'Unset' }
            elseif (($taps.Count -gt 0 -and ($missedRate -gt $policy.MaxMissedTapPct -or $null -eq $releaseP95 -or
                $releaseP95 -gt $policy.MaxAbsReleaseErrorP95Ms)) -or
                ($null -ne $reconciliation -and $reconciliation -gt $policy.MaxReconciliationP95Cm)) { 'Failed' } else { 'Passed' }
        $bitRate = if ($row.Variant -eq 'MoveDataBit' -and $profile.NewMoveCalls -and $profile.OldMoveCalls -and $profile.PendingMoveCalls) {
            ([double]$profile.NewMoveCalls + [double]$profile.PendingMoveCalls + [double]$profile.OldMoveCalls) / [double]$profile.Seconds
        }
        [pscustomobject]@{
            Path = $row.Path; Variant = $row.Variant; Network = $m.Network; Scenario = $m.Scenario; Block = $row.Block
            Frequency = $m.Frequency; Duty = $m.Duty; Condition = "$($m.ConditionHash)|$($m.Map)|$($m.X),$($m.Y),$($m.Z),$($m.HeadingDegrees)|$($m.ProfileEnvironment)|$($m.ServerEndpoint.ProfileEnvironment)|$($m.EngineVersion)|$($m.TrialBuild)|$($m.ServerEndpoint.ServerMaxTickRate)|$($m.ClientErrorUpdateRateLimit)|$($m.MaxPositionErrorSquared)"
            UplinkBytesPerSec = $deltaUp; DownlinkBytesPerSec = $deltaDown; BytesPerSec = $deltaUp + $deltaDown
            RpcBitsPerSec = $profile.BidirectionalBitsPerSec; SprintFieldBitsPerSec = $bitRate; ProfileStatus = $profile.Status
            Corrections = $corrections.Count; CorrectionsPerMin = 60 * $corrections.Count / $seconds
            ReconciliationP95Cm = $reconciliation; ReconciliationSamples = $reconciled.Count
            PositionErrorP95Cm = Get-P95 @($moves | ForEach-Object PositionErrorCm)
            ReplayMsPerSec = $profile.ReplayMsPerSec; ObserverMeshOffsetP95Cm = Get-P95 @($remote | ForEach-Object MeshOffset)
            InputWindows = $taps.Count; MissedOrLatePct = $missedRate; AbsReleaseP95Ms = $releaseP95; Quality = $quality
        }
    })
    $pairs = @(foreach ($group in ($inputs | Group-Object Condition,Block)) {
        $candidate = @($group.Group | Where-Object Variant -eq 'MoveDataBit')
        if ($candidate.Count -ne 1) { continue }
        $candidate = $candidate[0]
        foreach ($variant in 'SeparateRpc','MoveData') {
            $reference = @($group.Group | Where-Object Variant -eq $variant)
            if ($reference.Count -ne 1) { continue }
            $reference = $reference[0]
            [pscustomobject]@{ Condition = $candidate.Condition; Network = $candidate.Network; Scenario = $candidate.Scenario;
                Frequency = $candidate.Frequency; Duty = $candidate.Duty; Block = $candidate.Block; Reference = $variant; Candidate = $candidate.Variant
                ReferencePath = $reference.Path; CandidatePath = $candidate.Path; ReferenceQuality = $reference.Quality; CandidateQuality = $candidate.Quality
                QualityPassed = $reference.Quality -eq 'Passed' -and $candidate.Quality -eq 'Passed'
                UplinkDeltaBytesPerSec = $candidate.UplinkBytesPerSec - $reference.UplinkBytesPerSec
                DownlinkDeltaBytesPerSec = $candidate.DownlinkBytesPerSec - $reference.DownlinkBytesPerSec
                DeltaBytesPerSec = $candidate.BytesPerSec - $reference.BytesPerSec
                DeltaRpcBitsPerSec = $(if ($candidate.RpcBitsPerSec -and $reference.RpcBitsPerSec) {
                    [double]$candidate.RpcBitsPerSec - [double]$reference.RpcBitsPerSec })
                DeltaCorrectionsPerMin = $candidate.CorrectionsPerMin - $reference.CorrectionsPerMin }
        }
    })
    $estimates = @(foreach ($group in ($pairs | Group-Object Condition,Reference)) {
        $first = $group.Group[0]
        $valid = @($group.Group | Where-Object QualityPassed)
        $values = @($valid | ForEach-Object DeltaBytesPerSec)
        $lower, $upper = Get-BootstrapInterval $values
        $sign = if (-not $values.Count) { 'QualityUndetermined' } elseif ($null -eq $lower -or ($lower -le 0 -and $upper -ge 0)) {
            'Uncertain'
        } elseif ($upper -lt 0) { 'Reduced' } else { 'Increased' }
        [pscustomobject]@{ Network = $first.Network; Scenario = $first.Scenario; Frequency = $first.Frequency; Duty = $first.Duty
            Condition = $first.Condition; Reference = $first.Reference; PairedBlocks = $group.Count; QualityPassedBlocks = $values.Count
            AllPairsMedian = Get-Median @($group.Group.DeltaBytesPerSec); QualifiedMedian = $(if ($values.Count) { Get-Median $values })
            Lower95 = $lower; Upper95 = $upper; Verdict = $sign; IntervalToNextFrequency = '' }
    })
    foreach ($group in ($estimates | Where-Object Scenario -eq 'Periodic' | Group-Object Network,Reference)) {
        $ordered = @($group.Group | Sort-Object Frequency)
        for ($i = 0; $i -lt $ordered.Count - 1; ++$i) {
            $a = $ordered[$i]
            $b = $ordered[$i + 1]
            if ($a.Verdict -in 'Reduced','Increased' -and $b.Verdict -in 'Reduced','Increased' -and $a.Verdict -ne $b.Verdict) {
                $a.IntervalToNextFrequency = "$($a.Frequency)-$($b.Frequency) edges/min"
            }
        }
    }
    $inputs | Export-Csv -NoTypeInformation -Encoding utf8 -LiteralPath (Join-Path $batch 'cost_inputs.csv')
    $pairs | Export-Csv -NoTypeInformation -Encoding utf8 -LiteralPath (Join-Path $batch 'cost_pairs.csv')
    $estimates | Export-Csv -NoTypeInformation -Encoding utf8 -LiteralPath (Join-Path $batch 'break_even.csv')
    $crossings = @($estimates | Where-Object IntervalToNextFrequency | ForEach-Object { "- $($_.Network) vs $($_.Reference), $($_.IntervalToNextFrequency)" })
    $summary = @('# Sprint cost comparison', '', "Completed trials $($rows.Count). Comparable pairs $($pairs.Count).",
        'See attempts.csv for failures, cost_inputs.csv for timing and capture coverage, and cost_pairs.csv for matched differences.',
        '', '## Observed break-even intervals', '')
    if ($crossings.Count) { $summary += $crossings } else { $summary += 'No sign change established between neighboring qualified frequencies.' }
    $summary | Set-Content -Encoding utf8 -LiteralPath (Join-Path $batch 'results.md')
}

if ($PSCmdlet.ParameterSetName -eq 'SplitProcess') {
    Export-SplitProcessComparison (Resolve-Path -LiteralPath $CostBatch).Path
    return
}

$task = & (Join-Path $PSScriptRoot 'NewSavedTask.ps1') -TaskName 'SprintComparison' -Description 'Compare p95 movement errors across matched sprint trial blocks.' -Command $MyInvocation.Line -Artifacts @(
    'inputs.csv: run folders, conditions and outcomes, including excluded runs',
    'pairs.csv: per-block position error and paired differences in cm',
    'coverage.csv: successful, failed and missing trial counts per condition',
    'comparison.csv: condition-level median p95 and median paired differences with bootstrap intervals',
    'profile_inputs.csv and profile_coverage.csv: phase inputs and availability for each profiled block',
    'profile_pairs.csv and profile_comparison.csv: paired costs and their median and range across blocks'
)

function Export-ProfileComparison($inputs) {
    $addedMetrics = @(
        'UplinkBitsPerSec', 'ResponseBitsPerSec', 'BidirectionalBitsPerSec', 'AckBits', 'CorrectionResponseBits',
        'UnknownResponseBits', 'ServerUnattributedResendBits', 'NewMoveBits', 'OldMoveBits', 'OldMoveCalls',
        'PendingMoveBits', 'PendingMoveCalls', 'UnmatchedMoveBits', 'MoveFramingBits', 'UnmatchedMoveWrites',
        'UnmatchedResponseWrites'
    )
    $profileInputs = @(foreach ($trial in $inputs) {
        $path = Join-Path $trial.RunFolder 'profile.csv'
        if (-not (Test-Path -LiteralPath $path)) { continue }
        foreach ($row in (Import-Csv -LiteralPath $path)) {
            if ($row.Phase -notin @('InitialWalk', 'TransitionWindow', 'Total')) { continue }
            if ($row.Phase -ne 'InitialWalk' -and $row.Variant -eq 'CmcWalk') { continue }
            foreach ($column in ($addedMetrics + @('Scenario', 'TrafficSchema', 'DownlinkStatus'))) {
                if (!$row.PSObject.Properties[$column]) {
                    $value = if ($column -eq 'Scenario') { $trial.Scenario } else { '' }
                    $row | Add-Member -NotePropertyName $column -NotePropertyValue $value
                }
            }
            $row | Add-Member -NotePropertyMembers @{
                RunFolder = $trial.RunFolder
                ErrorP95Cm = $trial.ErrorP95Cm
                Transition100P95Cm = $trial.Transition100P95Cm
                Transition100MatchedMoves = $trial.Transition100MatchedMoves
                OwnerReconciliationP95Cm = $trial.OwnerReconciliationP95Cm
                OwnerCorrectionsPerMinute = $trial.OwnerCorrectionsPerMinute
                OwnerSprintRequests = $trial.OwnerSprintRequests
                ServerSprintSegments = $trial.ServerSprintSegments
                ServerAppliedSprintDistanceCm = $trial.ServerAppliedSprintDistanceCm
                TapMissingInAppliedWindow = $trial.TapMissingInAppliedWindow
                ObserverMeshOffsetP95Cm = $trial.ObserverMeshOffsetP95Cm
            } -PassThru
        }
    })
    if (-not $profileInputs.Count) { return }
    $profileInputs | Export-Csv -LiteralPath (Join-Path $task 'profile_inputs.csv') -Encoding UTF8 -NoTypeInformation
    $metrics = @(
        'MoveBitsPerSec', 'MoveBitsPerCall', 'StateBitsPerSec', 'StateBitsPerCall', 'UnattributedResendBits',
        'PredictionMsPerSec', 'PredictionP95Ms', 'PredictionPhysicsMsPerSec', 'PredictionPhysicsP95Ms',
        'ServerMoveMsPerSec', 'ServerMoveP95Ms', 'StateRpcMsPerSec', 'ReplayMsPerSec', 'ReplayP95Ms',
        'ReplayMsPerMove', 'Corrections', 'ReplayMoves', 'FrameP95Ms', 'FrameMaxMs'
    ) + $addedMetrics
    $cellQuality = @(
        'ErrorP95Cm', 'Transition100P95Cm', 'OwnerReconciliationP95Cm', 'OwnerCorrectionsPerMinute', 'OwnerSprintRequests',
        'ServerSprintSegments', 'ServerAppliedSprintDistanceCm', 'TapMissingInAppliedWindow', 'ObserverMeshOffsetP95Cm'
    )
    $coverage = [Collections.Generic.List[object]]::new()
    $pairs = @(foreach ($group in ($profileInputs | Group-Object Scenario, Phase, Condition, Block)) {
        $first = $group.Group[0]
        $walking = $first.Phase -eq 'InitialWalk'
        $required = @('SeparateRpc', 'MoveData')
        if ($walking) { $required += 'CmcWalk' }
        if ($group.Group.Variant -contains 'MoveDataBit') { $required += 'MoveDataBit' }
        $byVariant = @{}
        foreach ($variant in $required) {
            $rows = @($group.Group | Where-Object Variant -eq $variant)
            $status = if ($rows.Count -gt 1) { 'Duplicate' } elseif (!$rows.Count) { 'Missing' } else { $rows[0].Status }
            if ($status -eq 'Complete') { $byVariant[$variant] = $rows[0] }
            $coverage.Add([pscustomobject]@{
                Network = $first.Network
                Phase = $first.Phase
                Condition = $first.Condition
                Block = $first.Block
                Variant = $variant
                Attempts = $rows.Count
                Status = $status
                DownlinkStatus = if ($rows.Count -eq 1) { $rows[0].DownlinkStatus }
            })
        }
        $phaseMetrics = $metrics
        if ($first.Phase -eq 'TransitionWindow') { $phaseMetrics += 'Transition100P95Cm' }
        if ($first.Phase -eq 'Total') { $phaseMetrics += $cellQuality }
        $references = if ($walking) { @('CmcWalk', 'SeparateRpc', 'MoveData') } else { @('SeparateRpc', 'MoveData') }
        foreach ($reference in $references) {
            if (!$byVariant.ContainsKey($reference)) { continue }
            $baseline = $byVariant[$reference]
            $candidates = switch ($reference) {
                'CmcWalk' { 'SeparateRpc'; 'MoveData'; 'MoveDataBit' }
                'SeparateRpc' { 'MoveData'; 'MoveDataBit' }
                'MoveData' { 'MoveDataBit' }
            }
            foreach ($variant in $candidates) {
                if (!$byVariant.ContainsKey($variant)) { continue }
                $trial = $byVariant[$variant]
                foreach ($metric in $phaseMetrics) {
                    if ([string]::IsNullOrEmpty($baseline.$metric) -or [string]::IsNullOrEmpty($trial.$metric)) { continue }
                    $baseValue = [double]$baseline.$metric
                    $value = [double]$trial.$metric
                    $lower = $upper = $null
                    if ($metric -eq 'BidirectionalBitsPerSec') {
                        $baseResend = [double]$baseline.UnattributedResendBits + [double]$baseline.ServerUnattributedResendBits
                        $trialResend = [double]$trial.UnattributedResendBits + [double]$trial.ServerUnattributedResendBits
                        $lower = $value - $baseValue - $baseResend / [double]$baseline.Seconds
                        $upper = $value - $baseValue + $trialResend / [double]$trial.Seconds
                    }
                    [pscustomobject]@{
                        Network = $first.Network
                        Scenario = $trial.Scenario
                        Phase = $first.Phase
                        Condition = $first.Condition
                        Block = $first.Block
                        Variant = $variant
                        Reference = $reference
                        Metric = $metric
                        Value = $value
                        ReferenceValue = $baseValue
                        Difference = $value - $baseValue
                        DifferenceLowerWithResend = $lower
                        DifferenceUpperWithResend = $upper
                        BandwidthResult = if ($null -ne $lower) {
                            if ($upper -lt 0.) { 'Reduced' } elseif ($lower -gt 0.) { 'Increased' } else { 'Uncertain' }
                        }
                        DifferencePercent = if ($baseValue -ne 0.) { 100. * ($value - $baseValue) / $baseValue }
                        Frames = $trial.Frames
                        ReferenceFrames = $baseline.Frames
                        MoveCalls = $trial.MoveCalls
                        ReferenceMoveCalls = $baseline.MoveCalls
                        Transition100MatchedMoves = $trial.Transition100MatchedMoves
                        ReferenceTransition100MatchedMoves = $baseline.Transition100MatchedMoves
                    }
                }
            }
        }
    })
    $coverage | Export-Csv -LiteralPath (Join-Path $task 'profile_coverage.csv') -Encoding UTF8 -NoTypeInformation
    $pairs | Export-Csv -LiteralPath (Join-Path $task 'profile_pairs.csv') -Encoding UTF8 -NoTypeInformation
    $comparison = foreach ($group in ($pairs | Group-Object Phase, Condition, Variant, Reference, Metric)) {
        $first = $group.Group[0]
        $samples = @($group.Group.Difference)
        $range = $samples | Measure-Object -Minimum -Maximum
        [pscustomobject]@{
            Network = $first.Network
            Scenario = $first.Scenario
            Phase = $first.Phase
            Condition = $first.Condition
            Variant = $first.Variant
            Reference = $first.Reference
            Metric = $first.Metric
            PairedBlocks = $samples.Count
            MedianDifference = Get-Median $samples
            MinDifference = $range.Minimum
            MaxDifference = $range.Maximum
        }
    }
    $comparison | Export-Csv -LiteralPath (Join-Path $task 'profile_comparison.csv') -Encoding UTF8 -NoTypeInformation
}

try {
    $sources = foreach ($folder in $RunFolders) {
        $source = (Resolve-Path -LiteralPath $folder).Path
        if (Test-Path -LiteralPath (Join-Path $source 'batch.csv')) {
            foreach ($cell in (Import-Csv -LiteralPath (Join-Path $source 'batch.csv'))) {
                if (!$cell.TrialFolder) { continue }
                $trial = Join-Path $source $cell.TrialFolder
                if (Test-Path -LiteralPath (Join-Path $trial 'summary.csv')) { $trial }
            }
        } else { $source }
    }
    $inputs = foreach ($source in $sources) {
        $rows = @(Import-Csv -LiteralPath (Join-Path $source 'summary.csv'))
        if ($rows.Count -ne 1) { throw "Expected one trial summary in $source" }
        $rows[0] | Add-Member -NotePropertyName RunFolder -NotePropertyValue $source -PassThru |
            Select-Object RunFolder, RunId, Variant, Scenario, Network, ConditionHash, Block, Instrumentation, Outcome, Failure,
                ErrorP95Cm, Transition100P95Cm, Transition100MatchedMoves, Transition100UnmatchedCount, Transition100UnmatchedMoves, OwnerCorrections,
                OwnerReconciliationP95Cm, OwnerCorrectionsPerMinute, OwnerSprintRequests, ServerSprintSegments,
                ServerAppliedSprintDistanceCm, TapMissingInAppliedWindow, ObserverMeshOffsetP95Cm
    }
    $inputs | Export-Csv -LiteralPath (Join-Path $task 'inputs.csv') -Encoding UTF8 -NoTypeInformation
    Export-ProfileComparison $inputs
    $blocks = @($inputs | Where-Object { $_.Instrumentation -eq 'ServerCheck' -and $_.Variant -in $variants } |
        Group-Object ConditionHash, Block)
    $coverageRows = @()
    $pairs = @(foreach ($block in $blocks) {
        $byVariant = @{}
        foreach ($row in $block.Group) {
            if (!$byVariant.ContainsKey($row.Variant)) { $byVariant[$row.Variant] = @() }
            $byVariant[$row.Variant] += $row
        }
        $first = $block.Group[0]
        $successful = @(foreach ($variant in $byVariant.Keys) {
            $rows = $byVariant[$variant]
            if ($rows.Count -eq 1 -and $rows[0].Outcome -eq 'Completed' -and $rows[0].ErrorP95Cm) { $rows[0] }
        })
        $coverageRows += [pscustomobject]@{
            ConditionHash = $first.ConditionHash
            Scenario = $first.Scenario
            Network = $first.Network
            Completed = $successful.Count
            Failed = $byVariant.Count - $successful.Count
            Missing = $variants.Count - $byVariant.Count
        }
        if ($successful.Count -ne $variants.Count) { continue }

        $rpc = $byVariant.SeparateRpc[0]
        $moveData = $byVariant.MoveData[0]
        [pscustomobject]@{
            ConditionHash = $first.ConditionHash
            Block = $first.Block
            Scenario = $first.Scenario
            Network = $first.Network
            RpcP95Cm = [double]$rpc.ErrorP95Cm
            MoveDataP95Cm = [double]$moveData.ErrorP95Cm
            RpcMinusMoveDataCm = [double]$rpc.ErrorP95Cm - [double]$moveData.ErrorP95Cm
            RpcTransition100P95Cm = $rpc.Transition100P95Cm
            MoveDataTransition100P95Cm = $moveData.Transition100P95Cm
            RpcMinusMoveDataTransition100Cm = if ($rpc.Transition100P95Cm -and $moveData.Transition100P95Cm) {
                [double]$rpc.Transition100P95Cm - [double]$moveData.Transition100P95Cm
            }
            RpcCorrections = $rpc.OwnerCorrections
            MoveDataCorrections = $moveData.OwnerCorrections
        }
    })
    $coverage = foreach ($condition in ($coverageRows | Group-Object ConditionHash)) {
        $first = $condition.Group[0]
        [pscustomobject]@{
            ConditionHash = $first.ConditionHash
            Scenario = $first.Scenario
            Network = $first.Network
            Completed = ($condition.Group | Measure-Object Completed -Sum).Sum
            Failed = ($condition.Group | Measure-Object Failed -Sum).Sum
            Missing = ($condition.Group | Measure-Object Missing -Sum).Sum
        }
    }
    $coverage | Export-Csv -LiteralPath (Join-Path $task 'coverage.csv') -Encoding UTF8 -NoTypeInformation
    if (-not $pairs.Count -and !(Test-Path -LiteralPath (Join-Path $task 'profile_inputs.csv'))) {
        throw 'No complete RPC and MoveData pair. See coverage.csv and inputs.csv.'
    }
    $pairs | Export-Csv -LiteralPath (Join-Path $task 'pairs.csv') -Encoding UTF8 -NoTypeInformation
    $comparisons = foreach ($condition in ($pairs | Group-Object ConditionHash)) {
        $first = $condition.Group[0]
        $result = [ordered]@{
            ConditionHash = $first.ConditionHash
            Scenario = $first.Scenario
            Network = $first.Network
            CompleteBlocks = $condition.Count
            RpcMedianP95Cm = Get-Median @($condition.Group.RpcP95Cm)
            MoveDataMedianP95Cm = Get-Median @($condition.Group.MoveDataP95Cm)
        }
        foreach ($metric in @('RpcMinusMoveDataCm', 'RpcMinusMoveDataTransition100Cm')) {
            $samples = @($condition.Group | ForEach-Object { $_.$metric } | Where-Object { $null -ne $_ })
            $result["PairedBlocks$metric"] = $samples.Count
            $result["Median$metric"] = if ($samples.Count) { Get-Median $samples }
            $lower, $upper = Get-BootstrapInterval $samples
            $result["Lower95$metric"] = $lower
            $result["Upper95$metric"] = $upper
        }
        [pscustomobject]$result
    }
    $comparisons | Export-Csv -LiteralPath (Join-Path $task 'comparison.csv') -Encoding UTF8 -NoTypeInformation
    Add-Content -LiteralPath (Join-Path $task 'README.md') -Value "Completed. $($pairs.Count) complete block(s). See inputs.csv for excluded and failed runs. Retain the original trial folders with this comparison."
    $comparisons | Format-Table Scenario, Network, CompleteBlocks, RpcMedianP95Cm, MoveDataMedianP95Cm
    Write-Host "Results: $task"
}
catch {
    Add-Content -LiteralPath (Join-Path $task 'README.md') -Value "Failed. $($_.Exception.Message)"
    Write-Host "Results: $task"
    throw
}
