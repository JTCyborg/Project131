[CmdletBinding()]
param(
    [string]$BuildDirectory = "build/pcem-486"
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildPath = Join-Path $repoRoot $BuildDirectory
$exe = Join-Path $buildPath "src/pcem.exe"
if (-not (Test-Path $exe)) {
    $exe = Join-Path $buildPath "src/pcem"
}
if (-not (Test-Path $exe)) {
    & (Join-Path $PSScriptRoot "build_486_pcem.ps1") -BuildDirectory $BuildDirectory
    if ($LASTEXITCODE -ne 0) {
        throw "PCem build script failed."
    }
    $exe = Join-Path $buildPath "src/pcem.exe"
    if (-not (Test-Path $exe)) {
        $exe = Join-Path $buildPath "src/pcem"
    }
}
if (-not (Test-Path $exe)) {
    throw "PCem executable was not produced. See build output above."
}

# PCem opens its machine/configuration UI. Select the Award SiS 496/497
# 486 machine profile documented in docs/atm-486-module/PCEM_MACHINE_PROFILE.md.
Start-Process -FilePath $exe -WorkingDirectory $repoRoot
