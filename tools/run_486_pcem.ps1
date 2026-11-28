[CmdletBinding()]
param(
    [string]$BuildDirectory = "build/pcem-486"
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildPath = Join-Path $repoRoot $BuildDirectory
$exeCandidates = @(
    (Join-Path $buildPath "src/pcem.exe"),
    (Join-Path $buildPath "src/Release/pcem.exe"),
    (Join-Path $buildPath "src/pcem"),
    (Join-Path $buildPath "src/Release/pcem")
)
$exe = $exeCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $exe) {
    & (Join-Path $PSScriptRoot "build_486_pcem.ps1") -BuildDirectory $BuildDirectory
    if (-not $?) {
        throw "PCem build script failed."
    }
    $exe = $exeCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $exe) {
    throw "PCem executable was not produced. See build output above."
}

$configPath = Join-Path $buildPath "pcem_486.cfg"
if (-not (Test-Path $configPath)) {
    throw "Missing generated machine profile: $configPath. Run build_486_pcem.ps1."
}

# ROMs are intentionally not bundled. PCem will report missing ROMs until
# legally obtained files are placed in the portable .pcem/roms directory.
$exeDirectory = Split-Path $exe -Parent
Start-Process -FilePath $exe -WorkingDirectory $exeDirectory -ArgumentList @("--config", "`"$configPath`"")
