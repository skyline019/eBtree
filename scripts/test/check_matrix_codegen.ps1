param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
)

$ErrorActionPreference = "Stop"
$matrixDir = Join-Path $RepoRoot "test\matrix"
$dataDir = Join-Path $RepoRoot "test\data\matrix"
$genPy = Join-Path $RepoRoot "scripts\test\gen_matrix_inc.py"
$genPs = Join-Path $RepoRoot "scripts\test\gen_matrix_inc.ps1"

$before = @{}
Get-ChildItem -Path $matrixDir -Filter "*_matrix_inc.h" | ForEach-Object {
    $before[$_.Name] = Get-FileHash $_.FullName -Algorithm SHA256
}

function Test-RealPythonExe([string]$Exe) {
    if (-not (Test-Path $Exe)) { return $false }
    if ($Exe -match 'WindowsApps\\python') { return $false }
    & $Exe --version *> $null
    return $LASTEXITCODE -eq 0
}

function Invoke-MatrixGen([string]$PythonExe) {
    & $PythonExe $genPy --repo $RepoRoot
    if ($LASTEXITCODE -ne 0) { throw "gen_matrix_inc.py failed" }
}

$ranGen = $false
$pythonCandidates = @()
if ($env:CONDA_PREFIX) { $pythonCandidates += (Join-Path $env:CONDA_PREFIX "python.exe") }
$pythonCandidates += "d:\anaconda3\python.exe"
$pythonCandidates += "python"

foreach ($candidate in $pythonCandidates) {
    if ($candidate -eq "python") {
        if (Test-RealPythonExe (Get-Command python -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source)) {
            Invoke-MatrixGen "python"
            $ranGen = $true
            break
        }
    } elseif (Test-RealPythonExe $candidate) {
        Invoke-MatrixGen $candidate
        $ranGen = $true
        break
    }
}

if (-not $ranGen -and (Get-Command py -ErrorAction SilentlyContinue)) {
    & py -3 $genPy --repo $RepoRoot
    if ($LASTEXITCODE -ne 0) { throw "gen_matrix_inc.py via py -3 failed" }
    $ranGen = $true
}

if (-not $ranGen) {
    & $genPs -RepoRoot $RepoRoot | Out-Host
    $ranGen = $true
}

if (-not $ranGen) {
    Write-Error "Matrix codegen could not run"
}

Get-ChildItem -Path $dataDir -Filter "*.matrix" | ForEach-Object {
    $incPath = Join-Path $matrixDir ($_.BaseName + "_matrix_inc.h")
    if (-not (Test-Path $incPath)) {
        Write-Error "Missing generated header: $incPath"
    }
}

Get-ChildItem -Path $matrixDir -Filter "*_matrix_inc.h" | ForEach-Object {
    $hash = Get-FileHash $_.FullName -Algorithm SHA256
    if ($before.ContainsKey($_.Name)) {
        if ($before[$_.Name].Hash -ne $hash.Hash) {
            Write-Error "Matrix inc out of sync: $($_.Name). Regenerate matrix headers."
        }
    }
}

Write-Host "Matrix codegen check OK ($((Get-ChildItem $dataDir -Filter '*.matrix').Count) matrix files)"
