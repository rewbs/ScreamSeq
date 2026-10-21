# Native white-placeholder follow-up

The user reports that the previous changes improved flicker, but white
placeholders still flash on automation-point release and view changes. This
follow-up targets the native control erase stage; the earlier final-frame and
message-count checks did not cover that stage.

## Source findings and bounded change

`NativeControls.hpp` supplied buffered owner-drawn button contents but passed
`WM_ERASEBKGND` through to the native BUTTON class. The stock erase can replace
the existing pixels with the system button-face background before the owner
publishes its complete item. Main.cpp and NativeToolWindow.hpp also omitted
`WM_CTLCOLORBTN`, allowing that color request to use the system default.
Microsoft documents the [owner-drawn button color request](https://learn.microsoft.com/en-us/windows/win32/controls/wm-ctlcolorbtn).

The frozen product source is `f7eeec1d6d12387919f64266f2cc3d6213e59aa2`:

- Consume background erase only for installed `BS_OWNERDRAW` buttons. Keep
  their existing pixels until the complete item is drawn. Standard push,
  radio and check buttons retain native erase behavior. The same rule applies
  in high contrast because the owner already draws the entire system-color
  item there.
- Route button color requests through the existing main/inspector palettes.
- Buffer the custom page tabs in the parameter and graph automation editors.
  Their overrides previously bypassed the base button buffer and drew the
  background, label, underline and focus cue directly to the destination.

No editing admission, enable state, focus, capture, persistence, history,
project format, audio callback or Mac code changes. Controls and their HWNDs
remain native and retained. This removes a concrete source of white intermediate
rectangles; it is not a claim that every possible flicker source is eliminated.

## Validation batch

One coordinated ARM64 Release build: app, workspace harness, mixer strip,
parameter automation, graph curve, native control and native tool harnesses.
The previous build completed at 07:29:40 UTC; this batch began at 18:05:38 UTC.
Use `bin/white-flash-20261021/` in the primary checkout for frozen-source build,
check and packaging scripts and receipts. Do not repeat a receipted build.

The new native control test compares stock owner-button erase with the retained
ScreamSeq button. It examines the intermediate destination pixels in enabled,
disabled, normal and explicit system-color branches, without changing the OS
theme. Tool and real Application tests check the returned button brush. Existing
tests cover final paint colors, GDI-resource leaks, focus, local Undo, selectors,
docking, command admission and worker completion.

Run 19 native gates and 11 actual-app checks in one pass. The actual-app checks
include native view controls and retained workspace layouts in addition to the
previous mixer and three envelope drag regressions. After passing, inspect a
separate packaged executable with Computer Use: switch views, drag/release an
automation point, and verify keyboard focus. Preserve the musician's process
and all earlier packages. Screenshots remain point-in-time observations; the
erase regression supplies evidence about the intermediate paint stage.

## Build and intermediate-paint evidence

The one build completed successfully at **18:08:26 UTC**. No further build may
start before **19:08:27 UTC** under the conservative hourly gate. The executable
SHA-256 is `85442c616adb3a92064f91a6d754fba54f4409eba108ae5a6d461837437bd7f7`.
The existing PluginParametersWindow enum/floating-point warning remains.

`native-control-tests` ran first and passed in 6.03 seconds. Its stock-button
control replaced the destination pixels with COLORREF 15790320 (`#F0F0F0`).
The installed ScreamSeq owner-drawn button preserved every sampled pixel during
erase, enabled and disabled, in both explicit normal/system-color branches.
Existing final rendering, native selection/keyboard, accessibility names,
high-contrast color mapping and GDI-lifetime checks also passed. These are
intermediate-paint and final-paint checks, not a screen recording. The raw log
and output are in `native-control.log` and `native-control.xml`.

## Completed qualification and package

All **19 native gates and 11 actual-app checks passed** with the frozen source
and executable unchanged. The remaining 18 native gates took 286.53 seconds;
the 11 actual-app tests took 19.781 seconds. Receipts are `checks.json`,
`native.xml`, `native.log`, `app.log` and `app-launch.log`. The actual-app mixer
test observed zero neighboring-strip layout messages across 24 previews.
Envelope tests covered parameter, graph and bank drafts, and the parameter
test retained its Undo/Redo and native save/reopen assertions.

Packaged executable:
`C:/Users/P14/Downloads/ScreamSeq-Windows-ARM64-white-flash-f7eeec1d6/ScreamSeq.exe`.
It is an optimized ARM64 Release with the hash above, scanner, redistributable
runtime DLLs, source provenance and retained third-party notices. Earlier
packages were preserved. `ReleaseProvenance.json` lists package hashes;
`package-check.json` records their verification.

Computer Use inspected this exact packaged executable in a separate process,
PID 35932, with `--inspection --automation`. No other ScreamSeq process was
running at launch. The disposable Midnight Circuit example was supplemented
through guarded API writes with a built-in Gainer and two-point smooth/linear
parameter envelope. No preferences or user project were written.

- Switched Compose to Pattern focus to Sound design, then Mixer and Graph
  dock views. Final layouts and controls rendered correctly.
- In floating parameter automation, dragged the first point from row 8/value
  0.25 to row 10/value 0.379509. Release retained the curve and reported saved.
- Docked the same inspector and dragged the first point to row 12/value
  0.509434. A real Right key then moved that selected point to row 13
  (position 3328), proving the canvas received keyboard input after release.
  The second point remained at row 32/value 0.75; 1024 preview samples remained,
  with dirty/pending false and status saved. Receipts:
  `live-after-float-drag.json`, `live-after-keyboard.json`.
- Switched Curve to Formula and back; the edited curve, selection and saved
  values remained. Closed only this QA process, discarded its disposable
  changes, and verified process exit 0 and no remaining ScreamSeq processes.

The live session lasted 154.55 seconds (`live-report.json`). This was an
inspection session without audio; sustainedPresentationQualified and
realtimeAuditPassed correctly remain false. The snapshots establish interaction
and final-state behavior, **not temporal absence of all flashes**. The paired
stock/fixed erase-pixel test is the specific intermediate-paint evidence for
this correction. Existing Windows-native light scrollbars and standard dialogs
remain; those are not temporary placeholders and were not restyled here.

One build was used. Product source is committed locally as `f7eeec1d6`; no
push or merge was performed. Shared model, engine, storage and Mac sources are
unchanged, so Mac/audio suites were not repeated for this Windows paint-only
change. Further observed flicker outside the corrected owner-button/tab paths
requires a separately identified cause rather than a blanket completion claim.
