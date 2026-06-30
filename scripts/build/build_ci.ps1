# Thin wrapper for GitHub Actions / local CI (Release + build-ci directory).
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts/build/build_ci.ps1

param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path,
    [string]$Target = "ebtree_test_runner"
)

$ErrorActionPreference = "Stop"
$buildDir = Join-Path $RepoRoot "build-ci"

& (Join-Path $PSScriptRoot "build_msvc.ps1") `
    -RepoRoot $RepoRoot `
    -BuildDir $buildDir `
    -Config Release `
    -Target $Target `
    -Reconfigure
