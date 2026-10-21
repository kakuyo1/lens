# Render the README's GIFs and the pet still offscreen, one case at a time.
#
# Each clip is a frame sequence. test/qtest/surfaces/tst_readme_gifs.qml steps through one interaction
# and grabs every step with grabImage, under Qt's offscreen platform: no window is shown, no pointer or
# keyboard is driven, the clipboard and the network are not touched, and the settings it writes go to a
# copy that exists only for the run. This script runs one case per clip and language, then encodes the
# frames with ffmpeg. Frames and intermediate files live under -Scratch, never in the repository.
#
# The word bubble (origin, example, Known and New) and the year table are rendered for every language.
# The word bubble's origin is in the language's own text and its example is English, both from
# test/qtest/surfaces/readme-glosses.json; the Japanese and Spanish texts are translations and have not
# been reviewed by a native speaker. The four pets have no text, so each renders once, in English, into
# gifs/pets/. The pet still is one PNG per language, taken from the real settings page, and goes to
# -StillDir/<folder>/pet-ui.png.
#
# Each GIF is checked before it is kept: 640 px wide, under 2 MB, 3 to 6 seconds long. A file that fails
# a check is not written to -OutDir, and the script exits non-zero.
#
# Examples:
#   powershell -File scripts/qa/readme-gifs.ps1
#   powershell -File scripts/qa/readme-gifs.ps1 -Clip year -Language ja
#   powershell -File scripts/qa/readme-gifs.ps1 -Clip pet-still
#
# This is the legacy README pipeline; see scripts/README.md. New clips follow the pattern in $clips.

[CmdletBinding()]
param(
    [ValidateSet('all', 'word-bubble', 'year', 'pets', 'pet-still')]
    [string]$Clip = 'all',
    [ValidateSet('all', 'en', 'zh-CN', 'ja', 'es')]
    [string]$Language = 'all',
    [string]$OutDir = 'docs/readme-images/gifs',
    [string]$StillDir = 'docs/readme-images',
    [string]$BuildDir = 'build-ninja',
    [string]$Scratch = (Join-Path $env:TEMP 'readme-gifs'),
    [string]$QtBin
)

$ErrorActionPreference = 'Stop'

# Clip name -> the case that renders it, the file it is written to, and whether it shows an explanation.
$clips = [ordered]@{
    'word-bubble' = @{ test = 'test_wordBubbleClip'; file = 'word-bubble.gif';   explained = $true }
    'year'        = @{ test = 'test_yearTableClip';  file = 'year-in-words.gif'; explained = $false }
}

# Pet clip name -> the case that renders it. Each one is written to gifs/pets/<name>.gif.
$pets = [ordered]@{
    'mochi-celebrate' = @{ test = 'test_petMochiCelebrateClip' }
    'cat-knead'       = @{ test = 'test_petCatKneadClip' }
    'dog-poked'       = @{ test = 'test_petDogPokedClip' }
    'owl-reading'     = @{ test = 'test_petOwlReadingClip' }
}

# Folder -> the app's language code (uiLanguage and explanationLang) and the .ts it is translated from.
# English is the source language, so it has no translation file.
$languages = [ordered]@{
    'en'    = @{ code = 'en'; ts = $null }
    'zh-CN' = @{ code = 'zh'; ts = 'lens_zh_CN.ts' }
    'ja'    = @{ code = 'ja'; ts = 'lens_ja_JP.ts' }
    'es'    = @{ code = 'es'; ts = 'lens_es_ES.ts' }
}

$frameHold = 0.6
$maxBytes  = 2MB

function Get-RepositoryRoot {
    $root = (& git rev-parse --show-toplevel 2>$null)
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($root)) {
        throw 'readme-gifs: run this script inside the repository'
    }
    return $root.Trim()
}

function Format-Seconds([double]$value) {
    return $value.ToString('0.000', [System.Globalization.CultureInfo]::InvariantCulture)
}

# The settings copy for one language: the fixture with this language's UI and explanation set, the origin and
# the example switched on, and a cache holding the one word the clips look up. The entry is the fixture's panel
# with this language's origin and the shared example from readme-glosses.json. Both fields are needed: with the
# example setting on, an entry without one is a miss, and a miss sends a request (explanation_duty.h, servable).
function New-TestSettings([string]$root, [string]$code, [string]$target) {
    $fixture = Get-Content -LiteralPath (Join-Path $root 'test\qtest\surfaces\settings.json') -Raw -Encoding UTF8 |
        ConvertFrom-Json
    $fixture.uiLanguage = $code
    $fixture.explanationLang = $code
    $fixture | Add-Member -NotePropertyName etymology -NotePropertyValue $true -Force
    $fixture | Add-Member -NotePropertyName example -NotePropertyValue $true -Force
    $panel = $fixture.cache.zh.panel
    $text = $glosses.languages.$code
    $entry = [pscustomobject]@{ ipa = $panel.ipa; en = $panel.en; zh = $panel.zh; etymology = $text.etymology; example = $glosses.example }
    # Any other language needs a translation field (known_store.cpp readCache), so its entry carries the gloss too.
    if ($text.translation) { $entry | Add-Member -NotePropertyName translation -NotePropertyValue $text.translation }
    $fixture.cache = [pscustomobject]@{ $code = [pscustomobject]@{ panel = $entry } }
    $json = $fixture | ConvertTo-Json -Depth 20
    [System.IO.File]::WriteAllText($target, $json, (New-Object System.Text.UTF8Encoding($false)))
}

# Encode one frame directory into one GIF. Each frame is held for the time it was on screen, read from
# its name (<prefix>-<index>-<ms>.png), and the last frame is held for $frameHold seconds. Frames are first
# written out at one canvas size, because the surface can grow inside a clip.
function ConvertTo-Gif([string]$framesDir, [string]$outFile, [string]$work, [string]$ffmpeg, [string]$ffprobe) {
    $names = @(Get-ChildItem -LiteralPath $framesDir -Filter '*.png' | Sort-Object Name | ForEach-Object { $_.Name })
    if ($names.Count -lt 2) { throw "fewer than two frames in $framesDir" }

    $times = @()
    foreach ($name in $names) {
        $times += [int64][IO.Path]::GetFileNameWithoutExtension($name).Split('-')[-1]
    }

    $maxW = 0
    $maxH = 0
    foreach ($name in $names) {
        $dims = (& $ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0 (Join-Path $framesDir $name)).Trim().Split(',')
        if ([int]$dims[0] -gt $maxW) { $maxW = [int]$dims[0] }
        if ([int]$dims[1] -gt $maxH) { $maxH = [int]$dims[1] }
    }

    $canvasDir = Join-Path $work 'canvas'
    if (Test-Path -LiteralPath $canvasDir) { Remove-Item -LiteralPath $canvasDir -Recurse -Force }
    New-Item -ItemType Directory -Path $canvasDir | Out-Null
    foreach ($name in $names) {
        & $ffmpeg -hide_banner -loglevel error -y -i (Join-Path $framesDir $name) `
            -vf "format=rgba,pad=${maxW}:${maxH}:0:0:color=0x00000000" (Join-Path $canvasDir $name)
        if ($LASTEXITCODE -ne 0) { throw "could not normalise frame $name" }
    }

    # The concat list: each frame lasts until the next capture, and the last one is held.
    $lines = New-Object System.Collections.Generic.List[string]
    $total = 0.0
    for ($i = 0; $i -lt $names.Count; $i++) {
        $seconds = $frameHold
        if ($i -lt $names.Count - 1) { $seconds = [math]::Max(0.02, ($times[$i + 1] - $times[$i]) / 1000.0) }
        $total += $seconds
        $lines.Add("file '$((Join-Path $canvasDir $names[$i]) -replace '\\', '/')'")
        $lines.Add("duration $(Format-Seconds $seconds)")
    }
    $lines.Add("file '$((Join-Path $canvasDir $names[-1]) -replace '\\', '/')'")
    $list = Join-Path $work 'frames.txt'
    [System.IO.File]::WriteAllLines($list, $lines, (New-Object System.Text.UTF8Encoding($false)))

    # The page colour is the README's background, so transparent pixels read as the page.
    $height = [int][math]::Round($maxH * 640 / $maxW)
    if ($height % 2) { $height++ }
    $filter = "[0:v]format=rgba[bg];[bg][1:v]overlay=format=auto,scale=640:${height}:flags=lanczos,split[a][b];" +
              "[a]palettegen=max_colors=128:stats_mode=full[p];[b][p]paletteuse=dither=bayer:bayer_scale=5"
    $gif = Join-Path $work 'out.gif'
    if (Test-Path -LiteralPath $gif) { Remove-Item -LiteralPath $gif -Force }
    & $ffmpeg -hide_banner -loglevel error -y `
        -f lavfi -i "color=c=0xe9ebef:s=${maxW}x${maxH}:r=20:d=$(Format-Seconds $total)" `
        -f concat -safe 0 -i $list -filter_complex $filter -loop 0 $gif
    if ($LASTEXITCODE -ne 0) { throw "ffmpeg failed encoding $outFile" }

    # The checks run on the scratch file. Only a file that passes reaches the output directory.
    $width = [int](& $ffprobe -v error -select_streams v:0 -show_entries stream=width -of csv=p=0 $gif).Trim()
    $seconds = [double]::Parse((& $ffprobe -v error -show_entries format=duration -of csv=p=0 $gif).Trim(), [System.Globalization.CultureInfo]::InvariantCulture)
    $bytes = (Get-Item -LiteralPath $gif).Length
    $problems = @()
    if ($width -ne 640) { $problems += "width $width, want 640" }
    if ($bytes -ge $maxBytes) { $problems += "$bytes bytes, want under $maxBytes" }
    if ($seconds -lt 3 -or $seconds -gt 6) { $problems += "duration $(Format-Seconds $seconds) s, want 3 to 6" }
    if ($problems.Count -gt 0) { throw "$outFile failed: $($problems -join '; ')" }

    New-Item -ItemType Directory -Path (Split-Path -Parent $outFile) -Force | Out-Null
    Copy-Item -LiteralPath $gif -Destination $outFile -Force
    return [pscustomobject]@{ file = $outFile; width = $width; seconds = $seconds; bytes = $bytes; frames = $names.Count }
}

# Run one case of tst_readme_gifs.qml for one language, with its frames or still written to $runDir.
# Returns the case's exit code.
function Invoke-Case([string]$test, [string]$runDir, [string]$folder) {
    if (Test-Path -LiteralPath $runDir) { Remove-Item -LiteralPath $runDir -Recurse -Force }
    New-Item -ItemType Directory -Path $runDir | Out-Null

    $env:LENS_QA_SNAPSHOT_DIR = $runDir
    $env:LENS_QA_SETTINGS = $settingsFiles[$folder]
    $env:LENS_QA_UI_LANGUAGE = $languages[$folder].code
    $env:LENS_QA_TRANSLATION = $qmFiles[$folder]
    & $exe -input $caseFile "ReadmeGifs::$test" | Out-Host
    return $LASTEXITCODE
}

$root = Get-RepositoryRoot

# The glosses are data, not code: a UTF-8 JSON file, because Windows PowerShell 5.1 reads a script without a
# BOM as ANSI and would garble them. Each language has its origin, and a translation where the app needs one.
$glosses = Get-Content -LiteralPath (Join-Path $root 'test\qtest\surfaces\readme-glosses.json') -Raw -Encoding UTF8 |
    ConvertFrom-Json
$explainedCodes = @($glosses.languages.PSObject.Properties.Name)

. "$PSScriptRoot\..\build\paths.ps1"
$paths = Get-LensPaths -Root $root

if (-not $QtBin) { $QtBin = $paths.qtBin }
$ffmpeg = $paths.ffmpeg
if (-not $ffmpeg -or -not (Test-Path -LiteralPath $ffmpeg)) {
    throw "readme-gifs: ffmpeg is not at '$ffmpeg'. Set the 'ffmpeg' key in config/paths.json (config/README.md)."
}
$ffprobe = Join-Path (Split-Path -Parent $ffmpeg) 'ffprobe.exe'
$lrelease = Join-Path $QtBin 'lrelease.exe'

$build = $BuildDir
if (-not [System.IO.Path]::IsPathRooted($build)) { $build = Join-Path $root $build }
$exe = Join-Path $build 'test\qtest\lens_qtest_surfaces.exe'
if (-not (Test-Path -LiteralPath $exe)) {
    throw "readme-gifs: $exe not found. Build it first: scripts\build\build.bat --target lens_qtest_surfaces"
}
$caseFile = Join-Path $root 'test\qtest\surfaces\tst_readme_gifs.qml'

$outRoot = $OutDir
if (-not [System.IO.Path]::IsPathRooted($outRoot)) { $outRoot = Join-Path $root $outRoot }
$stillRoot = $StillDir
if (-not [System.IO.Path]::IsPathRooted($stillRoot)) { $stillRoot = Join-Path $root $stillRoot }

$frames = Join-Path $Scratch 'frames'
$qmDir = Join-Path $Scratch 'qm'
$settingsDir = Join-Path $Scratch 'settings'
$workDir = Join-Path $Scratch 'work'
foreach ($dir in @($frames, $qmDir, $settingsDir, $workDir)) {
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
}

$env:PATH = "$QtBin;$env:PATH"
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_FORCE_STDERR_LOGGING = '1'

$chosenLanguages = if ($Language -eq 'all') { @($languages.Keys) } else { @($Language) }
$chosenClips = if ($Clip -eq 'all') { @('word-bubble', 'year', 'pets', 'pet-still') } else { @($Clip) }

# Translation files and settings for every language, in scratch. The pets render in English whatever the
# language filter, so the English settings are written even when another language is chosen.
$qmFiles = @{}
$settingsFiles = @{}
foreach ($folder in $languages.Keys) {
    $code = $languages[$folder].code
    $ts = $languages[$folder].ts
    $qmFiles[$folder] = ''
    if ($ts) {
        $qm = Join-Path $qmDir ([IO.Path]::ChangeExtension($ts, '.qm'))
        & $lrelease (Join-Path $root "i18n\$ts") -qm $qm
        if ($LASTEXITCODE -ne 0) { throw "lrelease failed on $ts" }
        $qmFiles[$folder] = $qm
    }
    $settingsFiles[$folder] = Join-Path $settingsDir "settings-$code.json"
    New-TestSettings -root $root -code $code -target $settingsFiles[$folder]
}

$failures = @()
$produced = @()
$skipped = @()
$stills = @()

# The word bubble and the year table: one GIF per language.
foreach ($clipName in $chosenClips) {
    if (-not $clips.Contains($clipName)) { continue }
    $info = $clips[$clipName]
    foreach ($folder in $chosenLanguages) {
        $code = $languages[$folder].code
        $outFile = Join-Path (Join-Path $outRoot $folder) $info.file
        if ($info.explained -and -not ($explainedCodes -contains $code)) {
            $skipped += "$clipName/${folder}: no cached explanation in '$code' (add an entry to test/qtest/surfaces/readme-glosses.json)"
            continue
        }

        $runDir = Join-Path $frames "$code-$clipName"
        $exit = Invoke-Case -test $info.test -runDir $runDir -folder $folder
        if ($exit -ne 0) {
            $failures += "$clipName/${folder}: the case failed (exit $exit)"
            continue
        }
        try {
            $produced += ConvertTo-Gif -framesDir $runDir -outFile $outFile -work $workDir -ffmpeg $ffmpeg -ffprobe $ffprobe
        } catch {
            $failures += "$clipName/${folder}: $($_.Exception.Message)"
        }
    }
}

# The pets: one GIF each, rendered in English and filed under gifs/pets, not in a language folder.
if ($chosenClips -contains 'pets') {
    foreach ($name in $pets.Keys) {
        $info = $pets[$name]
        $outFile = Join-Path (Join-Path $outRoot 'pets') "$name.gif"
        $runDir = Join-Path $frames "en-pet-$name"
        $exit = Invoke-Case -test $info.test -runDir $runDir -folder 'en'
        if ($exit -ne 0) {
            $failures += "pets/${name}: the case failed (exit $exit)"
            continue
        }
        try {
            $produced += ConvertTo-Gif -framesDir $runDir -outFile $outFile -work $workDir -ffmpeg $ffmpeg -ffprobe $ffprobe
        } catch {
            $failures += "pets/${name}: $($_.Exception.Message)"
        }
    }
}

# The pet still: the extensions page with the pet type list open, one PNG per language.
if ($chosenClips -contains 'pet-still') {
    foreach ($folder in $chosenLanguages) {
        $code = $languages[$folder].code
        $runDir = Join-Path $frames "$code-pet-still"
        $exit = Invoke-Case -test 'test_petSettingsStill' -runDir $runDir -folder $folder
        $shot = Join-Path $runDir 'pet-ui.png'
        if ($exit -ne 0 -or -not (Test-Path -LiteralPath $shot)) {
            $failures += "pet-still/${folder}: the case failed (exit $exit) or wrote no still"
            continue
        }
        $outFile = Join-Path (Join-Path $stillRoot $folder) 'pet-ui.png'
        New-Item -ItemType Directory -Path (Split-Path -Parent $outFile) -Force | Out-Null
        Copy-Item -LiteralPath $shot -Destination $outFile -Force
        $stills += $outFile
    }
}

foreach ($item in $produced) {
    Write-Output ("readme-gifs: {0}  {1} px  {2} s  {3} bytes  {4} frames" -f $item.file, $item.width, (Format-Seconds $item.seconds), $item.bytes, $item.frames)
}
foreach ($item in $stills) { Write-Output "readme-gifs: $item  still" }
foreach ($item in $skipped) { Write-Output "readme-gifs: skipped $item" }
foreach ($item in $failures) { Write-Output "readme-gifs: FAILED $item" }
Write-Output "readme-gifs: $($produced.Count) GIF(s), $($stills.Count) still(s), $($skipped.Count) skipped, $($failures.Count) failed"

if ($failures.Count -gt 0) { exit 1 }
exit 0
