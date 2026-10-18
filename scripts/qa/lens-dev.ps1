# Run a developer-surface build of lens.exe from a command script, and print the answers it gives.
#
# A build with -DLENS_ENABLE_DEV_SURFACE=ON (docs/adr/0068) drives its own surfaces from a script
# instead of the mouse. -Script starts one copy with --dev-script, lets it run the script and quit,
# and prints each answer as a JSON line. The answers also go to -Out, so a run stays on disk after
# its window is gone.
#
# Like ui-run.ps1, it refuses to start while another lens.exe is running: a second copy answers the
# mouse hook and the tray icon while the first still holds them. It stops only the copy it started.
#
# Examples. Run through powershell, as ui-run.ps1 is:
#   powershell -File scripts/qa/lens-dev.ps1 -Script scripts/qa/scenarios/two.txt
#   powershell -File scripts/qa/lens-dev.ps1 -Script probe.txt -Out logs/probe.jsonl

[CmdletBinding()]
param(
    # The script to run: one command per line. A line starting with # is a comment.
    [Parameter(Mandatory)]
    [string]$Script,
    # Where the answers go, one JSON line each. Defaults to a timestamped file under logs/.
    [string]$Out,
    # The dev-surface executable. Defaults to the dev-surface build tree.
    [string]$Exe,
    # Seconds to wait for the copy to quit on its own before it is stopped.
    [int]$TimeoutSeconds = 60
)

$ErrorActionPreference = "Stop"

function Get-RepositoryRoot {
    $root = (& git rev-parse --show-toplevel 2>$null)
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($root)) {
        throw "lens-dev: run this script inside the repository"
    }
    return $root.Trim()
}

$root = Get-RepositoryRoot
. (Join-Path $root "scripts/build/paths.ps1")
$paths = Get-LensPaths -Root $root

if (-not $Exe) { $Exe = Join-Path $root "build-ninja/src/app/lens.exe" }
if (-not (Test-Path -LiteralPath $Exe)) {
    throw "lens-dev: $Exe is not built; configure with -DLENS_ENABLE_DEV_SURFACE=ON and build lens"
}

$running = @(Get-Process -Name lens -ErrorAction SilentlyContinue)
if ($running.Count -gt 0) {
    throw "lens-dev: lens is already running (PID $($running[0].Id)); a second copy answers the mouse hook and the tray icon"
}

$scriptPath = (Resolve-Path -LiteralPath $Script).Path
if (-not $Out) { $Out = Join-Path $root ("logs/dev-surface-{0:yyyyMMdd-HHmmss}.jsonl" -f (Get-Date)) }
if (-not [System.IO.Path]::IsPathRooted($Out)) { $Out = Join-Path $root $Out }
$outPath = [System.IO.Path]::GetFullPath($Out)
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $outPath) | Out-Null

$started = Get-Date
$previousPath = $env:PATH
$env:PATH = "$($paths.qtBin);$env:PATH"
try {
    $arguments = "--dev-script=`"$scriptPath`" --dev-out=`"$outPath`""
    $process = Start-Process -FilePath $Exe -ArgumentList $arguments -WorkingDirectory $root -PassThru
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $process.Id -Force
        throw "lens-dev: no quit after $TimeoutSeconds s; stopped PID $($process.Id)"
    }
} finally {
    $env:PATH = $previousPath
}

$answered = (Test-Path -LiteralPath $outPath) -and ((Get-Item -LiteralPath $outPath).LastWriteTime -ge $started)
if (-not $answered) {
    throw "lens-dev: no answers at $outPath; is this executable built with LENS_ENABLE_DEV_SURFACE?"
}

$lines = @(Get-Content -LiteralPath $outPath)
$lines
$refused = @($lines | Where-Object { $_ -match '"ok":false' }).Count
Write-Output "lens-dev: $($lines.Count) answers, $refused failed; written to $outPath"
