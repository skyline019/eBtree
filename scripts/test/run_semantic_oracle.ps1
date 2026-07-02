param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path,
    [string]$Corpus = (Join-Path $RepoRoot "test/data/sqllogic/semantic/semantic.test")
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($env:EBTREE_TEST_TMP_ROOT)) {
    . (Join-Path $PSScriptRoot "test_run_env.ps1") -RepoRoot $RepoRoot -RunLabel "semantic-oracle" | Out-Null
}
$tmpRoot = $env:EBTREE_TEST_TMP_ROOT
if ([string]::IsNullOrWhiteSpace($tmpRoot)) {
    $tmpRoot = Join-Path (Join-Path $RepoRoot ".test-runs") "tmp/semantic-oracle"
    New-Item -ItemType Directory -Force -Path $tmpRoot | Out-Null
    $env:EBTREE_TEST_TMP_ROOT = $tmpRoot
    $env:TEMP = $tmpRoot
    $env:TMP = $tmpRoot
}

$sqlite3 = Get-Command sqlite3 -ErrorAction SilentlyContinue
if (-not $sqlite3) {
    Write-Warning "sqlite3 not in PATH; oracle diff skipped (CI may bundle sqlite3.exe)"
    exit 0
}

function Invoke-SqliteCase {
    param([string[]]$Setup, [string]$Query)
    $db = Join-Path $tmpRoot ("semantic_oracle_" + [guid]::NewGuid().ToString("N") + ".db")
    try {
        foreach ($s in $Setup) {
            & sqlite3 $db $s | Out-Null
            if ($LASTEXITCODE -ne 0) { throw "setup failed: $s" }
        }
        $out = & sqlite3 -batch -noheader $db $Query 2>&1
        if ($LASTEXITCODE -ne 0) { throw "query failed: $Query -> $out" }
        return @($out | Where-Object { $_ -ne $null -and $_.ToString().Trim() -ne "" })
    } finally {
        if (Test-Path $db) { Remove-Item $db -Force -ErrorAction SilentlyContinue }
    }
}

$content = Get-Content $Corpus -Raw
$blocks = $content -split "(?m)^-- name:"
$mismatch = 0
foreach ($block in $blocks) {
    if ($block.Trim().Length -eq 0) { continue }
    $lines = $block -split "`n"
    $name = $lines[0].Trim()
    $setup = @()
    $sql = ""
    $expected = @()
    $inExpected = $false
    foreach ($line in $lines) {
        if ($line -match "^-- setup:") {
            $setup += $line.Substring(9).Trim()
        } elseif ($line -eq "----") {
            $inExpected = $true
        } elseif ($inExpected) {
            if ($line.Trim().Length -gt 0) { $expected += $line.TrimEnd() }
        } elseif ($line.Trim().Length -gt 0 -and $line[0] -ne "-") {
            $sql = $line.Trim()
        }
    }
    if ($sql.Length -eq 0) { continue }
    $actual = Invoke-SqliteCase -Setup $setup -Query $sql
    if ($actual.Count -ne $expected.Count) {
        Write-Host "MISMATCH $name row count expected=$($expected.Count) actual=$($actual.Count)"
        $mismatch++
        continue
    }
    for ($i = 0; $i -lt $expected.Count; $i++) {
        if ($actual[$i] -ne $expected[$i]) {
            Write-Host "MISMATCH $name row $i expected='$($expected[$i])' actual='$($actual[$i])'"
            $mismatch++
        }
    }
}

if ($mismatch -gt 0) {
    Write-Error "semantic oracle corpus has $mismatch mismatches vs sqlite3"
}
Write-Host "semantic oracle corpus validated against sqlite3 ($($blocks.Count - 1) cases)"
