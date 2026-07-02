param(
    [Parameter(Mandatory = $true)]
    [string]$Runner,
    [Parameter(Mandatory = $true)]
    [string]$Suite,
    [string]$Filter = "*",
    [string]$LogRoot = "",
    [int]$StaleSec = 180,
    [int]$PollSec = 8,
    [double]$CpuStaleThreshold = 0.25,
    [string]$Label = "gate",
    [switch]$KillProcessTree
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($LogRoot)) {
    $LogRoot = Join-Path (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path ".test-runs"
}
New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null

function Stop-ProcessTreeSafe {
    param([int]$RootPid)
    try {
        $children = Get-CimInstance Win32_Process -Filter "ParentProcessId=$RootPid" -ErrorAction SilentlyContinue
        foreach ($child in $children) {
            Stop-ProcessTreeSafe -RootPid $child.ProcessId
        }
    } catch {}
    Stop-Process -Id $RootPid -Force -ErrorAction SilentlyContinue
}

function Get-LastGtestRunLine {
    param([string]$LogPath)
    if (-not (Test-Path $LogPath)) { return "" }
    $lines = Get-Content $LogPath -ErrorAction SilentlyContinue
    for ($i = $lines.Count - 1; $i -ge 0; $i--) {
        if ($lines[$i] -match '\[\s*RUN\s+\]') { return $lines[$i].Trim() }
    }
    return ""
}

function Write-HangMeta {
    param(
        [string]$MetaPath,
        [hashtable]$Fields
    )
    ($Fields + @{
        updated_at = (Get-Date).ToString("o")
        status     = "HANG"
    }) | ConvertTo-Json -Depth 4 | Set-Content -Path $MetaPath -Encoding UTF8
}

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$log = Join-Path $LogRoot "$Label-$Suite-$stamp.log"
$err = "$log.err"
$hangMeta = Join-Path $LogRoot "$Label-HANG.meta.json"

Write-Host ""
Write-Host "== [$Label] suite=$Suite START =="
Write-Host "    filter=$Filter"
Write-Host "    log=$log"
Write-Host "    stale=${StaleSec}s poll=${PollSec}s cpuThreshold=$CpuStaleThreshold dualSignal=true"

$args = @("--suite=$Suite", "--gtest_filter=$Filter")
$p = Start-Process -FilePath $Runner `
    -ArgumentList $args `
    -RedirectStandardOutput $log `
    -RedirectStandardError $err `
    -PassThru `
    -NoNewWindow

$lastSize = 0L
$lastCpu = $null
$staleSince = $null
$start = Get-Date
$exitCode = $null
$killedStale = $false
$lastCpuDelta = 0.0
$lastLogGrowth = $false

while (-not $p.HasExited) {
    Start-Sleep -Seconds $PollSec
    $elapsed = [math]::Round(((Get-Date) - $start).TotalSeconds, 0)

    $logGrew = $false
    if (Test-Path $log) {
        $size = (Get-Item $log).Length
        if ($size -gt $lastSize) {
            $logGrew = $true
            $allLines = Get-Content $log -ErrorAction SilentlyContinue
            $skip = [math]::Max(0, $allLines.Count - 8)
            foreach ($line in ($allLines | Select-Object -Skip $skip)) {
                if ($line -match '\[( RUN\s+|OK\s+|FAILED\s+|PASSED\s+|==)' ) {
                    Write-Host "    | $line"
                }
            }
            $lastSize = $size
        }
    }
    $lastLogGrowth = $logGrew

    $cpuDelta = $null
    try {
        $proc = Get-Process -Id $p.Id -ErrorAction Stop
        if ($null -ne $lastCpu) {
            $cpuDelta = $proc.CPU - $lastCpu
        }
        $lastCpu = $proc.CPU
    } catch {
        break
    }
    if ($null -ne $cpuDelta) { $lastCpuDelta = $cpuDelta }

    # Dual-signal stale: log must NOT grow AND CPU delta below threshold.
    $cpuStale = ($null -ne $cpuDelta) -and ($cpuDelta -lt $CpuStaleThreshold)
    $dualStale = (-not $logGrew) -and $cpuStale

    if ($dualStale) {
        if (-not $staleSince) { $staleSince = Get-Date }
        $staleFor = [math]::Round(((Get-Date) - $staleSince).TotalSeconds, 0)
        Write-Host "    .. running ${elapsed}s cpuDelta=$([math]::Round($cpuDelta,2)) logGrew=$logGrew staleFor=${staleFor}s"
        if ($staleFor -ge $StaleSec) {
            Write-Host "== [$Label] suite=$Suite STALE (no log+CPU progress ${StaleSec}s) - KILL pid=$($p.Id) =="
            if ($KillProcessTree) {
                Stop-ProcessTreeSafe -RootPid $p.Id
            } else {
                Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
            }
            Get-Process -Name "ebtree_test_runner" -ErrorAction SilentlyContinue |
                Stop-Process -Force -ErrorAction SilentlyContinue
            $logTail = @()
            if (Test-Path $log) {
                $logTail = @(Get-Content $log -Tail 25 -ErrorAction SilentlyContinue)
            }
            Write-HangMeta -MetaPath $hangMeta -Fields @{
                label      = $Label
                suite      = $Suite
                filter     = $Filter
                pid        = $p.Id
                elapsed_sec = $elapsed
                stale_sec  = $StaleSec
                cpu_delta  = [math]::Round($lastCpuDelta, 4)
                log_grew   = $lastLogGrowth
                last_gtest = (Get-LastGtestRunLine -LogPath $log)
                log        = $log
                log_tail   = ($logTail -join "`n")
            }
            Write-Host "    hang meta: $hangMeta"
            $exitCode = 124
            $killedStale = $true
            break
        }
    } else {
        $staleSince = $null
        $cpuStr = if ($null -eq $cpuDelta) { "n/a" } else { [math]::Round($cpuDelta, 2).ToString() }
        Write-Host "    .. running ${elapsed}s cpuDelta=$cpuStr logGrew=$logGrew"
    }
}

if (-not $killedStale) {
    $p.WaitForExit()
    $exitCode = $p.ExitCode
}
if ($null -eq $exitCode) { $exitCode = 0 }

# Some DLL suites report exit 0 despite gtest failures; honor log markers.
if ($exitCode -eq 0 -and (Test-Path $log)) {
    $logText = Get-Content $log -Raw -ErrorAction SilentlyContinue
    if ($logText -match '\[\s*FAILED\s*\]\s*\d+\s+test') {
        $exitCode = 1
    }
}

Write-Host "== [$Label] suite=$Suite EXIT=$exitCode =="
if (Test-Path $log) {
    Write-Host "-- tail $log --"
    Get-Content $log -Tail 25 | ForEach-Object { Write-Host "    $_" }
}
if ($exitCode -ne 0 -and (Test-Path $err)) {
    $errTail = Get-Content $err -Tail 10 -ErrorAction SilentlyContinue
    if ($errTail) {
        Write-Host "-- stderr --"
        $errTail | ForEach-Object { Write-Host "    $_" }
    }
}

exit $exitCode
