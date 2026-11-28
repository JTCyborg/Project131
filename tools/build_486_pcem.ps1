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

$cmakeGeneratorArgs = @()
if ($env:OS -eq "Windows_NT") {
    $gcc = Get-Command gcc -ErrorAction SilentlyContinue
    $gxx = Get-Command g++ -ErrorAction SilentlyContinue
    if (-not $gcc -or -not $gxx) {
        throw "PCem's Windows build requires MinGW-w64 GCC/G++ and Ninja. Install them and ensure gcc, g++, and ninja are on PATH."
    }
    if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
        throw "Ninja is required for the MinGW-w64 PCem build on Windows."
    }
    $cmakeGeneratorArgs = @(
        "-G", "Ninja",
        "-DCMAKE_C_COMPILER=$($gcc.Source)",
        "-DCMAKE_CXX_COMPILER=$($gxx.Source)"
    )
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
if ($cmakeGeneratorArgs.Count -gt 0) {
    $cmakeArgs += $cmakeGeneratorArgs
} elseif (Get-Command ninja -ErrorAction SilentlyContinue) {
    $cmakeArgs += @("-G", "Ninja")
}

Write-Host "[486/PCem] Configure standalone PCem build"
Invoke-Checked "cmake" $cmakeArgs
Write-Host "[486/PCem] Build PCem"
Invoke-Checked "cmake" @("--build", $buildPath, "--config", "Release", "--parallel")

$exeCandidates = @(
    (Join-Path $buildPath "src/pcem.exe"),
    (Join-Path $buildPath "src/Release/pcem.exe"),
    (Join-Path $buildPath "src/pcem"),
    (Join-Path $buildPath "src/Release/pcem")
)
$exe = $exeCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $exe) {
    throw "Build finished but the PCem executable was not found under $buildPath/src."
}

$configSource = Join-Path $repoRoot "i486/pcem_486.cfg"
if (-not (Test-Path $configSource)) {
    throw "Missing PCem 486 machine profile: $configSource"
}
Copy-Item -Force $configSource (Join-Path $buildPath "pcem_486.cfg")

$portableData = Join-Path (Split-Path $exe -Parent) ".pcem"
$romRoot = Join-Path $portableData "roms"
New-Item -ItemType Directory -Force -Path (Join-Path $romRoot "sis496") | Out-Null
Write-Host "[486/PCem] Executable: $exe"
Write-Host "[486/PCem] ROM directory: $romRoot"
Write-Host "[486/PCem] Machine profile: $(Join-Path $buildPath 'pcem_486.cfg')"
