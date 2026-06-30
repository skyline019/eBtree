param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path,
    [string]$BuildDir = "",
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",
    [ValidateSet("P0", "P1", "P2", "P3", "P3-complete", "P3d-complete", "P4", "P4-complete", "P5", "P5-complete", "P6", "P6-complete", "P6-sql", "P6-compress", "P7", "P7-complete", "P7-sql", "P8", "P8-complete", "P8-program-complete", "P8-perf", "P9", "P9-complete", "P9-perf", "P10-compress", "P10-sql", "P10-program-honest", "P11-real-sql", "P11-program-honest", "P12-semantic-sql", "all")]
    [string]$Gate = "all"
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $RepoRoot "build-msvc-2026"
}

$runner = Join-Path $BuildDir "$Config/ebtree_test_runner.exe"
if (-not (Test-Path $runner)) {
    Write-Error "Runner not found: $runner (build first)"
}

$manifest = Join-Path $RepoRoot "test/TEST_MANIFEST.yaml"
if (-not (Test-Path $manifest)) {
    Write-Error "Missing TEST_MANIFEST.yaml"
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
    "P4-complete" { & $runner --suite=unit,failure,pipeline,complex,composite,matrix,sql,audit; exit $LASTEXITCODE }
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
    "P8-complete" { & $runner --suite=unit,failure,pipeline,complex,composite,matrix,nested,sql,audit; exit $LASTEXITCODE }
    "P8-program-complete" { & $runner --suite=unit,failure,pipeline,complex,composite,matrix,nested,sql,audit; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; & (Join-Path $RepoRoot "scripts/test/run_sqllogictest.ps1") -RepoRoot $RepoRoot -BuildDir $BuildDir -Config $Config -MinPassRate 0.90; exit $LASTEXITCODE }
    "P10-compress" { & $runner --suite=unit,pipeline,complex; exit $LASTEXITCODE }
    "P10-sql" { & $runner --suite=sql; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; & (Join-Path $RepoRoot "scripts/test/run_sqllogictest.ps1") -RepoRoot $RepoRoot -BuildDir $BuildDir -Config $Config -MinPassRate 0.90 -UseCuratedCorpus; exit $LASTEXITCODE }
    "P10-program-honest" { & $runner --suite=unit,failure,pipeline,complex,composite,matrix,nested,sql,audit; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; & (Join-Path $RepoRoot "scripts/test/run_sqllogictest.ps1") -RepoRoot $RepoRoot -BuildDir $BuildDir -Config $Config -MinPassRate 0.90 -UseCuratedCorpus; exit $LASTEXITCODE }
    "P11-real-sql" { & $runner --suite=sql; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; & (Join-Path $RepoRoot "scripts/test/run_sqllogictest.ps1") -RepoRoot $RepoRoot -BuildDir $BuildDir -Config $Config -MinPassRate 1.0 -UseOfficialCorpus; exit $LASTEXITCODE }
    "P11-program-honest" { & $runner --suite=unit,failure,pipeline,complex,composite,matrix,nested,sql,audit; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; & (Join-Path $RepoRoot "scripts/test/run_sqllogictest.ps1") -RepoRoot $RepoRoot -BuildDir $BuildDir -Config $Config -MinPassRate 0.90 -UseCuratedCorpus; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; & (Join-Path $RepoRoot "scripts/test/run_sqllogictest.ps1") -RepoRoot $RepoRoot -BuildDir $BuildDir -Config $Config -MinPassRate 1.0 -UseOfficialCorpus; exit $LASTEXITCODE }
    "P12-semantic-sql" { & $runner --suite=sql --filter=SemanticOracle*; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; & (Join-Path $RepoRoot "scripts/test/run_semantic_oracle.ps1") -RepoRoot $RepoRoot; exit $LASTEXITCODE }
    "P9" { & $runner --suite=unit,pipeline,failure,complex,matrix; exit $LASTEXITCODE }
    "P9-complete" { & $runner --suite=unit,failure,pipeline,complex,composite,matrix,nested; exit $LASTEXITCODE }
    "P9-perf" {
        & $runner --suite=pipeline
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        $bench = Join-Path $BuildDir "$Config/ebtree_write_bench.exe"
        if (Test-Path $bench) { & $bench; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE } }
        exit 0
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
