param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path,
    [string]$RunLabel = "",
    [switch]$ClearLegacySystemTemp
)

$ErrorActionPreference = "Stop"

function Stop-EbtreeTestProcesses {
    Get-Process -Name "ebtree_test_runner" -ErrorAction SilentlyContinue | Stop-Process -Force
}

function Clear-EbtreeLegacyTemp {
    param([string]$Root)

    $repoDefault = Join-Path (Join-Path (Join-Path $Root ".test-runs") "tmp") "default"
    if (Test-Path $repoDefault) {
        Remove-Item -LiteralPath $repoDefault -Recurse -Force -ErrorAction SilentlyContinue
        Write-Host "cleared repo tmp cache: $repoDefault"
    }

    if (-not $ClearLegacySystemTemp) {
        return
    }

    $paths = @(
        (Join-Path $env:TEMP "ebtree_test"),
        (Join-Path $env:LOCALAPPDATA "Temp\ebtree_test"),
        (Join-Path $env:TMP "ebtree_test")
    )
    foreach ($p in $paths) {
        if (Test-Path $p) {
            Remove-Item -LiteralPath $p -Recurse -Force -ErrorAction SilentlyContinue
            Write-Host "cleared legacy system temp cache: $p"
        }
    }
}

function New-EbtreeTestRunDir {
    param([string]$Root, [string]$Label)
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    if ([string]::IsNullOrWhiteSpace($Label)) {
        $Label = "run"
    }
    $runDir = Join-Path (Join-Path $Root ".test-runs") "${stamp}_${Label}"
    New-Item -ItemType Directory -Force -Path (Join-Path $runDir "tmp") | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $runDir "reports") | Out-Null
    return $runDir
}

Stop-EbtreeTestProcesses
Clear-EbtreeLegacyTemp -Root $RepoRoot

$runDir = New-EbtreeTestRunDir -Root $RepoRoot -Label $RunLabel
$tmpRoot = Join-Path $runDir "tmp"
$reportRoot = Join-Path $runDir "reports"

$env:EBTREE_TEST_TMP_ROOT = $tmpRoot
$env:EBTREE_SQLLOGIC_BASELINE_OUT = Join-Path $reportRoot "sqllogic_baseline.json"
$env:TEMP = $tmpRoot
$env:TMP = $tmpRoot

Write-Host "test tmp root: $tmpRoot"
Write-Host "test reports:  $reportRoot"

return @{
    RunDir = $runDir
    TmpRoot = $tmpRoot
    ReportRoot = $reportRoot
}
