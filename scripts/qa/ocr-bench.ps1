# Baseline a local OCR engine against test/ocr/corpus.
#
# The corpus is synthetic and committed; this script never reads the desktop, never uploads
# anything, and writes only the CSV given by -Out. Tesseract is taken from the merged machine
# paths (tesseractRoot), never from PATH, matching how the application resolves it.
#
#   powershell -File scripts/qa/ocr-bench.ps1
#   powershell -File scripts/qa/ocr-bench.ps1 -Language chi_sim+eng -Out test/records/ocr-zh.csv
#
# A sample whose language data is not installed is reported as skipped with the reason, not as a
# failure: the baseline records what this machine can actually measure.

[CmdletBinding()]
param(
    [string]$Corpus = "",
    [string]$Out = "",
    [string]$Language = "",
    [string]$TesseractRoot = ""
)

$ErrorActionPreference = "Stop"

function Get-RepositoryRoot {
    $root = (& git rev-parse --show-toplevel 2>$null)
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($root)) {
        throw "ocr-bench: run this script inside the repository"
    }
    return $root.Trim()
}

function Get-Levenshtein([string]$A, [string]$B) {
    if ($A.Length -eq 0) { return $B.Length }
    if ($B.Length -eq 0) { return $A.Length }
    $previous = New-Object int[] ($B.Length + 1)
    $current = New-Object int[] ($B.Length + 1)
    for ($j = 0; $j -le $B.Length; ++$j) { $previous[$j] = $j }
    for ($i = 1; $i -le $A.Length; ++$i) {
        $current[0] = $i
        $ca = $A[$i - 1]
        for ($j = 1; $j -le $B.Length; ++$j) {
            $cost = if ($ca -eq $B[$j - 1]) { 0 } else { 1 }
            $delete = $previous[$j] + 1
            $insert = $current[$j - 1] + 1
            $substitute = $previous[$j - 1] + $cost
            $current[$j] = [Math]::Min($delete, [Math]::Min($insert, $substitute))
        }
        $swap = $previous
        $previous = $current
        $current = $swap
    }
    return $previous[$B.Length]
}

function Get-Normalized([string]$Text) {
    if ($null -eq $Text) { return "" }
    $value = $Text -replace "`r", " "
    $value = $value -replace "\s+", " "
    $value = $value.Trim().ToLowerInvariant()
    $value = $value.Replace([char]0x201C, '"').Replace([char]0x201D, '"')
    $value = $value.Replace([char]0x2018, "'").Replace([char]0x2019, "'")
    $value = $value.Replace([char]0x2013, "-").Replace([char]0x2014, "-")
    return $value
}

function Get-WordLevenshtein([string[]]$A, [string[]]$B) {
    if ($A.Count -eq 0) { return $B.Count }
    if ($B.Count -eq 0) { return $A.Count }
    $previous = New-Object int[] ($B.Count + 1)
    $current = New-Object int[] ($B.Count + 1)
    for ($j = 0; $j -le $B.Count; ++$j) { $previous[$j] = $j }
    for ($i = 1; $i -le $A.Count; ++$i) {
        $current[0] = $i
        for ($j = 1; $j -le $B.Count; ++$j) {
            $cost = if ($A[$i - 1] -ceq $B[$j - 1]) { 0 } else { 1 }
            $delete = $previous[$j] + 1
            $insert = $current[$j - 1] + 1
            $substitute = $previous[$j - 1] + $cost
            $current[$j] = [Math]::Min($delete, [Math]::Min($insert, $substitute))
        }
        $swap = $previous
        $previous = $current
        $current = $swap
    }
    return $previous[$B.Count]
}

function Get-Cer([string]$Expected, [string]$Actual) {
    $left = (Get-Normalized $Expected) -replace "\s+", ""
    $right = (Get-Normalized $Actual) -replace "\s+", ""
    if ($left.Length -eq 0) { return if ($right.Length -eq 0) { 0.0 } else { 1.0 } }
    return [double](Get-Levenshtein $left $right) / $left.Length
}

function Get-Wer([string]$Expected, [string]$Actual) {
    $left = @((Get-Normalized $Expected) -split " " | Where-Object { $_ -ne "" })
    $right = @((Get-Normalized $Actual) -split " " | Where-Object { $_ -ne "" })
    if ($left.Count -eq 0) { return if ($right.Count -eq 0) { 0.0 } else { 1.0 } }
    return [double](Get-WordLevenshtein $left $right) / $left.Count
}

function Get-Percentile([double[]]$Values, [double]$Percentile) {
    if ($Values.Count -eq 0) { return 0.0 }
    $sorted = $Values | Sort-Object
    $index = [Math]::Min($sorted.Count - 1, [Math]::Ceiling($Percentile * $sorted.Count) - 1)
    return $sorted[$index]
}

$root = Get-RepositoryRoot
. (Join-Path $root "scripts/build/paths.ps1")
if ([string]::IsNullOrWhiteSpace($TesseractRoot)) {
    $TesseractRoot = (Get-LensPaths -Root $root).tesseractRoot
}
if ([string]::IsNullOrWhiteSpace($TesseractRoot)) {
    throw "ocr-bench: the merged config carries no tesseractRoot; see scripts/build/show-paths.ps1"
}
$exe = Join-Path $TesseractRoot "tesseract.exe"
if (-not (Test-Path -LiteralPath $exe)) {
    throw "ocr-bench: no tesseract.exe under '$TesseractRoot'"
}
if ([string]::IsNullOrWhiteSpace($Corpus)) {
    $Corpus = Join-Path $root "test/ocr/corpus"
}
$manifestPath = Join-Path $Corpus "manifest.json"
if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "ocr-bench: no corpus manifest at '$manifestPath'"
}
if ([string]::IsNullOrWhiteSpace($Out)) {
    $Out = Join-Path $root "test/records/ocr-baseline-tesseract.csv"
}
$outDirectory = Split-Path -Parent $Out
if ($outDirectory -and -not (Test-Path -LiteralPath $outDirectory)) {
    New-Item -ItemType Directory -Path $outDirectory | Out-Null
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
$dataRoot = Join-Path $TesseractRoot "tessdata"
$engine = "tesseract-$((& $exe --version | Select-Object -First 1) -replace '^tesseract\s+', '')"
$rows = @()
$cerValues = [System.Collections.Generic.List[double]]::new()
$times = [System.Collections.Generic.List[double]]::new()

foreach ($sample in $manifest.samples) {
    $sampleLanguage = switch ($sample.language) {
        "en" { "eng" }
        "zh-Hans" { "chi_sim" }
        "zh-Hans+en" { "chi_sim+eng" }
        default { "eng" }
    }
    if ($Language -and $sampleLanguage -ne $Language) { continue }

    $needed = $sampleLanguage -split "\+"
    $missing = @($needed | Where-Object { -not (Test-Path -LiteralPath (Join-Path $dataRoot "$_.traineddata")) })
    if ($missing.Count -gt 0) {
        $rows += [pscustomobject]@{
            sample = $sample.id; language = $sample.language; source = $sample.source
            layout = $sample.layout; scale = $sample.scale; engine = $engine
            status = "skipped-missing-data:$($missing -join '+')"; chars = 0; words = 0
            cer = ""; wer = ""; ms = 0; outBytes = 0
        }
        continue
    }

    $image = Join-Path $Corpus $sample.file
    $start = New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName = $exe
    $start.Arguments = "`"$image`" stdout -l $sampleLanguage --psm 3"
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = [Text.Encoding]::UTF8
    $start.StandardErrorEncoding = [Text.Encoding]::UTF8
    $start.CreateNoWindow = $true
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    $process = [System.Diagnostics.Process]::Start($start)
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(30000)) {
        $process.Kill()
        $timer.Stop()
        $rows += [pscustomobject]@{
            sample = $sample.id; language = $sample.language; source = $sample.source
            layout = $sample.layout; scale = $sample.scale; engine = $engine
            status = "timed-out"; chars = 0; words = 0
            cer = ""; wer = ""; ms = [Math]::Round($timer.Elapsed.TotalMilliseconds, 1); outBytes = 0
        }
        continue
    }
    $timer.Stop()
    $text = ($stdout.Result).Trim()
    $status = if ($process.ExitCode -eq 0) { "ok" } else { "failed-exit-$($process.ExitCode)" }
    $bytes = [Text.Encoding]::UTF8.GetByteCount($text)
    $cer = Get-Cer $sample.text $text
    $wer = if ($sampleLanguage -eq "chi_sim") { "" } else { Get-Wer $sample.text $text }
    if ($status -eq "ok") {
        $cerValues.Add($cer)
        $times.Add($timer.Elapsed.TotalMilliseconds)
    }
    $rows += [pscustomobject]@{
        sample = $sample.id; language = $sample.language; source = $sample.source
        layout = $sample.layout; scale = $sample.scale
        engine = $engine
        status = $status; chars = $text.Length; words = ($text -split "\s+" | Where-Object { $_ -ne "" }).Count
        cer = [Math]::Round($cer, 4); wer = if ($wer -eq "") { "" } else { [Math]::Round($wer, 4) }
        ms = [Math]::Round($timer.Elapsed.TotalMilliseconds, 1); outBytes = $bytes
    }
}

$rows | Export-Csv -LiteralPath $Out -NoTypeInformation -Encoding UTF8

$meanCer = if ($cerValues.Count -gt 0) { ($cerValues | Measure-Object -Average).Average } else { 0.0 }
Write-Output "engine      $engine"
Write-Output "corpus      $manifestPath"
Write-Output "rows        $($rows.Count) -> $Out"
Write-Output ("mean CER    {0:P2}" -f $meanCer)
Write-Output ("CER p50/p95 {0:P2} / {1:P2}" -f (Get-Percentile $cerValues.ToArray() 0.50), (Get-Percentile $cerValues.ToArray() 0.95))
Write-Output ("time p50/p95 {0:N1} ms / {1:N1} ms" -f (Get-Percentile $times.ToArray() 0.50), (Get-Percentile $times.ToArray() 0.95))
