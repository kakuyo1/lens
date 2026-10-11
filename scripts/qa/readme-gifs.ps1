# Render the README's GIFs offscreen and encode them, one clip in one language at a time.
#
# Each clip is a frame sequence. test/qtest/surfaces/tst_readme_gifs.qml steps through one interaction
# and grabs every step with grabImage, under Qt's offscreen platform: no window is shown, no pointer or
# keyboard is driven, the clipboard and the network are not touched, and the settings it writes go to a
# copy that exists only for the run. This script runs one case per clip and language, then encodes the
# frames with ffmpeg. Frames and intermediate files live under -Scratch, never in the repository.
#
# A clip that shows an explanation is rendered only for a language with a cached one. English and Chinese
# come from the fixture's own "panel" entry. Japanese and Spanish get a gloss from
# test/qtest/surfaces/readme-glosses.json, which is a translation and has not been reviewed by a native
# speaker. Any other language is skipped and the skip is printed, not filled in. The year clip needs no
# explanation. The pet clip has no text, so it is rendered once and copied into each language folder.
#
# Each GIF is checked before it is kept: 640 px wide, under 2 MB, 3 to 6 seconds long. A file that fails
# a check is not written to -OutDir, and the script exits non-zero.
#
# Examples:
#   powershell -File scripts/qa/readme-gifs.ps1
#   powershell -File scripts/qa/readme-gifs.ps1 -Clip year -Language ja
#
# To add a clip, see TEST.md section 5.

[CmdletBinding()]
param(
    [ValidateSet('all', 'bubble', 'marking', 'year', 'pet')]
    [string]$Clip = 'all',
    [ValidateSet('all', 'en', 'zh-CN', 'ja', 'es')]
    [string]$Language = 'all',
    [string]$OutDir = 'docs/readme-images/gifs',
    [string]$BuildDir = 'build-ninja',
    [string]$Scratch = (Join-Path $env:TEMP 'readme-gifs'),
    [string]$QtBin
)

$ErrorActionPreference = 'Stop'

# Clip name -> the case that renders it, the file it is written to, and whether it shows an explanation.
$clips = [ordered]@{
    bubble  = @{ test = 'test_bubbleClip';       file = 'word-bubble.gif';       explained = $true }
    marking = @{ test = 'test_markingClip';      file = 'marking-known-new.gif'; explained = $true }
    year    = @{ test = 'test_yearTableClip';    file = 'year-in-words.gif';     explained = $false }
    pet     = @{ test = 'test_petCelebrateClip'; file = 'pet-celebrate.gif';     explained = $false }
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

# The fixture is the source of every explanation the clips show. A language that has no entry in it gets
# no entry from here either, so the bubble and marking clips for that language are skipped.
function New-TestSettings([string]$root, [string]$code, [string]$target) {
    $fixture = Get-Content -LiteralPath (Join-Path $root 'test\qtest\surfaces\settings.json') -Raw -Encoding UTF8 |
        ConvertFrom-Json
    $fixture.uiLanguage = $code
    $fixture.explanationLang = $code
    $panel = $fixture.cache.zh.panel
    if ($code -eq 'en') {
        # The app reads an entry only when it has both an en and a zh text (known_store.cpp readCache), so
        # the English copy is the fixture's own "panel" entry, filed under en.
        $fixture.cache | Add-Member -NotePropertyName en -NotePropertyValue ([pscustomobject]@{ panel = $panel })
    } elseif ($explainedCodes -contains $code -and $code -ne 'zh') {
        # Any other language needs a translation field (known_store.cpp readCache), so its entry is the same
        # panel with the gloss added.
        $gloss = [pscustomobject]@{ ipa = $panel.ipa; en = $panel.en; zh = $panel.zh; translation = $glosses.$code }
        $fixture.cache | Add-Member -NotePropertyName $code -NotePropertyValue ([pscustomobject]@{ panel = $gloss })
    }
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

$root = Get-RepositoryRoot

# The glosses are data, not code: a UTF-8 JSON file, because Windows PowerShell 5.1 reads a script without a
# BOM as ANSI and would garble them. A language with a gloss gets a cache entry built from the fixture's panel.
$glosses = Get-Content -LiteralPath (Join-Path $root 'test\qtest\surfaces\readme-glosses.json') -Raw -Encoding UTF8 |
    ConvertFrom-Json
$explainedCodes = @('en', 'zh') + @($glosses.PSObject.Properties.Name)

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
$chosenClips = if ($Clip -eq 'all') { @($clips.Keys) } else { @($Clip) }

# Translation files and per-language settings, both in scratch.
$qmFiles = @{}
$settingsFiles = @{}
foreach ($folder in $chosenLanguages) {
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
$petRendered = $null

foreach ($clipName in $chosenClips) {
    $info = $clips[$clipName]
    foreach ($folder in $chosenLanguages) {
        $code = $languages[$folder].code
        $outFile = Join-Path (Join-Path $outRoot $folder) $info.file

        if ($clipName -eq 'pet') {
            # The pet has no text, so one render serves every language. It is copied below.
            if ($null -ne $petRendered) { continue }
        }
        if ($info.explained -and -not ($explainedCodes -contains $code)) {
            $skipped += "$clipName/${folder}: no cached explanation in '$code' (add a gloss to test/qtest/surfaces/readme-glosses.json)"
            continue
        }

        $runDir = Join-Path $frames "$code-$clipName"
        if (Test-Path -LiteralPath $runDir) { Remove-Item -LiteralPath $runDir -Recurse -Force }
        New-Item -ItemType Directory -Path $runDir | Out-Null

        $env:LENS_QA_SNAPSHOT_DIR = $runDir
        $env:LENS_QA_SETTINGS = $settingsFiles[$folder]
        $env:LENS_QA_UI_LANGUAGE = $code
        $env:LENS_QA_TRANSLATION = $qmFiles[$folder]
        & $exe -input $caseFile "ReadmeGifs::$($info.test)"
        if ($LASTEXITCODE -ne 0) {
            $failures += "$clipName/${folder}: the case failed (exit $LASTEXITCODE)"
            continue
        }

        try {
            $result = ConvertTo-Gif -framesDir $runDir -outFile $outFile -work $workDir -ffmpeg $ffmpeg -ffprobe $ffprobe
            if ($clipName -eq 'pet') { $petRendered = $result } else { $produced += $result }
        } catch {
            $failures += "$clipName/${folder}: $($_.Exception.Message)"
        }
    }
}

# The pet GIF rendered once is copied into the folder of every language that was asked for.
if ($null -ne $petRendered) {
    foreach ($folder in $chosenLanguages) {
        $outFile = Join-Path (Join-Path $outRoot $folder) $clips['pet'].file
        if ($outFile -ne $petRendered.file) {
            New-Item -ItemType Directory -Path (Split-Path -Parent $outFile) -Force | Out-Null
            Copy-Item -LiteralPath $petRendered.file -Destination $outFile -Force
        }
        $produced += [pscustomobject]@{ file = $outFile; width = $petRendered.width; seconds = $petRendered.seconds; bytes = $petRendered.bytes; frames = $petRendered.frames }
    }
}

foreach ($item in $produced) {
    Write-Output ("readme-gifs: {0}  {1} px  {2} s  {3} bytes  {4} frames" -f $item.file, $item.width, (Format-Seconds $item.seconds), $item.bytes, $item.frames)
}
foreach ($item in $skipped) { Write-Output "readme-gifs: skipped $item" }
foreach ($item in $failures) { Write-Output "readme-gifs: FAILED $item" }
Write-Output "readme-gifs: $($produced.Count) GIF(s), $($skipped.Count) skipped, $($failures.Count) failed"

if ($failures.Count -gt 0) { exit 1 }
exit 0
