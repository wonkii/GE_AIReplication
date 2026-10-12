<#
.SYNOPSIS
    Sharp Movement 실험 실행: 데디케이티드 서버 1개 + 클라이언트 N개를 같은 조건으로 띄우고 결과 폴더를 정리한다.

.DESCRIPTION
    - M0 실행 계약(-SharpRunId, -SharpTrialId, -SharpMode)을 그대로 사용한다.
    - 네트워크 조건은 Unreal Network Emulation 커맨드라인 인자로 클라이언트에만 적용한다 (서버 기록은 영향 없음).
    - 실행마다 결과 폴더에 run_meta.json(실행 조건)과 서버·클라이언트 로그를 남기고,
      로그의 RunId/TrialId/Mode가 서버·클라이언트에서 일치하는지 검사한다.
    - Recorder에는 -SharpOutDir, -SharpClientId를 넘긴다. Recorder는 CSV를 이 폴더에 써야 한다 (Scripts/Analysis/README.md).
    - Enemy(PR #3)가 있으면 서버에서 모드별 전송 정책이 적용됐는지, 클라이언트에 Enemy가 복제됐는지도 검사한다.
    - Adaptive 모드에서만 서버의 net.UseAdaptiveNetUpdateFrequency를 1로, 나머지 모드에서는 0으로 넘긴다.
    - -NetTrace를 쓰면 Tools/summarize_network_cost.py 입력 양식에 실행 조건을 미리 채운 CSV를 결과 폴더에 만든다.

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

function Read-PolicyBinds([string]$LogPath) {
    # SharpEnemyCharacter가 서버에서 정책을 붙인 결과: Actor별로 마지막 줄 사용
    $binds = [ordered]@{}
    if (-not (Test-Path $LogPath)) { return $binds }
    $pattern = '\[M5\]\[PolicyBind\] Actor=(\S+) ActorId=(-?\d+) RunId=(\S+) TrialId=(\d+) Mode=(\S+) InitSuccess=(\d) NetUpdateHz=([\d.]+)'
    foreach ($hit in (Select-String -Path $LogPath -Pattern $pattern)) {
        $g = $hit.Matches[0].Groups
        $binds[$g[1].Value] = [ordered]@{
            Actor = $g[1].Value; ActorId = [int]$g[2].Value; Mode = $g[5].Value
            InitSuccess = ($g[6].Value -eq '1'); NetUpdateHz = [double]$g[7].Value
        }
    }
    return $binds
}

function Get-ClientEnemyCount([string]$LogPath) {
    # 클라이언트에서 Simulated Proxy로 생성된 Enemy 수 (SharpEnemyCharacter BeginPlay 로그)
    if (-not (Test-Path $LogPath)) { return 0 }
    $pattern = '\[M1\]\[Enemy\] Actor=(\S+) NetMode=Client Role=SimulatedProxy'
    $actors = Select-String -Path $LogPath -Pattern $pattern | ForEach-Object { $_.Matches[0].Groups[1].Value }
    return @($actors | Sort-Object -Unique).Count
}

function Add-NetworkCostRows([string]$Path, $Meta, [int]$ActorCount) {
    # Tools/summarize_network_cost.py 입력 양식. 패킷·비트 값은 Networking Insights에서 직접 채운다.
    $utf8 = New-Object System.Text.UTF8Encoding($true)
    if (-not (Test-Path $Path)) {
        $header = 'RunId,TrialId,Mode,ActorCount,Connection,EventSource,LagMs,LossPct,WindowLabel,WindowStartSec,WindowEndSec,PacketCount,TracedPacketBits,EnemyExclusiveBits,SnapshotExclusiveBits,Notes'
        [IO.File]::AppendAllText($Path, $header + "`r`n", $utf8)
    }
    $inv = [Globalization.CultureInfo]::InvariantCulture
    foreach ($id in $Meta.Clients.Keys) {
        $c = $Meta.Clients[$id]
        $loss = [string]::Format($inv, '{0}', $c.LossPct)
        $line = "$($Meta.RunId),$($Meta.TrialId),$($Meta.Mode),$ActorCount,Client$id-$($c.Profile),None,$($c.LagMs),$loss,,,,,,,,T$($Meta.TrialId)\server.utrace"
        [IO.File]::AppendAllText($Path, $line + "`r`n", $utf8)
    }
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
        # Adaptive는 엔진 전역 설정이 켜져 있어야 동작하고, 다른 모드는 꺼져 있어야 결과가 섞이지 않는다
        $adaptiveCVar = if ($m -eq 'Adaptive') { 1 } else { 0 }
        $serverArgs += "-ini:Engine:[ConsoleVariables]:net.UseAdaptiveNetUpdateFrequency=$adaptiveCVar"
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
            ClientMaxFps = $ClientMaxFps; NetTrace = [bool]$NetTrace; AdaptiveCVar = $adaptiveCVar
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
            # Enemy와 전송 정책 (PR #3 SharpEnemyCharacter 로그)
            $binds = Read-PolicyBinds $serverLog
            $meta.Enemies = @($binds.Values)
            $meta.EnemyCount = $binds.Count
            if ($binds.Count -eq 0) {
                $issues += '서버 로그에 Enemy 정책 적용 기록([M5][PolicyBind]) 없음. Enemy가 있는 버전(PR #3 이후)인지 확인'
            }
            foreach ($b in $binds.Values) {
                if (-not $b.InitSuccess) { $issues += "Enemy $($b.Actor): 모드 $($b.Mode) 정책 적용 실패 (server.log의 [M5][Policy] InitRejected 확인)" }
                elseif ($b.Mode -ne $m) { $issues += "Enemy $($b.Actor): 적용된 모드 $($b.Mode)가 요청한 $m 과 다름" }
            }
            $rejected = Select-String -Path $serverLog -Pattern '\[M5\]\[Policy\] InitRejected Reason=(\S+)' -ErrorAction SilentlyContinue |
                ForEach-Object { $_.Matches[0].Groups[1].Value } | Sort-Object -Unique
            if ($rejected) { $issues += "정책 적용 거부 사유: $($rejected -join ', ')" }
            if (Select-String -Path $serverLog -Pattern 'SYNTHETIC_TEST_ENABLED' -Quiet -ErrorAction SilentlyContinue) {
                $issues += '진단용 가짜 사건 Actor(SharpPolicyDiagnosticActor)가 켜져 있음. 실험 데이터로 쓰면 안 됨'
            }
            if (Select-String -Path $serverLog -Pattern 'GlobalAdaptiveContamination' -Quiet -ErrorAction SilentlyContinue) {
                Write-Warning 'Adaptive 전역 설정이 켜진 상태로 다른 모드가 실행됨 (server.log의 GlobalAdaptiveContamination 확인)'
            }
            $clientEnemies = [ordered]@{}
            for ($i = 1; $i -le $ClientProfiles.Count; $i++) {
                $n = Get-ClientEnemyCount (Join-Path $outDir "client_$i.log")
                $clientEnemies["$i"] = $n
                if ($binds.Count -gt 0 -and $n -lt $binds.Count) {
                    $issues += "클라이언트 $i 에 Enemy가 $n / $($binds.Count) 개만 복제됨"
                }
            }
            $meta.ClientEnemyCounts = $clientEnemies

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
        if ($NetTrace -and -not $DryRun) {
            $enemyCount = 0
            if ($meta.Contains('EnemyCount')) { $enemyCount = [int]$meta.EnemyCount }
            Add-NetworkCostRows (Join-Path (Join-Path $BatchDir $runId) 'network_cost_inputs.csv') $meta $enemyCount
        }
        $validText = if ($DryRun) { 'DryRun' } else { [string]$meta.Valid }  # DryRun은 실행하지 않으므로 검사 결과가 없음
        $enemyText = if ($meta.Contains('EnemyCount')) { $meta.EnemyCount } else { '-' }
        $results += [pscustomobject]@{ RunId = $runId; Trial = $trial; Valid = $validText; Enemies = $enemyText; ForcedKill = $meta.ForcedKill; Issues = ($issues -join '; ') }

        if (-not $DryRun) { Start-Sleep -Seconds 3 }  # 포트 해제 대기
    }
}

Write-Host ""
$results | Format-Table -AutoSize | Out-String -Width 200 | Write-Host
$analysis = Join-Path $PSScriptRoot 'Analysis\sharp_metrics.py'
$convert = Join-Path $PSScriptRoot 'Analysis\convert_logs.py'
Write-Host "Recorder가 없을 때 (로그 변환): python `"$convert`" `"$BatchDir`""
Write-Host "분석: python `"$analysis`" `"$BatchDir`""
