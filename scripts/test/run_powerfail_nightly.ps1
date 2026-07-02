param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path,
    [string]$BuildDir = "",
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release",
    [int]$Trials = 500
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $RepoRoot "build-msvc-2026"
}

$runner = Join-Path $BuildDir "$Config/ebtree_test_runner.exe"
if (-not (Test-Path $runner)) {
    Write-Error "Runner not found: $runner (build Release first)"
}

Write-Host "== StandardDefaults powerfail nightly trials=$Trials =="
$env:EBTREE_PF_NIGHTLY_TRIALS = "$Trials"
& $runner --suite=failure `
    "--gtest_filter=EbFailureRandomPowerfail.StandardRandomDestroyFuzz:EbFailureRandomPowerfail.StandardFourShardRandomDestroy"
exit $LASTEXITCODE
