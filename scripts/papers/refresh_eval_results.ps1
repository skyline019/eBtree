param(
    [string]$RepoRoot = (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent),
    [string]$BuildDir = "build-msvc-2026",
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
$bench = Join-Path $RepoRoot "$BuildDir/bench/$Config/ebtree_carl_eval.exe"
$out = Join-Path $RepoRoot "Docs/papers/carl/eval-results.md"
$tmpdir = Join-Path $RepoRoot ".test-runs/carl_eval_refresh"

if (-not (Test-Path $bench)) {
    throw "Build first: cmake --build $BuildDir --config $Config --target ebtree_carl_eval"
}

New-Item -ItemType Directory -Force -Path $tmpdir | Out-Null
& $bench $tmpdir | Set-Content -Path $out -Encoding utf8
Write-Host "Wrote $out"
