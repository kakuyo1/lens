# Run one case, or one suite, out of the two QTest targets -- offscreen, against a fresh build.
#
# The suites are the slowest part of the loop: lens_qtest_surfaces is 188 cases and about two
# minutes, where a single case is about five seconds. Which of the two to run follows from what
# changed, and TEST.md section 2 owns that. This script is here so that choosing the narrow one
# costs nothing: it reads the Qt bin from the merged machine config (config/paths.default.json, overridden
# by config/paths.local.json), sets QT_QPA_PLATFORM=offscreen, builds the target (incremental, a
# few seconds) so a stale binary cannot answer for the code, and then forwards the case name to
# the runner.
#
# -Case takes what the runner takes: `Main::test_x` or the bare `test_x`. A class on its own is not
# one of those, and `-Case Main` is the quiet way to run nothing: the runner prints "Could not find
# the following test functions" and exits green. Without -Case the whole suite runs, which is what
# a hand-over wants and an edit does not.
#
# Examples. The powershell prefix is the one that works from Git Bash, where these are typed.
#   powershell -File scripts/qa/qtest.ps1 -Target lens_qtest_surfaces -Case Main::test_hover
#   powershell -File scripts/qa/qtest.ps1 -Target lens_qtest_components

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("lens_qtest_components", "lens_qtest_surfaces")]
    [string]$Target,
    # A test-function selector, forwarded to the runner as-is.
    [string]$Case,
    # The Qt the tree runs against. Defaults to qtBin in config/paths.default.json.
    [string]$QtBin,
    # The Debug tree, which is where the assertions and the PDB are.
    [string]$BuildDir = "build-ninja"
)

$ErrorActionPreference = "Stop"

function Get-RepositoryRoot {
    $root = (& git rev-parse --show-toplevel 2>$null)
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($root)) {
        throw "qtest: run this script inside the repository"
    }
    return $root.Trim()
}

$root = Get-RepositoryRoot
. (Join-Path $root "scripts/build/paths.ps1")

if ([string]::IsNullOrWhiteSpace($QtBin)) {
    $QtBin = (Get-LensPaths -Root $root).qtBin
}
if ([string]::IsNullOrWhiteSpace($QtBin)) {
    throw "qtest: the merged config carries no qtBin, and -QtBin was not given; see scripts/build/show-paths.ps1"
}
if (-not (Test-Path -LiteralPath (Join-Path $QtBin "Qt6Core.dll")) -and
    -not (Test-Path -LiteralPath (Join-Path $QtBin "Qt6Cored.dll"))) {
    throw "qtest: no Qt6Core DLL in '$QtBin'; the merged machine paths are probably stale -- see scripts/build/show-paths.ps1"
}

Push-Location $root
try {
    & (Join-Path $root "scripts/build/build.bat") --target $Target | Select-Object -Last 3
    if ($LASTEXITCODE -ne 0) { throw "qtest: the $Target build failed" }
} finally {
    Pop-Location
}

$exe = Join-Path $root "$BuildDir/test/qtest/$Target.exe"
if (-not (Test-Path -LiteralPath $exe)) { throw "qtest: $exe is not there after the build" }

$env:PATH = "$QtBin;$env:PATH"
$env:QT_QPA_PLATFORM = "offscreen"
$env:QT_FORCE_STDERR_LOGGING = "1"

if ([string]::IsNullOrWhiteSpace($Case)) {
    Write-Output "running $Target (the whole suite)"
    & $exe
} else {
    Write-Output "running $Target::$Case"
    & $exe $Case
}

# The verdict is what the caller needs, and a QTest run reports it in its exit code.
exit $LASTEXITCODE
