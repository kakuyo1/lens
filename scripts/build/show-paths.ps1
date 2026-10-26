# Print the machine paths Lens actually uses: config/paths.json merged with the gitignored
# config/paths.local.json, the latter winning key by key.
#
# The merged values are what every script reads through scripts/build/paths.ps1. This wrapper
# exists so a person or an agent does not have to re-implement that merge -- and, more to the
# point, so nobody reads config/paths.json alone: that file is the committed template, and a
# machine that differs overrides it in paths.local.json.
#
# Each row names the file the key came from and whether the value resolves on this machine. A
# MISSING row is the first thing to check when a test binary will not start.
#
#   powershell -File scripts/build/show-paths.ps1
#   powershell -File scripts/build/show-paths.ps1 -Key qtBin     # one value, for a command substitution

[CmdletBinding()]
param(
    # A single key to print, without the table.
    [string]$Key
)

$ErrorActionPreference = "Stop"

function Get-RepositoryRoot {
    $root = (& git rev-parse --show-toplevel 2>$null)
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($root)) {
        throw "show-paths: run this script inside the repository"
    }
    return $root.Trim()
}

$root = Get-RepositoryRoot
. (Join-Path $root "scripts/build/paths.ps1")

$paths = Get-LensPaths -Root $root

if (-not [string]::IsNullOrWhiteSpace($Key)) {
    if (-not $paths.ContainsKey($Key)) {
        throw "show-paths: neither config file carries '$Key'"
    }
    $value = $paths[$Key]
    if ($value -is [string]) {
        [Console]::Out.WriteLine($value)
    } else {
        foreach ($item in $value) { [Console]::Out.WriteLine($item) }
    }
    exit 0
}

# Where each key was last written. Get-LensPaths returns the merge; provenance is only for this
# report, so reading the two documents again here is cheaper than widening the reader.
$source = @{}
foreach ($name in @('paths.json', 'paths.local.json')) {
    $file = Join-Path $root "config/$name"
    if (-not (Test-Path -LiteralPath $file)) { continue }
    $document = Get-Content -LiteralPath $file -Raw | ConvertFrom-Json
    foreach ($property in $document.PSObject.Properties) {
        if ($property.Name.StartsWith('_')) { continue }
        $source[$property.Name] = $name
    }
}

function Test-LensPathValue {
    param($Value)
    if ($Value -is [string]) {
        if ([string]::IsNullOrWhiteSpace($Value)) { return "-" }
        return $(if (Test-Path -LiteralPath $Value) { "ok" } else { "MISSING" })
    }
    $missing = 0
    foreach ($item in $Value) {
        if (-not (Test-Path -LiteralPath $item)) { $missing++ }
    }
    if ($missing -gt 0) { return "$missing MISSING" }
    return "ok"
}

Write-Output ("{0,-20} {1,-17} {2,-10} {3}" -f "key", "source", "resolves", "value")
foreach ($name in ($paths.Keys | Sort-Object)) {
    $value = $paths[$name]
    $shown = if ($value -is [string]) { $value } else { ($value -join "; ") }
    $where = if ($source.ContainsKey($name)) { $source[$name] } else { "?" }
    Write-Output ("{0,-20} {1,-17} {2,-10} {3}" -f $name, $where, (Test-LensPathValue $value), $shown)
}
