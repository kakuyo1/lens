<#
.SYNOPSIS
    Smoke test for the developer surface (docs/adr/0068): starts a dev build, runs four lines, checks the answers, and
    expects the run to end by itself.

.DESCRIPTION
    Runs the executable with --dev-script, its answers written to a file under logs\dev-smoke, and its settings in a
    file of its own, so the smoke never writes the reader's profile. It fails when a line is refused, when the settings
    window does not paint, when the build does not report the surface, or when the run does not quit within the time
    limit. Only the process this script started is ever stopped.

.PARAMETER Exe
    The lens.exe built with -DLENS_ENABLE_DEV_SURFACE=ON.

.PARAMETER TimeoutSeconds
    How long the run may take before it is stopped and the smoke fails.
#>
param(
    [string]$Exe = 'build-ninja\src\app\lens.exe',
    [int]$TimeoutSeconds = 90
)

$ErrorActionPreference = 'Stop'

$work = Join-Path (Get-Location) 'logs\dev-smoke'
New-Item -ItemType Directory -Force -Path $work | Out-Null
$scriptFile   = Join-Path $work 'smoke.txt'
$answersFile  = Join-Path $work 'answers.jsonl'
$settingsFile = Join-Path $work 'settings.json'
Remove-Item -LiteralPath $answersFile, $settingsFile -ErrorAction SilentlyContinue

# Four lines: show the settings window, wait for its first frame, read the state, quit.
@(
    'surface show settings',
    'wait painted:settings',
    'state',
    'quit'
) | Set-Content -LiteralPath $scriptFile -Encoding UTF8

$exePath = (Resolve-Path -LiteralPath $Exe).Path
$process = Start-Process -FilePath $exePath -PassThru -ArgumentList @(
    "--dev-script=$scriptFile",
    "--dev-out=$answersFile",
    "--settings-file=$settingsFile"
)

if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
    Stop-Process -Id $process.Id -Force
    throw "smoke: lens did not quit within $TimeoutSeconds s; the process it started was stopped"
}
if ($process.ExitCode -ne 0) {
    throw "smoke: lens exited with code $($process.ExitCode)"
}

$answers = @(Get-Content -LiteralPath $answersFile | ForEach-Object { $_ | ConvertFrom-Json })
if ($answers.Count -ne 4) {
    throw "smoke: expected 4 answers, got $($answers.Count)"
}
foreach ($answer in $answers) {
    if (-not $answer.ok) {
        throw "smoke: '$($answer.cmd)' was refused: $($answer.error)"
    }
}

$state    = $answers[2]
$settings = $state.surfaces | Where-Object { $_.name -eq 'settings' }
if (-not $settings.visible) {
    throw 'smoke: the settings window is not visible after its first frame'
}
if ($settings.painted -lt 1) {
    throw 'smoke: the settings window painted no frame'
}
if ($state.build.devSurface -ne $true) {
    throw 'smoke: the build does not report the developer surface'
}
if ($null -eq $state.theme) {
    throw 'smoke: the state has no theme'
}

Write-Host "smoke: ok -- settings painted $($settings.painted) frame(s), theme $($state.theme), $($state.windows.total) windows"
