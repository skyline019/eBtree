param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path,
    [string]$BuildDir = "",
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release",
    [int]$Trials = 10,
    [int]$MinPass = 9,
    [string[]]$Gates = @("P4-complete", "P17-deep-core", "P18-stability"),
    [string]$ResumeDir = "",
    [switch]$CleanOnly
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $RepoRoot "build-msvc-2026"
}

$runTests = Join-Path $RepoRoot "scripts/test/run_tests.ps1"
$testRunEnv = Join-Path $RepoRoot "scripts/test/test_run_env.ps1"

function Stop-EbtreeTestProcesses {
    Get-Process -Name "ebtree_test_runner" -ErrorAction SilentlyContinue | Stop-Process -Force
}

function Clear-EbtreeGateRunCaches {
    param(
        [string]$Root,
        [string]$KeepStreakDir = ""
    )
    Stop-EbtreeTestProcesses

    $runsRoot = Join-Path $Root ".test-runs"
    if (-not (Test-Path $runsRoot)) { return 0 }

    $freed = 0L
    Get-ChildItem $runsRoot -Directory | ForEach-Object {
        if ($_.Name -like "streak-*") { return }
        if ($KeepStreakDir -and ($_.FullName -eq $KeepStreakDir)) { return }
        if ($_.Name -match '^\d{8}-\d{6}_') {
            $size = (Get-ChildItem $_.FullName -Recurse -File -ErrorAction SilentlyContinue |
                Measure-Object -Property Length -Sum).Sum
            Remove-Item -LiteralPath $_.FullName -Recurse -Force -ErrorAction SilentlyContinue
            $freed += $size
        }
    }

    $defaultTmp = Join-Path (Join-Path $runsRoot "tmp") "default"
    if (Test-Path $defaultTmp) {
        $size = (Get-ChildItem $defaultTmp -Recurse -File -ErrorAction SilentlyContinue |
            Measure-Object -Property Length -Sum).Sum
        Remove-Item -LiteralPath $defaultTmp -Recurse -Force -ErrorAction SilentlyContinue
        $freed += $size
    }

    [math]::Round($freed / 1GB, 2)
}

function Test-StreakTrialComplete {
    param(
        [string]$ReportDir,
        [int]$Trial,
        [string[]]$GateList
    )
    foreach ($gate in $GateList) {
        $log = Join-Path $ReportDir ("trial{0:D2}_{1}.log" -f $Trial, $gate)
        if (-not (Test-Path $log)) { return $false }
    }
    $lastLog = Join-Path $ReportDir ("trial{0:D2}_{1}.log" -f $Trial, $GateList[-1])
    $tail = Get-Content -Path $lastLog -Tail 30 -ErrorAction SilentlyContinue
    return ($tail -match '\[  PASSED  \]')
}

function Get-StreakResumeState {
    param(
        [string]$ReportDir,
        [int]$TotalTrials,
        [string[]]$GateList
    )
    $results = @()
    $nextTrial = 1
    for ($t = 1; $t -le $TotalTrials; $t++) {
        if (Test-StreakTrialComplete -ReportDir $ReportDir -Trial $t -GateList $GateList) {
            $sec = 0.0
            $statePath = Join-Path $ReportDir ("trial{0:D2}.state.json" -f $t)
            if (Test-Path $statePath) {
                try {
                    $state = Get-Content $statePath -Raw | ConvertFrom-Json
                    $sec = [double]$state.Seconds
                } catch {}
            }
            $results += [pscustomobject]@{
                Trial   = $t
                Passed  = $true
                Seconds = $sec
                Gates   = ($GateList | ForEach-Object { "${_}:OK" }) -join " | "
            }
            $nextTrial = $t + 1
        } else {
            Get-ChildItem $ReportDir -Filter ("trial{0:D2}_*" -f $t) -ErrorAction SilentlyContinue |
                Remove-Item -Force -ErrorAction SilentlyContinue
            break
        }
    }
    return @{ Results = $results; StartTrial = $nextTrial }
}

function Write-StreakSummary {
    param(
        [string]$ReportDir,
        [array]$Rows,
        [int]$TotalTrials,
        [int]$Threshold
    )
    $summaryPath = Join-Path $ReportDir "summary.csv"
    $Rows | Export-Csv -Path $summaryPath -NoTypeInformation
    $passCount = @($Rows | Where-Object { $_.Passed }).Count
    $meta = @{
        updated_at  = (Get-Date).ToString("o")
        trials      = $TotalTrials
        completed   = $Rows.Count
        pass_count  = $passCount
        min_pass    = $Threshold
        status      = if ($passCount -ge $Threshold) { "ACCEPT" } elseif ($Rows.Count -ge $TotalTrials) { "REJECT" } else { "IN_PROGRESS" }
    } | ConvertTo-Json
    Set-Content -Path (Join-Path $ReportDir "summary.meta.json") -Value $meta
    return @{ SummaryPath = $summaryPath; PassCount = $passCount }
}

if ($CleanOnly) {
    $keep = if ($ResumeDir) { (Resolve-Path $ResumeDir).Path } else { "" }
    $gb = Clear-EbtreeGateRunCaches -Root $RepoRoot -KeepStreakDir $keep
    Write-Host "cleared gate run caches (~${gb} GB)"
    exit 0
}

if ([string]::IsNullOrWhiteSpace($ResumeDir)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $reportDir = Join-Path $RepoRoot ".test-runs/streak-$stamp"
    New-Item -ItemType Directory -Force -Path $reportDir | Out-Null
    $results = @()
    $startTrial = 1
} else {
    $reportDir = (Resolve-Path $ResumeDir).Path
    $resume = Get-StreakResumeState -ReportDir $reportDir -TotalTrials $Trials -GateList $Gates
    $results = @($resume.Results)
    $startTrial = [int]$resume.StartTrial
    Write-Host "resume: $reportDir (completed $($results.Count), start trial $startTrial)"
}

Write-Host "== gate streak: trials=$Trials minPass=$MinPass config=$Config =="
Write-Host "gates: $($Gates -join ' -> ')"
Write-Host "report: $reportDir"

if ($startTrial -gt $Trials) {
    $summary = Write-StreakSummary -ReportDir $reportDir -Rows $results -TotalTrials $Trials -Threshold $MinPass
    Write-Host "already complete: $($summary.PassCount)/$Trials passed"
    exit $(if ($summary.PassCount -ge $MinPass) { 0 } else { 1 })
}

Clear-EbtreeGateRunCaches -Root $RepoRoot -KeepStreakDir $reportDir | Out-Null

for ($t = $startTrial; $t -le $Trials; $t++) {
    $gb = Clear-EbtreeGateRunCaches -Root $RepoRoot -KeepStreakDir $reportDir
    if ($gb -gt 0) { Write-Host "pre-trial cache clear: ~${gb} GB" }

    $trialOk = $true
    $trialStart = Get-Date
    $gateResults = @()

    foreach ($gate in $Gates) {
        $log = Join-Path $reportDir ("trial{0:D2}_{1}.log" -f $t, $gate)
        Write-Host ""
        Write-Host "[$t/$Trials] gate=$gate -> $log"
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        $errLog = "$log.err"
        $args = @(
            "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $runTests,
            "-RepoRoot", $RepoRoot, "-BuildDir", $BuildDir, "-Config", $Config, "-Gate", $gate
        )
        $proc = Start-Process -FilePath "powershell.exe" -ArgumentList $args `
            -RedirectStandardOutput $log -RedirectStandardError $errLog `
            -Wait -PassThru -NoNewWindow
        $code = $proc.ExitCode
        if (Test-Path $errLog) {
            $errText = Get-Content -Path $errLog -Raw -ErrorAction SilentlyContinue
            if ($errText) {
                Add-Content -Path $log -Value "`n--- stderr ---`n$errText"
            }
        }
        Get-Content -Path $log -Tail 5 -ErrorAction SilentlyContinue | Out-Host
        $sw.Stop()
        $passed = ($code -eq 0)
        $hang = ($code -eq 124)
        if (-not $passed) { $trialOk = $false }
        $gateResults += [pscustomobject]@{
            Gate   = $gate
            Passed = $passed
            Exit   = $code
            Hang   = $hang
            Sec    = [math]::Round($sw.Elapsed.TotalSeconds, 1)
        }
        if ($hang) {
            @{
                status     = "HANG"
                trial      = $t
                gate       = $gate
                exit       = $code
                updated_at = (Get-Date).ToString("o")
            } | ConvertTo-Json | Set-Content -Path (Join-Path $reportDir "summary.meta.json")
            Write-Host "HANG: gate=$gate exit=124 — fix root cause before resume"
            break
        }
        if (-not $passed) { break }
    }

    $trialSec = [math]::Round(((Get-Date) - $trialStart).TotalSeconds, 1)
    $row = [pscustomobject]@{
        Trial   = $t
        Passed  = $trialOk
        Seconds = $trialSec
        Gates   = ($gateResults | ForEach-Object {
            "{0}:{1}" -f $_.Gate, ($(if ($_.Passed) { "OK" } else { "FAIL($($_.Exit))" }))
        }) -join " | "
    }
    $results += $row
    @{
        trial   = $t
        passed  = $trialOk
        seconds = $trialSec
        gates   = $row.Gates
        at      = (Get-Date).ToString("o")
    } | ConvertTo-Json | Set-Content -Path (Join-Path $reportDir ("trial{0:D2}.state.json" -f $t))

    $summary = Write-StreakSummary -ReportDir $reportDir -Rows $results -TotalTrials $Trials -Threshold $MinPass
    $status = if ($trialOk) { "PASS" } else { "FAIL" }
    Write-Host "[$t/$Trials] $status (${trialSec}s) cumulative=$($summary.PassCount)/$($results.Count)"

    $gb = Clear-EbtreeGateRunCaches -Root $RepoRoot -KeepStreakDir $reportDir
    if ($gb -gt 0) { Write-Host "post-trial cache clear: ~${gb} GB" }
}

$passCount = @($results | Where-Object { $_.Passed }).Count
Write-Host ""
Write-Host "== streak summary =="
Write-Host "passed: $passCount / $($results.Count) completed (need >= $MinPass of $Trials)"
$results | Format-Table -AutoSize | Out-String | Write-Host
Write-Host "csv: $(Join-Path $reportDir 'summary.csv')"

if ($results.Count -lt $Trials) {
    Write-Host "PAUSED: resume with -ResumeDir '$reportDir'"
    exit 2
}

if ($passCount -ge $MinPass) {
    Write-Host "ACCEPT: streak meets ADR-044 threshold ($passCount >= $MinPass)"
    exit 0
}
Write-Host "REJECT: streak below ADR-044 threshold ($passCount < $MinPass)"
exit 1
