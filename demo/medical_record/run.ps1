param(
    [string]$BuildDir,
    [string]$Config = "Release",
    [string]$RepoRoot
)

$ErrorActionPreference = "Stop"
$demoDir = Join-Path $RepoRoot "demo_data_medical"
if (Test-Path $demoDir) { Remove-Item -Recurse -Force $demoDir }

$demo = Join-Path $BuildDir "$Config/ebtree_demo_medical.exe"
if (-not (Test-Path $demo)) { throw "Build ebtree_demo_medical first: $demo" }

$env:EBTREE_CARL_ANCHOR_PATH = Join-Path $demoDir "carl_anchors"
& $demo $demoDir
if ($LASTEXITCODE -ne 0) { throw "medical demo failed" }

Write-Host "[medical] SQL INSERT + PRAGMA rar_status + anchor OK"
