<#
.SYNOPSIS
    Take two UMDH heap snapshots of a running lens.exe and report the allocations that grew.

.DESCRIPTION
    Every memory measurement in this repository so far answers "did the water line rise?"
    (Get-Process private bytes, VMMap segments, thread start addresses). None of them says
    WHERE the bytes were allocated. The 2026-10-18 glass report ends with an unresolved
    slow climb of about 1 MB per open/close cycle and names QML's image-cache serial entries
    and the font cache as suspects, without a stack to confirm either. This is the missing
    layer: UMDH diffs two snapshots of the same process and prints the allocation stacks
    that grew between them.

    Prerequisites, all outside this repository:
      1. Debugging Tools for Windows (umdh.exe and gflags.exe, from the Windows SDK).
      2. User-mode stack traces enabled for the process image, once, as administrator:
             gflags /i lens.exe +ust
         That is an IFEO setting for the image name, so the target process must be RESTARTED
         afterwards. -EnableUst runs it, but this script never assumes administrator rights.
      3. Symbols. Set _NT_SYMBOL_PATH to the build tree's PDBs and Microsoft's symbol server;
         without them UMDH reports addresses instead of function names.

    SECURITY: a UMDH snapshot is a dump of the process's heap. The reader's settings live in
    that heap, and the API key in %APPDATA%\Lens\settings.json is read into it. Snapshots stay
    under logs/ (gitignored), must never be committed, attached to an issue or sent anywhere.
    Delete them once the diff has been read. The script refuses an -Out outside the repository.

    Typical use, with lens-soak.ps1 driving the workload in another shell:
        # one shell
        powershell -File scripts/qa/lens-soak.ps1 -Rounds 240 -IntervalSeconds 60
        # another shell, after the first round has produced a step
        powershell -File scripts/qa/heap-diff.ps1 -ProcessId 1234 -WaitSeconds 600 `
          -Out logs/heap/soak-diff.txt

.PARAMETER ProcessId
    The lens.exe process to snapshot. Nothing is written into it; UMDH only reads its heap.

.PARAMETER WaitSeconds
    Time between the two snapshots. Long enough for the workload to do many rounds.

.PARAMETER EnableUst
    Run gflags /i lens.exe +ust before snapshotting. Needs administrator rights and a restart
    of the target process; refusing is normal and the script says so instead of failing quietly.

.PARAMETER Top
    How many growing allocation stacks to print to the console.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][int]$ProcessId,
    [int]$WaitSeconds = 300,
    [string]$Out = 'logs/heap/heap-diff.txt',
    [string]$Umdh = '',
    [string]$Gflags = '',
    [int]$Top = 25,
    [switch]$EnableUst
)

$ErrorActionPreference = 'Stop'

function Get-RepositoryRoot {
    $root = (& git rev-parse --show-toplevel 2>$null)
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($root)) {
        throw 'heap-diff: run this script inside the repository'
    }
    return $root.Trim()
}

function Find-Tool([string]$Given, [string]$Name) {
    if (-not [string]::IsNullOrWhiteSpace($Given)) { return $Given }
    $onPath = (Get-Command "$Name.exe" -ErrorAction SilentlyContinue).Source
    if ($onPath) { return $onPath }
    $sdk = "${env:ProgramFiles(x86)}\Windows Kits\10\Debuggers\x64\$Name.exe"
    if (Test-Path -LiteralPath $sdk) { return $sdk }
    return ''
}

# git prints the toplevel with forward slashes; GetFullPath returns backslashes, so the
# containment check below would fail for every path without this normalization.
$root = [System.IO.Path]::GetFullPath((Get-RepositoryRoot))
$umdhPath = Find-Tool $Umdh 'umdh'
if ([string]::IsNullOrWhiteSpace($umdhPath) -or -not (Test-Path -LiteralPath $umdhPath)) {
    throw 'heap-diff: umdh.exe not found. Install Debugging Tools for Windows or pass -Umdh <path>.'
}

if ($EnableUst) {
    $gflagsPath = Find-Tool $Gflags 'gflags'
    if ([string]::IsNullOrWhiteSpace($gflagsPath) -or -not (Test-Path -LiteralPath $gflagsPath)) {
        throw 'heap-diff: gflags.exe not found. Install Debugging Tools for Windows or pass -Gflags <path>.'
    }
    & $gflagsPath /i lens.exe +ust
    if ($LASTEXITCODE -ne 0) {
        throw 'heap-diff: gflags refused (administrator rights are usually the reason). No snapshot was taken.'
    }
    Write-Host 'heap-diff: +ust set. Restart lens.exe before taking snapshots, or the stacks will be empty.'
}

$process = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
if ($null -eq $process) { throw "heap-diff: no process with PID $ProcessId" }
if ($process.ProcessName -ne 'lens') { throw "heap-diff: PID $ProcessId is $($process.ProcessName), not lens" }

$outPath = if ([System.IO.Path]::IsPathRooted($Out)) { [System.IO.Path]::GetFullPath($Out) }
           else { [System.IO.Path]::GetFullPath((Join-Path $root $Out)) }
if (-not $outPath.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'heap-diff: snapshots contain heap contents; -Out must stay inside the repository (logs/ is gitignored).'
}
$dir = Split-Path -Parent $outPath
New-Item -ItemType Directory -Force -Path $dir | Out-Null
if (Test-Path -LiteralPath $outPath) { throw "heap-diff: $outPath already exists; choose a new -Out" }

$stem = [System.IO.Path]::GetFileNameWithoutExtension($outPath)
$before = Join-Path $dir "$stem.before.txt"
$after = Join-Path $dir "$stem.after.txt"

if ([string]::IsNullOrWhiteSpace($env:_NT_SYMBOL_PATH)) {
    Write-Host 'heap-diff: _NT_SYMBOL_PATH is unset; the diff will show addresses, not function names.'
}

Write-Host "heap-diff: first snapshot of PID $ProcessId"
& $umdhPath "-p:$ProcessId" "-f:$before"
if ($LASTEXITCODE -ne 0) { throw 'heap-diff: the first UMDH snapshot failed' }

Write-Host "heap-diff: waiting $WaitSeconds s for the workload, then the second snapshot"
Start-Sleep -Seconds $WaitSeconds
& $umdhPath "-p:$ProcessId" "-f:$after"
if ($LASTEXITCODE -ne 0) { throw 'heap-diff: the second UMDH snapshot failed' }

Write-Host 'heap-diff: diffing'
& $umdhPath $before $after "-f:$outPath"
if ($LASTEXITCODE -ne 0) { throw 'heap-diff: UMDH diff failed' }

$growth = @(Get-Content -LiteralPath $outPath | Where-Object { $_ -match '^\+\s' })
Write-Host ''
Write-Host "heap-diff: $($growth.Count) growing allocation stacks; top $Top by bytes:"
$growth | Select-Object -First $Top | ForEach-Object { Write-Host $_ }
Write-Host ''
Write-Host "heap-diff: full diff $outPath"
Write-Host 'heap-diff: snapshots contain heap contents. Read them, then delete both .before/.after files.'
