param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path,
    [string]$BuildDir = "build-msvc-2026",
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
$bench = Join-Path $RepoRoot "$BuildDir/bench/$Config/ebtree_carl_eval.exe"
$runner = Join-Path $RepoRoot "$BuildDir/$Config/ebtree_test_runner.exe"
$out = Join-Path $RepoRoot "Docs/archive/perf/kernel-rar-baseline.md"
$tmpdir = Join-Path $RepoRoot ".test-runs/kernel_baseline_refresh"
$date = Get-Date -Format "yyyy-MM-dd"

if (-not (Test-Path $runner)) {
    throw "Build first: cmake --build $BuildDir --config $Config --target ebtree_test_runner"
}

New-Item -ItemType Directory -Force -Path $tmpdir | Out-Null

$lazyNote = "see P17-deep-core gate (LazyScan10kBudget <=40ms local / <=60ms CI)"
$rarNote = "see P17-deep-core gate (KBalancedWrite100kWithRarMonitor >=0.99x)"
$lsvHotNote = "see P19-lsv (SnapshotGetHotKeyBudget P50)"
$lsvAfterNote = "see P19-lsv (SnapshotGetAfterUpdateBudget P50)"
$lsvScanNote = "see P19-lsv (SnapshotScan10kBudget)"
$lsvWriteNote = "see P19-lsv (SnapshotWriteOverheadBudget TPS)"

if (Test-Path $bench) {
    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $evalOut = (& $bench $tmpdir 2>&1 | Out-String)
    $ErrorActionPreference = $prevEap
    if ($evalOut -match "lazy scan 10k rows \| (\d+) ms") {
        $lazyNote = "$($Matches[1]) ms (carl_eval harness)"
    }
    if ($evalOut -match "CARL MONITOR \| (\d+) \| (\d+) \| ratio=([\d.]+)") {
        $rarNote = "ratio=$($Matches[3]) (carl_eval harness)"
    }
}

$lsvFilters = @(
    "LsvPerfRegression.SnapshotGetHotKeyBudget",
    "LsvPerfRegression.SnapshotGetAfterUpdateBudget",
    "LsvPerfRegression.SnapshotScan10kBudget",
    "LsvPerfRegression.SnapshotWriteOverheadBudget"
)
foreach ($f in $lsvFilters) {
    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $testOut = (& $runner --gtest_filter=$f 2>&1 | Out-String)
    $ErrorActionPreference = $prevEap
    if ($testOut -match "\[  PASSED  \]") {
        switch -Wildcard ($f) {
            "*HotKey*" { $lsvHotNote = "PASS ($f)" }
            "*AfterUpdate*" { $lsvAfterNote = "PASS ($f)" }
            "*Scan10k*" { $lsvScanNote = "PASS ($f)" }
            "*WriteOverhead*" { $lsvWriteNote = "PASS ($f)" }
        }
    }
}

$content = @"
# Kernel × RAR Performance Baseline

**Date**: $date  
**Refresh**:

``````powershell
.\scripts\perf\refresh_kernel_baseline.ps1
``````

## Lazy scan 10k (ProductionDefaults, post-reopen)

| Metric | Observed | Gate |
|--------|----------|------|
| raw lazy scan P50 | $lazyNote | <=40ms local / <=60ms CI |

## RAR MONITOR write (100k Put)

| Metric | Observed | Gate |
|--------|-------|------|
| CARL/no-CARL ratio | $rarNote | P16-carl-eval >=0.99 |

## LSV snapshot read (P19-lsv)

| Metric | Observed | Gate |
|--------|----------|------|
| Snapshot get hot key P50 (1000x) | $lsvHotNote | <=1.05ms local / <=5ms CI |
| Snapshot get after update P50 (1000x) | $lsvAfterNote | <=2ms/key local / <=5ms CI |
| Snapshot scan 10k | $lsvScanNote | <=55ms local / <=75ms CI |
| Snapshot write overhead (5000 Put, balanced) | $lsvWriteNote | >=776 TPS (0.97×800 baseline) |

## Verification

``````powershell
.\scripts\test\run_tests.ps1 -Gate P17-deep-core -Config Release
.\scripts\test\run_tests.ps1 -Gate P19-lsv -Config Release
``````

## Notes

- Hardware-bound (local NVMe). GHA uses ``EBTEST_CI`` relaxed lazy scan budget.
- CARL frozen eval: [eval-results.md](../../papers/carl/eval-results.md) (CIDR deferred).
"@

Set-Content -Path $out -Value $content -Encoding utf8
Write-Host "Wrote $out"
