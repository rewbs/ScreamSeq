# Precise-note timeline: immediate composing, 2026-10-23

## Scope and evidence status

**Latest checkpoint:** the c7d2fbd78 Windows build passed, but its gate did not:
five of six native groups and 21 of 23 diagnostic actual-app cases passed.
The native menu fixture used an unsupported dispatcher read; one app fixture
tried to reuse an editor retired by document replacement. The real product
failure is Save moving keyboard focus back to Main. All three are corrected
in source below; Save and the native menu changes await the next build.
Computer Use confirmed the requested core timeline interaction on c7d2fbd78
in an isolated silent session. **No package or merge is qualified yet.**

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
the subsequent correction changes only Windows behavior/tests and documentation
(the Mac session change corrects a stale lifecycle comment only).
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

At 06:52:29 UTC the same Intel job was confirmed **completed successfully**.
The retained log records all134 CTests passing, including `precise-notes`
(12.78 seconds), `unified-history` and `document-history-allocation`, plus the
native precise-notes interface group and socket/typed-fixture gates. Its log
SHA-256 is `04c69d364535e1001099d4d3e6256194696a38f14fd080f598b2d84936c2e7b3`.
Both Mac architecture jobs therefore pass for f28's shared publication inputs.
This does not qualify the later Windows-only corrections or hardware performance.

## Second candidate: compiled, failed gate, bounded UI evidence

Source `c7d2fbd786a8616964e59b648db0d0d21dc43164` compiled once from
06:59:32.7446645 to 07:02:25.4896574 UTC. The ARM64 Release executable hash is
`8b9e29733ae126eb3161e14ca620420436e31a832d3af1cf2b081a9f32dda34a`.
Receipts remain in `bin/mac-gesture-20261022/precise-live-correction/`.

- `checks.json` / `native.log`: five of six groups passed, including the precise
  owner, MIDI owner, live document controller, hosted-project PCM/allocation and
  history-allocation gates. `workspace-menu-bar-tests` failed because the newly
  added fixture called `pattern.get` through `documentOperation`; that read is
  served by the immutable API snapshot adapter. Its source correction compares
  the complete copied pattern cells, rows and channels, and the precise events.
- The gate stopped before app checks. A separate **diagnostic** execution of
  those previously unrun cases passed 21/23 (`diagnostic-app.json`, appExit 1).
  The complete live composing journey and native live Undo/Redo passed. No
  failed receipt was replaced or interpreted as qualification.
- The selected sample/instrument fixture assumed its old owner survived
  `document.open`. Successful replacement intentionally retires document-scoped
  HWNDs. The corrected fixture asserts retirement and uses the native opening
  command for row 6; exact identity/history/save/reopen assertions remain.
  This corrected Python case alone passed against the same compiled binary
  in 1.995 seconds (`fixture-correction.log`), without another build.
- The live native Save case found a real focus loss. `WorkspaceView.inc` called
  `SetFocus(window)` after Save. Save/Save As now finish their normal layout and
  return without navigating away. The live test retains a selected text range,
  and the native File-menu fixture now checks queued-note flushing, saved state,
  unchanged text, field focus and caret selection. These assertions still need
  rebuilt execution; the failed Save receipt is retained.

Computer Use diagnostic process **42688** used the disposable built-in demo and
silent WASAPI playback; it exited normally with code 0. The musician's PID39856
and its c900d2b2e Downloads executable were untouched. At 200 percent DPI,
observed actions confirmed populated-row double click, selected-hit Backspace,
velocity typing, text Backspace, Tab/Shift+Tab, interior drag and Ctrl+Z. The
cursor was visible at different positions. Eight API snapshots confirm accepted
edits, clean current editor state, advancing frames and unchanged playback epoch
3. `diagnostic-summary.json` records observations, hashes and limitations.

The initial helper image showed the musician's window despite QA metadata;
the QA window was explicitly activated and its title/content verified before
any input. Docked precise child controls were absent from the helper's UIA tree;
focus evidence used visible caret/selection plus native assertions. A first
attempt to drag the left-edge marker made no change; the interior marker drag
succeeded. Edge hit tolerance remains an ergonomic follow-up, not a qualified
claim. No fresh resize/float/list-Backspace or corrected Save UI result is claimed.

The diagnostic audio report covers 250.837 seconds at 48 kHz / 480 frames:
zero deadline overruns, starvation indicators or device errors; maximum callback
3957.8 microseconds. It explicitly reports `realtimeAuditPassed:false` and
`sustainedPresentationQualified:false`; this is bounded functional evidence,
not realtime/performance or temporal-flicker qualification.

The next attempt uses **`precise-live-followup/`**, separate receipts and a new
frozen source. Earliest start is **2026-10-23 08:00:12 UTC**, based on the current
Windows CI build start at 07:00:12. Inspect the current CI completion and any
later starts first, never overlap another checkpoint. Keep the six native groups
and 23 actual-app cases, including all failed assertions. After passing, repeat
fresh Computer Use on the new executable, particularly Save focus/caret, and
complete resize/float/list-key checks before packaging. No main merge follows
from diagnostic results. The previous package remains c900d2b2e.

## Source continuation: visible hit handles and native Tab order

The left-edge drag observation has a source-level explanation: pointer admission
required `plot_.contains`, while handles and selected outlines render across that
border. Pointer grabbing now admits the same six-DIP visible handle margin on
all four edges, then uses the existing nearest-hit test. Empty margin double
clicks cannot add notes. Musical position/velocity still clamp to the original
row bounds; the normal one-save drag completion and shared Undo path remain.
Native regressions cover start/end offsets and maximum/minimum velocities,
exact unrelated events and empty-margin exclusion. The actual-app canvas case
adds an edge drag with exact Undo/Redo assertions.

Computer Use also showed Tab going from Velocity back to Offset because native
dialog traversal followed control creation order. Each page now sets the native
child traversal order to its visible top-to-bottom, left-to-right sequence,
only when changing pages. No synthetic focus navigation replaces Win32 Tab.
Native tests traverse every visible control forward and backward on all four
pages while preserving incomplete raw input and prohibiting document writes.
The fixture page helper now uses the explicit Details command ID instead of
assuming all page IDs are contiguous.

These source changes join the existing followup freeze and six-group/23-case
checkpoint; they have not been built or run. No second build occurred this hour.
At07:23:46 the exact c7 CI jobs were still running; ARM compilation passed at
07:15:48, while Intel and Windows compilation were still in progress. A transient
GitHub502 during the prior inspection was recovered by repeating the read-only
inspection, never by restarting a job. The earliest gate remains08:00:12 UTC,
subject to fresh job/start inspection.

## Cross-domain chronological history continuation

`test_precise_note_live.py` now includes a fourth live journey: convert an
ordinary note through the native velocity field while looping, capture and Keep
a built-in Balance take through the public original-time API, then edit the
native note again. Three Undo steps and three Redo steps must cross those actions
in exact chronological order while playback keeps its epoch. The complete cell
array, precise-event collection, automation collection, unrelated channel and
plugin state are checked at each boundary. The pinned note editor must refresh
cleanly without losing field focus; save/reopen must preserve both kinds of music.
Existing native-slider tests remain the input-delivery gate; this test does not
claim another native-slider or foreground qualification.

The new journey passed once against the unchanged c7 executable in 3.294 seconds
on its own disposable private desktop. Evidence is
`precise-live-correction/history-diagnostic.log` and `history-diagnostic.json`.
The initial runner invocation rejected a relative log path before launching;
the corrected absolute-path invocation is the sole execution. This is bounded
diagnostic evidence, not a replacement for the failed candidate's receipts.
The next frozen checkpoint includes this new case: **six native groups and
24 actual-app cases**. The source-only Save, edge and Tab corrections still need
the rebuilt gate and fresh Computer Use. No extra build was run.

## Native accessibility labels

The precise editor now annotates its twelve standard input/list/details controls
with stable accessible names. Their changing numeric values or child creation
order must not act as their labels when controls move between pages. This uses
the existing native property annotation helper, preserving Windows text/value,
selection and editing behavior. The precise-owner group now queries the native
accessible provider on every page and checks incomplete raw offset text, the
standard text role, caret selection, focus and unchanged music.

This correction is source-only until the next checkpoint. It does not claim to
resolve the earlier Computer Use tree omission or qualify Narrator; those require
client/runtime inspection. The build/test scope remains six groups and24 app
cases. At07:38:40 UTC c7 Windows and Intel compilation had passed and their tests
were running; Mac ARM's entire job120286202292 had completed successfully. No
jobs were restarted, and the earliest next build remains08:00:12 UTC.

## Completed c7 cross-platform checkpoint and CI coverage correction

At07:48:59 UTC both Mac jobs120286202072/120286202292 were terminal success.
Their retained logs each confirm all134 CTests, the full native interface harness,
AU/VST3 notification lifecycle, socket admission and typed-fixture/conformance
steps. `precise-live-correction/completed-ci-logs.json` pins all three job logs
and SHA-256 values. Mac Intel log hash is
`f37404d8f2688c755e5658ec0e58d19f39184bd97ff2e25cd9ef1e4c246e649a`;
ARM is `5ceb47973fbf6f281d723f1f222c91894ef06681fe36c9fcedd6460f2d121c8e`.

Windows job120286202015 compiled and passed158 portable/worker registrations,
49/50 native editor groups, scratch, vendor ownership and typed pipe fixtures.
The one native failure is the same unsupported `pattern.get` menu fixture already
corrected in source. Its integration step ran97 cases with18 failure records
and one skip:15 distinct cases failed during live-audio startup, with three
additional teardown failures after missing QA windows. Recording/routing
diagnostics explicitly returned an empty audio-device catalogue. These are
failed/unexecuted playback acceptance paths, not green CI or proof of correct
behavior with a device. Log hash:
`ff323f304d5c5600b054516c24ca318e3a7342dd3704020e083cd899fa5593d2`.

Inspection also found the workflow only scheduled one precise host case. The
existing post-build integration step now includes the complete editor, host and
live classes:16 precise cases, replacing that single case (15 additional cases;
112 total at this source). No build job, retry or endpoint skip was added. Keep
the real WASAPI/clock assertions. Local six-group/24-case qualification remains
the bounded changed-input gate; native x64/device evidence remains separate.

The shared actual-app recording launcher now checks its owned process handle
before each readiness read. A terminal startup refusal fails immediately with
PID, exit code, arguments and the last workspace/transport observations; it
never restarts or skips the case. A still-live process retains the same20-second
readiness deadline and mandatory clock assertion. The terminal branch was
exercised with an owned Python child rejecting the product arguments: exit2 was
reported in0.062 seconds (`startup-exit-diagnostic.json`). The normal branch then
passed the combined live note/take journey against the existing c7 executable
in3.587 seconds (`startup-live-diagnostic.log`). This repeat was limited to the
changed launcher; no build or broad suite was repeated.

## 08:00 UTC checkpoint and foreground findings

Frozen `dfe9300ea1b6729499fd810cf3d560d52c76e34a` built once from
08:00:43 to 08:02:49 UTC. Executable SHA-256:
`82db328ccb00fda1027f4edd8d1a461e19f47d23450af0ea03edfe711d56967f`.
Receipts are in `bin/mac-gesture-20261022/precise-live-followup/`.
The next coordinated build cannot start before **09:01:15 UTC**, and later
actual CI starts/current running jobs must be inspected before proceeding.

Five of six native groups passed, including the complete precise-owner group
(edge hit testing, accessibility names and all-page Tab order), MIDI, live
document publication, hosted project and history allocation. The native menu
group failed its combined File Save focus/caret/queued-edit assertion. That
failure remains open. The assertion is now split into exact focus, selection,
raw text, retained draft, saved dirty state and selected velocity failures,
with native status/snapshot diagnostics and explicit starting preconditions.
No assertion was removed. This diagnostic change requires the next build.

All **24 actual-app cases passed in 34.291 seconds** in the separately retained
`app-diagnostic-prepared.log`, including native Save and the combined precise
note/plugin-take chronological history. The first diagnostic runner omitted
TMPDIR and failed before launching any product process; its log is retained.
Two bounded diagnostic cases then passed the actual File menu path: live row4
Save and stopped row0 Save in the bottom dock. These do not erase the failed
native menu gate or prove its cause.

Computer Use inspected fresh isolated PID18832 with the above executable,
silent WASAPI output, a disposable built-in demo and a separate saved QA song.
Observed at 200 percent DPI: double-click inserted a second hit; canvas and Hit
list Backspace removed their selected hit; the visible playback cursor changed
position; a hit at the left plot edge was successfully grabbed from its outer
half and dragged; velocity typing updated the point automatically; Ctrl+S
retained field focus; subsequent Backspace edited velocity text rather than
deleting music; Shift+Tab and Tab moved between Units and Velocity; maximize
and restore retained the precise editor and values. Read-only snapshots retained
playback epoch3 with increasing frames and no fault. The helper still exposes
Main rather than the docked field in its accessibility tree; visible caret,
selection and the independent native assertions are the bounded focus evidence.

**New confirmed failure:** Ctrl+Z from the Hit list did nothing after Backspace.
The shared native shortcut dispatcher reserved Ctrl+Z/Y for all list controls,
although this list has no local text history. The pending correction lets only
this precise Hit list yield those keys to configured document Undo/Redo;
editable fields keep Windows text Undo. A real native owner regression sends
Backspace, Ctrl+Z, Ctrl+Y and Ctrl+Shift+Z, compares music and focus, and checks
that Ctrl+Z in the velocity EDIT still changes text without changing the song.
This correction is **not built yet**. A fresh visible Hit-list keyboard check
is required after its build; no complete foreground pass is claimed now.

QA PID18832 exited normally with code0; do not reuse its window handles.
Its 469.789-second report records 48kHz/480-frame silent playback, zero deadline
overruns, **two starvation indicators**, zero device errors and maximum service
time5807.2 microseconds. Realtime audit and sustained presentation qualification
are both false. This is neither a clean performance pass nor temporal flicker
qualification. The musician's PID39856 and audio defaults were preserved.
No package or merge was performed from this failed checkpoint.

## Subsequent CI and compact menu-fixture preparation

The exact dfe jobs have finished. Mac ARM120308061400 and Intel120308061761
passed134 CTests each, all37 native interface groups and their socket/typed
gates. Windows120308061320 compiled and passed158 portable/worker registrations
and all50 native editor groups, including the menu Save fixture at96percent DPI.
Its112-case integration step failed with22 failure records and one skip;
the four new precise-live cases exited during audio startup, before their
assertions, and the retained diagnostics show no audio endpoint. These remain
failures, not qualified playback. Full logs and hashes are retained in
`precise-live-followup/completed-ci-logs.json`.

The menu fixture's root is1400x1000 physical pixels. At200percent DPI its
workspace is compact, and `layoutFor("notes")` preserves the Pattern compact
selection. Restoring a panel location does not reveal its compact tab. The
pending test correction enters Notes using the production `workspaceEditorRequest`
before native input in the three precise menu cases. Every focus, text selection,
raw value, queued-edit, saved-dirty and music assertion remains. This setup
correction still requires rebuilt confirmation; do not erase the previous failure.

The independent layered composing PCM question now has additional bounded
evidence without a build; see [the audio report](LAYERED-COMPOSING-AUDIO-2026-10-23.md).
The next keyboard checkpoint retains two native groups and seven actual-app
cases. Its rendering inputs are unchanged, so the new offline layer evidence
does not need another render merely because the shortcut dispatcher is rebuilt.

## 09:01 UTC keyboard checkpoint and fresh foreground check

Frozen `173ba8bf96477922b2f7ae76aaffbba8e4292ce2` compiled once from
09:01:33.736 to 09:03:24.938 UTC. Executable SHA-256:
`649dca711422e6a67cddb86f64bb483a4bb6844e0f47c02eb9893077f7c1e0ad`.
Receipts are in `bin/mac-gesture-20261022/precise-keyboard/`. No CI was triggered;
the backup branch was pushed. The next build is forbidden before
**10:01:33.7362309 UTC**, subject to inspecting any later actual build starts.

The precise-note native group passed. The menu group reached and passed the new
Hit-list Backspace, Ctrl+Z, Ctrl+Y and Ctrl+Shift+Z assertions, then failed its
native Velocity text Undo assertion. Its Save assertions were not reached, so
the compact setup correction remains unconfirmed by that group. The failed
native receipt is retained. Separately, all seven actual-app host/live cases
passed in 13.713 seconds, including Save and chronological note/plugin history.

The keyboard fixture sent only WM_KEYDOWN. Native EDIT text Undo consumes the
translated Ctrl+Z character. The pending test correction uses TranslateMessage
and DispatchMessage, then dispatches the resulting character, matching the
production message pump. All assertions remain. This test correction still
requires a build; diagnostic results do not replace that gate.

Computer Use inspected fresh owned PID31296 at 200 percent DPI on the exact
candidate, using silent playback and a disposable demo/song. Observed:

- Hit-list Backspace removed the hit; Ctrl+Z restored it, Ctrl+Y removed it,
  and Ctrl+Z restored it again.
- In Velocity, replacing `75` with incomplete `-` showed a validation message;
  native Ctrl+Z restored `75`, leaving one hit. Typing `97` updated the hit
  automatically. Ctrl+S retained the caret; Backspace changed `97` to `9`
  rather than deleting the hit. Tab moved to the effect control and Shift+Tab
  returned to Velocity.
- Timeline double-click added a second hit at approximately 0.125118 beat and
  velocity 64. Backspace removed that selected hit. The white playback cursor
  changed position throughout. No Write note or Save now action was needed.
- Read-only initial, after-add and final snapshots retained playback epoch 3,
  increasing frames, active playback and no fault; no stop/restart was used.

The helper reports Main as the accessibility focus owner. Visible caret and
selection, the consequences of actual keyboard input, and native assertions
are the bounded focus evidence; no screen-reader qualification is claimed.
PID31296 was saved and closed normally with exit 0. Its 402.889-second report
records 48 kHz / 480-frame silent output, zero deadline overruns, starvation
indicators and device errors; maximum service time 3656.2 microseconds.
Realtime audit and sustained presentation qualification remain false. This is
bounded functional evidence, not temporal-flicker or general performance proof.

No production package or merge was made: the corrected native menu gate must
pass first. The foreground receipt is explicitly diagnostic and cannot qualify
the failed automated candidate for packaging.
