# Windows arrangement matrix

Source audit and implementation plan, 2026-10-07. The retained order/timing and
[sections and annotations](ANNOTATION_PLAN.md) checkpoints are complete. The
isolated matrix draft now addresses the shared gaps below and adds reciprocal
Mac/Windows API adapters. This note is not runtime qualification; build, native
and actual-app evidence must be recorded for the final integrated source.

## Existing behavior and shared boundaries

[Mac ArrangementMatrix](../mac/App/ArrangementMatrix.swift) presents paged
order-by-track blocks with density bins, selection, Open, Copy/Paste and local
drag-copy. The current page is 64 orders by 12 tracks. API limits are 128 orders
and 32 tracks per request. End/Skip slots have no blocks. The selected occurrence
and track are independent of playback; Open navigates to row zero and detaches
follow. Copy captures its revision and rejects stale destinations. Overwrite,
Merge and Mix modes use the shared pattern-paste semantics. `makeUnique:true`
clones a destination pattern if any sequence uses it more than once.

The contracts live in [TrackerSessionAPI.inc](../mac/Bridge/TrackerSessionAPI.inc)
and the shared [ArrangementTools](../editor/ArrangementTools.cpp). A Windows
adapter must bound response construction, preflight view-cache growth, stop only
when a changed musical operation is accepted, and preserve one Undo/persistence.
Reuse the stable order and track IDs exposed by the annotation checkpoint.

## Original shared gaps and current draft

The following gaps were verified in the preceding source. The shared draft now
has one native-aware summary, complete candidate validation with a Windows view
admission callback, exact independent clones, and explicit native copy policies.
See the contract beside `ArrangementCopy` and the public API guide for precise
collision/clipping rules. Shared and controller fixtures cover these changes;
their presence alone does not establish that they have passed.

- Matrix density currently counts only six-field tracker cells. A block with
  only precise notes or extra FX appears empty. Define a native-aware summary
  shared by both platforms, with separate useful event counts if needed.
- Block copy transfers only tracker cells. Precise notes are independent of
  ordinary cell transforms, so explicitly define their copy/merge/overwrite
  policy before changing the shared operation. Do not silently conflate them.
- Apply calls `Document::put` directly, bypassing ordinary editing's native FX-1
  conflict invalidation. Existing destination commands can survive alongside a
  pasted tracker effect. Reuse the established unified-effect preparation path
  and verify extra columns remain intact.
- An independent clone currently allocates a fresh pattern and copies cells and
  native metadata, losing engine pattern-specific signature/groove. Preserve
  exact pattern properties, including already-normalized groove values; do not
  normalize again. The separate `Document::addPattern` duplicate fix is not a
  correction to `applyArrangementCopy`.
- `clonePatternAutomation` already clones precise notes and native automation.
  Copy preparation must validate the complete prospective metadata capacity,
  so dry-run cannot succeed and then stop playback before Apply rejects capacity.

## Native interaction and evidence

Use a retained, bounded matrix with scrollable track columns, visible selection,
section/pattern labels and keyboard navigation. Preserve page/scroll and captured
copy target through polling, resize and hide/show. Reject document replacement
or stale copy tokens without discarding the user's intended source. Provide
Copy/Paste buttons and keyboard actions before optional drag-copy; no system
clipboard dependency is necessary for this project-local operation.

Qualify repeated occurrences across sequences, sentinel slots, precise-only and
FX-only blocks, exact native conflict semantics, unequal lengths/clipping,
overlapping ranges, shared-destination cloning, capacity rejection, no-op
revision/redo/playback preservation, one-Undo restore and native save/reopen.
Render source/destination timing and native events with multiple callback sizes.
Review real default/minimum native views with long names and many tracks. Mac
runtime reciprocity, foreground presentation and accessibility remain explicit
separate evidence requirements.
