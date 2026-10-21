# Mixer playback flicker follow-up

The user reports that the previous Windows rendering fix did not resolve the
flicker, specifically when moving mixer sliders during playback. The prior
composition and item-buffer tests did not exercise that interaction. Their
passing result must not be described as proof that this complaint was fixed.

## Reproduction and source findings

The packaged `f8183a7b1` executable (SHA-256
`e511954c7457ecd2ecfc4d5f07b53403f24ace4998b883883dfb783000b732f1`)
was launched as owned PID 15988 with `--audio-test-silent --automation`.
Computer Use opened and enabled the disposable demo mixer, then dragged the
Track 1 gain fader during real playback. No output defaults were changed.
The 209.87-second run recorded 48 kHz/480-frame audio, 20,985 callbacks and no
processor fault, deadline overrun or device error. Its screenshots do not
measure temporal flicker. The QA demo was closed without saving.

The new actual-app regression, run against that unchanged executable on a
private desktop, **failed**: 24 fader previews sent 24 `WM_WINDOWPOSCHANGED`
messages to a neighboring unchanged slider. Original output is in
`bin/mixer-flicker-20261021/baseline-regression.log`.

Source explains two mechanisms:

1. `MixerStripsWindow::controlScroll` sets the numeric gain field. Its synchronous
   `EN_CHANGE` reaches the base owner, which calls `layout()` even when the
   derived action correctly ignores a programmatic write. Layout resets other
   trackbars with repaint enabled and cycles every child's Z order for Tab order.
2. `Application::await` presents busy immediately on every worker round trip.
   During playback, `draw()` calls `refreshContextPanel`, whose availability stamp
   switches with `busy`. Fast preview calls therefore alternate sidebar enabled
   states and window captions even though no lasting editor availability changed.

## Changes

- Suppress mixer layout while publishing native field values, compare trackbar
  positions before setting them, and only reorder native children when the
  required Tab sequence changes. Retained HWNDs, focus reservations, keyboard
  traversal, stable bus identity and gesture semantics remain.
- Keep worker admission immediate, but defer its visible busy state for 150 ms.
  Short calls preserve the native title and sidebar/toolbar appearance; sustained
  calls still display busy and restore availability on success or failure.
  API/command checks and menu admission continue using the actual `busy` flag.
  Identical window titles are no longer written repeatedly.
- Add native checks for neighboring slider position/layout/paint, fast and slow
  worker presentation, failure cleanup and reentrant admission. Add an actual-app
  neighbor-layout regression alongside existing preview/history/save/reopen checks.

## Validation scope

One ARM64 Release build of app, workspace harness and mixer-strip harness.
The previous build completed at 04:42:02 UTC; this batch starts after 05:42:03.
Run targeted workspace presentation, restore, menus, departure, command receipts,
transport and pattern-paste checks, the full mixer-strip harness and actual-app
mixer tests. Repeat visible silent-playback fader/balance interaction and keyboard
focus checks. Preserve failed evidence and report any remaining limitation.
No shared model, Mac, audio callback, project format or plugin state changes.

Results and executable provenance follow after execution.
