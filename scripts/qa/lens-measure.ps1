<#
.SYNOPSIS
    Samples one running lens.exe into a CSV, one row per sample, with the developer surface's state beside each row.

.DESCRIPTION
    docs/adr/0068 section 8. The product only answers what state it is in; what counts as settled is a question about
    the measurement, so it is decided here, in the harness, and never in the application.

    Start lens.exe with --dev-pipe=<name> and pass the same name as -Pipe, or the state column is left empty.
    The CSV is created new and never appended to: an existing file is refused, not overwritten.

.PARAMETER ProcessId
    The lens.exe process to sample. Not named Pid, which would shadow PowerShell's automatic $PID.

.PARAMETER Pipe
    The name given to --dev-pipe. The state is asked over it once per sample.

.PARAMETER Seconds
    How long to sample, in fixed-interval mode.

.PARAMETER Interval
    Seconds between samples.

.PARAMETER Out
    The CSV to create. Relative paths are taken from the repository root, where the script is run.

.PARAMETER Settle
    Instead of a fixed run, sample until private bytes stay within -Tolerance MB over -Samples samples, then stop.

.PARAMETER Samples
    How many consecutive samples the settle test looks at.

.PARAMETER Tolerance
    The largest spread, in MB, that still counts as settled.

.EXAMPLE
    .\scripts\qa\lens-measure.ps1 -ProcessId 1234 -Pipe lens-dev-1 -Seconds 60 -Interval 10 -Out logs\two.csv

.EXAMPLE
    .\scripts\qa\lens-measure.ps1 -ProcessId 1234 -Pipe lens-dev-1 -Settle -Samples 3 -Tolerance 2 -Out logs\settle.csv
#>
param(
    [Parameter(Mandatory = $true)][int]$ProcessId,
    [string]$Pipe = '',
    [int]$Seconds = 60,
    [int]$Interval = 10,
    [string]$Out = 'logs\measure.csv',
    [switch]$Settle,
    [int]$Samples = 3,
    [double]$Tolerance = 2,
    [int]$SettleTimeoutSeconds = 300
)

$ErrorActionPreference = 'Stop'

if (Test-Path -LiteralPath $Out) {
    throw "$Out already exists; the measurement is written to a new file, so choose another -Out."
}

function Get-Sample([System.Diagnostics.Process]$Process) {
    $Process.Refresh()
    [pscustomobject]@{
        PrivateMb    = [math]::Round($Process.PrivateMemorySize64 / 1MB, 1)
        WorkingSetMb = [math]::Round($Process.WorkingSet64 / 1MB, 1)
        Threads      = $Process.Threads.Count
        Handles      = $Process.HandleCount
    }
}

# One connection per question: the pipe takes one client at a time, and a client that stays would block the next sample.
function Get-State([string]$PipeName) {
    $client = New-Object System.IO.Pipes.NamedPipeClientStream('.', $PipeName, [System.IO.Pipes.PipeDirection]::InOut)
    try {
        $client.Connect(2000)
        # UTF-8 without a byte order mark: a BOM would arrive as the first character of the command and break it.
        $utf8 = New-Object System.Text.UTF8Encoding($false)
        $writer = New-Object System.IO.StreamWriter($client, $utf8)
        $writer.AutoFlush = $true
        $reader = New-Object System.IO.StreamReader($client, $utf8)
        $writer.WriteLine('state')
        return $reader.ReadLine()
    }
    finally {
        $client.Dispose()
    }
}

function Write-Row($Process, [double]$ElapsedSeconds) {
    $sample = Get-Sample $Process
    $state  = ''
    if ($Pipe -ne '') {
        try { $state = Get-State $Pipe } catch { $state = "error: $($_.Exception.Message)" }
    }
    $row = [pscustomobject]@{
        time_iso       = (Get-Date).ToString('o')
        elapsed_s      = [math]::Round($ElapsedSeconds, 1)
        private_mb     = $sample.PrivateMb
        working_set_mb = $sample.WorkingSetMb
        threads        = $sample.Threads
        handles        = $sample.Handles
        state          = $state
    }
    $row | Export-Csv -LiteralPath $Out -NoTypeInformation -Encoding UTF8 -Append
    return $sample
}

$process = Get-Process -Id $ProcessId

if ($Settle) {
    $window = New-Object System.Collections.Generic.List[double]
    $begin  = Get-Date
    while ($true) {
        if ($process.HasExited) { throw "process $ProcessId exited before it settled" }
        $elapsed = ((Get-Date) - $begin).TotalSeconds
        $sample  = Write-Row $process $elapsed
        $window.Add([double]$sample.PrivateMb)
        if ($window.Count -gt $Samples) { $window.RemoveAt(0) }
        if ($window.Count -eq $Samples) {
            $spread = ($window | Measure-Object -Maximum -Minimum | ForEach-Object { $_.Maximum - $_.Minimum })
            if ($spread -lt $Tolerance) {
                Write-Host "settled: private bytes within $Tolerance MB over $Samples samples, at $([math]::Round($elapsed, 1)) s"
                exit 0
            }
        }
        if ($elapsed -ge $SettleTimeoutSeconds) {
            Write-Host "did not settle within $SettleTimeoutSeconds s; the CSV holds every sample taken"
            exit 2
        }
        Start-Sleep -Seconds $Interval
    }
}

$begin = Get-Date
$end   = $begin.AddSeconds($Seconds)
while ((Get-Date) -lt $end) {
    if ($process.HasExited) { Write-Host "process $ProcessId exited; stopping"; break }
    $elapsed = ((Get-Date) - $begin).TotalSeconds
    $null = Write-Row $process $elapsed
    Start-Sleep -Seconds $Interval
}
Write-Host "wrote $Out"
