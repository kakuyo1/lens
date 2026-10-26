# Run one GoogleTest target out of the Debug tree -- a filter, or the whole target -- with the Qt
# bin and environment the binary needs.
#
# The Qt bin comes from config/paths.json merged with config/paths.local.json, through the same
# scripts/build/paths.ps1 the build reads. Never copy qtRoot out of config/paths.json by hand:
# that file is the committed template, and a machine that differs overrides it in
# paths.local.json. scripts/build/show-paths.ps1 prints the merged values and their source.
#
# The Qt bin is checked before the build and the run, because when it is wrong the Windows loader
# can answer with an unrelated api-ms-win-core-* DLL instead of Qt6Core.dll, which sends the
# reader hunting through the OS rather than the config.
#
#   powershell -File scripts/qa/gtest.ps1 -Filter 'PetThrow*'
#   powershell -File scripts/qa/gtest.ps1 -Target lens_gtest_integration

[CmdletBinding()]
param(
    # A target from test/googletest/CMakeLists.txt.
    [string]$Target = "lens_gtest_unit",
    # Forwarded as --gtest_filter. Empty runs the whole target.
    [string]$Filter,
    # The merged qtBin, by default.
    [string]$QtBin,
    # The Debug tree, which is where the assertions and the PDB are.
    [string]$BuildDir = "build-ninja"
)

$ErrorActionPreference = "Stop"

function Get-RepositoryRoot {
    $root = (& git rev-parse --show-toplevel 2>$null)
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($root)) {
        throw "gtest: run this script inside the repository"
    }
    return $root.Trim()
}

$root = Get-RepositoryRoot
. (Join-Path $root "scripts/build/paths.ps1")

if ([string]::IsNullOrWhiteSpace($QtBin)) {
    $QtBin = (Get-LensPaths -Root $root).qtBin
}
if ([string]::IsNullOrWhiteSpace($QtBin)) {
    throw "gtest: the merged config carries no qtBin; see scripts/build/show-paths.ps1"
}
if (-not (Test-Path -LiteralPath (Join-Path $QtBin "Qt6Core.dll")) -and
    -not (Test-Path -LiteralPath (Join-Path $QtBin "Qt6Cored.dll"))) {
    throw "gtest: no Qt6Core DLL in '$QtBin'; the merged machine paths are probably stale -- see scripts/build/show-paths.ps1"
}

Push-Location $root
try {
    & (Join-Path $root "scripts/build/build.bat") --target $Target | Select-Object -Last 3
    if ($LASTEXITCODE -ne 0) { throw "gtest: the $Target build failed" }
} finally {
    Pop-Location
}

$exe = Join-Path $root "$BuildDir/test/googletest/$Target.exe"
if (-not (Test-Path -LiteralPath $exe)) { throw "gtest: $exe is not there after the build" }

$env:PATH = "$QtBin;$env:PATH"
$env:QT_FORCE_STDERR_LOGGING = "1"

if ([string]::IsNullOrWhiteSpace($Filter)) {
    Write-Output "running $Target (the whole target)"
    & $exe
} else {
    Write-Output "running $Target --gtest_filter=$Filter"
    & $exe "--gtest_filter=$Filter"
}

# The verdict is what the caller needs, and GoogleTest reports it in its exit code.
exit $LASTEXITCODE
