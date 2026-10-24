# Instrument editing and envelope-bank handoff

Status: source-only continuation after `84fac64a3`. No build or runtime pass is
claimed here. This joins the next coordinated recorder checkpoint, not a separate
build. Preserve the09:05:43 UTC minimum gate and recheck actual CI starts and
terminal status before dispatch.

## Observed source problems

`windows/App/InstrumentEnvelopeWindow.hpp` already queues ordinary edits through
`instrument.patch`, but its opening hint still instructs Apply. Its layout sets
Use preview/Retry changes, then overwrites that label with Apply instrument (or
Apply in a short dock). Its context menu always exposes Apply even for a clean
automatic editor. A successful explicit retry leaves the base failure flag set,
so the retry action can remain visible after the edit is saved.

A new envelope bank rejects pending instrument properties before the ordinary
600ms edit timer runs. The corresponding pattern-automation handoff already
flushes queued edits first. Existing instrument banks intentionally retain their
independent source; tool previews intentionally remain unaccepted drafts.

## Changes and boundaries

- A new instrument bank flushes an already-queued ordinary edit, then rechecks
  pending work, creation state, source revision and incomplete fields. Opening a
  retained bank still raises the existing capture without flushing or recapturing.
  Opening from a pure envelope tool preview does not accept that preview.
- Automatic edits wait while the native context menu tracks. After a validated
  menu choice returns, tracking ends before the chosen handoff flushes edits.
  Existing target/generation/revision guards remain; external changes still reject
  the menu action.
- Opening/preview instructions and native buttons/menu entries use their actual
  semantics. The explicit action appears for a preview or failed automatic edit.
  Successful reload/commit clears an obsolete retry flag.
- Shared instrument operations, history, persistence and audio are unchanged.
  Invalid/stale edits remain drafts; failed writes are not replayed by a timer.

## Required bounded checks

The next local selection adds `instrument-short-dock-window-tests` and
`workspace-automatic-edit-tests` to the three frozen recorder/export groups.
The short-dock group asserts preview labels in short and wide layouts, no implicit
preview acceptance, invalid raw-value retention, no automatic retry after a
refused patch, and clearing the retry action after one successful explicit retry.

The real Application/worker automatic-edit group adds ordinary and preview bank
journeys: invalid text/caret/focus and music are retained; pending properties save
before a new capture; reopening preserves the capture; saved bank points match
the current editor exactly; sample PCM and other instrument/envelope settings
remain unchanged; bank Save and the automatic property batch are separate Undo
steps. The preview branch asserts that the saved instrument remains unchanged.

Two actual-app native-menu cases run locally and in the existing Windows CI
integration step (124 cases total after the prior122). The old manual-Apply
canvas case now waits for ordinary save completion and checks each separate edit,
two exact Undo/Redo steps, absence of redundant Apply and native save/reopen. The
bank case traverses Properties → native menu → bank, saves the exact shape and
checks separate bank/property history. The shared context fixture now prepares
its ADSR using automatic completion; no test presses a hidden Apply to establish
this baseline. Python parsing and `git diff --check` are cheap pre-build checks,
not execution evidence.

Fresh Computer Use should include the visible Properties-to-bank route, automatic
name/volume changes, invalid-field caret, preview acceptance versus Retry, and
short/wide layouts in the isolated QA process. Existing recorder keyboard checks
remain required. Keep sustained audio, current Mac gates and reciprocal project
evidence separate; no packaging or main merge follows from this source batch.
