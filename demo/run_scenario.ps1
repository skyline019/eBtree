param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("industrial", "medical", "finance")]
    [string]$Scenario,
    [string]$BuildDir = "build-msvc-2026",
    [string]$Config = "Release",
    [string]$RepoRoot = (Split-Path $PSScriptRoot -Parent)
)

$ErrorActionPreference = "Stop"
$scenarioDir = Join-Path $PSScriptRoot $(
    switch ($Scenario) {
        "industrial" { "industrial_config" }
        "medical" { "medical_record" }
        "finance" { "finance_edge" }
    }
)
$script = Join-Path $scenarioDir "run.ps1"
if (-not (Test-Path $script)) {
    throw "Missing scenario script: $script"
}
& $script -BuildDir (Join-Path $RepoRoot $BuildDir) -Config $Config -RepoRoot $RepoRoot
