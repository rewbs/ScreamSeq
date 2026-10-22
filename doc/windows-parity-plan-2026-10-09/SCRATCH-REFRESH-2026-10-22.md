# Scratch inspector refresh after external history

## Parameter refresh follow-up, 22 October — source only

The same batch now protects automatic plugin-parameter reads with a short native
input lease instead of disabling their focused controls. Search/value fields keep
their HWND and caret selection; a focused Reload button also remains enabled.
The read still guards document/revision/generation, and a revision that changes
during a background read retries the read without forcing a permanent Reload.
Dirty/uncertain controls retain their previous protection. Explicit Reload and
durable writes retain their existing admission rules. This is a source-confirmed
focus hazard, not an explanation proven for the intermittent timeout below.

`applicationParameterRefreshFocus` covers focused numeric/search/button controls,
reentrant input rejection, busy reads and changing-context reads. The existing
parameter-preview group executes it. These new assertions are unbuilt. Add the
two actual-app rack/search cases and all three plugin-recording cases to the four
scratch cases at the next checkpoint (nine cases total). No additional build.

Current Windows x64 CI run39784093856 passed158 portable/worker and49 native
UI/preferences/recovery registrations. Its audio diagnostic returned devices=[];
the merged92-case app group retained15 failures (12 unique playback-dependent
methods) and2 skips. The exact CI binary, SHA256
`f33c65d4f96520f1ba4df9fc9e66bbf7d4e81d277242e4163728c1e2aa62698a`, was downloaded
and hash/PE-verified locally. Synthetic merge d10e53f1 has exactly the1fb source
tree. Local silent-WASAPI execution passed11/12 affected cases; the plugin take
Keep/Undo/Redo case timed out in the fixture's inspector idle wait after Redo.
One focused diagnostic and three bounded repeats passed unchanged assertions.
The original failure is unresolved, not erased by those passes. Improved timeout
diagnostics retain expected/current document and full workspace state for the
next occurrence. Receipts/logs: `focus-followup/ci/windows-x64/`. These runs used
the unchanged x64 CI executable under ARM64 emulation, with the653 fixture-only
long-path helpers; they do not qualify native x64 hardware or realtime behavior.

## Observed on 22 October, 13:18–13:28 UTC

The 13:07 coordinated checkpoint built source
`1fbfebd7346013926b908250390b8dd0b5af31a7` once. ARM64 Release executable SHA256:
`9cdd7271e4a26102f9561f07533eb966e4a0a89d0735023a678eda8322f7f56c`.
Receipts are in `bin/mac-gesture-20261022/focus-followup/`.

Six native groups passed, including the repaired scratch focus/caret admission
case and deep recovery storage. The original 23 app/controller cases produced
21 passes and two Python long-path fixture errors. Commit `653b2697a` corrects
only fixture access/cleanup; those same two cases then passed against the
unchanged executable. `qualification.json` combines the exact 21+2 results,
without replacing the failed original logs. This is not a second build.

Fresh Computer Use controlled disposable PID 38636, with a copy of F04 and
silent 48 kHz / 480-frame output. It observed:

- Tools → Pattern → Scratch phrases opens the paired-curve window. Dragging
  the middle motion point changed it from 0.5 cycle / 100% to 0.5625 / 71.725%,
  retained the rendered curve and reported that linked uses were saved.
- Ctrl+Z addressed to Main consumed that one history entry, but the still-open
  scratch window continued to show the old curve and saved status. This is a
  clean stale inspector, not evidence that musical Undo failed.
- Arrow keys selected EQ10 in the native rack list. Its dock search retained
  `frequency`, Backspace produced `frequenc`, and Tab / Shift+Tab followed by
  typing `y` restored `frequency`. Ctrl+A / Backspace cleared the field.
- Params exposed ten controls on page 1 of 5. The output gain slider applied
  5.1264 dB during silent playback. Main Undo consumed the entry; Reload values
  showed 0 dB. A screenshot immediately after reopening showed the old value;
  it did not test the existing 250 ms parameter refresh timer. Do not classify
  this observation as a confirmed plugin timer failure.
- Maximize/restore kept the native workspace readable. Ctrl+Alt+P followed by
  Right changed Pattern's active field from Note to Instrument. The private
  song saved, the process exited normally, and original F04 bytes were unchanged.

Computer Use treats floating tools as related windows and key injection targets
Main. Native focus/caret tests supply the scratch typing evidence; this session
does not establish direct floating-field typing continuity. Screenshots do not
qualify temporal flicker. The loaded silent session reported 35 deadline
overruns in 429.317 seconds, zero device errors/starvation indicators, and
`realtimeAuditPassed:false`; it is functional UI evidence, not a realtime pass.
No production package is approved by this foreground receipt yet.

## Reproduced baseline and source correction — unbuilt

The new actual-app case
`ScratchEditorTests.test_main_history_refreshes_clean_curve_without_retargeting_or_losing_draft`
fails on the unchanged 1fb executable: after an external curve mutation, the
inspector remains `stale:true`, `dirty:false` with its original points for five
seconds. `scratch-refresh-baseline-ready.log` preserves that failure. An earlier
setup-only attempt lacked TMPDIR; its log is retained and is not product evidence.

`Main.cpp::refreshDocument` now notifies the scratch owner. Its event-scheduled
refresh reads only a clean same-document phrase after the worker returns. It
preserves lane and viewport, keeps point selection only when the same cycle
position still exists, and does not create history. A deleted phrase becomes an
empty selection rather than silently choosing another phrase. It revalidates
the original stable pattern/track/cell and command; cursor navigation cannot
redirect it. Changed captured commands still require explicit recapture.

Raw field edits, active drags, queued saves, unknown write outcomes and retained
formula work keep their captured revision and contents. Focused text fields
defer clean refresh; no refresh steals focus or disables their HWND. Escaping
an unfinished point schedules the deferred read. Hidden tools stop their timers.
No song data, API mutation, audio processing or project format changes.

## Next bounded checkpoint

No build before **2026-10-22 14:07:23 UTC**, including PR-triggered CI. The local
build started 13:07:18 UTC; paired Mac/Windows runs started 13:07:22. Inspect
receipts before execution. Current PR remains 1fb; backup branches need no CI.

Build ScreamSeq and workspace-restore-tests together once. Run native
workspace-command-result-tests (new scratch refresh/focus assertions),
workspace-parameter-preview-tests (new explicit clean timer assertion), and
workspace-departure-tests. Run all four actual-app scratch cases, including
the previously failing baseline. Reuse unchanged recovery and recording results
from this checkpoint; broaden only for a failure or another affected subsystem.

Fresh foreground checks must repeat scratch drag → Main Undo/Redo with the
inspector visible, a clean external edit, raw invalid text retention, and normal
keyboard navigation. Check plugin Undo after the existing refresh interval,
not just immediately after reopening. Preserve the private process and original
fixture discipline; never run private-desktop input concurrently with Computer
Use. Package only after these gates pass, retaining scanner/runtime/notices,
exact source/hash and documented limitations. Mac/Windows CI and final native
audio/reciprocal integration requirements remain separate merge gates.
