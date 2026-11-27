<#
.SYNOPSIS
    Rubberband B 별도 프로세스 측정: 데디케이티드 서버 + 보는 클라(A) + 움직이는 클라(B)를 판마다 새로 띄운다.

.DESCRIPTION
    PIE 단일 프로세스(rb.Batch / rb.Plan) 결과가 별도 프로세스에서도 같은지 확인하는 용도.
    - 네트워크 조건은 A의 받는 쪽에만 커맨드라인 에뮬레이션으로 건다 (-PktIncomingLagMin/Max/Loss).
    - 스무딩 방식은 rb.Batch와 같은 표기(2, 2@0.25, 3t, 3t:g12, 4@0.10, 4a)를 A의 콘솔 변수로 바꿔 넘긴다.
    - B는 rb.AutoScript로 상대가 접속하면 스크립트를 시작하고, 끝나면 rb.AutoQuit로 종료한다.
      서버와 A는 B가 나가면 기록을 마감하고 스스로 종료한다. 시간 안에 안 끝나면 강제 종료하고 경고를 남긴다.
    - 결과: Saved/Experiments/Rubberband/<BatchId>_r<k>S_<방식>/<조건>/  (rb.Plan과 같은 분석 스크립트로 분석)
      로그: Saved/Experiments/Rubberband/_logs/<BatchId>/

.EXAMPLE
    .\Scripts\RunRubberbandProcess.ps1 -DryRun
.EXAMPLE
    .\Scripts\RunRubberbandProcess.ps1 -Conditions N0,J100-300 -Modes 2,3t,3t:g12,4a -Repeats 2
.EXAMPLE
    python Scripts/Analysis/rubberband_metrics.py Saved/Experiments/Rubberband --batch S1127-1800
#>
[CmdletBinding()]
param(
    [string[]]$Conditions = @('N0', 'J50-150', 'J100-300'),
    [string[]]$Modes = @('2', '3t', '3t:g12', '4a'),
    [int]$Repeats = 2,
    [int]$Loops = 3,
    [string]$BatchId = ('S' + (Get-Date -Format 'MMdd-HHmm')),

    [string]$Editor = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe',
    [string]$Project = (Join-Path $PSScriptRoot '..\GE_AIReplication.uproject'),
    [string]$Map = '/Game/YHJ/Rubberband/Lv_Rubberband',
    [int]$Port = 7777,

    [int]$StartupTimeoutSec = 180,
    [int]$ExitTimeoutSec = 30,
    [int]$ClientMaxFps = 0,          # 0 = 제한 없음 (PIE 측정과 같게)
    [switch]$Hidden,                 # 창 숨김 (렌더링이 멈추지 않는지 먼저 확인할 것)
    [switch]$DryRun
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
$Inv = [Globalization.CultureInfo]::InvariantCulture

function Get-Emulation([string]$Name) {
    # N0 | L<ms> | J<min>-<max>, 끝에 -P<손실%> (rb.Batch와 같은 규칙)
    if ($Name -notmatch '^(N0|L(\d+)|J(\d+)-(\d+))(?:-P(\d+))?$') { throw "알 수 없는 조건: $Name" }
    $min = 0; $max = 0
    if ($Matches[2]) { $min = [int]$Matches[2]; $max = $min }
    elseif ($Matches[3]) { $min = [int]$Matches[3]; $max = [int]$Matches[4] }
    $loss = if ($Matches[5]) { [int]$Matches[5] } else { 0 }
    return @{ Min = $min; Max = $max; Loss = $loss }
}

function Get-ModeCommands([string]$Token) {
    # rb.Batch ParseMode와 같은 표기 -> 보는 클라(A)의 콘솔 변수. 항상 전부 명시한다 (-1 = 유지는 재현이 안 됨).
    $parts = $Token.Split(':')
    $head = $parts[0]; $time = $null
    if ($head.Contains('@')) { $time = $head.Split('@')[1]; $head = $head.Split('@')[0] }
    if ($head -notmatch '^([0-4])([a-z]?)$') { throw "알 수 없는 방식: $Token" }
    $mode = [int]$Matches[1]; $suffix = $Matches[2]
    $cmds = @("rb.Smooth.Mode $mode", 'rb.Ada.Gain 6', 'rb.Ada.MaxTime 0.25', 'rb.Buf.Adaptive 0', 'rb.Buf.Delay 0.10')
    $smooth = if ($time -and $mode -le 2) { $time } else { '0.06' }
    $cmds += "rb.Smooth.LocationTime $smooth"
    if ($mode -eq 3) {
        $cmds += "rb.Ada.UseTime $(if ($suffix -eq 'r') { 0 } else { 1 })"
        $cmds += "rb.Ada.UseRate $(if ($suffix -eq 't') { 0 } else { 1 })"
        if ($parts.Count -gt 1) {
            foreach ($p in $parts[1..($parts.Count - 1)]) {
                if ($p -match '^m([\d.]+)$') { $cmds += "rb.Ada.MaxTime $($Matches[1])" }
                elseif ($p -match '^g([\d.]+)$') { $cmds += "rb.Ada.Gain $($Matches[1])" }
                else { throw "알 수 없는 조정값: $p ($Token)" }
            }
        }
    }
    if ($mode -eq 4) {
        if ($suffix -eq 'a') { $cmds += 'rb.Buf.Adaptive 1' }
        if ($time) { $cmds += "rb.Buf.Delay $time" }
    }
    return $cmds
}

function Get-Label([string]$Token) { return ($Token -replace '[@:]', '-') -replace '[^\w\.\-]', '' }

function Start-Unreal([string[]]$ArgList, [string]$Label) {
    $line = ($ArgList | ForEach-Object { if ($_ -match '\s') { if ($_ -match '^(-[^=]+=)(.*)$') { $Matches[1] + '"' + $Matches[2] + '"' } else { '"' + $_ + '"' } } else { $_ } }) -join ' '
    Write-Host "    [$Label] $line"
    if ($DryRun) { return $null }
    $style = if ($Hidden) { 'Hidden' } else { 'Normal' }
    return Start-Process -FilePath $Editor -ArgumentList $line -WindowStyle $style -PassThru
}

function Wait-Log([string]$Path, [string]$Pattern, [int]$TimeoutSec, $Proc) {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        if ($Proc -and $Proc.HasExited) { return $false }
        if ((Test-Path $Path) -and (Select-String -Path $Path -Pattern $Pattern -Quiet)) { return $true }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

function Wait-Exit($Proc, [int]$TimeoutSec, [string]$Label) {
    if ($null -eq $Proc) { return 'none' }
    if ($Proc.WaitForExit($TimeoutSec * 1000)) { return 'exited' }
    [void]$Proc.CloseMainWindow()
    if ($Proc.WaitForExit(10000)) { return 'closed' }
    Write-Warning "[$Label] 종료되지 않아 강제 종료 (마지막 기록이 잘렸을 수 있음)"
    Stop-Process -Id $Proc.Id -Force -ErrorAction SilentlyContinue
    return 'killed'
}

# ------------------------------------------------------------------ 준비
$Project = [IO.Path]::GetFullPath($Project)
if (-not $DryRun) {
    if (-not (Test-Path $Editor)) { throw "UnrealEditor.exe를 찾을 수 없음: $Editor (-Editor로 지정)" }
    if (-not (Test-Path $Project)) { throw ".uproject를 찾을 수 없음: $Project" }
    if (Get-Process -Name UnrealEditor -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowTitle -match 'Unreal Editor' }) {
        Write-Warning '에디터가 열려 있음. 같은 포트·같은 Saved 폴더를 쓰므로 PIE는 끄고 진행할 것.'
    }
}
$ExpRoot = Join-Path (Split-Path $Project -Parent) 'Saved\Experiments\Rubberband'
$LogDir = Join-Path $ExpRoot "_logs\$BatchId"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
foreach ($m in $Modes) { [void](Get-ModeCommands $m) }
foreach ($c in $Conditions) { [void](Get-Emulation $c) }

$total = $Repeats * $Conditions.Count * $Modes.Count
$runTimeout = $Loops * 25 + 60
Write-Host "Rubberband 별도 프로세스 측정 $BatchId : $total 판 (반복 $Repeats x 조건 $($Conditions.Count) x 방식 $($Modes.Count)), 판당 스크립트 $Loops 바퀴"
$summary = @()
$n = 0
for ($r = 1; $r -le $Repeats; $r++) {
    foreach ($cond in $Conditions) {
        foreach ($mode in $Modes) {
            $n++
            $runId = "${BatchId}_r${r}S_$(Get-Label $mode)"
            $tag = "r${r}_${cond}_$(Get-Label $mode)"
            Write-Host "[$n/$total] $runId / $cond"
            $emu = Get-Emulation $cond
            $common = "rb.RunId $runId, rb.Condition $cond, rb.Record 1, rb.AutoQuit 1"
            $fps = if ($ClientMaxFps -gt 0) { ", t.MaxFPS $ClientMaxFps" } else { '' }
            $serverLog = Join-Path $LogDir "$tag-server.log"
            $aLog = Join-Path $LogDir "$tag-A.log"
            $bLog = Join-Path $LogDir "$tag-B.log"

            $server = Start-Unreal @($Project, $Map, '-server', '-log', "-port=$Port", '-unattended', '-nosplash',
                "-abslog=$serverLog", "-ExecCmds=$common") 'server'
            if (-not $DryRun -and -not (Wait-Log $serverLog 'Rubberband recording started' $StartupTimeoutSec $server)) {
                Write-Warning '서버가 시작되지 않음. 이 판을 건너뜀'
                [void](Wait-Exit $server 1 'server'); $summary += [pscustomobject]@{ Run = $n; RunId = $runId; Condition = $cond; Result = 'ServerFailed' }; continue
            }
            $aCmds = (@($common) + (Get-ModeCommands $mode)) -join ', '
            $a = Start-Unreal @($Project, "127.0.0.1:$Port", '-game', '-log', '-windowed', '-ResX=640', '-ResY=360', '-WinX=0', '-WinY=40',
                '-nosound', '-nosplash', '-SharpClientId=A', "-PktIncomingLagMin=$($emu.Min)", "-PktIncomingLagMax=$($emu.Max)",
                "-PktIncomingLoss=$($emu.Loss)", "-abslog=$aLog", "-ExecCmds=$aCmds$fps") 'A'
            if (-not $DryRun -and -not (Wait-Log $aLog 'Rubberband recording started' $StartupTimeoutSec $a)) {
                Write-Warning 'A가 접속·기록을 시작하지 못함'
            }
            $b = Start-Unreal @($Project, "127.0.0.1:$Port", '-game', '-log', '-windowed', '-ResX=640', '-ResY=360', '-WinX=660', '-WinY=40',
                '-nosound', '-nosplash', '-SharpClientId=B', "-abslog=$bLog", "-ExecCmds=$common, rb.AutoScript $Loops$fps") 'B'
            if ($DryRun) { continue }
            $bState = Wait-Exit $b ($StartupTimeoutSec + $runTimeout) 'B'
            $aState = Wait-Exit $a $ExitTimeoutSec 'A'
            $sState = Wait-Exit $server $ExitTimeoutSec 'server'
            $started = (Test-Path $bLog) -and (Select-String -Path $bLog -Pattern 'rb.AutoScript: started' -Quiet)
            $dir = Join-Path $ExpRoot "$runId\$cond"
            $metaA = Join-Path $dir 'client_A_meta.json'
            $applied = ''
            if (Test-Path $metaA) {
                $meta = Get-Content $metaA -Raw | ConvertFrom-Json
                $applied = "$($meta.PacketSimulation.PktIncomingLagMin)-$($meta.PacketSimulation.PktIncomingLagMax)/$($meta.PacketSimulation.PktIncomingLoss)%"
            }
            $ok = $started -and ($bState -eq 'exited') -and ($aState -ne 'killed') -and ($sState -ne 'killed') -and (Test-Path $metaA)
            $summary += [pscustomobject]@{ Run = $n; RunId = $runId; Condition = $cond; ScriptStarted = $started; B = $bState; A = $aState
                Server = $sState; AppliedLagLoss = $applied; Result = $(if ($ok) { 'OK' } else { 'CHECK' }) }
            Write-Host "    -> $(if ($ok) { 'OK' } else { 'CHECK' }) (B $bState, A $aState, server $sState, A 에뮬레이션 $applied)"
            Start-Sleep -Seconds 2
        }
    }
}
if (-not $DryRun) {
    $summary | Export-Csv -Path (Join-Path $LogDir 'summary.csv') -NoTypeInformation -Encoding UTF8
    $summary | Format-Table -AutoSize | Out-String | Write-Host
}
Write-Host "분석: python Scripts/Analysis/rubberband_metrics.py Saved/Experiments/Rubberband --batch $BatchId"
