param(
    [string]$BuildDir,
    [string]$Config = "Release",
    [string]$RepoRoot
)

$ErrorActionPreference = "Stop"
$demoDir = Join-Path $RepoRoot "demo_data_industrial"
if (Test-Path $demoDir) { Remove-Item -Recurse -Force $demoDir }

$demo = Join-Path $BuildDir "$Config/ebtree_demo_industrial.exe"
$audit = Join-Path $BuildDir "$Config/ebtree_audit.exe"
if (-not (Test-Path $demo)) { throw "Build ebtree_demo_industrial first: $demo" }
if (-not (Test-Path $audit)) { throw "Build ebtree_audit first: $audit" }

& $demo $demoDir
if ($LASTEXITCODE -ne 0) { throw "industrial demo failed" }

& $audit chain-verify --path $demoDir --require-anchor
if ($LASTEXITCODE -ne 0) { throw "chain-verify --require-anchor failed" }

Write-Host "[industrial] CARL e2e: KV, crash-reopen, anchor, write circuit OK"
