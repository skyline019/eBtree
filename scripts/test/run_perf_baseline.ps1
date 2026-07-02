param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path,
    [string]$BuildDir = "",
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $RepoRoot "build-msvc-2026"
}

Write-Host "== perf baseline: raw P9 + standard P14 + compress P10 =="
& (Join-Path $RepoRoot "scripts/test/run_tests.ps1") -RepoRoot $RepoRoot -BuildDir $BuildDir -Config $Config -Gate P9-perf-read
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& (Join-Path $RepoRoot "scripts/test/run_tests.ps1") -RepoRoot $RepoRoot -BuildDir $BuildDir -Config $Config -Gate P9-perf-write
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& (Join-Path $RepoRoot "scripts/test/run_tests.ps1") -RepoRoot $RepoRoot -BuildDir $BuildDir -Config $Config -Gate P14-standard-perf
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& (Join-Path $RepoRoot "scripts/test/run_tests.ps1") -RepoRoot $RepoRoot -BuildDir $BuildDir -Config $Config -Gate P10-compress-v2
exit $LASTEXITCODE
