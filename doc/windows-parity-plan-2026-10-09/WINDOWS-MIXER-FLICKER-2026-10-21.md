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

## First candidate results and corrections

Candidate `5ea1aec9644dbf27846ab5f87230622b5a0aa8b2` built successfully
from 06:22:49 through **06:24:41 UTC on 2026-10-21**. Its ARM64 Release
executable hash is `39f59ead10b09061e8fcda4e07e477fff9a747d228c2afe23234ad21e94a0fac`.
The next build must start **no earlier than 07:24:42 UTC**, retaining the user's
one-build-per-hour limit. Receipts and complete logs are under
`bin/mixer-flicker-20261021/` in the primary checkout.

- 14 of 15 selected native tests passed, including the new short/long worker
  presentation test, context panel, restore, departure, receipts, command result,
  menus, transport, pattern selection, sample input and plugin search checks.
  The mixer harness failed its existing Cancel action assertion.
- 5 of 6 actual-app checks passed. The same 24 previews now produced **zero**
  neighboring slider layout messages. Preview/history/save/reopen and retained
  drafts passed. The keyboard width commit check failed (saved width 1.0 instead
  of 1.5). A bounded diagnostic repeated the timing-sensitive width failure five
  times in 20 iterations. No failed assertion was relaxed.
- A 205.48-second visible silent-playback run on owned PID 40132 exercised gain
  drag, gain arrow key and balance drag. The gain key changed -30.1 to -30.2 dB;
  the API confirmed the gain drag's captured bus and completed write. Audio ran
  at 48 kHz/480 frames, with 20,545 callbacks and zero reported faults, deadline
  overruns, starvation indicators or device errors. This is bounded interaction
  evidence, **not temporal flicker or sustained frame-pacing qualification**.
  The disposable demo was closed without saving. No candidate package was released.

Corrections awaiting the next coordinated build:

1. Update Cancel availability explicitly in `enableEdits()`. It previously
   depended on the redundant readout-triggered layout being removed.
2. Supply worker availability to the mixer strip owner. Defer an unsubmitted
   release/Enter or Escape while another worker call pumps input, then service
   it after admission is free. Preserve the original document/revision and
   generation checks. Never retry a submitted/uncertain write. Skip previews
   while an unrelated worker operation is active. Expose `queuedInput` in the
   inspection snapshot so bounded UI waiters observe this real pending intent.
3. Add deterministic native coverage for all four slider kinds, mouse/key
   release, Cancel availability, Escape precedence, stale revision refusal and
   exactly one write. Extend the real Application `await` harness to pump a
   width key release during an unrelated read and verify its saved result.

The correction batch must rerun the complete mixer-strip harness, worker
presentation and context/command/departure checks, plus actual-app mixer tests.
Reuse the first candidate's unchanged broader restore and presentation evidence
unless new failures or further edits invalidate it. After passing, run isolated
visible playback with gain, balance, pre-balance and width, including keyboard
focus. Package a separate production Release directory with matching provenance;
do not replace either existing Downloads package or the musician's process.

## Envelope drag rendering (same pending build)

The user additionally reported that moving automation points makes the envelope
disappear. Source confirms that ParameterAutomationWindow, GraphCurveWindow and
EnvelopeBankWindow clear their sampled values on each move, then defer the
worker preview until dragging ends. The native line renderer therefore has no
curve to draw throughout the gesture.

Three new actual-app regressions were run against the unchanged first candidate
(`5ea1aec96` executable above). All three failed specifically with
`previewSamples == 0` during a drag: parameter automation, graph-source curves
and envelope templates. The log is `bin/mixer-flicker-20261021/envelope-baseline.log`.
This reproduced the reported disappearance without a new build.

`windows/App/AutomationDraftPreview.hpp` now provides a UI-owned preview of the
current unsaved points. It reuses `Tracker::automationValue` and `CurveFormula`,
including the captured span and rows-per-beat clock, samples at a fixed 257
positions, and caches samples/formula programs across unchanged layouts and
point moves. It calls no worker, audio or history API. `AutomationCanvas` uses
this fallback when a visible participating editor has no validated worker
samples. Ordinary worker validation remains, as do existing save-on-release,
Undo, revision, focus/capture and bank Save master semantics. Hidden preparation
does not start preview sampling. Invalid formulas produce no misleading
replacement curve and retain the normal validation diagnostic path.

Only the three affected automation owners opt into this fallback. Instrument
and absolute/song automation already build their curve geometry directly from
points. Scratch gesture canvases have their own cycle timing and are unchanged.
No Mac, DSP, native project, plugin state or musical API mutation changed.

Additional gates for the coordinated correction build:

- Build `parameter-automation-dock-tests` and `graph-curve-window-tests` together
  with the already planned app/workspace/mixer targets, then run both complete
  native suites. The parameter suite now checks all eight built-in interpolation
  modes, scripted beat timing, invalid/corrected formulas, bounded samples, live
  point changes and Escape with focus/capture retained and no worker/write calls.
- Run the three new actual-app regressions. Parameter drag also checks one Undo,
  Redo and native save/reopen after release; the other two check visible draft
  geometry, unchanged saved history during movement and restored geometry after
  Escape. Existing assertions were not weakened. Python syntax and `git diff
  --check` passed; new C++ and passing executable evidence remain pending.
- Include an automation-point drag in isolated live computer-use inspection of
  the final candidate. Update the follow-up automation and frozen build-script
  commit to include this batch before the 07:24:42 UTC build gate.
