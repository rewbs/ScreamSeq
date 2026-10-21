# Windows native-control flicker

## Scope and source finding

The user reported widespread Windows UI flicker in the ARM64 Release package
from main `b98631b48`. That package has the same product source as `55d11e131`.
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

Execution results will be appended after the new build and recheck.
