param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path,
    [string]$BuildDir = "",
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release",
    [switch]$SkipDownload,
    [ValidateSet("cn", "jsdelivr", "ghproxy", "direct")]
    [string]$Mirror = "cn"
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $RepoRoot "build-msvc-2026"
}

$runInfo = . (Join-Path $PSScriptRoot "test_run_env.ps1") -RepoRoot $RepoRoot -RunLabel "official-baseline"

$python = "d:\anaconda3\python.exe"
if (-not (Test-Path $python)) {
    $python = "python"
}

$importArgs = @("$RepoRoot/scripts/test/import_sqlite_sqllogic.py", "--mirror", $Mirror)
if ($SkipDownload) {
    $importArgs += "--skip-download"
}
Push-Location (Join-Path $RepoRoot "scripts/test")
& $python @importArgs
if ($LASTEXITCODE -ne 0) { Pop-Location; exit $LASTEXITCODE }
Pop-Location

$runner = Join-Path $BuildDir "$Config/ebtree_test_runner.exe"
if (-not (Test-Path $runner)) {
    Write-Error "Runner not found: $runner (build ebtree_test_runner first)"
}

Write-Host "== Real SQLite sqllogic baseline (mirror=$Mirror) =="
Write-Host "run dir: $($runInfo.RunDir)"
Write-Host "baseline out: $env:EBTREE_SQLLOGIC_BASELINE_OUT"

Push-Location $RepoRoot
try {
    & $runner --suite=sql --gtest_filter="SqllogicRunner.RealSqliteBaselineReport"
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
