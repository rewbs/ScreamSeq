# Sample drawing on release

Status: **implemented in source; not built or qualified**. This closes a concrete
workflow difference in reference rows 14–16/C30, not the entire sample-editor
parity requirement.

## Observed behavior

Current Mac `SampleWaveformEditor.swift::commitDrawing` submits `sample.draw`
when the pointer stroke ends. Windows `SampleDetailWindow.hpp::mouse` previously
retained that stroke until the separate Apply drawing action.

The new release acceptance test was run against the existing Windows ARM64
context-panel executable (`e79133155`, SHA-256
`4c01af17b6e9f0fc4112e9f61442b90868b988fb6baf17d1dc8a336609367dc0`).
It failed at the expected assertion: after release, actual sample PCM was still
identical to the original. The preceding cancellation and in-progress stroke
assertions passed. This is a reproduced workflow gap, not evidence of the fix.
Log: `bin/sample-stroke-20261020/before-release.log`; one case, 1.853 seconds.
The disposable process ran on the existing never-switched private desktop.
No computer-use session or physical audio capture was started.

## Implementation and ownership

Each completed pointer stroke now submits one ordinary `sample.draw` through
the existing `NativeWriteCompletion` owner. There are no per-move document edits,
new history rules, API methods, storage changes or audio callback changes.
Escape and capture loss cancel before submission. Changed document revision or
newer raw input cancels the gesture while retaining that newer input. The existing
individual-frame zoom and 4096-point bounds remain.

A new pointer stroke cannot absorb an earlier staged numeric batch. Numeric
Stage point remains deliberate batch construction; its CTA now says
**Save staged points**, is disabled without points, and is explained alongside
the automatic pointer behavior. Ordinary drawn strokes need no separate CTA.
Failed or uncertain writes retain the stroke, target and completion for Review.
Refresh failure after an accepted write does not cause a retry, erase new text,
steal newer focus or add another Undo entry.

The relevant native files are `SampleDetailWindow.hpp`,
`SampleDetailWorkflows.inc`, `SampleMutation.inc` (existing completion path),
and `SampleReadback.inc` (existing staged waveform adoption). Shared
`sample.draw` validation, PCM, persistence and Undo semantics are unchanged.
Mac remains unchanged.

## Bounded qualification

Batch with the pending recipe-preview build and context test correction.
The last build completed at 03:49:50 UTC; **no configure/build before
04:50 UTC on 20 October**. Compile the app and workspace harness once alongside
the recipe/controller/shared targets.

- Run `workspace-sample-result-tests`: real native gesture messages, no write
  during movement, Escape/capture cancellation, new raw text, isolation from
  staged points, single release, unrelated sample preservation, target/viewport,
  Undo/Redo/no-op, retained refresh failure and read-only Review.
- Run the targeted sample-detail Python cases for numeric drawing/channel/
  interpolation/save/reopen and pointer cancellation/release/history/no-op/
  save/reopen/stale document. The latter now explicitly requires release to
  change audio without clicking Apply.
- Run `test_dense_gesture_bounds_and_atomic_point_limit`. Its old expectation
  that release leaves 512 staged points has been replaced with the new contract:
  movement is temporary, release saves the stroke, one Undo restores the sample,
  and Redo restores the same PCM. A separate numeric batch still accepts exactly
  4096 points, rejects the next point without changing PCM or history, saves the
  exact expected values and leaves the remaining sample untouched. This updated
  test is source only until the pending candidate is exercised.
- Run existing sample readback and automatic-edit groups to protect retained
  fields, loops/settings and focus. Broaden only on a failure or relevant change.
- Reuse shared PCM/codec evidence only where inputs are unchanged. Recipe work
  in the same candidate independently requires its documented shared/audio/
  API/fixture checks; this UI change does not waive them.

Private native checks do not establish pointer feel, visual readability, DPI
presentation or physical audition. Those remain open while computer use is
stopped. Direct loop handles, other sample workflows and cross-platform final
integration gates remain separate work.
