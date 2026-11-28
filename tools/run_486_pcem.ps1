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

# ROMs are intentionally not bundled. Refuse to start a blank machine when
# the motherboard BIOS or the selected VGA Option ROM is absent.
$exeDirectory = Split-Path $exe -Parent
$romRoot = Join-Path $exeDirectory ".pcem/roms"
$requiredRoms = @(
    (Join-Path $romRoot "sis496/sis496-1.awa"),
    (Join-Path $romRoot "s3virge.bin")
)
$missingRoms = $requiredRoms | Where-Object { -not (Test-Path $_) }
if ($missingRoms) {
    $missingText = ($missingRoms | ForEach-Object { "  - $_" }) -join [Environment]::NewLine
    throw "Required PCem ROM images are missing. Obtain them legally and copy them to:$([Environment]::NewLine)$missingText"
}

$arguments = '--config "{0}"' -f $configPath
Start-Process -FilePath $exe -WorkingDirectory $exeDirectory -ArgumentList $arguments
