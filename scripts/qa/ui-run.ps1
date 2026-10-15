# Launch the release build, so a surface can be looked at, driven, or photographed.
#
# The surfaces have no main window and appear only on a trigger, so looking at one means running
# the real application and driving its pointer (TEST.md section 5). Done by hand that is a
# handful of steps that fail quietly: three lens.exe copies exist and only one is current, the Qt
# bin has to be on PATH, a launch has to outlive the shell that started it, and the log's path
# depends on the working directory. This script is that recipe in one place.
#
# Two things it refuses to do. It will not start a second instance -- a second copy answers the
# mouse hook and the tray icon while the first one still holds them -- and it will not launch an
# executable older than src/, so nobody verifies yesterday's build. A stale or missing lens.exe
# rebuilds the release tree first; -Build asks for that unconditionally.
#
# The glass surfaces hide themselves from screen capture (docs/adr/0038, 0041), so
# scripts/qa/ui-capture.ps1 photographs an empty desktop unless the app itself was started with
# LENS_QA_VISIBLE_IN_CAPTURE set. Set it on this call, not on the capture.
#
# Examples. The explicit powershell form is the one that works from Git Bash, which is where these
# are typed; a bare ./scripts/qa/x.ps1 goes to bash and dies on the first line of PowerShell.
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/qa/ui-run.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/qa/ui-run.ps1 -Restart
#   LENS_QA_VISIBLE_IN_CAPTURE=1 powershell -NoProfile -ExecutionPolicy Bypass -File scripts/qa/ui-run.ps1

[CmdletBinding()]
param(
    # Rebuild the release tree even when its executable is newer than the sources it came from.
    [switch]$Build,
    # Stop a running instance and start a fresh one, instead of refusing to start a second.
    [switch]$Restart,
    # Seconds to wait for the app's own "Lens is up" line.
    [int]$TimeoutSeconds = 30,
    # The Qt the tree runs against. Defaults to qtBin in config/paths.json.
    [string]$QtBin
)

$ErrorActionPreference = "Stop"

$marker = "Lens is up"

function Get-RepositoryRoot {
    $root = (& git rev-parse --show-toplevel 2>$null)
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($root)) {
        throw "ui-run: run this script inside the repository"
    }
    return $root.Trim()
}

# The newest write among the inputs that end up inside the executable: the sources, the
# translations it compiles, and the root CMakeLists.txt that decides how.
function Get-NewestSourceWriteTime([string]$root) {
    $newest = [datetime]::MinValue
    foreach ($relative in @("src", "i18n", "CMakeLists.txt")) {
        $path = Join-Path $root $relative
        if (-not (Test-Path -LiteralPath $path)) { continue }
        $items = if ((Get-Item -LiteralPath $path).PSIsContainer) {
            Get-ChildItem -LiteralPath $path -Recurse -File
        } else {
            Get-Item -LiteralPath $path
        }
        foreach ($item in $items) {
            if ($item.LastWriteTime -gt $newest) { $newest = $item.LastWriteTime }
        }
    }
    return $newest
}

# Everything the file has gained since $offset. Read from the offset rather than from the tail,
# because the previous run's "Lens is up" is still among the last lines: reading that as this
# run's would report a start that never happened.
function Read-Since([string]$path, [long]$offset) {
    if (-not (Test-Path -LiteralPath $path)) { return "" }
    $stream = [System.IO.File]::Open($path, [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    try {
        if ($offset -gt $stream.Length) { $offset = 0 }   # rotated away since it was measured
        [void]$stream.Seek($offset, [System.IO.SeekOrigin]::Begin)
        $reader = New-Object System.IO.StreamReader($stream)
        return $reader.ReadToEnd()
    } finally {
        $stream.Dispose()
    }
}

$root = Get-RepositoryRoot
. (Join-Path $root "scripts/build/paths.ps1")

$exe = Join-Path $root "build-ninja-release/src/app/lens.exe"
$log = Join-Path $root "logs/lens.log"

$running = @(Get-Process -Name lens -ErrorAction SilentlyContinue)
if ($running.Count -gt 0) {
    if (-not $Restart) {
        Write-Output "lens is already running: PID $($running[0].Id). Pass -Restart to replace it."
        return
    }
    Write-Output "stopping PID $($running[0].Id)"
    $running | Stop-Process -Force
    Start-Sleep -Milliseconds 700
}

$reason = $null
if (-not (Test-Path -LiteralPath $exe)) {
    $reason = "build-ninja-release has no lens.exe yet"
} else {
    $built = (Get-Item -LiteralPath $exe).LastWriteTime
    $sourceTime = Get-NewestSourceWriteTime $root
    if ($sourceTime -gt $built) {
        $reason = "lens.exe is from $($built.ToString('yyyy-MM-dd HH:mm:ss')) and a source file is from " +
            "$($sourceTime.ToString('yyyy-MM-dd HH:mm:ss'))"
    }
}
if ($Build -or $reason) {
    if (-not $reason) { $reason = "-Build asked for it" }
    Write-Output "building the release tree: $reason"
    Push-Location $root
    try {
        & (Join-Path $root "scripts/build/build-release.bat") | Select-Object -Last 3
        if ($LASTEXITCODE -ne 0) { throw "ui-run: the release build failed" }
    } finally {
        Pop-Location
    }
}

if ([string]::IsNullOrWhiteSpace($QtBin)) {
    $QtBin = (Get-LensPaths -Root $root).qtBin
}
if ([string]::IsNullOrWhiteSpace($QtBin)) {
    throw "ui-run: config/paths.json carries no qtBin, and -QtBin was not given"
}

$before = if (Test-Path -LiteralPath $log) { (Get-Item -LiteralPath $log).Length } else { 0 }
$env:PATH = "$QtBin;$env:PATH"
$app = Start-Process -FilePath $exe -WorkingDirectory $root -PassThru
Write-Output "started: PID $($app.Id)"

$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
$since = ""
$cameUp = $false
while ((Get-Date) -lt $deadline) {
    # A log that cannot be read right now is not a reason to abandon a process already started:
    # wait out the rest of the window and let the verdict say what was seen.
    try { $since = Read-Since $log $before } catch { $since = "" }
    if ($since -match [regex]::Escape($marker)) { $cameUp = $true; break }
    Start-Sleep -Milliseconds 300
}

Write-Output "exe: $exe"
Write-Output "log: $log   (the working directory is the repository)"
Write-Output "running: $(-not $app.HasExited)"
Write-Output "came up: $cameUp"
if ($since.Trim().Length -gt 0) {
    Write-Output "--- written since it started"
    Write-Output $since.Trim()
}
