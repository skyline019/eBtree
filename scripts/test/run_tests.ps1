param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path,
    [string]$BuildDir = "",
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",
    [ValidateSet("P0", "P1", "P2", "P3", "P3-complete", "P3d-complete", "P4", "P4-complete", "P5", "P5-complete", "P6", "P6-complete", "P6-sql", "P6-compress", "P7", "P7-complete", "P7-sql", "P8", "P8-complete", "P8-program-complete", "P8-perf", "P9", "P9-complete", "P9-perf", "P9-nofallback-hard", "P9-perf-read", "P9-perf-write", "P9-audit-complete", "P9-program-complete", "P10-compress", "P10-compress-v2", "P14-standard-product", "P14-standard-perf", "P14-powerfail-compress", "P14-rar-dynamic", "P14-rar-product", "P14-program-complete", "P15-carl-complete", "P16-carl-eval", "P16-demo-e2e", "P17-deep-core", "P18-stability", "P19-lsv", "P20-lsv-tso", "P10-sql", "P10-program-honest", "P11-real-sql", "P11-program-honest", "P12-semantic-sql", "P13-expr-sql", "P13-constraints-sql", "all")]
    [string]$Gate = "all",
    [switch]$UseWatchdog
)

$ErrorActionPreference = "Continue"
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $RepoRoot "build-msvc-2026"
}

$runInfo = . (Join-Path $RepoRoot "scripts/test/test_run_env.ps1") -RepoRoot $RepoRoot -RunLabel $Gate
Push-Location $RepoRoot
try {

$runner = Join-Path $BuildDir "$Config/ebtree_test_runner.exe"
if (-not (Test-Path $runner)) {
    Write-Error "Runner not found: $runner (build first)"
}

$manifest = Join-Path $RepoRoot "test/TEST_MANIFEST.yaml"
if (-not (Test-Path $manifest)) {
    Write-Error "Missing TEST_MANIFEST.yaml"
}

# GTest uses a single '-' to split positive/negative; exclusions after the first '-' are colon-separated (no extra '-' prefixes).
$SlowSqlGtestFilter = "-SqllogicRunner.RealSqliteBaselineReport:SqllogicRunner.RealSqliteOfficialPassRate"
$P4GtestFilter = "-EbPipelinePerf.*:SqllogicRunner.RealSqliteBaselineReport:SqllogicRunner.RealSqliteOfficialPassRate"
$P8GtestFilter = "-EbPipelinePerf.*:SqllogicRunner.RealSqliteBaselineReport:SqllogicRunner.RealSqliteOfficialPassRate"
$P9PerfReadFilter = "EbPipelinePerf.LazyScan10kBudget:EbPipelinePerf.KSyncScan10kSmokeBudget:EbPipelineRto.*"
$P9PerfWriteFilter = "EbPipelinePerf.KSyncWrite10kSmokeBudget:EbPipelinePerf.KBalancedWrite100kConcurrentBudget"
$P9GtestFilter = "-EbPipelinePerf.*:SqllogicRunner.RealSqliteBaselineReport:SqllogicRunner.RealSqliteOfficialPassRate"
$P14StandardPerfFilter = "EbPipelinePerf.StandardWrite100kConcurrentBudget:EbPipelinePerf.StandardLazyScan10kBudget"
$NoFallbackMatrixFilter = "EbMatrix/NoFallbackMatrixTest.*:EbMatrixSchema.NoFallbackCasesNonEmpty"
$P17DeepCoreRarFilter = "SqlRarMonitor*:RarChain*:RarAsync*:RarBuildRarV3*:RarChainPolicy*:RarChainAutoSign*:EbPipelinePerf.KBalancedWrite100kWithRarMonitor:KvRarMonitor*"
$P18StabilityFilter = "KvRarMonitor*:RarStability*:-RarOracleEquivalence.ProductionRandomDestroy"
$P19LsvFilter = "VcsChain*:VcsInvariant*:VcsGcPin*:SnapshotResolver*:VcsPowerfail*:VcsSnapshotPowerfail*:LsvPerfRegression*:SqlTxn.SnapshotReadOwnWriteBeforeCommit*:SnapshotSiSql.*"
$P20LsvTsoFilter = "VcsChain*:VcsInvariant*:VcsGcPin*:SnapshotResolver*:VcsPowerfail*:VcsSnapshotPowerfail*:LsvPerfRegression*:SqlTxn.SnapshotReadOwnWriteBeforeCommit*:SnapshotSiSql.*:SnapshotOccSql.*:SnapshotPhantomSql.*:Tsl3Lock*:SfsRead*:TxnWal*"
$P14PowerfailFilter = "EbFailureRandomPowerfail.Standard*"

function Invoke-LsvProgressGate {
    param(
        [string]$Runner,
        [string]$RepoRoot,
        [string]$Filter,
        [string]$Label,
        [int]$FailureStaleSec = 420,
        [int]$ConcurrentStaleSec = 600,
        [int]$DefaultStaleSec = 180,
        [switch]$KillProcessTree
    )
    $progress = Join-Path $RepoRoot "scripts/test/Invoke-TestRunnerWithProgress.ps1"
    $logRoot = Join-Path $RepoRoot ".test-runs"
    $treeArg = @{}
    if ($KillProcessTree) { $treeArg["KillProcessTree"] = $true }
    foreach ($suite in @("unit", "failure", "pipeline", "sql")) {
        $stale = if ($suite -eq "failure") { $FailureStaleSec } else { $DefaultStaleSec }
        if ($suite -eq "failure") {
            foreach ($failFilter in @("VcsSnapshotPowerfail*", "VcsPowerfail*", "TxnWalRecovery*")) {
                $sec = if ($failFilter -like "VcsSnapshot*") { $ConcurrentStaleSec } else { $stale }
                & $progress -Runner $Runner -Suite $suite -Filter $failFilter `
                    -LogRoot $logRoot -StaleSec $sec -Label $Label @treeArg
                if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
            }
        } else {
            & $progress -Runner $Runner -Suite $suite -Filter $Filter `
                -LogRoot $logRoot -StaleSec $stale -Label $Label @treeArg
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        }
    }
    & $progress -Runner $Runner -Suite "matrix/no_fallback" -Filter $NoFallbackMatrixFilter `
        -LogRoot $logRoot -StaleSec $DefaultStaleSec -Label $Label @treeArg
    exit $LASTEXITCODE
}

Write-Host "== eB-Tree tests gate=$Gate config=$Config =="

if ($Gate -match "^P3" -or $Gate -match "^P4" -or $Gate -match "^P5" -or $Gate -match "^P6" -or $Gate -match "^P7" -or $Gate -match "^P8" -or $Gate -match "^P9" -or $Gate -eq "all") {
    & (Join-Path $RepoRoot "scripts/test/check_matrix_codegen.ps1") -RepoRoot $RepoRoot
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

switch ($Gate) {
    "P0" { & $runner --suite=unit; exit $LASTEXITCODE }
    "P1" { & $runner --suite=unit,failure,pipeline,composite,matrix; exit $LASTEXITCODE }
    "P2" { & $runner --suite=all; exit $LASTEXITCODE }
    "P3" { & $runner --suite=pipeline,matrix; exit $LASTEXITCODE }
    "P3-complete" { & $runner --suite=unit,failure,pipeline,matrix; exit $LASTEXITCODE }
    "P3d-complete" { & $runner --suite=unit,failure,pipeline,matrix,sql,audit; exit $LASTEXITCODE }
    "P4" { & $runner --suite=pipeline,failure,complex,matrix/recovery; exit $LASTEXITCODE }
    "P4-complete" {
        & $runner --suite=unit,failure,pipeline,complex,composite,matrix,sql,audit `
            "--gtest_filter=$P4GtestFilter"
        exit $LASTEXITCODE
    }
    "P5" { & $runner --suite=pipeline,failure,complex,matrix/recovery,matrix/flashback; exit $LASTEXITCODE }
    "P5-complete" { & $runner --suite=unit,failure,pipeline,complex,composite,matrix,sql,audit; exit $LASTEXITCODE }
    "P6" { & $runner --suite=pipeline,failure,complex,matrix/shard,matrix/chaos; exit $LASTEXITCODE }
    "P6-complete" { & $runner --suite=unit,failure,pipeline,complex,composite,matrix,sql,audit; exit $LASTEXITCODE }
    "P6-sql" { & $runner --suite=sql; exit $LASTEXITCODE }
    "P6-compress" { & $runner --suite=unit,pipeline,complex; exit $LASTEXITCODE }
    "P7" { & $runner --suite=pipeline,failure,complex,matrix/paged,matrix/concurrent; exit $LASTEXITCODE }
    "P7-complete" { & $runner --suite=unit,failure,pipeline,complex,composite,matrix,sql,audit; exit $LASTEXITCODE }
    "P7-sql" { & $runner --suite=sql; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; & (Join-Path $RepoRoot "scripts/test/run_sqllogictest.ps1") -RepoRoot $RepoRoot -BuildDir $BuildDir -Config $Config -MinPassRate 0.60; exit $LASTEXITCODE }
    "P8" { & $runner --suite=unit,pipeline,failure,complex,matrix/paged,matrix/shard,matrix/flashback; exit $LASTEXITCODE }
    "P8-complete" {
        & $runner --suite=unit,failure,pipeline,complex,composite,matrix,nested,sql,audit `
            "--gtest_filter=$P8GtestFilter"
        exit $LASTEXITCODE
    }
    "P8-program-complete" {
        & $runner --suite=unit,failure,pipeline,complex,composite,matrix,nested,sql,audit `
            "--gtest_filter=$P8GtestFilter"
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        & (Join-Path $RepoRoot "scripts/test/run_sqllogictest.ps1") -RepoRoot $RepoRoot -BuildDir $BuildDir -Config $Config -MinPassRate 0.90 -UseCuratedCorpus
        exit $LASTEXITCODE
    }
    "P10-compress" { & $runner --suite=unit,pipeline,complex; exit $LASTEXITCODE }
    "P10-sql" { & $runner --suite=sql; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; & (Join-Path $RepoRoot "scripts/test/run_sqllogictest.ps1") -RepoRoot $RepoRoot -BuildDir $BuildDir -Config $Config -MinPassRate 0.90 -UseCuratedCorpus; exit $LASTEXITCODE }
    "P10-program-honest" { & $runner --suite=unit,failure,pipeline,complex,composite,matrix,nested,sql,audit; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; & (Join-Path $RepoRoot "scripts/test/run_sqllogictest.ps1") -RepoRoot $RepoRoot -BuildDir $BuildDir -Config $Config -MinPassRate 0.90 -UseCuratedCorpus; exit $LASTEXITCODE }
    "P11-real-sql" {
        & $runner --suite=sql `
            "--gtest_filter=$SlowSqlGtestFilter"
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        & (Join-Path $RepoRoot "scripts/test/run_sqllogictest.ps1") -RepoRoot $RepoRoot -BuildDir $BuildDir -Config $Config -MinPassRate 1.0 -UseOfficialCorpus
        exit $LASTEXITCODE
    }
    "P11-program-honest" { & $runner --suite=unit,failure,pipeline,complex,composite,matrix,nested,sql,audit; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; & (Join-Path $RepoRoot "scripts/test/run_sqllogictest.ps1") -RepoRoot $RepoRoot -BuildDir $BuildDir -Config $Config -MinPassRate 0.90 -UseCuratedCorpus; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; & (Join-Path $RepoRoot "scripts/test/run_sqllogictest.ps1") -RepoRoot $RepoRoot -BuildDir $BuildDir -Config $Config -MinPassRate 1.0 -UseOfficialCorpus; exit $LASTEXITCODE }
    "P12-semantic-sql" {
        & $runner --suite=sql "--gtest_filter=SemanticOracle*"
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        & (Join-Path $RepoRoot "scripts/test/run_semantic_oracle.ps1") -RepoRoot $RepoRoot
        exit $LASTEXITCODE
    }
    "P13-expr-sql" {
        & $runner --suite=sql "--gtest_filter=SqlSubsetB.LikeFilter:SqlSubsetB.CastInteger:SemanticOracle*"
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        & (Join-Path $RepoRoot "scripts/test/run_semantic_oracle.ps1") -RepoRoot $RepoRoot
        exit $LASTEXITCODE
    }
    "P13-constraints-sql" {
        & $runner --suite=sql "--gtest_filter=SqlSubsetB.CheckConstraintInsertFail"
        exit $LASTEXITCODE
    }
    "P9" { & $runner --suite=unit,pipeline,failure,complex,matrix; exit $LASTEXITCODE }
    "P9-nofallback-hard" {
        & $runner --suite=matrix/no_fallback "--gtest_filter=$NoFallbackMatrixFilter"
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        & $runner --suite=unit,failure,complex,audit `
            "--gtest_filter=EbForbiddenEnforcement.*:Rar*"
        exit $LASTEXITCODE
    }
    "P9-perf-read" {
        & $runner --suite=pipeline "--gtest_filter=$P9PerfReadFilter"
        exit $LASTEXITCODE
    }
    "P9-perf-write" {
        & $runner --suite=pipeline "--gtest_filter=$P9PerfWriteFilter"
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        $bench = Join-Path $BuildDir "$Config/ebtree_multishard_write_bench.exe"
        if (Test-Path $bench) { & $bench; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE } }
        exit 0
    }
    "P9-audit-complete" {
        & $runner --suite=audit "--gtest_filter=Rar*"
        exit $LASTEXITCODE
    }
    "P9-program-complete" {
        & $runner --suite=unit,failure,pipeline,complex,composite,matrix,nested,sql,audit `
            "--gtest_filter=$P9GtestFilter"
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        & (Join-Path $RepoRoot "scripts/test/run_sqllogictest.ps1") -RepoRoot $RepoRoot -BuildDir $BuildDir -Config $Config -MinPassRate 0.90 -UseCuratedCorpus
        exit $LASTEXITCODE
    }
    "P9-complete" { & $runner --suite=unit,failure,pipeline,complex,composite,matrix,nested; exit $LASTEXITCODE }
    "P9-perf" {
        & $runner --suite=pipeline
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        $bench = Join-Path $BuildDir "$Config/ebtree_write_bench.exe"
        if (Test-Path $bench) { & $bench; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE } }
        exit 0
    }
    "P10-compress-v2" { & $runner --suite=unit,pipeline,complex; exit $LASTEXITCODE }
    "P14-standard-product" {
        & $runner --suite=unit,failure,matrix/no_fallback,pipeline,sql `
            "--gtest_filter=-EbPipelinePerf.*:SqllogicRunner.RealSqliteBaselineReport"
        exit $LASTEXITCODE
    }
    "P14-standard-perf" {
        & $runner --suite=pipeline "--gtest_filter=$P14StandardPerfFilter"
        exit $LASTEXITCODE
    }
    "P14-powerfail-compress" {
        & $runner --suite=failure "--gtest_filter=$P14PowerfailFilter"
        exit $LASTEXITCODE
    }
    "P14-rar-dynamic" {
        & $runner --suite=audit,sql "--gtest_filter=RarChain*:RarAsync*:RarBuildRarV3*:RarChainPolicy*:SqlRarMonitor*"
        exit $LASTEXITCODE
    }
    "P14-rar-product" {
        & $runner --suite=sql,audit,pipeline `
            "--gtest_filter=SqlRarMonitor*:RarChain*:RarAsync*:RarBuildRarV3*:RarChainPolicy*:RarChainAutoSign*:EbPipelinePerf.KBalancedWrite100kWithRarMonitor"
        exit $LASTEXITCODE
    }
    "P15-carl-complete" {
        & $runner --suite=audit,sql `
            "--gtest_filter=CarlAnchor*:CarlMerkle*:CarlWorker*:SqlSubsetB.IndexScanRange*:SqlSubsetB.CompositeIndexLeadingEq*:SqlSubsetB.ExplainMatchesIndexScan"
        exit $LASTEXITCODE
    }
    "P16-carl-eval" {
        & $runner --suite=audit `
            "--gtest_filter=CarlEvalGate*"
        exit $LASTEXITCODE
    }
    "P16-demo-e2e" {
        & $runner --suite=sql `
            "--gtest_filter=DemoE2eGate*"
        exit $LASTEXITCODE
    }
    "P17-deep-core" {
        & $runner --suite=pipeline "--gtest_filter=$P14StandardPerfFilter"
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        & $runner --suite=matrix/no_fallback "--gtest_filter=$NoFallbackMatrixFilter"
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        & $runner --suite=unit,failure,complex,audit `
            "--gtest_filter=EbForbiddenEnforcement.*:Rar*:-RarOracleEquivalence.ProductionRandomDestroy"
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        & $runner --suite=sql,audit,pipeline `
            "--gtest_filter=$P17DeepCoreRarFilter"
        exit $LASTEXITCODE
    }
    "P18-stability" {
        & $runner --suite=unit,failure,audit `
            "--gtest_filter=$P18StabilityFilter"
        exit $LASTEXITCODE
    }
    "P19-lsv" {
        Invoke-LsvProgressGate -Runner $runner -RepoRoot $RepoRoot `
            -Filter $P19LsvFilter -Label "P19-lsv"
    }
    "P20-lsv-tso" {
        Invoke-LsvProgressGate -Runner $runner -RepoRoot $RepoRoot `
            -Filter $P20LsvTsoFilter -Label "P20-lsv-tso" -KillProcessTree
    }
    "P14-program-complete" {
        & $runner --suite=unit,failure,pipeline,complex,composite,matrix,nested,sql,audit `
            "--gtest_filter=-EbPipelinePerf.*:SqllogicRunner.RealSqliteBaselineReport"
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        & (Join-Path $RepoRoot "scripts/test/run_sqllogictest.ps1") -RepoRoot $RepoRoot -BuildDir $BuildDir -Config $Config -MinPassRate 0.90 -UseCuratedCorpus
        exit $LASTEXITCODE
    }
    "P8-perf" {
        & $runner --suite=pipeline
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        $bench = Join-Path $BuildDir "$Config/ebtree_write_bench.exe"
        if (Test-Path $bench) { & $bench; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE } }
        exit 0
    }
    default { & $runner --suite=all; exit $LASTEXITCODE }
}
} finally {
    Pop-Location
}
