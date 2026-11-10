param(
    [Parameter(Mandatory=$true)][ValidatePattern('^[a-zA-Z0-9가-힣_-]+$')][string]$TaskName,
    [Parameter(Mandatory=$true)][string]$Description,
    [string]$Command = '수동 실행 (실제 명령을 기록하세요)',
    [string[]]$Artifacts = @('작업 결과 파일: 파일별 의미와 확인 방법을 추가하세요.'),
    [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
if (-not $OutputDirectory) {
    $taskId = (Get-Date -Format 'yyyyMMdd-HHmmssfff') + '-' + $TaskName + '-' + [guid]::NewGuid().ToString('N').Substring(0,8)
    $OutputDirectory = Join-Path (Split-Path -Parent $PSScriptRoot) "Saved/Tasks/$taskId"
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$readme = Join-Path $OutputDirectory 'README.md'
if (-not (Test-Path -LiteralPath $readme)) {
    $artifactLines = ($Artifacts | ForEach-Object { '- ' + $_ }) -join "`n"
    $descriptionText = @"
# $TaskName

- 목적: $Description
- 생성 시각: $([DateTimeOffset]::Now.ToString('yyyy-MM-dd HH:mm:ss zzz'))
- 생성 도구/명령: $Command

## 결과 파일과 사용법

$artifactLines

## 결과 및 보관

- 생성 시점에는 작업 완료 여부가 확정되지 않았습니다. 작업 종료 후 성공/실패와 확인 결과를 기록하세요.
- 실험 원본은 보고서와 함께 보관하고, 일회성 로그는 검토 후 삭제할 수 있습니다.
- 이 폴더는 Git으로 공유되지 않습니다. 공유 시 README를 포함한 작업 폴더 전체를 전달하세요.
"@
    Set-Content -LiteralPath $readme -Value $descriptionText -Encoding UTF8
}
Write-Output $OutputDirectory
