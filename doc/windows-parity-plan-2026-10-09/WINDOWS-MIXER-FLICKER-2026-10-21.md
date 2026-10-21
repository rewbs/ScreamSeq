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

## Correction qualification and production package

Frozen source `6bea5c08ff9fa9eec49df5f50b48fe7953a5c73f` built once, on
2026-10-21 from **07:27:33 to 07:29:40 UTC**, after the hourly gate. All five
planned targets compiled successfully. The existing PluginParametersWindow
enum/floating-point warning remains. No additional build was run. A subsequent
build must start no earlier than **08:29:41 UTC** under the conservative gate.

The candidate passed **17/17 native gates** (281.49 seconds) and **9/9
actual-app checks** (19.16 seconds). Source and executable hashes were unchanged
through qualification. Receipts and original logs are retained in
`bin/mixer-flicker-20261021/correction/` in the primary checkout:
`build.json`, `native.xml`, `native.log`, `app.log` and `checks.json`.

- Mixer previews again produced **zero neighboring layout messages across 24
  previews**, versus 24 in the original failing baseline. Native checks also
  observed zero neighboring position/layout/paint messages across 20 fader and
  meter previews, with focus retained. These are measured native-message
  regressions, not a screen recording or display-frame timing measurement.
- All four slider types passed deferred mouse/key release checks while another
  worker operation held admission, including exactly one final write, Cancel,
  Escape precedence and stale revision refusal. The previously failing keyboard
  width and Cancel checks now pass. Actual-app mixer Undo/Redo, retained drafts
  and native save/reopen passed.
- Parameter automation, graph-source automation and envelope-template drags all
  retained sampled curves during movement and restored them after Escape. The
  parameter editor additionally passed release/save, one-step Undo, Redo and
  native reopen checks. Native local-preview checks cover interpolation modes,
  scripted timing, invalid formulas and focus/capture without worker writes.

The separate production ARM64 Release package is:

`C:/Users/P14/Downloads/ScreamSeq-Windows-ARM64-ui-fixes-6bea5c08f/ScreamSeq.exe`

Executable SHA-256:
`4c6f23d4abd82520175fb576e0eb37507c01c3a6edb1ce5fea837c91c4cbd86c`.
The package includes the scanner, ARM64 CRT, retained OpenMPT/third-party/VST3
notices, `BuildInfo.json` and `ReleaseProvenance.json`. All 22 listed packaged
files were hash-checked; older Downloads builds were preserved. The correction
`package.ps1` uses the new receipts and commit, not the failed candidate's paths.

### Live UI qualification remains blocked

There were no running ScreamSeq processes before launching the packaged
executable as owned PID 37944 with `--audio-test-silent --automation
--audio-test-allow-stop --seconds 900` and a private report destination. Its
exact executable and command line were verified. Computer Use found the QA
window, but its capture showed wallpaper and activation failed twice with
`GetCursorPos failed: Access is denied. (0x80070005)`, including one fresh-window
recovery attempt. No further desktop input was attempted. The user was asked
to unlock the desktop.

A read-only API snapshot confirmed actual silent playback: 12,820 callbacks,
6,153,600 frames (128.2 seconds at 48 kHz/480 frames), maximum callback
3,837.9 microseconds, no reported callback overrun or processor fault. This
snapshot is **not** a completed audio qualification report. Only the owned,
unchanged disposable demo process was terminated after saving the snapshot;
no musician session or system audio default was changed. The expected normal
shutdown report was not produced. Details are in `live-check.json`,
`live-transport.json` and `live-workspace.json`.

Visible gain/balance/pre-balance/width interaction, automation point dragging
and keyboard focus on this final package remain to be checked after desktop
access is restored. Automated focus/capture and drag checks passed, but no
claim of live temporal flicker elimination or sustained frame pacing is made.
This outstanding UI check needs no rebuild: reuse the packaged executable and
hash above. No push or merge was performed in this follow-up.

### Resumed live interaction check

After the user restored desktop access and requested continuation, the same
packaged executable/hash was launched as isolated PID 37760 with silent output,
automation and a private report path. There were no existing ScreamSeq
processes at launch. No build or test-suite rerun was needed.

Computer Use successfully exercised gain, balance, pre-balance and width during
demo playback. API reads confirmed Track 1 (`n2`) retained the edits: gain
-57 dB, balance 0.75, pre-balance approximately 0.42, width 1.50. After dragging,
arrow input remained on the gain fader (-57.1 to -57.0 dB), and another Left
arrow on width changed the saved value from 1.50 to 1.49. All four sliders
accepted drag and subsequent arrow input. No lost edit or busy-worker error
was observed in this bounded interaction.

After stopping playback, a guarded API setup added the built-in Gainer and
the same two-point gain curve used by the regression. Computer Use dragged
the first point in the floating inspector from row 8/value 0.25 to row
10/value approximately 0.379509. The resulting curve was visible and saved.
The inspector was then docked through the supported workspace API; another
drag moved the point to row 12/value approximately 0.518868. Right-arrow input
then moved the same selected point to row 13 (position 3328), with focus on
the automation canvas, 1024 preview samples, no pending operation, no dirty
draft and the saved status. The other point stayed at position 8192/value
0.75. This verifies the real drag/release path and docked keyboard focus.

The floating inspector is not independently enumerated by Computer Use;
targeting its parent for keyboard input activated the main window instead.
That attempted floating-key check is inconclusive, not an application focus
failure. The docked keyboard check above and the passing native focus/capture
checks provide the relevant verified evidence. Live captures occur after
each drag, so continuous mid-drag geometry remains supported by the three
automated regressions rather than by a frame-by-frame screen capture.

The QA session closed normally, discarding only its disposable edits.
`live-resumed-playback.json` records 370.71 seconds of total process activity,
6,165 drawn frames and zero occluded frames. Audio ran for 15,494 callbacks
(7,438,080 frames, 154.94 seconds at 48 kHz/480 frames) before being stopped
for the envelope check. Maximum callback time was 4,577.4 microseconds, with
zero processor faults, deadline overruns, starvation indicators or device
errors. These results are bounded to this demo and interaction sequence;
`sustainedPresentationQualified` and `realtimeAuditPassed` remain false.

The requested live mixer/envelope interaction check is now complete. The
measured repaint regressions and these live interactions support the fixes;
they do not establish display-frame pacing or eliminate every possible
flicker source. Detailed API snapshots are `resumed-mixer-*.json`,
`resumed-envelope-*.json` and `resumed-docked-key.json` beside the existing
qualification receipts. No product source changed, and nothing was pushed
or merged during this resumed check.
