# PotPlayer subtitles for auto scan — working plan

Temporary campaign document; deleted when the stages close (the decisions land as ADRs then).

## Goal

The auto scan reads the subtitle text a local video plays in PotPlayer and marks words above the
reader's level, the way it already does for Chrome, Notepad++ and SumatraPDF — no OCR, no screen
reading, and far more information than the pixels carry (exact text, cue timing, context lines).

## Evidence (measured on this machine, 2026-11-19)

- Clock through window messages, class `PotPlayer64`: `SendMessage(hwnd, WM_USER (1024), 20484, 0)`
  returns the current position in ms (357148 matched the OSD's 00:05:57 exactly, also while
  paused); 20486 returns play status (1 paused, 2 playing); 20482 total ms; 24624/24625 video
  width/height in pixels. `ProgramAPI=1` is set in `HKCU\Software\DAUM\PotPlayerMini64\Settings`.
- The current media's full path: the window title is the file name only. Two sources matched it
  live: the process command line (`PotPlayerMini64.exe "<path>"` when opened by association) and
  `B:\PotPlayer\Playlist\PotPlayerMini64.dpl` (`playname=` line, saved by the player itself).
- Subtitles: this MKV carries three embedded text tracks (bilingual ASS, plain English SRT,
  English SDH SRT); ffmpeg extraction matched the displayed line cue-for-cue.
- Style: `HKCU\Software\DAUM\PotPlayerMini64\Settings\CaptionStyle` =
  `5;5;5;5;2;0;2;2;3;3;0xffffff;0x000000;…;Segoe UI;15;100;100;0;700;…;50;95;…`
  (field 19 family, 20 size, 24 weight). At a 1920×1020 window the rendered line is ~46 px high,
  white on black outline, caption band y≈900–945; predicted word starts landed within 3 px.
- A Translate-extension probe pushed every displayed line to a local listener (live + prefetch),
  but it needs real-time translation enabled, restarts to register, and visibly changes the
  subtitle rendering — rejected for v1 (kept on record as the fallback for streams/unknown tracks).

## Locked decisions

- D1 Route: subtitle file + playback clock. Net no new dependency: a native SRT/ASS/SSA/VTT parser
  plus a minimal Matroska text extractor (EBML walk, payloads seek-skipped, compressed tracks
  skipped and logged).
- D2 Source precedence: an external subtitle next to the video (`stem.LANG.ext` / `stem.ext`,
  srt/ass/ssa/vtt) wins — the reader put it there deliberately; otherwise the embedded text
  tracks. Bitmap tracks (PGS/VobSub) and ContentEncoded tracks are skipped.
- D3 Track choice: forced tracks excluded; a sample whose letters are mostly CJK excluded; English
  language tag +, default flag +, SDH/CC name or hearing-impaired flag −; lowest track number
  breaks ties. External candidates rank en/eng suffix over no suffix over others.
- D4 Media path resolution: window title file name must match the resolved path's file name, for
  both the process command line and the playlist file; on no match the scan skips the window and
  logs once.
- D5 Geometry: video rect = video width/height fitted and centered in the window's client rect;
  caption font from the CaptionStyle registry (family/size/weight, defaults Segoe UI 15 / 700);
  lines centered; font pixels, bottom margin and line gap are constants tuned live in Stage 4.
  Cues keep their source line breaks; explicit `\an` top cues render at the top margin; `\pos`/
  `\move`/middle anchors are skipped (signs, not dialogue).
- D6 Integration: new `ScanForeground::Kind::PotPlayer`; the track factory gains the kind; a
  `SubtitleTrack` (`src/app/platform/`) runs on its own worker, polls the clock (≈100 ms playing,
  ≈250 ms paused), emits a frame when the active cue or the band changes, and keeps parsed cues in
  a two-entry cache keyed by path and mtime.
- D7 Setting: `autoScanPotplayer` (toggle, default on, Signal::Capture) replaces the "Video
  subtitles — Coming soon" row; label "PotPlayer". i18n updated in all five catalogs.
- D8 Known limits, logged rather than hidden: the player's own subtitle delay/position overrides
  shift the real band (we draw at the default); subtitles switched off in the player cannot be
  detected; a track PotPlayer picks differently from our choice misplaces lines; web streams are
  out of scope.

## Stages

1. ✅ Subtitle sources (core, no Qt): `core/subtitle/` — models + timeline, SRT/ASS/SSA/VTT
   parsers, Matroska text-track extractor, source choice, `loadSubtitlesFor`. gtest `Subtitle*`:
   15 green. Real check 2026-11-19: the South Park MKV yields 524 cues on the plain-English track
   (ffmpeg's own extraction: 524), cue 164 @00:05:56,690 is the line the player showed; the 2 GB
   walk took 927 ms, once per path+mtime (Stage 3 caches it).
2. ✅ Playback clock and media path (Win32): `app/platform/potplayer_clock` — WM_USER queries via
   `SendMessageTimeout` (20484/20486/24624/24625), media path from the process command line
   (NtQueryInformationProcess) else `<exe dir>\Playlist\<exe stem>.dpl`, both guarded by the
   title's file name. Live 2026-11-19 against the paused player: time 357826 ms (exactly the
   restored position), status 1, video 1920×1080 (answers while paused, so geometry is safe),
   path resolved and matching the title. Pure helpers gtest `PotPlayer*`: 5 green (probe removed).
   Note for Stage 3: `ScanForeground::context` is pid:hwnd, which does not change when the player
   swaps files in the same window — the track watches the title itself and swaps cue sets.
3. ✅ The track and the gate: `app/platform/subtitle_track` — clock poll (50 ms playing / 150 ms
   paused), title watch, two-entry path+mtime parse cache, QFontMetricsF layout on a 1:1 probe
   device, frames on cue/band change (added only on the cue's first frame). `Kind::PotPlayer`
   through the session (factory now takes the kind), duty gate + `autoScanPotplayer` row, QML
   switch replaces the Coming-soon row, catalog summary, five catalogs at zero unfinished
   (`lupdate -no-obsolete`: 3 new, 4 removed). Verified: gtest `ScanSession*:Subtitle*:PotPlayer*:
   SettingsTable*` 49 green (incl. the new PotPlayer tick test); qtest `CaptureSettings::
   test_autoScanAppsAreSwitches` and `::test_potplayerSwitchWritesItsSetting` pass; app builds,
   qml-lint 0 warnings.
4. ⇣ Live verification 2026-11-19, first findings fixed:
   - Z-order: the user's PotPlayer has `OnTopMode=2` (`HKCU\Software\DAUM\PotPlayerMini64\Settings`):
     while playing its window is WS_EX_TOPMOST and covered both the marks and the pet; paused it
     drops the flag. A manual `SetWindowPos(HWND_TOPMOST)` kept Lens above for the rest of playback
     (the player does not re-assert). Fix: the pencil layer (while it has marks, the mark button
     right after it) and the pet (while shown) re-raise on a 1 s cadence. Verified live: playing,
     `WindowFromPoint` at the pet centre answers the pet's own window with no manual help.
   - Placement: PotPlayer draws its toolbar (39 px) and control bars inside the client area; the
     video viewport is the window's largest visible child (1920x896 of a 1920x1021 client), and the
     1920x1080 picture is letterboxed inside it. The old client-based rect put every line ~65 px
     low. `potplayerVideoRect` now fits the video into that child. Caption geometry re-measured
     from the pixels: 46 px font, last baseline 63 px above the picture's bottom, 67 px between a
     two-line cue's baselines; the word box ends 2 px under the baseline so the dash rides the
     glyphs' feet. Verified on "He had a laryngectomy" (frozen frame + capture-visible QA run): the
     dashes sit on the letters' feet, descenders crossed like a CSS underline.
   - Still open for this stage: window-resize scaling (the caption may be sourced to the video's
     own pixels rather than the window's), fullscreen, and the reader's own confirmation.
5. Close: ADR(s), PRODUCT/GLOSSARY/UI-SPEC touch-ups, CHANGELOG, delete this document.
