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
