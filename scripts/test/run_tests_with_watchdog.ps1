param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path,
    [string]$BuildDir = "",
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release",
    [Parameter(Mandatory = $true)]
    [string]$Gate
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $RepoRoot "build-msvc-2026"
}

$runTests = Join-Path $RepoRoot "scripts/test/run_tests.ps1"
$logRoot = Join-Path $RepoRoot ".test-runs"

Write-Host "== watchdog gate=$Gate config=$Config =="
& $runTests -RepoRoot $RepoRoot -BuildDir $BuildDir -Config $Config -Gate $Gate -UseWatchdog
$code = $LASTEXITCODE

if ($code -eq 124) {
    $hangMeta = Get-ChildItem $logRoot -Filter "*-HANG.meta.json" -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    Write-Host ""
    Write-Host "== WATCHDOG HANG DETECTED (exit 124) =="
    if ($hangMeta) {
        Write-Host "meta: $($hangMeta.FullName)"
        Get-Content $hangMeta.FullName | Write-Host
    } else {
        Write-Host "no HANG.meta.json found under $logRoot"
    }
    Write-Host ""
    Write-Host "Fix hang root cause, then re-run:"
    Write-Host "  .\scripts\test\run_tests_with_watchdog.ps1 -Gate $Gate -Config $Config"
    exit 124
}

exit $code
