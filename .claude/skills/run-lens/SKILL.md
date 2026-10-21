---
name: run-lens
description: Launch the Lens app and look at a surface — start the release build, drive the pointer, photograph the screen. Use when the user says 启动lens, asks to see a surface on screen, wants a UI change checked by eye, or asks for a screenshot of the app.
---

# Run Lens and look at a surface

Lens is tray-resident with no main window: a surface appears on a trigger, so looking at one means
running the real app and driving it.

## Steps

From the repository root, in Git Bash, where a `.ps1` runs only through `powershell -File <script>` —
the flags and arguments go on the end.

1. Start the app: `scripts/qa/ui-run.ps1` builds the release tree when its `lens.exe` is older than
   `src/`, launches it detached, and reports the PID, the log, and whether it came up. `-Restart`
   replaces a running instance.
2. Put `LENS_QA_VISIBLE_IN_CAPTURE=1` in front of that command when the run is for a screenshot.
   Without it every glass surface is absent from captures (docs/adr/0038, 0041), and
   `scripts/qa/ui-capture.ps1 -Virtual` photographs an empty desktop.
3. Drive a surface that needs a trigger with `scripts/qa/ui-input.ps1 click|drag|dragfast|reveal`.
   `scripts/qa/ui-tray-rects.ps1` prints the taskbar rectangles to check a placement against.

`docs/qa.md` owns the rest: the order of a round, what each tool measures, and the two driving
traps around focus and the clipboard.

This runs on the reader's own desktop: it opens windows and moves their pointer.
