# Precise-note timeline: immediate composing, 2026-10-23

## Scope and evidence status

User request: double-click insertion, selected-hit Backspace, visible playback
cursor, immediate edits without Write note or interrupted playback. This takes
priority within the ongoing Windows/macOS parity work. Includes the preceding
immediate MIDI Arm source batch, previously committed as `0575d1b68`.

At initial writing this source is **unbuilt and unqualified**. Last passing local
package remains `c900d2b2e`; do not infer its evidence qualifies these changes.
Last coordinated starts were local 04:57:47 UTC and CI 04:58:20 UTC; the next
checkpoint cannot start before 05:58:20 UTC on October 23, or while prior CI runs.
Inspect actual receipts and later starts before building. One coordinated build
checkpoint per hour, including CI; never repeat a recorded build/check.

## Observed causes

- `PreciseNoteOwnerInteraction.inc` already registered double clicks, but the
  insertion path constructed an event at the previous occupied offset before
  assigning the clicked position. The first click also discarded its field seed.
- Only Delete on the visible canvas removed a draft; neither Backspace nor the
  native hit list had the requested action.
- The precise owner had no playback telemetry or cursor rendering.
- The owner required Apply; `PatternOperations::pattern.notes.set` explicitly
  stopped playback before `editNative`. Existing graph publication deliberately
  rejected precise-note changes. Removing the stop alone would leave stale audio.

## Source design

- `PreciseNoteOwnerModel/Interaction/Window/Layout/Presentation` retain the single
  captured native owner. Click position is validated directly; text fields keep
  their editing keys. Discrete gestures and completed drags save immediately,
  typed values debounce 300 ms, invalid/incomplete fields stay raw. Check remains
  a dry run; Save now is a manual flush/retry. Closing flushes valid queued input.
  Unadmitted busy fields can wait; failed or uncertain writes are never timer
  retried. Revision and draft-generation guards remain enforced.
- `Main.cpp` forwards existing audio telemetry (256 units/row) to an independent
  playback offset. Only the matching document/pattern/row shows a white cursor.
  Cursor repaint changes no field, selection, target, focus or revision.
- `editor/PreciseNoteRuntime.*` separates immutable compiled schedules from the
  active cursor/effect-override state. Adoption seeks at the current engine
  position; it does not replay past events or reset held voices. Existing loop,
  jump, release-before-onset, per-hit effect and exact sample scheduling remain.
- `Renderer` publishes `PreciseNoteUpdate` (schedule + companion cell edits) via
  the FIFO variant of `RealtimePlan`. One plan per render boundary, at most three
  queued plans, at most 512 companion edits admitted by the native host. No audio
  callback allocation/free/lock or document/UI access. Producer owns reclamation.
  FIFO is essential: skipping intermediate plans would lose cell conversions.
  A marker in the existing ordinary-cell FIFO orders both payload kinds, so a
  later ordinary write cannot be overwritten by an earlier precise conversion.
- `Document::editNative` stages all history allocation before an optional
  publication callback; refusal rolls back that entry without consuming Redo.
  Undo/Redo can explicitly admit already-prepared cells, while retaining the
  default rejection for unprepared cells and all structural/sample entries.
- `HostedProject`, `DocumentController` and `PatternOperations` integrate this
  path for guarded API edits and precise conversion Undo/Redo. Unsupported
  combined edits keep existing guards. No wire/schema/storage/identity changes,
  no new UI/audio framework, no plugin-state changes. Mac uses the shared core
  without changing its native controller behavior.

## Required bounded checkpoint

Build app/scanner, precise-note-window, MIDI-window, document-controller,
hosted-project and document-history-allocation targets together. Run:

1. Native precise owner (including real double-click sequence on populated row,
   canvas/list Backspace, text exclusion, auto field save/focus/caret, stale/raw
   guards, cancel, density, dock/float and cursor isolation).
2. Document controller live-native gate: publication refusal, atomic ordinary
   conversion, live Undo/Redo, no-op and unchanged transport.
3. Hosted-project gate: exact PCM versus preconfigured song at 44.1/48/96 kHz and
   17/128/511 frame partitions; no past onset replay; FIFO/overflow; callback C++
   allocation/free audit. Existing renderer/plugin tests remain required.
4. Shared history allocation sweep, including edit admission and prepared mixed
   cell Undo/Redo. Mac precise-note test adds shared live-schedule PCM/audit at
   three rates and 17/128/4096 frames; Mac ARM/Intel CI is required before merge.
5. All precise-note editor/host app cases plus `test_precise_note_live`: real
   silent playback epoch/frames, advancing cursor, automatic conversion, one
   Undo/Redo, populated-row insertion/deletion, unrelated row preservation and
   exact save/reopen. Update old draft-only expectations to automatic behavior;
   retain invalid/stale/collision/Undo/persistence assertions.
6. Preceding immediate MIDI Arm gates: window suite, five actual MIDI UI cases,
   timestamped chord and stopped-step/loss journeys. Plugin recording source is
   unchanged; reuse c900 evidence only within that unchanged scope.

After passing: Computer Use on a fresh isolated QA process, silent row-loop
playback; double-click in empty/populated row, drag/release/cancel, Backspace on
canvas/list versus text, auto field change, Tab/Shift+Tab, dock/float and resize.
Observe a moving cursor and uninterrupted transport. Preserve musician processes
and routes. Screenshots alone do not prove temporal flicker or audio performance.
Package only passing automated + fresh foreground evidence with exact source and
hash. Required Mac gates, native device/performance and parity work remain open;
this batch does not authorize a main merge from partial evidence.

## Preceding checkpoint CI, inspected 05:44–05:46 UTC

`c900d2b2e`: Mac run 39903133011 passed ARM and Intel; Windows run 39903133025
compiled, passed 50 native registrations and all four MIDI UI cases (including
the previously failing keyboard-settings case). Its 96-case app integration
stage reported 18 failures / one optional skip, involving playback/recording/
audition on a runner reporting no WASAPI endpoints; the later 39- and 18-case
stages passed. Preserve these failures as unqualified device evidence. No reruns,
assertion weakening or new CI dispatch occurred during this inspection.

## First precise candidate: compile failure and correction

Frozen `f28a6eff8c2280a078e5b42b8025cbff6ab3823a` was pushed to PR7 and built
once at 05:58:40 UTC. `bin/mac-gesture-20261022/precise-live/build.json` and
`build.log` preserve the attempt. Configuration passed; compilation stopped at
`MidiRecordingWindow.hpp:200` with MSVC C7692: the preceding immediate-arm
change compared a `std::string` against a JSON revision. No application hash,
automated acceptance or fresh foreground qualification was produced. Do not run
that folder's check/launch/package scripts against an older executable.

The correction explicitly extracts the revision as `std::string`. It also makes
the remaining precise-editor status text consistent with automatic saves and
the **Save now** action, and adds two native timer regressions: busy admission
waits without a submission then saves once with caret retained; an acknowledged
document commit followed by a failed reply retains the draft and never repeats
the write. Explicit recapture reconciles that uncertain result. These assertions
remain unrun and must pass in the next candidate.

The same frozen commit started Mac ARM at 05:58:59, Mac Intel at 05:59:09 and
Windows x64 at 05:59:10 UTC. Runs 39908756862 / 39908756796 were still running
at 05:59:43. The next coordinated checkpoint is **not before 06:59:10 UTC**,
and must inspect `inspect-precise-live-ci.py` for final results and later starts
before proceeding. Do not restart those jobs or rebuild to bypass the limit.

Use the separate `precise-live-correction` receipts for the corrected source.
Retain the same five native groups and actual-app selection, with the added
timer assertions included in the precise-window group. After passing, the
foreground helper prepares only a disposable, silent row-loop process; verify
real double-click, Backspace, typing and Tab/Shift+Tab, cursor advancement and
unchanged transport. Preserve the musician's running c900 package/process.
The previous usable executable does **not** include these timeline changes.

## Source continuation: editing after Undo without Reload

Source inspection found `followWorkspaceEditors` deliberately skipped pinned or
focused owners. After immediate saves this left a clean precise editor stale
following Undo/Redo, and the initial live journey explicitly clicked Reload to
continue. The next source revision removes that interruption: before cursor
following, a visible clean precise owner refreshes its **existing stable target**
when the same document revision changes. Pinning still prevents retargeting to
another cursor row. Dirty/invalid fields, pending writes, drags, changed documents
and an open selector prevent the implicit refresh. The read retains the existing
context/generation guards; it cannot adopt over newer input during a pumped read.

Unchanged selected events keep their raw numeric formatting; changed values
update with field focus and a clamped caret preserved. Empty rows retain their
captured insertion sound. Effect/pitch catalogues and unchanged virtual-list
count/selection/scroll are no longer rebuilt on every refresh. Timeline painting
uses the existing native curve palette and focus border, including Windows
contrast colors; release markers are hollow so they differ from onsets by shape
as well as color. Actual contrast-mode presentation is still unqualified.

Native regression additions cover fixed-target refresh, unchanged formatting,
caret, changed musical values, editing after refresh, unrelated row preservation,
invalid drafts, document identity and empty-row sound retention. The actual live
journey now requires a current editor after Undo/Redo, updated hit counts/values
and retained field focus **without** Reload between history and the next edit.
The retained-draft host fixture uses invalid `92x` rather than a valid value that
now correctly autosaves. Its exact preservation/navigation assertions remain.

At 06:14:30 UTC the original f28 Mac ARM compilation passed; its tests, Mac Intel
and Windows x64 jobs were still running. No additional build or test ran for this
continuation; only source review, whitespace validation and Python syntax parsing.
Include this revision in the existing next checkpoint rather than adding a build.

## Source continuation: Save and canceled drag preserve queued input

`Application::saveFile` previously captured the save revision without finishing
a precise-note field's 300 ms automatic edit. A fast Ctrl+S could therefore save
the previous value; a Save As dialog could allow the timer to change that revision
and reject the later file write. Native Save now flushes an existing precise-note
automatic intent before plugin collection and before capturing its save revision.
A pending/dragging owner or still-busy admission blocks that save attempt. Invalid
raw fields and failed/uncertain writes have no automatic intent and are never
replayed by Save. Other retained-draft/save policies and the `document.save` API
remain unchanged; the API still saves the already accepted document model.

Starting a timeline drag also canceled any earlier field timer. Escape restored
the pre-drag values but stranded that earlier valid edit. The drag now records
whether an automatic intent existed; canceling resumes only that prior intent
against the retained target/new generation. Completing the drag still commits
one combined edit. A drag with no earlier automatic edit gains no new write.

Native owner regressions cover timer-free Save flushing, exact one-write/caret
retention, invalid raw values, busy admission, uncertain acknowledgements and
Escape after a queued field edit. A second actual silent-playback journey invokes
the native Save command immediately after typing, checks the latest note value,
unchanged playback epoch, retained focus, no revision change on repeated Save,
unrelated event preservation and exact saved-file reopening. The bounded next
checkpoint remains five native groups and now selects **22 actual-app cases**.
These source additions have only whitespace and Python syntax inspection so far.

Mac lifecycle source was also rechecked: `restoreStoppedPluginBaselines` queries
the live vendor's opaque state after restoring scalar baselines, and VST3 state
capture can process a flush. Its stopped-only guard and closed-editor opaque
Discard guard remain necessary and unchanged. Live Mac restoration still needs
a separately prepared provider handoff and its native audio/device evidence;
the current Windows composing fixes do not claim that gap is closed.

## Completed CI evidence from the first candidate

The f28a6eff8 Mac ARM job 120266634155 completed successfully. Its downloaded
job log records all 134 CTest registrations passing, including `precise-notes`
(11.11 seconds), `precise-ramps`, `unified-history` and
`document-history-allocation`. The native interface `precise-notes` group also
passed. This is evidence for the shared renderer/history inputs at that commit;
the subsequent correction changes only Windows source/tests and documentation.
It is not Windows UI evidence or Mac device/performance qualification.

Windows x64 job 120266633422 failed on the same MIDI C7692 expression already
corrected locally, reported by the app, MIDI window test and workspace test
targets. Its log contains no additional compiler/linker error. Mac Intel was
still running at the 06:27:12 UTC inspection. Neither job was restarted.
The downloaded logs and SHA-256 fingerprints are retained in
`bin/mac-gesture-20261022/precise-live/completed-ci-logs.json`; recheck the Intel
result and any later build start before the next checkpoint.

## Source continuation: finish the edited hit before changing selection

An additional source trace found that a queued valid field edit could be stranded
by selecting a different hit and then entering invalid text before its timer
fired. Deselecting on blank timeline space had the same issue when preparing the
next hit's insertion fields. These selection boundaries now finish only an
existing automatic intent before replacing the selected fields. A failed write
retains the current selection; input received during the pumped request changes
the draft generation and also prevents the later selection from overwriting it.
Native assertions cover both selection paths, exact one-write preservation and
newer raw input during the save. They join the existing next checkpoint and have
not yet run. No additional build was started.

## Source continuation: native Undo includes the queued note edit

Native song Undo/Redo now finishes any queued precise-note automatic intent
before taking the current document revision. This prevents rapid Undo from
undoing older music while leaving the just-entered value stale. Text-field Undo
still goes through the native Edit control. Failed/uncertain writes are not
replayed, and invalid raw text remains retained under the existing draft policy.
Song menus, context menus and the rack's unified-history buttons account for a
queued musical edit: the first edit can already be undone, and a new edit
supersedes the old Redo branch. Presentation-only snap changes do not advertise
an Undo step. External `history.undo/redo` API semantics are unchanged.

The owner regression covers timer-free flushing and no replay. An actual native
menu test starts with empty history, undoes the first queued edit and redoes it,
checking exact ordinary cells and precise events. A third actual-app silent
playback journey checks native Undo/Redo, current editor state, transport epoch,
continued frames, no delayed timer replay and save/reopen. The next single build
also includes `workspace-restore-tests`; the bounded selection is now **six native
groups** (adding `workspace-menu-bar-tests`) and **23 actual-app cases**. Source
review, whitespace inspection and Python syntax parsing have run; these new
behavioral assertions are still unexecuted.

The read-only CI observation at 06:37:47 UTC found Mac Intel compilation passed
at 06:36:56; its remaining job steps were still running. This does not supersede
the fresh CI inspection required at the 06:59:10 UTC earliest build gate.
