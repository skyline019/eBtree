param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path,
    [string]$Matrix = ""
)

$ErrorActionPreference = "Stop"
$matrixDir = Join-Path $RepoRoot "test\data\matrix"
$outDir = Join-Path $RepoRoot "test\matrix"

function Get-SymbolPrefix([string]$Stem) {
    $parts = $Stem -split '[_\-]'
    $prefix = ""
    foreach ($p in $parts) {
        if ($p.Length -gt 0) {
            $prefix += $p.Substring(0, 1).ToUpper() + $p.Substring(1)
        }
    }
    return $prefix
}

function Parse-Ops([string[]]$Block) {
    for line in block:
        $stripped = $line.Trim()
        if ($stripped -match '^ops:\s*(.*)$') {
            $raw = $Matches[1].Trim()
            if (-not $raw) { return @() }
            return @($raw -split ';' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
        }
    }
    return @()
}

function Parse-Case([string]$Chunk) {
    $case = @{
        id = ""
        durability = "sync"
        setup_ops = @()
        run = ""
        expect = "ok"
        get_key = ""
        get_value = ""
        error_contains = ""
        corrupt = ""
        assert_stat = ""
    }
    $section = $null
    $sectionLines = @()
    foreach ($line in ($Chunk -split "`n" | ForEach-Object { $_.TrimEnd() } | Where-Object { $_.Trim() })) {
        if ($line -eq "setup:") {
            if ($section -eq "setup") { $case.setup_ops = Parse-Ops $sectionLines }
            $section = "setup"
            $sectionLines = @()
            continue
        }
        if ($line -match '^case:\s*(.*)$') { $case.id = $Matches[1].Trim(); continue }
        if ($line -match '^run:\s*(.*)$') {
            if ($section -eq "setup") { $case.setup_ops = Parse-Ops $sectionLines }
            $section = $null
            $case.run = $Matches[1].Trim()
            continue
        }
        if ($line -match '^expect:\s*(.*)$') { $case.expect = $Matches[1].Trim(); continue }
        if ($line -match '^get:\s*(.*)$') {
            $kv = $Matches[1].Trim()
            if ($kv -match '=') {
                $parts = $kv -split '=', 2
                $case.get_key = $parts[0].Trim()
                $case.get_value = $parts[1].Trim()
            } else {
                $case.get_key = $kv
            }
            continue
        }
        if ($line -match '^error_contains:\s*(.*)$') { $case.error_contains = $Matches[1].Trim(); continue }
        if ($stripped -match '^durability:\s*(.*)$') { $case.durability = $Matches[1].Trim(); continue }
        if ($line -match '^assert_stat:\s*(.*)$') { $case.assert_stat = $Matches[1].Trim(); continue }
        if ($section -eq "setup") { $sectionLines += $line }
    }
    if ($section -eq "setup") { $case.setup_ops = Parse-Ops $sectionLines }
    return $case
}

function Emit-Cases([array]$Cases, [string]$OutPath, [string]$Stem) {
    $prefix = Get-SymbolPrefix $Stem
    $arrayName = "k${prefix}MatrixCases"
    $countName = "k${prefix}MatrixCaseCount"
    $lines = @(
        "#pragma once",
        "",
        '#include "matrix_case.h"',
        "",
        "inline const EbMatrixCase ${arrayName}[] = {"
    )
    foreach ($c in $Cases) {
        $ops = ($c.setup_ops | ForEach-Object { "`"$_`"" }) -join ", "
        $lines += @(
            "    {{"
            "`"$($c.id)`", `"$($c.durability)`", {$ops}, "
            "`"$($c.run)`", `"$($c.expect)`", "
            "`"$($c.get_key)`", `"$($c.get_value)`", "
            "`"$($c.error_contains)`", `"$($c.corrupt)`", "
            "`"$($c.assert_stat)`"}}"
        )
    }
    $lines += @(
        "};",
        "",
        "inline constexpr int ${countName} = $($Cases.Count);",
        ""
    )
    $content = $lines -join "`n"
    [System.IO.File]::WriteAllText($OutPath, $content, (New-Object System.Text.UTF8Encoding $false))
}

function Generate-Matrix([string]$MatrixFile, [string]$OutFile) {
    $text = Get-Content -Path $MatrixFile -Raw
    $chunks = @($text -split '---' | Where-Object { $_.Trim() })
    $cases = @($chunks | ForEach-Object { Parse-Case $_ })
    $stem = [System.IO.Path]::GetFileNameWithoutExtension($MatrixFile)
    Emit-Cases $cases $OutFile $stem
    Write-Host "Wrote $OutFile ($($cases.Count) cases)"
    return $cases.Count
}

$total = 0
if ($Matrix) {
    $stem = [System.IO.Path]::GetFileNameWithoutExtension($Matrix)
    $matrixFile = Join-Path $matrixDir "$stem.matrix"
    $outFile = Join-Path $outDir "${stem}_matrix_inc.h"
    $total = Generate-Matrix $matrixFile $outFile
} else {
    Get-ChildItem -Path $matrixDir -Filter "*.matrix" | Sort-Object Name | ForEach-Object {
        $outFile = Join-Path $outDir ($_.BaseName + "_matrix_inc.h")
        $total += Generate-Matrix $_.FullName $outFile
    }
}
Write-Host "Generated $total total cases"
exit 0
