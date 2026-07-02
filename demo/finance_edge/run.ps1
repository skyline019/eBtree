param(
    [string]$BuildDir,
    [string]$Config = "Release",
    [string]$RepoRoot
)

$ErrorActionPreference = "Stop"
$demoDir = Join-Path $RepoRoot "demo_data_finance"
if (Test-Path $demoDir) { Remove-Item -Recurse -Force $demoDir }

$demo = Join-Path $BuildDir "$Config/ebtree_demo_finance.exe"
if (-not (Test-Path $demo)) { throw "Build ebtree_demo_finance first: $demo" }

& $demo $demoDir
if ($LASTEXITCODE -ne 0) { throw "finance demo failed" }

Write-Host "[finance] kSync seed + MONITOR vs REQUIRE_PASS open comparison OK"
