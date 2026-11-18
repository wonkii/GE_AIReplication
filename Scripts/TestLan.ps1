param(
    [string]$Executable = 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe',
    [switch]$Packaged,
    [switch]$Rendered,
    [string]$ExpectedMap = '/Game/JWJ/Lv_Jang'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if ($Packaged) {
    # Use the game process directly rather than the bootstrap launcher, so cleanup owns its PID.
    $innerExecutable = Join-Path (Split-Path -Parent $Executable) 'GE_AIReplication/Binaries/Win64/GE_AIReplication.exe'
    if (Test-Path -LiteralPath $innerExecutable) { $Executable = $innerExecutable }
}
$testDirectory = & (Join-Path $PSScriptRoot 'NewSavedTask.ps1') -TaskName 'LAN-Smoke' `
    -Description 'Lv_Jang 두 프로세스의 LAN 접속, 양방향 플레이어 이동 복제 및 메뉴 복귀 검증' `
    -Command "Scripts/TestLan.ps1 -Packaged:$Packaged -Rendered:$Rendered -ExpectedMap $ExpectedMap -Executable $Executable" `
    -Artifacts @('Host.log: [LAN] 상태 전이와 [LAN-SMOKE] PASS 확인', 'Client.log: 검색, 접속 및 종료 검증. FAIL/timeout은 실패')
$hostLog = Join-Path $testDirectory 'Host.log'
$clientLog = Join-Path $testDirectory 'Client.log'
$prefix = if ($Packaged) { '' } else { '"' + (Join-Path $projectRoot 'GE_AIReplication.uproject') + '" -game ' }
$renderArgs = if ($Rendered) { '-windowed -ResX=960 -ResY=540 ' } else { '-nullrhi ' }
$common = $renderArgs + '-nosound -unattended -nosplash -LANExpectedMap=' + $ExpectedMap + ' '
$hostProcess = $null
$clientProcess = $null
function Wait-Log([string]$Path, [string]$Pattern, [int]$Seconds = 90) {
    $deadline = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $deadline) {
        if (Test-Path -LiteralPath $Path) {
            $lines = Get-Content -LiteralPath $Path
            if ($lines -match '\[LAN-SMOKE\] FAIL') { throw "LAN smoke failed. See $Path" }
            $matching = $lines | Select-String -Pattern $Pattern | Select-Object -Last 1
            if ($matching) { return $matching.Line }
        }
        Start-Sleep -Milliseconds 300
    }
    throw "Timed out waiting for $Pattern. See $Path"
}
try {
    $hostProcess = Start-Process -FilePath $Executable -ArgumentList ($prefix + $common + '-port=7777 -ExecCmds="t.MaxFPS 60,LAN.Smoke Host" -abslog="' + $hostLog + '"') -WindowStyle Hidden -PassThru
    $ready = Wait-Log $hostLog '\[LAN-SMOKE\] READY ([0-9.]+:[0-9]+)'
    $serverAddress = [regex]::Match($ready, 'READY ([0-9.]+:[0-9]+)').Groups[1].Value
    $clientProcess = Start-Process -FilePath $Executable -ArgumentList ($prefix + $common + '-port=7778 -ExecCmds="t.MaxFPS 60,LAN.Smoke Client ' + $serverAddress + '" -abslog="' + $clientLog + '"') -WindowStyle Hidden -PassThru
    Wait-Log $clientLog '\[LAN-SMOKE\] PASS disconnect returns to menu' | Write-Output
    Wait-Log $hostLog '\[LAN-SMOKE\] PASS explicit connect role=Host' | Write-Output
    Wait-Log $hostLog '\[LAN-SMOKE\] PASS remote movement role=Host' | Write-Output
    Wait-Log $clientLog '\[LAN-SMOKE\] PASS remote movement role=Client' | Write-Output
    Write-Output "LAN integration passed: $testDirectory"
    Add-Content -LiteralPath (Join-Path $testDirectory 'README.md') -Value "`n실행 결과: 성공. 기본 맵=$ExpectedMap, Rendered=$Rendered. 두 프로세스의 명시적 접속, 양방향 원격 플레이어 이동과 Client 메뉴 복귀를 확인했습니다. 끊김 개선의 정량 검증과 실제 두 PC 검증은 별도입니다." -Encoding UTF8
}
catch {
    Add-Content -LiteralPath (Join-Path $testDirectory 'README.md') -Value "`n실행 결과: 실패. $($_.Exception.Message)" -Encoding UTF8
    throw
}
finally {
    foreach ($testProcess in @($clientProcess, $hostProcess)) {
        if ($null -ne $testProcess -and -not $testProcess.HasExited) { $testProcess.Kill(); $testProcess.WaitForExit() }
    }
}
