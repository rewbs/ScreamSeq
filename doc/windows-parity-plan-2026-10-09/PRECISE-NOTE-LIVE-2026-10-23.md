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
