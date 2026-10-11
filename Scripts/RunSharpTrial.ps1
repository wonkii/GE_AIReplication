<#
.SYNOPSIS
    Sharp Movement 실험 실행: 데디케이티드 서버 1개 + 클라이언트 N개를 같은 조건으로 띄우고 결과 폴더를 정리한다.

.DESCRIPTION
    - M0 실행 계약(-SharpRunId, -SharpTrialId, -SharpMode)을 그대로 사용한다.
    - 네트워크 조건은 Unreal Network Emulation 커맨드라인 인자로 클라이언트에만 적용한다 (서버 기록은 영향 없음).
    - 실행마다 결과 폴더에 run_meta.json(실행 조건)과 서버·클라이언트 로그를 남기고,
      로그의 RunId/TrialId/Mode가 서버·클라이언트에서 일치하는지 검사한다.
    - Recorder에는 -SharpOutDir, -SharpClientId를 넘긴다. Recorder는 CSV를 이 폴더에 써야 한다 (Scripts/Analysis/README.md).

    결과 폴더: <프로젝트>/Saved/Experiments/<BatchId>/<RunId>/T<TrialId>/
    RunId    : <BatchId>-<Mode>  (같은 모드의 반복 실행은 같은 RunId, TrialId만 다름)

.EXAMPLE
    # 실행하지 않고 명령만 확인
    .\Scripts\RunSharpTrial.ps1 -Mode Default -DryRun

.EXAMPLE
    # 세 모드 × 5회, 클라이언트 1은 N0, 클라이언트 2는 N2 (기본값)
    .\Scripts\RunSharpTrial.ps1 -Mode Default,Burst,AlwaysHigh -Trials (1..5)

.EXAMPLE
    # 클라이언트 2를 직접 지정한 조건으로 (편도 80ms, 지터 ±15ms, 손실 2%)
    .\Scripts\RunSharpTrial.ps1 -Mode Burst -ClientProfiles N0,Custom -LagMs 80 -JitterMs 15 -LossPct 2
#>
[CmdletBinding()]
param(
    [ValidateSet('Default', 'Adaptive', 'AlwaysHigh', 'SingleShot', 'Burst', 'EventOnly', 'EventPredictor')]
    [string[]]$Mode = @('Default'),

    [int[]]$Trials = @(1),

    [string]$BatchId = (Get-Date -Format 'yyyyMMdd-HHmmss'),

    # 클라이언트 수 = 프로필 개수. 1번 클라이언트부터 차례로 적용
    [ValidateSet('N0', 'N1', 'N2', 'N3', 'N4', 'Custom')]
    [string[]]$ClientProfiles = @('N0', 'N2'),

    # Custom 프로필 값 (편도 지연, ±지터, 손실률)
    [int]$LagMs = 0,
    [int]$JitterMs = 0,
    [double]$LossPct = 0,

    [string]$Editor = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe',
    [string]$Project = (Join-Path $PSScriptRoot '..\GE_AIReplication.uproject'),
    [string]$Map = '/Game/SharpMovementExperiment/Maps/SharpMovementExperiment',
    [int]$Port = 7777,

    [int]$ServerReadyTimeoutSec = 90,
    [int]$ConnectWaitSec = 10,
    [int]$WarmupSec = 5,
    [int]$MeasureSec = 60,
    [int]$ShutdownTimeoutSec = 15,
    [int]$ClientMaxFps = 60,

    # 서버에 Networking Insights 트레이스 저장 (-trace=net -NetTrace=1)
    [switch]$NetTrace,

    [string[]]$ExtraServerArgs = @(),
    [string[]]$ExtraClientArgs = @(),

    # 프로세스를 띄우지 않고 명령과 run_meta.json만 만든다
    [switch]$DryRun
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

# 네트워크 프로필 (테스트_계획_v2 / 네트워크_에뮬레이션_정리 기준). L=편도 지연, J=±지터, P=손실률
$Profiles = @{
    'N0' = @{ LagMs = 0;   JitterMs = 0;  LossPct = 0 }
    'N1' = @{ LagMs = 25;  JitterMs = 5;  LossPct = 1 }
    'N2' = @{ LagMs = 50;  JitterMs = 10; LossPct = 1 }
    'N3' = @{ LagMs = 100; JitterMs = 20; LossPct = 5 }
    'N4' = @{ LagMs = 250; JitterMs = 0;  LossPct = 10 }
}

function Get-Profile([string]$Name) {
    if ($Name -eq 'Custom') {
        return @{ LagMs = $LagMs; JitterMs = $JitterMs; LossPct = $LossPct }
    }
    return $Profiles[$Name]
}

function Get-EmulationArgs($p) {
    if ($p.LagMs -eq 0 -and $p.JitterMs -eq 0 -and $p.LossPct -eq 0) { return @() }
    $min = [Math]::Max(0, $p.LagMs - $p.JitterMs)
    $max = $p.LagMs + $p.JitterMs
    $loss = [string]::Format([Globalization.CultureInfo]::InvariantCulture, '{0}', $p.LossPct)
    # 서버→클라(Incoming)와 클라→서버(Outgoing) 양쪽에 같은 값. RTT ≈ 2 × LagMs
    return @(
        "-PktIncomingLagMin=$min", "-PktIncomingLagMax=$max", "-PktIncomingLoss=$loss",
        "-PktLagMin=$min", "-PktLagMax=$max", "-PktLoss=$loss"
    )
}

function Format-Arg([string]$Arg) {
    # 공백이 있는 값만 따옴표로 감싼다 (-Key=값 형태면 값 부분만)
    if ($Arg -notmatch '\s') { return $Arg }
    if ($Arg -match '^(-[^=]+=)(.*)$') { return $Matches[1] + '"' + $Matches[2] + '"' }
    return '"' + $Arg + '"'
}

function Join-Args([string[]]$Items) {
    return (($Items | ForEach-Object { Format-Arg $_ }) -join ' ')
}

function Start-Unreal([string]$ArgLine, [string]$Label) {
    Write-Host "  [$Label] `"$Editor`" $ArgLine"
    if ($DryRun) { return $null }
    return Start-Process -FilePath $Editor -ArgumentList $ArgLine -PassThru
}

function Stop-Unreal($Proc, [string]$Label) {
    # 창 닫기(정상 종료)를 먼저 시도해 Recorder가 파일을 마무리할 기회를 준다. 안 되면 강제 종료
    if ($null -eq $Proc -or $Proc.HasExited) { return $false }
    [void]$Proc.CloseMainWindow()
    if ($Proc.WaitForExit($ShutdownTimeoutSec * 1000)) { return $false }
    Write-Warning "[$Label] 정상 종료되지 않아 강제 종료함 (마지막 기록이 잘렸을 수 있음)"
    Stop-Process -Id $Proc.Id -Force -ErrorAction SilentlyContinue
    return $true
}

function Wait-LogLine([string]$Path, [string]$Pattern, [int]$TimeoutSec, $Proc) {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        if ($Proc -and $Proc.HasExited) { return $false }
        if ((Test-Path $Path) -and (Select-String -Path $Path -Pattern $Pattern -Quiet)) { return $true }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

function Read-Identity([string]$LogPath, [string]$Role) {
    if (-not (Test-Path $LogPath)) { return $null }
    $pattern = "LogSharpExperiment.*Role=$Role RunId=(\S+) TrialId=(\d+) Mode=(\S+)"
    $hit = Select-String -Path $LogPath -Pattern $pattern | Select-Object -Last 1
    if (-not $hit) { return $null }
    $g = $hit.Matches[0].Groups
    return @{ RunId = $g[1].Value; TrialId = [int]$g[2].Value; Mode = $g[3].Value }
}

function Save-Meta($Meta, [string]$Dir) {
    $Meta | ConvertTo-Json -Depth 6 | Set-Content -Path (Join-Path $Dir 'run_meta.json') -Encoding UTF8
}

# ------------------------------------------------------------------ 준비

$Project = [IO.Path]::GetFullPath($Project)
if (-not $DryRun) {
    if (-not (Test-Path $Editor)) { throw "UnrealEditor.exe를 찾을 수 없음: $Editor  (-Editor로 경로 지정)" }
    if (-not (Test-Path $Project)) { throw ".uproject를 찾을 수 없음: $Project  (-Project로 경로 지정)" }
}
$BatchDir = Join-Path (Join-Path (Split-Path $Project -Parent) 'Saved\Experiments') $BatchId
$results = @()

Write-Host "BatchId=$BatchId  모드=$($Mode -join ',')  Trial=$($Trials -join ',')  클라이언트=$($ClientProfiles -join ',')"
Write-Host "결과 폴더: $BatchDir"
if ($DryRun) { Write-Host '(DryRun: 프로세스를 실행하지 않음)' }

# ------------------------------------------------------------------ 실행

foreach ($m in $Mode) {
    $runId = "$BatchId-$m"
    foreach ($trial in $Trials) {
        $outDir = Join-Path (Join-Path $BatchDir $runId) "T$trial"
        New-Item -ItemType Directory -Force -Path $outDir | Out-Null
        Write-Host ""
        Write-Host "=== $runId  Trial $trial ==="

        $serverLog = Join-Path $outDir 'server.log'
        $serverArgs = @(
            $Project, $Map, '-server', '-game', '-log', '-unattended', "-port=$Port",
            "-SharpRunId=$runId", "-SharpTrialId=$trial", "-SharpMode=$m",
            "-SharpOutDir=$outDir", "-abslog=$serverLog"
        )
        if ($NetTrace) {
            $serverArgs += @('-trace=net', '-NetTrace=1', "-tracefile=$(Join-Path $outDir 'server.utrace')")
        }
        $serverArgs += $ExtraServerArgs

        $clients = [ordered]@{}
        $clientLines = @()
        for ($i = 0; $i -lt $ClientProfiles.Count; $i++) {
            $id = $i + 1
            $prof = Get-Profile $ClientProfiles[$i]
            $emu = @(Get-EmulationArgs $prof)
            $clientArgs = @(
                $Project, "127.0.0.1:$Port", '-game', '-log', '-windowed', '-ResX=960', '-ResY=540',
                "-WinX=$(20 + $i * 980)", '-WinY=40',
                "-SharpClientId=$id", "-SharpOutDir=$outDir",
                "-abslog=$(Join-Path $outDir "client_$id.log")", "-ExecCmds=t.MaxFPS $ClientMaxFps"
            ) + $emu + $ExtraClientArgs
            $clientLines += , (Join-Args $clientArgs)
            $clients["$id"] = [ordered]@{
                Profile  = $ClientProfiles[$i]
                LagMs    = $prof.LagMs
                JitterMs = $prof.JitterMs
                LossPct  = $prof.LossPct
                EmulationArgs = $emu -join ' '
            }
        }

        $meta = [ordered]@{
            RunId = $runId; TrialId = $trial; Mode = $m; BatchId = $BatchId
            Map = $Map; Project = $Project; Editor = $Editor; Port = $Port
            ConnectWaitSec = $ConnectWaitSec; WarmupSec = $WarmupSec; MeasureSec = $MeasureSec
            ClientMaxFps = $ClientMaxFps; NetTrace = [bool]$NetTrace
            StartedAt = (Get-Date).ToString('o'); DryRun = [bool]$DryRun
            ServerArgs = Join-Args $serverArgs
            Clients = $clients
            Valid = $false; ForcedKill = $false; Issues = @()
        }
        Save-Meta $meta $outDir

        $issues = @()
        $server = Start-Unreal (Join-Args $serverArgs) 'Server'
        $clientProcs = @()
        try {
            if (-not $DryRun -and -not (Wait-LogLine $serverLog 'LogSharpExperiment.*Role=Server' $ServerReadyTimeoutSec $server)) {
                $issues += "서버가 $ServerReadyTimeoutSec 초 안에 시작 로그를 남기지 않음"
            }
            if ($issues.Count -eq 0) {
                for ($i = 0; $i -lt $clientLines.Count; $i++) {
                    $clientProcs += , (Start-Unreal $clientLines[$i] "Client$($i + 1)")
                }
                $total = $ConnectWaitSec + $WarmupSec + $MeasureSec
                Write-Host "  접속 대기 $ConnectWaitSec s + 워밍업 $WarmupSec s + 측정 $MeasureSec s = $total s"
                if (-not $DryRun) {
                    $meta.ClientsStartedAt = (Get-Date).ToString('o')
                    Start-Sleep -Seconds $total
                    $meta.EndedAt = (Get-Date).ToString('o')
                }
            }
        }
        finally {
            $forced = $false
            foreach ($p in $clientProcs) { if (Stop-Unreal $p 'Client') { $forced = $true } }
            if (Stop-Unreal $server 'Server') { $forced = $true }
            $meta.ForcedKill = $forced
        }

        # ------------------------------------------------------------ 검증
        if (-not $DryRun) {
            $srv = Read-Identity $serverLog 'Server'
            if (-not $srv) { $issues += 'server.log에 Role=Server 시작 로그 없음' }
            elseif ($srv.RunId -ne $runId -or $srv.TrialId -ne $trial -or $srv.Mode -ne $m) {
                $issues += "서버 식별값 불일치: $($srv.RunId)/$($srv.TrialId)/$($srv.Mode)"
            }
            for ($i = 1; $i -le $ClientProfiles.Count; $i++) {
                $cli = Read-Identity (Join-Path $outDir "client_$i.log") 'Client'
                if (-not $cli) { $issues += "client_$i.log에 Role=Client 로그 없음 (접속 실패 가능)" }
                elseif ($srv -and ($cli.RunId -ne $srv.RunId -or $cli.TrialId -ne $srv.TrialId -or $cli.Mode -ne $srv.Mode)) {
                    $issues += "클라이언트 $i 식별값이 서버와 다름: $($cli.RunId)/$($cli.TrialId)/$($cli.Mode)"
                }
            }
            # Recorder 구현 전에는 CSV가 없을 수 있으므로 경고만 남긴다
            $csvMissing = @()
            if (-not (Test-Path (Join-Path $outDir 'server_samples.csv'))) { $csvMissing += 'server_samples.csv' }
            for ($i = 1; $i -le $ClientProfiles.Count; $i++) {
                if (-not (Test-Path (Join-Path $outDir "client_$($i)_samples.csv"))) { $csvMissing += "client_$($i)_samples.csv" }
            }
            if ($csvMissing.Count -gt 0) { Write-Warning "CSV 없음: $($csvMissing -join ', ')" }
            $meta.MissingCsv = $csvMissing
        }

        $meta.Issues = $issues
        $meta.Valid = (-not $DryRun) -and ($issues.Count -eq 0)
        Save-Meta $meta $outDir
        if ($issues.Count -gt 0) { $issues | ForEach-Object { Write-Warning $_ } }
        $results += [pscustomobject]@{ RunId = $runId; Trial = $trial; Valid = $meta.Valid; ForcedKill = $meta.ForcedKill; Issues = ($issues -join '; ') }

        if (-not $DryRun) { Start-Sleep -Seconds 3 }  # 포트 해제 대기
    }
}

Write-Host ""
$results | Format-Table -AutoSize | Out-String -Width 200 | Write-Host
$analysis = Join-Path $PSScriptRoot 'Analysis\sharp_metrics.py'
Write-Host "분석: python `"$analysis`" `"$BatchDir`""
