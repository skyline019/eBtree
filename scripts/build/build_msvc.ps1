# eB-Tree MSVC (Visual Studio 2026) build script.
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts/build/build_msvc.ps1
#   powershell -ExecutionPolicy Bypass -File scripts/build/build_msvc.ps1 -Config Release -Target ebtree_test_runner

param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path,
    [string]$BuildDir = "",
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "All")]
    [string]$Config = "Debug",
    [string]$Target = "ebtree_test_runner",
    [int]$Jobs = 8,
    [string]$Generator = "",
    [switch]$Reconfigure
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $RepoRoot "build-msvc-2026"
}

function Find-VsWhere {
    $candidates = @(
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe",
        "D:\Microsoft Visual Studio\Installer\vswhere.exe"
    )
    foreach ($path in $candidates) {
        if (Test-Path $path) { return $path }
    }
    return $null
}

function Find-VsInstance {
    $vswhere = Find-VsWhere
    if ($vswhere) {
        $inst = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath 2>$null
        if ($inst) { return $inst.Trim() }
    }
    if ($env:VSINSTALLDIR -and (Test-Path $env:VSINSTALLDIR)) {
        return $env:VSINSTALLDIR.TrimEnd('\')
    }
    return $null
}

function Resolve-VsGenerator {
    if ($Generator) { return $Generator }
    if ($env:EBTREE_VS_GENERATOR) { return $env:EBTREE_VS_GENERATOR }

    $vswhere = Find-VsWhere
    if ($vswhere) {
        $line = & $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property catalog_productLineVersion 2>$null
        if ($line -match '^\s*18') { return "Visual Studio 18 2026" }
        if ($line -match '^\s*17') { return "Visual Studio 17 2022" }
    }
    return "Visual Studio 17 2022"
}

function Resolve-PythonExecutable {
    if ($env:Python3_EXECUTABLE -and (Test-Path $env:Python3_EXECUTABLE)) {
        return $env:Python3_EXECUTABLE
    }
    $py = Get-Command python -ErrorAction SilentlyContinue
    if ($py -and $py.Source -notmatch 'WindowsApps\\python') {
        return $py.Source
    }
    return $null
}

function Invoke-EbtreeBuild {
    param([string]$Configuration, [string[]]$Targets)
    foreach ($t in $Targets) {
        Write-Host ">> cmake --build --config $Configuration --target $t"
        cmake --build $BuildDir --config $Configuration --target $t -j $Jobs
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
}

Write-Host "== eB-Tree MSVC build =="
Write-Host "Repo:   $RepoRoot"
Write-Host "Build:  $BuildDir"
Write-Host "Config: $Config"
Write-Host "Target: $Target"

$needsConfigure = $Reconfigure -or -not (Test-Path (Join-Path $BuildDir "CMakeCache.txt"))
if ($needsConfigure) {
    if (-not (Test-Path $BuildDir)) {
        New-Item -ItemType Directory -Path $BuildDir | Out-Null
    }
    $vsInst = Find-VsInstance
    $extraArgs = @()
    if ($vsInst) {
        Write-Host "VS install: $vsInst"
        $extraArgs += "-DCMAKE_GENERATOR_INSTANCE=$vsInst"
    }
    $vsGenerator = Resolve-VsGenerator
    $cmakeArgs = @(
        "-S", $RepoRoot,
        "-B", $BuildDir,
        "-G", $vsGenerator,
        "-A", "x64",
        "-DEBTREE_BUILD_TESTS=ON",
        "-DGTEST_CAPI_MSVC_STATIC_RUNTIME=ON"
    )
    $pythonExe = Resolve-PythonExecutable
    if ($pythonExe) {
        $cmakeArgs += "-DPython3_EXECUTABLE=$pythonExe"
    }
    $cmakeArgs += $extraArgs
    Write-Host ">> cmake configure ($vsGenerator, x64, /MT)"
    & cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$targets = @($Target)
if ($Config -eq "All") {
    foreach ($c in @("Debug", "Release")) {
        Write-Host "--- $c ---"
        Invoke-EbtreeBuild -Configuration $c -Targets $targets
    }
} else {
    Invoke-EbtreeBuild -Configuration $Config -Targets $targets
}

Write-Host "Done."
