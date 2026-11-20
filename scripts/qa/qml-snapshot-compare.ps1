# Compare a rendered QML snapshot set against the tracked goldens, pixel by pixel.
#
# The snapshot cases write their PNGs only when LENS_QA_SNAPSHOT_DIR is set, so a normal
# developer run skips them and this script is only ever handed a directory that a snapshot run
# wrote. Goldens live under test/snapshots/golden/ (components/ and surfaces/); the reference is
# this repository's development machine, at the Qt version the build tree was configured with.
# docs/qa.md owns collection, the renderer pin and the tolerance.
#
# The gate: a pixel differs when any of its R, G, B, A channels differs by more than 16 levels;
# a case fails when the differing pixels exceed 5% of the image. Sixteen levels keep a platform's
# own rounding and dithering from going red while a shifted glyph or a changed fill still does;
# five percent lets an antialiased seam pass while a moved row cannot hide in it.
#
# Every golden must have a rendered counterpart, and a size change is a failure: a surface that
# grew is a visual change. Extra renders are ignored on purpose -- a full target run writes every
# snapshot case the suite carries, and the goldens are the chosen batch.
#
# -Update rewrites the goldens that exist from the rendered directory and exits 0 when every one
# of them had a counterpart. It touches nothing else, so the golden set can only be refreshed this
# way, never widened by a stray whole-suite render; a new case's first golden is copied in by hand.
# It never compares, so the gate is exactly what runs without it; CI never passes it.
#
# Examples:
#   powershell -File scripts/qa/qml-snapshot-compare.ps1 -GoldenDir test/snapshots/golden/surfaces -RenderedDir test/records/snapshots
#   powershell -File scripts/qa/qml-snapshot-compare.ps1 -GoldenDir test/snapshots/golden/surfaces -RenderedDir $env:TEMP\lens-snap -Update

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$GoldenDir,
    [Parameter(Mandatory = $true)]
    [string]$RenderedDir,
    # Rewrite the goldens from -RenderedDir instead of comparing. A human's refresh, never CI's.
    [switch]$Update
)

$ErrorActionPreference = "Stop"

# The two numbers the gate is: per-channel tolerance, and the share of the image that may differ.
$ChannelTolerance = 16
$MaxDiffFraction = 0.05

# System.Drawing only: both files are decoded by GDI+ and compared as 32-bpp BGRA rows, so a
# 2,000-pixel-wide card costs one pass rather than one GetPixel per pixel. The type is compiled
# from an inline source block because PowerShell loops over 32-bpp rows are an order of magnitude
# slower; it pulls in nothing beyond the System.Drawing assembly the script already uses.
if (-not ("LensSnapshotDiff" -as [type])) {
    Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public static class LensSnapshotDiff {
    /// @return { differing pixels, width, height }, or { -1, golden width, golden height,
    ///          rendered width, rendered height } when the sizes differ.
    public static long[] Compare(string goldenPath, string renderedPath, int tolerance) {
        using (var rawGolden = new Bitmap(goldenPath))
        using (var rawRendered = new Bitmap(renderedPath))
        using (var golden = new Bitmap(rawGolden.Width, rawGolden.Height, PixelFormat.Format32bppArgb))
        using (var rendered = new Bitmap(rawRendered.Width, rawRendered.Height, PixelFormat.Format32bppArgb)) {
            using (var graphics = Graphics.FromImage(golden)) graphics.DrawImage(rawGolden, 0, 0);
            using (var graphics = Graphics.FromImage(rendered)) graphics.DrawImage(rawRendered, 0, 0);
            if (golden.Width != rendered.Width || golden.Height != rendered.Height)
                return new long[] { -1, golden.Width, golden.Height, rendered.Width, rendered.Height };

            var rect = new Rectangle(0, 0, golden.Width, golden.Height);
            var goldenBits = golden.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            var renderedBits = rendered.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            var goldenRows = new byte[goldenBits.Stride * golden.Height];
            var renderedRows = new byte[renderedBits.Stride * rendered.Height];
            Marshal.Copy(goldenBits.Scan0, goldenRows, 0, goldenRows.Length);
            Marshal.Copy(renderedBits.Scan0, renderedRows, 0, renderedRows.Length);
            golden.UnlockBits(goldenBits);
            rendered.UnlockBits(renderedBits);

            long differing = 0;
            for (int y = 0; y < golden.Height; ++y) {
                int row = y * goldenBits.Stride;
                for (int x = 0; x < golden.Width; ++x) {
                    int at = row + x * 4;
                    if (Math.Abs(goldenRows[at] - renderedRows[at]) > tolerance ||
                        Math.Abs(goldenRows[at + 1] - renderedRows[at + 1]) > tolerance ||
                        Math.Abs(goldenRows[at + 2] - renderedRows[at + 2]) > tolerance ||
                        Math.Abs(goldenRows[at + 3] - renderedRows[at + 3]) > tolerance)
                        ++differing;
                }
            }
            return new long[] { differing, golden.Width, golden.Height };
        }
    }
}
"@
}

function Get-RepositoryRoot {
    $root = (& git rev-parse --show-toplevel 2>$null)
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($root)) {
        throw "qml-snapshot-compare: run this script inside the repository"
    }
    return $root.Trim()
}

function Resolve-PathFromRoot([string]$path, [string]$root) {
    if ([System.IO.Path]::IsPathRooted($path)) {
        return $path
    }
    return Join-Path $root $path
}

# @return Every PNG under `dir`, recursively; an absent directory is an empty list so the caller
#         says what it means rather than the enumerator failing first.
function Get-PngFiles([string]$dir) {
    if (-not (Test-Path -LiteralPath $dir)) {
        return @()
    }
    return @(Get-ChildItem -LiteralPath $dir -Filter *.png -File -Recurse)
}

$root = Get-RepositoryRoot
$goldenRoot = Resolve-PathFromRoot $GoldenDir $root
$renderedRoot = Resolve-PathFromRoot $RenderedDir $root

if ($Update) {
    $goldens = Get-PngFiles $goldenRoot
    if ($goldens.Count -eq 0) {
        Write-Output "qml-snapshot-compare: no golden PNG under $goldenRoot; nothing to update"
        exit 1
    }
    $missing = 0
    foreach ($golden in $goldens) {
        $relative = $golden.FullName.Substring($goldenRoot.Length).TrimStart('\', '/')
        $rendered = Join-Path $renderedRoot $relative
        if (-not (Test-Path -LiteralPath $rendered)) {
            Write-Output "qml-snapshot-compare: UPDATE-SKIP $relative rendered image missing ($rendered)"
            ++$missing
            continue
        }
        Copy-Item -LiteralPath $rendered -Destination $golden.FullName -Force
        Write-Output "qml-snapshot-compare: UPDATE $relative"
    }
    if ($missing -gt 0) {
        Write-Output "qml-snapshot-compare: $missing golden(s) kept, $($goldens.Count - $missing) updated"
        exit 1
    }
    exit 0
}

$goldens = Get-PngFiles $goldenRoot
if ($goldens.Count -eq 0) {
    Write-Output "qml-snapshot-compare: no golden PNG under $goldenRoot; the gate cannot run"
    exit 1
}

$failed = 0
foreach ($golden in $goldens) {
    $relative = $golden.FullName.Substring($goldenRoot.Length).TrimStart('\', '/')
    $rendered = Join-Path $renderedRoot $relative
    if (-not (Test-Path -LiteralPath $rendered)) {
        Write-Output "qml-snapshot-compare: FAIL $relative rendered image missing ($rendered)"
        ++$failed
        continue
    }

    try {
        $result = [LensSnapshotDiff]::Compare($golden.FullName, $rendered, $ChannelTolerance)
    } catch {
        Write-Output "qml-snapshot-compare: FAIL $relative unreadable image ($($_.Exception.Message))"
        ++$failed
        continue
    }

    if ($result[0] -lt 0) {
        Write-Output "qml-snapshot-compare: FAIL $relative size $($result[1])x$($result[2]) -> $($result[3])x$($result[4])"
        ++$failed
        continue
    }

    $pixels = [double]$result[1] * $result[2]
    $fraction = if ($pixels -gt 0) { $result[0] / $pixels } else { 0.0 }
    $percent = "{0:F2}%" -f ($fraction * 100)
    if ($fraction -gt $MaxDiffFraction) {
        Write-Output "qml-snapshot-compare: FAIL $relative $percent ($($result[0]) of $([int]$pixels) pixels)"
        ++$failed
    } else {
        Write-Output "qml-snapshot-compare: PASS $relative $percent ($($result[0]) of $([int]$pixels) pixels)"
    }
}

Write-Output "qml-snapshot-compare: $($goldens.Count - $failed) passed, $failed failed, $($goldens.Count) golden(s)"
if ($failed -gt 0) {
    exit 1
}
exit 0
