[CmdletBinding()]
param(
    [string]$BuildDirectory = "build/pcem-486",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$source = Join-Path $repoRoot "third_party/pcem-full"
$buildPath = Join-Path $repoRoot $BuildDirectory

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Program failed with exit code $LASTEXITCODE"
    }
}

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw "Git is required to initialize the pinned PCem source submodule."
}
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "CMake is required. Install CMake and ensure it is on PATH."
}

$submoduleMarker = Join-Path $source ".git"
if (-not (Test-Path $submoduleMarker)) {
    Invoke-Checked "git" @("-C", $repoRoot, "submodule", "update", "--init", "--recursive", "third_party/pcem-full")
}
if (-not (Test-Path (Join-Path $source "CMakeLists.txt"))) {
    throw "PCem source is missing at $source. Check submodule initialization."
}

if ($Clean -and (Test-Path $buildPath)) {
    Remove-Item -Recurse -Force $buildPath
}

New-Item -ItemType Directory -Force -Path $buildPath | Out-Null
$cmakeArgs = @(
    "-S", $source,
    "-B", $buildPath,
    "-DCMAKE_BUILD_TYPE=Release",
    "-DPCEM_DISPLAY_ENGINE=SDL",
    "-DPLUGIN_ENGINE=OFF",
    "-DUSE_NETWORKING=OFF",
    "-DUSE_PCAP_NETWORKING=OFF"
)
if (Get-Command ninja -ErrorAction SilentlyContinue) {
    $cmakeArgs += @("-G", "Ninja")
}

Write-Host "[486/PCem] Configure standalone PCem build"
Invoke-Checked "cmake" $cmakeArgs
Write-Host "[486/PCem] Build PCem"
Invoke-Checked "cmake" @("--build", $buildPath, "--config", "Release", "--parallel")

$exe = Join-Path $buildPath "src/pcem.exe"
if (-not (Test-Path $exe)) {
    $exe = Join-Path $buildPath "src/pcem"
}
if (-not (Test-Path $exe)) {
    throw "Build finished but the PCem executable was not found under $buildPath/src."
}
Write-Host "[486/PCem] Executable: $exe"
