param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path,
    [string]$BuildDir = "",
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release",
    [double]$MinPassRate = 0.60,
    [switch]$UseCuratedCorpus,
    [switch]$UseOfficialCorpus
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $RepoRoot "build-msvc-2026"
}

$label = if ($UseOfficialCorpus) { "official-gate" }
         elseif ($UseCuratedCorpus) { "curated-gate" }
         else { "sqllogic-gate" }
. (Join-Path $PSScriptRoot "test_run_env.ps1") -RepoRoot $RepoRoot -RunLabel $label | Out-Null

$runner = Join-Path $BuildDir "$Config/ebtree_test_runner.exe"
if (-not (Test-Path $runner)) {
    Write-Error "Runner not found: $runner"
}

$filter = if ($UseOfficialCorpus) {
    "SqllogicRunner.RealSqliteOfficialPassRate"
} elseif ($UseCuratedCorpus) {
    "SqllogicRunner.CuratedCorpusPassRate"
} elseif ($MinPassRate -ge 0.90) {
    "SqllogicRunner.ProgramCompletePassRate"
} else {
    "SqllogicRunner.CuratedSubsetPassRate"
}

Push-Location $RepoRoot
try {
    & $runner --suite=sql --gtest_filter=$filter
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
