<#
.SYNOPSIS
    Count a process's handles by object type, twice, and report which types grew.

.DESCRIPTION
    The 2026-11-10 soak report found handles climbing about 33 per round of surface
    open/close, in both the releasing and the LENS_KEEP_GRAPHICS configurations, while a
    round of settings writes did not move them at all. That narrowed the suspect list but
    could not name the object kind. This script names it: it walks the system handle table,
    keeps the entries owned by one PID, and resolves each object-type index to its type
    name, so a second snapshot can be diffed against the first by type.

    It is read-only. Nothing is written into the target process; the only handle calls are
    DuplicateHandle into this process and NtQueryObject for the type name, both closed at
    once. It needs no administrator rights: a process's own handle table is readable by its
    own user, and the type objects are shared.

    Why not UMDH: heap stacks need gflags /i lens.exe +ust, which is a system-wide IFEO
    setting, administrator rights and a restart of every future lens.exe. This answers a
    coarser question ("which object type") without touching the machine.

    The same caveat as any handle walk: NtQueryObject can block on a few exotic object
    kinds. Only the type information class is asked for, once per type, which is the safe
    one; the run is still best done on a dev build nobody else is using.

.PARAMETER ProcessId
    The lens.exe process to inspect. Refused when the name is not lens.

.PARAMETER WaitSeconds
    Time between snapshots. 0 takes a single snapshot, which is what a smoke test wants.

.PARAMETER Samples
    How many snapshots to take. 2 (the default) diffs a before and an after; more than two
    turns the diff into a trend, which is what a handle kind that opens and closes in bursts
    needs: one 120 s pair crossed a release boundary and read as a net loss of 1300 Event
    handles while Section and Mutant still climbed.

.PARAMETER Out
    Text report to create. Refused when it already exists.

.EXAMPLE
    powershell -File scripts/qa/handle-types.ps1 -ProcessId 1234 -WaitSeconds 180 -Out logs/handle/surfaces-diff.txt

.EXAMPLE
    powershell -File scripts/qa/handle-types.ps1 -ProcessId 1234 -WaitSeconds 0 -Out logs/handle/one-shot.txt
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][int]$ProcessId,
    [int]$WaitSeconds = 120,
    [int]$Samples = 2,
    [string]$Out = 'logs/handle/handle-diff.txt',
    [int]$Top = 25
)

$ErrorActionPreference = 'Stop'

function Get-RepositoryRoot {
    $root = (& git rev-parse --show-toplevel 2>$null)
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($root)) {
        throw 'handle-types: run this script inside the repository'
    }
    return $root.Trim()
}

if (-not ('LensHandleScan' -as [type])) {
    Add-Type -Language CSharp -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public static class LensHandleScan
{
    [DllImport("ntdll.dll")]
    static extern int NtQuerySystemInformation(int cls, IntPtr buf, int len, out int ret);
    [DllImport("ntdll.dll")]
    static extern int NtQueryObject(IntPtr h, int cls, IntPtr buf, int len, out int ret);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr OpenProcess(int access, bool inherit, int pid);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool DuplicateHandle(IntPtr srcProc, IntPtr srcHandle, IntPtr dstProc,
                                       out IntPtr dstHandle, int access, bool inherit, int options);
    [DllImport("kernel32.dll")]
    static extern IntPtr GetCurrentProcess();
    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(IntPtr h);

    const int SystemExtendedHandleInformation = 64;
    const int ObjectTypeInformation = 2;
    const int ProcessDuplicateHandle = 0x0040;
    const int DuplicateSameAccess = 0x0002;
    const int StatusInfoLengthMismatch = unchecked((int)0xC0000004);
    // x64 SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX: Object 0, UniqueProcessId 8, HandleValue 16,
    // GrantedAccess 24, CreatorBackTraceIndex 28, ObjectTypeIndex 30, HandleAttributes 32.
    const int EntrySize = 40;

    public static Dictionary<int, long> CountByType(int pid, out Dictionary<int, string> names,
                                                   out Dictionary<int, long> unique)
    {
        names = new Dictionary<int, string>();
        unique = new Dictionary<int, long>();
        Dictionary<int, long> counts = new Dictionary<int, long>();
        Dictionary<int, HashSet<long>> objects = new Dictionary<int, HashSet<long>>();

        int len = 1 << 20;
        IntPtr buf = Marshal.AllocHGlobal(len);
        int ret = 0;
        int status = NtQuerySystemInformation(SystemExtendedHandleInformation, buf, len, out ret);
        while (status == StatusInfoLengthMismatch)
        {
            Marshal.FreeHGlobal(buf);
            len = ret + 65536;
            buf = Marshal.AllocHGlobal(len);
            status = NtQuerySystemInformation(SystemExtendedHandleInformation, buf, len, out ret);
        }
        if (status != 0)
        {
            Marshal.FreeHGlobal(buf);
            return counts;
        }

        long total = (long)Marshal.ReadIntPtr(buf);
        IntPtr entries = IntPtr.Add(buf, IntPtr.Size * 2);
        IntPtr hProc = OpenProcess(ProcessDuplicateHandle, false, pid);
        Dictionary<int, string> resolved = new Dictionary<int, string>();

        for (long i = 0; i < total; i++)
        {
            IntPtr e = IntPtr.Add(entries, (int)(i * EntrySize));
            long owner = (long)Marshal.ReadIntPtr(e, 8);
            if (owner != pid) continue;

            IntPtr handleValue = Marshal.ReadIntPtr(e, 16);
            int typeIndex = (int)Marshal.ReadInt16(e, 30);

            long c;
            counts.TryGetValue(typeIndex, out c);
            counts[typeIndex] = c + 1;

            // The object pointer says whether a growing count is new objects or the same one
            // opened repeatedly: N distinct pointers means CreateXxx without CloseHandle,
            // one pointer means OpenXxx without CloseHandle.
            long obj = (long)Marshal.ReadIntPtr(e, 0);
            HashSet<long> set;
            if (!objects.TryGetValue(typeIndex, out set))
            {
                set = new HashSet<long>();
                objects[typeIndex] = set;
            }
            set.Add(obj);

            if (!resolved.ContainsKey(typeIndex) && hProc != IntPtr.Zero)
            {
                IntPtr dup;
                if (DuplicateHandle(hProc, handleValue, GetCurrentProcess(), out dup, 0, false, DuplicateSameAccess))
                {
                    string name = TypeName(dup);
                    if (name != null && name.Length > 0) resolved[typeIndex] = name;
                    CloseHandle(dup);
                }
            }
        }

        if (hProc != IntPtr.Zero) CloseHandle(hProc);
        Marshal.FreeHGlobal(buf);
        names = resolved;
        // A non-elevated caller gets the object pointer zeroed on this Windows build, so a
        // set holding only 0 says nothing; -1 marks that instead of pretending it is 1 object.
        foreach (KeyValuePair<int, HashSet<long>> kv in objects)
        {
            HashSet<long> s = kv.Value;
            unique[kv.Key] = (s.Count == 1 && s.Contains(0L)) ? -1 : s.Count;
        }
        return counts;
    }

    static string TypeName(IntPtr h)
    {
        int len = 4096;
        IntPtr buf = Marshal.AllocHGlobal(len);
        int ret = 0;
        int status = NtQueryObject(h, ObjectTypeInformation, buf, len, out ret);
        string name = null;
        if (status == 0)
        {
            short length = Marshal.ReadInt16(buf, 0);
            IntPtr strBuf = Marshal.ReadIntPtr(buf, IntPtr.Size);
            if (length > 0 && strBuf != IntPtr.Zero) name = Marshal.PtrToStringUni(strBuf, length / 2);
        }
        Marshal.FreeHGlobal(buf);
        return name;
    }
}
'@
}

# git prints the toplevel with forward slashes; GetFullPath returns backslashes, so the
# containment check below would fail for every path without this normalization.
$root = [System.IO.Path]::GetFullPath((Get-RepositoryRoot))
$process = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
if ($null -eq $process) { throw "handle-types: no process with PID $ProcessId" }
if ($process.ProcessName -ne 'lens') { throw "handle-types: PID $ProcessId is $($process.ProcessName), not lens" }

$outPath = if ([System.IO.Path]::IsPathRooted($Out)) { [System.IO.Path]::GetFullPath($Out) }
           else { [System.IO.Path]::GetFullPath((Join-Path $root $Out)) }
if (-not $outPath.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'handle-types: -Out must stay inside the repository'
}
$dir = Split-Path -Parent $outPath
New-Item -ItemType Directory -Force -Path $dir | Out-Null
if (Test-Path -LiteralPath $outPath) { throw "handle-types: $outPath already exists; choose a new -Out" }

function Get-Snapshot {
    $names = $null
    $unique = $null
    $counts = [LensHandleScan]::CountByType($ProcessId, [ref]$names, [ref]$unique)
    $rows = foreach ($key in $counts.Keys) {
        $label = if ($names.ContainsKey($key)) { $names[$key] } else { "Type#$key" }
        $objects = if ($unique.ContainsKey($key)) { [long]$unique[$key] } else { [long]0 }
        [pscustomobject]@{ Type = $label; Count = [long]$counts[$key]; Objects = $objects }
    }
    return @($rows)
}

$samples = [Math]::Max(1, $Samples)
$snapshots = New-Object System.Collections.Generic.List[object]
$totals = New-Object System.Collections.Generic.List[long]
for ($i = 0; $i -lt $samples; $i++) {
    $snapshot = Get-Snapshot
    $total = ($snapshot | Measure-Object -Property Count -Sum).Sum
    $snapshots.Add(@{ Rows = $snapshot; Total = [long]$total })
    $totals.Add([long]$total)
    Write-Host ("handle-types: snapshot {0}/{1}, {2} handles in {3} type(s)" -f ($i + 1), $samples, $total, $snapshot.Count)
    if ($i -lt $samples - 1 -and $WaitSeconds -gt 0) {
        Write-Host "handle-types: waiting $WaitSeconds s for the workload"
        Start-Sleep -Seconds $WaitSeconds
    }
}

# Type -> one count per snapshot, so a burst kind's opening and closing both show up.
$series = @{}
foreach ($entry in $snapshots) {
    foreach ($row in $entry.Rows) {
        if (-not $series.ContainsKey($row.Type)) { $series[$row.Type] = New-Object System.Collections.Generic.List[long] }
    }
}
foreach ($entry in $snapshots) {
    $seen = @{}
    foreach ($row in $entry.Rows) { $seen[$row.Type] = [long]$row.Count }
    foreach ($type in $series.Keys) {
        $series[$type].Add($(if ($seen.ContainsKey($type)) { $seen[$type] } else { [long]0 }))
    }
}

$rows = foreach ($type in $series.Keys) {
    $values = $series[$type]
    $delta = $values[$values.Count - 1] - $values[0]
    $per = if ($values.Count -gt 1) { [math]::Round($delta / ($values.Count - 1), 1) } else { 0 }
    $lastRow = $snapshots[$snapshots.Count - 1].Rows | Where-Object { $_.Type -eq $type } | Select-Object -First 1
    $objects = if ($null -ne $lastRow) { [long]$lastRow.Objects } else { [long]0 }
    [pscustomobject]@{ Type = $type; Values = $values; Delta = $delta; Per = $per; Objects = $objects }
}
$rows = @($rows | Sort-Object -Property Delta -Descending)

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("process $ProcessId, $samples snapshot(s) ${WaitSeconds}s apart, total $($totals[0]) -> $($totals[$totals.Count - 1])")
$lines.Add('')
$header = '{0,-24}' -f 'type'
for ($i = 0; $i -lt $samples; $i++) { $header += ('{0,8}' -f "s$i") }
$header += ('{0,8}{1,8}{2,8}' -f 'delta', 'per', 'uniq')
$lines.Add($header)
foreach ($row in $rows) {
    $line = '{0,-24}' -f $row.Type
    foreach ($value in $row.Values) { $line += ('{0,8}' -f $value) }
    $uniq = if ($row.Objects -lt 0) { 'n/a' } else { [string]$row.Objects }
    $line += ('{0,8}{1,8}{2,8}' -f $row.Delta, $row.Per, $uniq)
    $lines.Add($line)
}
[System.IO.File]::WriteAllLines($outPath, $lines, (New-Object System.Text.UTF8Encoding($false)))

Write-Host ''
Write-Host ("handle-types: total {0} -> {1}, top $Top type(s) by delta:" -f $totals[0], $totals[$totals.Count - 1])
$rows | Where-Object { $_.Delta -ne 0 } | Select-Object -First $Top | ForEach-Object {
    $trail = ($_.Values | ForEach-Object { [string]$_ }) -join ' -> '
    $uniq = if ($_.Objects -lt 0) { 'objects n/a (needs elevation)' } else { "$($_.Objects) object(s)" }
    Write-Host ('  {0,-22} {1}  (delta {2}, {3}/sample, {4})' -f $_.Type, $trail, $_.Delta, $_.Per, $uniq)
}
Write-Host "handle-types: report $outPath"
