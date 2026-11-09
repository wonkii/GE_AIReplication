param([string]$EngineRoot = 'C:/Program Files/Epic Games/UE_5.8')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$python = Join-Path $EngineRoot 'Engine/Binaries/ThirdParty/Python3/Win64/python.exe'
if (!(Test-Path -LiteralPath $python)) { throw "Unreal Python not found: $python" }
$task = & "$PSScriptRoot/NewSavedTask.ps1" -TaskName 'anim-analysis-environment' `
    -Description 'UE Python 전용 관절 분석 의존성 설치; 엔진 및 전역 Python을 변경하지 않음' `
    -Command 'Scripts/SetupAnimationAnalysis.ps1' `
    -Artifacts @('packages/: 고정 버전 numpy/scipy/ruptures', 'environment.json: 성공한 환경 정보', 'install.log: 설치 기록')
try {
    & $python -m pip install --disable-pip-version-check --only-binary=:all: --no-warn-script-location `
        --target "$task/packages" -r "$PSScriptRoot/AnimationAnalysis/requirements.txt" *> "$task/install.log"
    if ($LASTEXITCODE -ne 0) { throw "Dependency installation failed. See $task/install.log" }
    & $python -I -c "import sys; sys.path.insert(0, sys.argv[1]); import numpy, scipy, ruptures; print(numpy.__version__, scipy.__version__, ruptures.__version__)" "$task/packages" >> "$task/install.log"
    if ($LASTEXITCODE -ne 0) { throw 'Dependency import check failed.' }
    [ordered]@{ status='ready'; python=$python; packages=(Join-Path $task 'packages'); requirements_sha256=(Get-FileHash "$PSScriptRoot/AnimationAnalysis/requirements.txt" -Algorithm SHA256).Hash.ToLower() } |
        ConvertTo-Json | Set-Content -LiteralPath "$task/environment.json" -Encoding UTF8
    Add-Content -LiteralPath "$task/README.md" -Value "`n결과: PASS. AnimAnalysis 명령이 최신 성공 환경을 자동 탐색합니다. 이 환경을 사용하는 동안 보관하세요." -Encoding UTF8
} catch {
    Add-Content -LiteralPath "$task/README.md" -Value "`n결과: FAIL. $($_.Exception.Message)" -Encoding UTF8
    throw
}
Write-Output $task
