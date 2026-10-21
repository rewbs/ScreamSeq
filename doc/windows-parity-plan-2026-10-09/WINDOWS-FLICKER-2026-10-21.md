# Windows native-control flicker

## Scope and source finding

The user reported widespread Windows UI flicker. The current ARM64 Release package
inspected here is from main `b98631b48`, with the same product source as `55d11e131`.
Its earlier moved-control invalidation fix prevents stale sibling captions;
it does not separate native-control painting from canvas presentation.

`windows/App/RenderSurface.hpp` presents a flip-discard swapchain directly to
the owner HWND. That same window owns native GDI controls throughout the main
workspace and `NativeToolWindow` editors. This is a presentation-interoperability
risk, not evidence of musical state changing. Separately,
`NativeControls::buttonItem/listItem` erase an item and draw its caption directly
to the destination DC, exposing an intermediate blank surface.

Microsoft documents the [flip-model restriction on layered GDI/HWND rendering](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model)
and the [DirectComposition layer below native child windows](https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-idcompositiondevice-createtargetforhwnd).
The proposed fix uses that explicit layer, preserving hardware rendering and
native controls rather than repainting every child after every canvas frame.

## Implementation

- `RenderSurface` uses a flip-sequential composition swapchain with a waitable
  frame-latency handle and a DirectComposition visual bound with `topmost=FALSE`.
  The existing device, D2D drawing, text cache, DPI units and idle frame scheduler
  remain. Child HWNDs, keyboard handling, native control identities and drafts
  are untouched. Both floating and docked tools use the same surface.
- Creation failure, destruction and device-loss teardown detach the visual,
  release its swapchain reference and release the HWND target. Recreating the
  surface must not leak the unique lower composition layer. Composition does
  not use the old HWND swapchain's automatic fullscreen association.
- Custom buttons/list rows render a complete item into an offscreen bitmap,
  then publish it with one blit. A failed/incomplete draw does not publish the
  intermediate bitmap. Allocation failure retains the original native draw
  path. GDI handles are scoped; focus cues and system palette remain native.
- The shared native UI CMake interface and standalone workspace test entry
  point link Windows' `dcomp` library. No portable model, Mac, persistence,
  plugin, audio or device-selection behavior changes.

## Bounded validation plan

One coordinated ARM64 build will include the app, workspace, native control,
native tool, parameter automation dock, graph curve and renderer probe targets.
The preceding main Intel interface harness completed at 03:39:38 UTC; do not
start this build before **2026-10-21 04:39:39 UTC**. No CI push in that window.

Run the targeted native checks before any foreground interaction:

- Control appearance pixels, native high-contrast branch, focus cues, text
  selection/Undo, offset item bounds, atomic publication and 400 buffered paints
  without GDI-handle growth.
- The real native tool fixture reserves the lower composition layer, verifies
  the upper layer is still available, presents 36 changing frames without
  invalidating an unchanged child, preserves partial text/selection/focus/Undo,
  resizes, hides/shows and recreates the surface three times. A duplicate target
  deliberately forces construction failure; subsequent reuse must still work.
- Existing retained tool docking, workspace advanced restore/context panel,
  sample-browser input, plugin search and pattern display checks, plus graph
  curve and parameter automation dock tests. No weakened assertions or skips.
- Existing actual-app idle rendering and API startup checks on a private desktop.
- Visible isolated QA: sample fields and keyboard traversal, pattern navigation,
  sidebar toggles, maximize/restore, graph and plugin control visibility, and
  retained-editor presentation. Recheck text/caret directly where accessibility
  snapshots report only the main window. Snapshots alone do not measure flicker
  rate or establish a sustained frame-pacing pass.

Keep exact source/executable hashes and original failures. Further builds wait
for the hourly gate. Broaden only for a failing affected owner or a new concern.

## Baseline inspection limits

Owned PID 40444 launched the unchanged Downloads package with
`--inspection --automation`, with path/arguments verified. No other ScreamSeq
process was present. Main controls and the empty sample library were visible.
Computer Use could observe the owned library but did not expose it as a distinct
target window; targeting main for text reactivated main instead. No successful
library typing or temporal flicker measurement is claimed from that attempt.
Alt+F4 closed the owned main process and its launch session exited successfully.
No project or preferences were saved, and hardware audio was disabled.

## Execution results

One coordinated build from clean source `f8183a7b1cf271fee5c38150b6f01b9b0862110d`
started **04:39:49 UTC** and completed **04:42:02 UTC** on 21 October 2026.
Configure and all seven Release targets passed, using the existing ARM64 cache
and two build jobs. Executable SHA-256:
`e511954c7457ecd2ecfc4d5f07b53403f24ace4998b883883dfb783000b732f1`.
The existing enum/floating-point warning in `PluginParametersWindow.hpp` remains.
No second build or CI push was performed. The next build gate is no earlier
than **05:42:03 UTC**, using this build's completion time conservatively.

All **nine selected native tests passed** in 73.83 seconds. This includes the
new composition/lifecycle checks and atomic-item pixel/resource checks, plus
every existing workspace, graph and retained-editor check selected above.
All **three actual-app checks passed** on one private desktop in 7.62 seconds:
idle rendering sleeps and edits wake it; guarded document/transport startup;
and the renderer probe draws 120 frames and reports a real GPU/window. The probe
explicitly does not qualify sustained presentation. Source and executable hash
were unchanged after testing.

Local receipts and original outputs are retained under
`bin/flicker-20261021/`: `build.json`, `BuildInfo.json`, `configure.log`,
`build.log`, `checks.json`, `native.xml`, `native.log`, `app.log` and the
build/check/package scripts. These generated artifacts are not committed.

### Visible packaged-build inspection

After the private-desktop tests finished, launched the packaged executable as
owned PID **40892**, with `--inspection --automation --seconds 900` and a new
report path. Verified the process path; no musician process was present.
Computer Use targeted returned main window **11143784**.

- Pattern, plugin-rack, sample waveform and graph-recipe controls were visible.
  Native buttons, selectors, lists and fields retained complete labels in the
  settled states, including after maximizing and restoring the main window.
- In sample Start, Ctrl+A selected `0`; typing `12` replaced it. Ctrl+B hid the
  sidebar while preserving the draft/caret; Backspace changed it to `1`.
  Tab then Ctrl+A selected End's `256`. Shift+Tab then Ctrl+A selected Start's
  `1`. Ctrl+B restored the sidebar with that selection intact. Escape restored
  the original range `0..256`. No song edit was applied or saved.
- The retained plugin library displayed its search field, selectors, buttons
  and populated builtin list. Its inspection-mode unavailable-preferences
  message was visible; no browser preferences were written.
- Accessibility continued to report the main root as focused even while text
  selection and editing visibly worked. This pre-existing accessibility gap
  is not claimed fixed. The floating library was observable but not separately
  targetable by Computer Use; its keyboard behavior is covered by native tests,
  not a claimed foreground typing pass.
- An immediate graph-tab transition capture showed old canvas content behind
  newly positioned controls; the next capture showed the correct settled graph.
  Separate HWND layout and GPU presentation are not an atomic frame transaction.
  These snapshots do not establish a temporal flicker rate or absence on every
  adapter. The verified mechanisms are lower-layer canvas isolation and complete
  owner-drawn item publication, not an exhaustive no-flicker guarantee.

Alt+F4 closed the owned QA process. `visible-qa.json` records 200.72 seconds,
22 drawn frames, 109 retained text layouts and zero audio callbacks/device
errors; `sustainedPresentationQualified` remains false. No hardware audio,
project save or user-session replacement occurred.

## Usable package

The optimized ARM64 executable, scanner, redistributable VC143 CRT DLLs and
retained attribution folders are in:

`C:/Users/P14/Downloads/ScreamSeq-Windows-ARM64-flicker-f8183a7b1/ScreamSeq.exe`

The package's executable has the qualified SHA-256 above. Every manifest file
hash was rechecked after the visible run; those same binaries were zipped,
without another build. ZIP SHA-256:
`a7ca7eaced9470b124a50c76e304c317fbfa9e47b5081b9cae78fd198531c9c8`.

The prior main package remains untouched. This fix is on the temporary
`codex/windows-flicker` branch based on shared main; it has not been pushed or
merged. No Mac or shared model inputs changed, so a redundant Mac build/full
musical suite was not run for this Windows-only patch. This targeted result
does not supersede outstanding full-suite or cross-platform parity work.
