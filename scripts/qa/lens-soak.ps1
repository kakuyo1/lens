<#
.SYNOPSIS
    Drive one dev-surface build through a repeated workload and sample its resource curve.

.DESCRIPTION
    The two gaps the 2026-10-05 leak report named and left open: "surfaces have no scripted
    open/close driver" and "long running only means ten minutes". This harness closes both.
    It starts a LENS_ENABLE_DEV_SURFACE build with --dev-pipe, replays one scenario file as
    many times as asked, and writes one CSV row per round with the timing of the round beside
    the process counters. One run therefore answers two questions at once:

      performance  -- round_ms is what a round of work costs, and the dev answers say which
                      command refused or timed out;
      memory       -- private_mb / threads / handles across rounds say whether the water line
                      comes back down, or climbs a step that never returns.

    A round is a scenario file. The three shipped with it are scripts/qa/scenarios/
    soak-surfaces.txt (the seven surfaces the dev face can raise), scan-frames.txt (eight
    injected scan frames) and glass-settings.txt (ten setter writes). Scripts are also valid
    on their own with lens-dev.ps1, so a single round can be checked before a long soak.

    The pipe keeps ONE client for the whole run. DevPipe refuses a second client, so this
    harness samples the process directly (Get-Process) instead of running lens-measure.ps1
    beside it. Use lens-measure.ps1 for the separate case where nothing drives the app.
    A read timeout on that pipe turns a wedged command into a named failure instead of a
    silent multi-hour hang. The harness also writes every question and answer to -DevOut
    itself: the app's --dev-out is only wired on its --dev-script path, not on --dev-pipe.

    Reading the curve: the first painted frame of a window costs a one-off step (about
    +60 MB, +80 threads, +1600 handles per window in the 2026-10-18 report). What matters
    is not that step but the water line AFTER it. -SettleSeconds waits out the 500 ms
    releaseResources() delay and the driver's gradual page return (15 s in that report)
    before each row is taken. -KeepGraphics keeps Qt's defaults in the same binary as a
    control; the two curves are only comparable to each other, never to a release build.

.PARAMETER Exe
    The dev-surface lens.exe. Default: build-ninja-dev/src/app/lens.exe.

.PARAMETER Script
    The scenario file replayed once per round. Comments, blank lines, state and quit lines
    are stripped; the harness sends state itself and sends quit when it is done.

.PARAMETER Out
    CSV to create. Refused when it already exists, so a run never appends to an older one.

.PARAMETER Rounds
    How many times the scenario is replayed. Long soaks use the round count together with
    -IntervalSeconds: 240 rounds at 60 s is four hours.

.PARAMETER Idle
    Drive nothing, only sample. The empty-run control for a tray-resident application.

.PARAMETER KeepGraphics
    Set LENS_KEEP_GRAPHICS=1 for the run, disabling main.cpp's releaseHiddenSurfaces().

.EXAMPLE
    powershell -File scripts/qa/lens-soak.ps1 -Rounds 3 -IntervalSeconds 2 -SettleSeconds 3

.EXAMPLE
    powershell -File scripts/qa/lens-soak.ps1 -Script scripts/qa/scenarios/soak-surfaces.txt `
      -Rounds 240 -IntervalSeconds 60 -Out logs/soak-4h.csv

.EXAMPLE
    powershell -File scripts/qa/lens-soak.ps1 -Idle -Rounds 1440 -IntervalSeconds 10 `
      -Out logs/soak-idle-4h.csv
#>
[CmdletBinding()]
param(
    [string]$Exe = 'build-ninja-dev/src/app/lens.exe',
    [string]$Script = 'scripts/qa/scenarios/soak-surfaces.txt',
    [string]$Out = '',
    [string]$DevOut = '',
    [string]$SettingsFile = '',
    [string]$Pipe = 'lens-soak',
    [int]$Rounds = 20,
    [int]$IntervalSeconds = 10,
    [int]$WarmupSeconds = 15,
    [int]$SettleSeconds = 15,
    [int]$ConnectTimeoutSeconds = 30,
    [int]$ReadTimeoutSeconds = 30,
    [int]$QuitTimeoutSeconds = 20,
    [switch]$Idle,
    [switch]$KeepGraphics
)

$ErrorActionPreference = 'Stop'

function Get-RepositoryRoot {
    $root = (& git rev-parse --show-toplevel 2>$null)
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($root)) {
        throw 'lens-soak: run this script inside the repository'
    }
    return $root.Trim()
}

function Resolve-RepoPath([string]$Path, [string]$Root) {
    if ([System.IO.Path]::IsPathRooted($Path)) { return [System.IO.Path]::GetFullPath($Path) }
    return [System.IO.Path]::GetFullPath((Join-Path $Root $Path))
}

$root = Get-RepositoryRoot
. (Join-Path $root 'scripts/build/paths.ps1')
$paths = Get-LensPaths -Root $root

$exePath = Resolve-RepoPath $Exe $root
$scriptPath = Resolve-RepoPath $Script $root
if (-not (Test-Path -LiteralPath $exePath)) {
    throw "lens-soak: $exePath is not built; configure a tree with -DLENS_ENABLE_DEV_SURFACE=ON"
}
if (-not $Idle -and -not (Test-Path -LiteralPath $scriptPath)) {
    throw "lens-soak: scenario not found: $scriptPath"
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
if ([string]::IsNullOrWhiteSpace($Out)) { $Out = "logs/soak-$stamp.csv" }
if ([string]::IsNullOrWhiteSpace($DevOut)) { $DevOut = "logs/soak-$stamp.jsonl" }
if ([string]::IsNullOrWhiteSpace($SettingsFile)) { $SettingsFile = 'logs/dev-run/soak-settings.json' }
$outPath = Resolve-RepoPath $Out $root
$devOutPath = Resolve-RepoPath $DevOut $root
$settingsPath = Resolve-RepoPath $SettingsFile $root
foreach ($parent in @((Split-Path -Parent $outPath), (Split-Path -Parent $devOutPath), (Split-Path -Parent $settingsPath))) {
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
}
if (Test-Path -LiteralPath $outPath) {
    throw "lens-soak: $outPath already exists; a run never appends to an older curve"
}

$roundLines = @()
if (-not $Idle) {
    $roundLines = @(Get-Content -LiteralPath $scriptPath |
        ForEach-Object { $_.Trim() } |
        Where-Object { $_ -ne '' -and -not $_.StartsWith('#') -and $_ -ne 'quit' -and $_ -ne 'state' })
    if ($roundLines.Count -eq 0) { throw "lens-soak: $scriptPath has no commands to replay" }
}

# lens-dev.ps1 has the same guard: a second copy answers the mouse hook and the tray icon
# while the first one still holds them, and the sampled PID would be the wrong process.
$running = @(Get-Process -Name lens -ErrorAction SilentlyContinue)
if ($running.Count -gt 0) {
    throw "lens-soak: lens is already running (PID $($running[0].Id)); stop it before a soak"
}

$script:process = $null
$script:client = $null
$script:devWriter = $null
$script:devReader = $null
$script:devAnswers = New-Object 'System.Collections.Generic.List[object]'
$script:prevCpu = 0.0
$script:prevAt = Get-Date
$script:begin = Get-Date
$previousKeep = $env:LENS_KEEP_GRAPHICS

function Invoke-Dev([string]$Line) {
    $script:devWriter.WriteLine($Line)
    # ReadLine() blocks forever on a pipe that never answers, and this stream rejects
    # ReadTimeout ("Timeouts are not supported on this stream"), so the ceiling goes on the
    # task instead. A wedged command then names itself instead of hanging a multi-hour soak
    # with no CSV and no error.
    $task = $script:devReader.ReadLineAsync()
    if (-not $task.Wait($ReadTimeoutSeconds * 1000)) {
        throw "lens-soak: no answer to '$Line' within $ReadTimeoutSeconds s"
    }
    $answer = $task.GetAwaiter().GetResult()
    if ($null -eq $answer) { throw "lens-soak: the dev pipe closed while answering '$Line'" }
    # --dev-out is only wired by the app's --dev-script path, not by --dev-pipe, so the
    # harness writes the answers itself. One JSON object per command, one per line.
    $script:devAnswers.Add([pscustomobject]@{ cmd = $Line; answer = $answer }) | Out-Null
    return $answer
}

function Write-Row([int]$Round, [string]$Phase, [double]$RoundMs, [int]$Failures, [string]$StateLine) {
    $p = $script:process
    $p.Refresh()
    $now = Get-Date
    $cpuNow = $p.TotalProcessorTime.TotalSeconds
    $wall = ($now - $script:prevAt).TotalSeconds
    $cores = if ($wall -gt 0.001) { [math]::Round(($cpuNow - $script:prevCpu) / $wall, 3) } else { 0 }
    $script:prevCpu = $cpuNow
    $script:prevAt = $now

    $windowsTotal = ''
    $windowsVisible = ''
    $scanGeneration = ''
    if ($StateLine) {
        try {
            $state = $StateLine | ConvertFrom-Json
            $windowsTotal = $state.windows.total
            $windowsVisible = $state.windows.visible
            $scanGeneration = $state.scan.generation
        } catch { }
    }

    [pscustomobject]@{
        time_iso          = $now.ToString('o')
        elapsed_s         = [math]::Round(($now - $script:begin).TotalSeconds, 1)
        round             = $Round
        phase             = $Phase
        round_ms          = [math]::Round($RoundMs, 1)
        private_mb        = [math]::Round($p.PrivateMemorySize64 / 1MB, 1)
        working_set_mb    = [math]::Round($p.WorkingSet64 / 1MB, 1)
        threads           = $p.Threads.Count
        handles           = $p.HandleCount
        cpu_cores         = $cores
        windows_total     = $windowsTotal
        windows_visible   = $windowsVisible
        scan_generation   = $scanGeneration
        command_failures  = $Failures
        state             = $StateLine
    } | Export-Csv -LiteralPath $outPath -NoTypeInformation -Encoding UTF8 -Append
}

try {
    $env:PATH = "$($paths.qtBin);$env:PATH"
    if ($KeepGraphics) { $env:LENS_KEEP_GRAPHICS = '1' } else { Remove-Item Env:LENS_KEEP_GRAPHICS -ErrorAction SilentlyContinue }

    # --settings-file keeps the run's document out of the reader's profile (docs/adr/0068).
    $arguments = @("--dev-pipe=$Pipe", "--dev-out=$devOutPath", "--settings-file=$settingsPath")
    Write-Host "lens-soak: starting $exePath"
    Write-Host "lens-soak: scenario $([System.IO.Path]::GetFileName($scriptPath)), $Rounds round(s), CSV $outPath"
    $script:process = Start-Process -FilePath $exePath -ArgumentList $arguments `
        -WorkingDirectory (Split-Path -Parent $exePath) -PassThru

    # One client for the whole run. DevPipe refuses a second one, so wait for the first
    # connection to be accepted before replaying anything.
    $deadline = (Get-Date).AddSeconds($ConnectTimeoutSeconds)
    while ($null -eq $script:client -and (Get-Date) -lt $deadline) {
        $candidate = New-Object System.IO.Pipes.NamedPipeClientStream('.', $Pipe, [System.IO.Pipes.PipeDirection]::InOut)
        try {
            $candidate.Connect(500)
            $script:client = $candidate
        } catch {
            $candidate.Dispose()
            Start-Sleep -Milliseconds 200
        }
        if ($script:process.HasExited) { throw 'lens-soak: the app exited before the dev pipe came up' }
    }
    if ($null -eq $script:client) { throw 'lens-soak: the dev pipe never accepted a connection' }

    $utf8 = New-Object System.Text.UTF8Encoding($false)
    $script:devWriter = New-Object System.IO.StreamWriter($script:client, $utf8)
    $script:devWriter.AutoFlush = $true
    $script:devReader = New-Object System.IO.StreamReader($script:client, $utf8)

    $buildLine = Invoke-Dev 'version'
    Write-Host "lens-soak: build $buildLine"

    if ($WarmupSeconds -gt 0) { Start-Sleep -Seconds $WarmupSeconds }
    $script:prevCpu = $script:process.TotalProcessorTime.TotalSeconds
    $script:prevAt = Get-Date
    $script:begin = Get-Date

    Write-Row 0 'baseline' 0 0 (Invoke-Dev 'state')

    $phaseName = if ($Idle) { 'idle' } else { [System.IO.Path]::GetFileNameWithoutExtension($scriptPath) }
    for ($round = 1; $round -le $Rounds; $round++) {
        if ($script:process.HasExited) { throw "lens-soak: the app exited during round $round" }

        $started = Get-Date
        $failures = 0
        if (-not $Idle) {
            foreach ($line in $roundLines) {
                $answer = Invoke-Dev $line
                try {
                    if (-not ($answer | ConvertFrom-Json).ok) {
                        $failures++
                        if ($failures -le 3) { Write-Host "lens-soak: round $round refused: $line -> $answer" }
                    }
                } catch { $failures++ }
            }
        }
        $roundMs = ((Get-Date) - $started).TotalMilliseconds

        # releaseHiddenSurfaces() fires 500 ms after hiding, and the driver returns its
        # pages gradually. Reading before that reports a step that is still on its way down.
        if ($SettleSeconds -gt 0) { Start-Sleep -Seconds $SettleSeconds }
        Write-Row $round $phaseName $roundMs $failures (Invoke-Dev 'state')

        Write-Host ("lens-soak: round {0}/{1} {2} ms, {3} refusal(s)" -f $round, $Rounds, [math]::Round($roundMs), $failures)
        if ($round -lt $Rounds -and $IntervalSeconds -gt 0) { Start-Sleep -Seconds $IntervalSeconds }
    }

    try { $null = Invoke-Dev 'quit' } catch { }
    if (-not $script:process.WaitForExit($QuitTimeoutSeconds * 1000)) {
        Write-Host 'lens-soak: the app did not quit on its own; stopping the PID this run started'
        Stop-Process -Id $script:process.Id -Force
    }

    # The numbers are the point, so the summary stays beside the CSV and is never a gate.
    $rows = @(Import-Csv -LiteralPath $outPath)
    if ($rows.Count -ge 2) {
        $first = $rows[0]
        $last = $rows[$rows.Count - 1]
        $privates = @($rows | ForEach-Object { [double]$_.private_mb })
        $threads = @($rows | ForEach-Object { [int]$_.threads })
        $handles = @($rows | ForEach-Object { [int]$_.handles })
        $tail = @($privates | Select-Object -Last 3 | Sort-Object)
        $settled = if ($tail.Count -gt 0) { $tail[[int][math]::Floor($tail.Count / 2)] } else { 0 }
        $steps = @()
        for ($i = 1; $i -lt $rows.Count; $i++) {
            $dPriv = [double]$rows[$i].private_mb - [double]$rows[$i - 1].private_mb
            $dThreads = [int]$rows[$i].threads - [int]$rows[$i - 1].threads
            if ([math]::Abs($dPriv) -ge 20 -or [math]::Abs($dThreads) -ge 20) {
                $steps += "round $($rows[$i].round): $([math]::Round($dPriv, 1)) MB, $dThreads threads"
            }
        }
        $summary = [ordered]@{
            csv              = $outPath
            scenario         = $phaseName
            rounds           = $Rounds
            rows             = $rows.Count
            keepGraphics     = [bool]$KeepGraphics
            private_first    = [double]$first.private_mb
            private_last     = [double]$last.private_mb
            private_peak     = ($privates | Measure-Object -Maximum).Maximum
            private_settled  = $settled
            private_per_round = [math]::Round(([double]$last.private_mb - [double]$first.private_mb) / ($rows.Count - 1), 3)
            threads_first    = [int]$first.threads
            threads_last     = [int]$last.threads
            threads_peak     = ($threads | Measure-Object -Maximum).Maximum
            handles_first    = [int]$first.handles
            handles_last     = [int]$last.handles
            handles_peak     = ($handles | Measure-Object -Maximum).Maximum
            round_ms_median  = ($rows | Where-Object { $_.round -ne '0' } | ForEach-Object { [double]$_.round_ms } | Sort-Object)[[int][math]::Floor(($rows.Count - 1) / 2)]
            command_failures = ($rows | Measure-Object -Property command_failures -Sum).Sum
            steps            = $steps
        }
        $summaryPath = [System.IO.Path]::ChangeExtension($outPath, '.summary.json')
        $summary | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $summaryPath -Encoding UTF8
        Write-Host ''
        Write-Host "lens-soak: private $($first.private_mb) -> $($last.private_mb) MB, peak $($summary.private_peak) MB"
        Write-Host "lens-soak: threads $($first.threads) -> $($last.threads), handles $($first.handles) -> $($last.handles)"
        Write-Host "lens-soak: settled tail $settled MB, slope $($summary.private_per_round) MB/round"
        if ($steps.Count -gt 0) { Write-Host "lens-soak: steps: $($steps -join '; ')" }
        Write-Host "lens-soak: summary $summaryPath"
    }
}
finally {
    # Written even on failure: a soak that wedges still leaves the answers it did get, which
    # is what says which command stopped answering.
    if ($null -ne $script:devAnswers -and $script:devAnswers.Count -gt 0) {
        try {
            $jsonLines = @($script:devAnswers | ForEach-Object {
                '{"cmd":' + ($_.cmd | ConvertTo-Json -Compress) + ',"answer":' + $_.answer + '}'
            })
            [System.IO.File]::WriteAllLines($devOutPath, $jsonLines, (New-Object System.Text.UTF8Encoding($false)))
        } catch { }
    }
    if ($null -ne $script:devWriter) { try { $script:devWriter.Dispose() } catch { } }
    if ($null -ne $script:devReader) { try { $script:devReader.Dispose() } catch { } }
    if ($null -ne $script:client) { try { $script:client.Dispose() } catch { } }
    if ($null -ne $script:process -and -not $script:process.HasExited) {
        if (-not $script:process.WaitForExit($QuitTimeoutSeconds * 1000)) {
            Stop-Process -Id $script:process.Id -Force
        }
    }
    if ($null -eq $previousKeep) { Remove-Item Env:LENS_KEEP_GRAPHICS -ErrorAction SilentlyContinue }
    else { $env:LENS_KEEP_GRAPHICS = $previousKeep }
}
