# Cursor redraw and flat selectors — 2026-09-22

The main workspace ran `layoutControls()` on cursor navigation. It repositioned
and showed every child HWND, rewrote unchanged labels and combo selections, and
explicitly invalidated every control. Some plugin/note buttons were also enabled
and disabled again in the same layout pass. That generated unrelated native
redraws on each cursor step.

`App/NativeControls.hpp` now retains requested control geometry, including a
combo's dropdown height (its closed HWND height is different), and applies only
changed positions, visibility, text and selection. Active workspace buttons are
invalidated when their active state changes. Label and enabled-state updates in
the inspector no longer oscillate. The modeless shell uses the same retained
updates and recreates fonts only when DPI changes.

Workspace and modeless dropdowns share a flat dark surface, thin border, a
chevron and a teal focus outline. Popup items use the same colors. The underlying
Win32 combo retains selection, type-ahead, popup and accessibility behavior;
high contrast uses system colors/native surface. Native popup keyboard handling
takes priority over editor shortcuts, so Escape dismisses a popup before closing
the editor. No musical editing, project format or audio callback code changes.

## Evidence

- ARM64 Release build: `bin/windows-flat-controls-build-final.log`.
- 20 focused native-control/workspace application tests pass:
  `bin/windows-flat-controls-focused-4.log`, with private-desktop isolation log.
- The actual app's Down-key handler moves 32 rows while 53 unchanged controls
  receive zero additional paints, owner draws, position changes, text writes,
  selection writes or enable changes. The song revision remains unchanged.
- Changed active buttons and labels still redraw. Main and modeless selectors
  retain arrow-key selection, F4 opening and Escape dismissal.
- Native CTest: 31/31 pass, 13.31 seconds,
  `bin/windows-flat-controls-ctest.log`.
- `native-control-tests` renders real same-process combo HWNDs to DIBs on an
  owned private desktop. Normal/focused/disabled/empty appearances were inspected
  at 192 DPI (200%): `bin/windows-flat-controls-evidence/*.bmp`. A 200-paint loop
  showed no GDI object growth; 100 repeated unchanged layouts/selections emitted
  no extra corresponding messages. Foreground and clipboard checks pass.
- Cross-process PrintWindow/WM_PRINTCLIENT cannot capture the private desktop
  here; initial capture attempts were rejected or untouched white buffers.
  Evidence above comes from the same-process native test, not those attempts.
- First full isolated application run: 266/268 pass in 590.967 seconds. Two
  existing harness assumptions failed: the live mixer test intentionally stops
  audio but omitted `--audio-test-allow-stop`, and the settings test could submit
  its next native action while Refresh was still pending. The mixer now opts
  into intentional stops; settings actions await the published pending state.
  A focused run passes all 16 runnable cases including repeated failing cases;
  one provider-fixture case was skipped in that focused run (the full run has
  its fixture configured). No production audio/worker changes were needed.
- Final full isolated application suite: **268/268 pass**, no failures or skips,
  587.728 seconds. Logs:
  `bin/windows-flat-controls-final-full-app-tests.log` and
  `bin/windows-flat-controls-final-full-isolation.log`. Strict foreground and
  clipboard isolation checks pass. The tested executable SHA-256 is
  `05247179DCD1F521E04C4549743658B4DE1A69C0D13B1EBF19891F1E2C935EB5`.

The separate ARM64 checkpoint is
`bin/windows-checkpoints/flat-controls-20260922/`. Its manifest records the source
commit and verifies packaged file hashes. Save the running song before switching
from the previous executable to this build.

This verifies the redundant-redraw cause and the shared control renderer. It is
not a foreground full-window visual/animation or sustained frame-rate claim.
The musician's `audio-settings-20260922` checkpoint process was left running.

## Mac coexistence

Both frontends already exist together on `codex/windows-native`. `editor/` and
`editor/hosted/` own shared document, history, DSP, scheduling and plugin/routing
orchestration; `mac/` owns AppKit/Metal/CoreAudio/AU/Mac VST3 adapters, and
`windows/` owns Win32/Direct2D/WASAPI/Windows VST3 adapters. Portable sources still
under historical `mac/Audio/` paths are compiled once by the shared hosted target.
Mac CMake already consumes that target through its `TrackerPlugins` forwarding
target. There is no requirement for permanently separate platform branches.

The native UIs and application/session adapters are still separate implementations
and Windows feature parity is unfinished. Both target native project container 6
and metadata 17; the Windows VST3 component/controller state wrapper preserves the
Mac representation. Binary paths and AU availability are platform-specific. The
shared hosting extraction changes Mac source too, and still requires an actual
Mac build, AU/VST3 qualification and reciprocal project round trips. Source
coexistence and Windows tests do not establish a validated Mac build.
