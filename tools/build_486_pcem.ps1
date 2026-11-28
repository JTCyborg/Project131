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

function Find-Tool([string]$Name, [string[]]$CandidatePaths) {
    $fromPath = Get-Command $Name -ErrorAction SilentlyContinue
    if ($fromPath -and $fromPath.Source) { return $fromPath.Source }
    foreach ($candidate in $CandidatePaths) {
        if (Test-Path $candidate -PathType Leaf) { return $candidate }
    }
    return $null
}

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw "Git is required. Install Git for Windows and ensure git.exe is on PATH."
}
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "CMake is required. Install CMake and ensure cmake.exe is on PATH."
}

$cmakeGeneratorArgs = @()
if ($env:OS -eq "Windows_NT") {
    # Search PATH first, then common MSYS2/MinGW/LLVM installation locations.
    $gccPaths = @(
        "C:\msys64\ucrt64\bin\gcc.exe",
        "C:\msys64\mingw64\bin\gcc.exe",
        "C:\msys64\clang64\bin\gcc.exe",
        "C:\mingw64\bin\gcc.exe",
        "C:\MinGW\bin\gcc.exe",
        "C:\tools\mingw64\bin\gcc.exe"
    )
    $gxxPaths = @(
        "C:\msys64\ucrt64\bin\g++.exe",
        "C:\msys64\mingw64\bin\g++.exe",
        "C:\msys64\clang64\bin\g++.exe",
        "C:\mingw64\bin\g++.exe",
        "C:\MinGW\bin\g++.exe",
        "C:\tools\mingw64\bin\g++.exe"
    )
    $clangPaths = @(
        "C:\Program Files\LLVM\bin\clang.exe",
        "C:\LLVM\bin\clang.exe",
        "C:\msys64\clang64\bin\clang.exe"
    )
    $clangxxPaths = @(
        "C:\Program Files\LLVM\bin\clang++.exe",
        "C:\LLVM\bin\clang++.exe",
        "C:\msys64\clang64\bin\clang++.exe"
    )
    $ninjaPaths = @(
        "C:\Program Files\CMake\bin\ninja.exe",
        "C:\Program Files\LLVM\bin\ninja.exe",
        "C:\msys64\ucrt64\bin\ninja.exe",
        "C:\msys64\mingw64\bin\ninja.exe",
        "C:\tools\ninja\ninja.exe"
    )

    $gcc = Find-Tool "gcc" $gccPaths
    $gxx = Find-Tool "g++" $gxxPaths
    $clang = Find-Tool "clang" $clangPaths
    $clangxx = Find-Tool "clang++" $clangxxPaths

    if ($gcc -and $gxx) {
        $cCompiler = $gcc
        $cxxCompiler = $gxx
        Write-Host "[486/PCem] Using GCC toolchain: $cCompiler"
    } elseif ($clang -and $clangxx) {
        $cCompiler = $clang
        $cxxCompiler = $clangxx
        Write-Host "[486/PCem] Using LLVM Clang toolchain: $cCompiler"
    } else {
        throw @"
No supported C/C++ compiler was found.
Install MSYS2 with the MinGW-w64 UCRT64 toolchain, then run:
  C:\msys64\ucrt64\bin\gcc.exe
  C:\msys64\ucrt64\bin\g++.exe
Recommended MSYS2 packages: mingw-w64-ucrt-x86_64-gcc, mingw-w64-ucrt-x86_64-cmake,
mingw-w64-ucrt-x86_64-ninja, mingw-w64-ucrt-x86_64-SDL2, mingw-w64-ucrt-x86_64-openal.
Alternatively install LLVM Clang and Ninja. Add their bin directories to PATH,
or install MSYS2 in C:\msys64; this script checks the standard locations.
"@
    }

    $compilerBin = Split-Path $cCompiler -Parent
    if (($env:PATH -split ';') -notcontains $compilerBin) {
        $env:PATH = "$compilerBin;$env:PATH"
    }

    $ninja = Find-Tool "ninja" $ninjaPaths
    if (-not $ninja) {
        throw "Ninja was not found. Install mingw-w64-ucrt-x86_64-ninja (MSYS2) or add ninja.exe to PATH."
    }
    $ninjaBin = Split-Path $ninja -Parent
    if (($env:PATH -split ';') -notcontains $ninjaBin) {
        $env:PATH = "$ninjaBin;$env:PATH"
    }

    $cmakeGeneratorArgs = @(
        "-G", "Ninja",
        "-DCMAKE_C_COMPILER=$cCompiler",
        "-DCMAKE_CXX_COMPILER=$cxxCompiler"
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
$wxCompatDir = Join-Path $repoRoot "cmake/wxwidgets-msys2"
if (-not (Test-Path (Join-Path $wxCompatDir "FindwxWidgets.cmake"))) {
    throw "Missing wxWidgets compatibility module: $wxCompatDir"
}

# CMake embeds CMAKE_MODULE_PATH in try_run() probe projects. Use forward
# slashes so Windows drive paths are not parsed as CMake escape sequences.
$wxCompatDirCMake = $wxCompatDir.Replace("\\", "/")
$sourceCMake = $source.Replace("\\", "/")
$buildPathCMake = $buildPath.Replace("\\", "/")

$cmakeArgs = @(
    "-S", $sourceCMake,
    "-B", $buildPathCMake,
    "-DCMAKE_BUILD_TYPE=Release",
    "-DPCEM_DISPLAY_ENGINE=wxWidgets",
    "-DCMAKE_MODULE_PATH=$wxCompatDirCMake",
    "-DPLUGIN_ENGINE=OFF",
    "-DUSE_NETWORKING=OFF",
    "-DUSE_PCAP_NETWORKING=OFF"
)

# Prefer the wxWidgets CMake package shipped by the selected MinGW toolchain.
$wxConfigCandidates = @(
    (Join-Path (Split-Path $compilerBin -Parent) "lib/cmake/wxWidgets-3.2"),
    "C:\msys64\ucrt64\lib\cmake\wxWidgets-3.2"
)
$wxConfigDir = $wxConfigCandidates | Where-Object {
    Test-Path (Join-Path $_ "wxWidgetsConfig.cmake")
} | Select-Object -First 1
if ($wxConfigDir) {
    $cmakeArgs += "-DwxWidgets_DIR=$($wxConfigDir.Replace("\\", "/"))"
} else {
    throw "wxWidgets CMake package was not found. Install the wxWidgets package for the selected MinGW toolchain."
}
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
