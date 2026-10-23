# Preview cancellation processing receipts — 23 October 2026

Status: implemented in source, **not built or runtime qualified**. This follows
the provider adapter in `c273627d7` and is outside the qualified Windows
`cc5553aab` package. No release/Keep/Discard safety guard is removed.

## Observed boundary and implementation

`PluginChain::applyPending()` consumes the shared host queue and forwards values
to each native plugin. A VST3 backend can still have those values waiting for its
next `process()` call. Neither the host read index nor a cleared preview flag
proves that cancellation has reached that processor. An inactive processor also
cannot be acknowledged merely because the rest of the rack rendered a block.

The shared implementation adds:

- `PluginChain::cancelParameterPreviewsTracked(targets)`: validates the whole
  group, prepares retained provider owners and publishes the existing atomic
  restoration once. It restores current accepted values, preserving unrelated
  previews. Invalid targets, capacity refusal and allocation failure precede
  publication. Existing boolean cancellation delegates to the same operation;
  its success still means submission, not completed rendering.
- `ParameterPreviewCancellation`: an opaque, control-owned receipt retaining
  each target's rack entry, facade, preset and original provider source. The
  caller must retain the owning chain. It includes already-clean targets whose
  accepted control changes may still be queued.
- `parameterPreviewCancellationState(receipt)`: Pending until every target has
  successfully processed its required serial with nonzero frames and its vendor
  handoff has settled. New queued parameter traffic, changed recorded/graph
  revisions, changed provider owners or chain failure make it Invalid. No work
  is drained and no rendering or state serialization occurs in this query.
- A separate monotonic 64-bit control publication serial covers both batched
  controls and forwarded original native edits. Saturation refuses before
  mutation or consuming an original edit. The existing wrapping 32-bit queue
  indices/preset fences retain their ordering semantics.
- `NativePlugin` acknowledges its serial only after successful positive-frame
  processing. Audio-side bookkeeping is fixed storage with a lock-free atomic
  publication only when the serial changes. Receipt allocation, ownership and
  retirement remain on the control owner.

This is evidence of host processing, **not** physical output, completion of
vendor smoothing/tails, a canonical opaque-state snapshot or an original-input
cut. Native editor events that have not entered the host queue remain the
responsibility of the take's input barriers and fixed Stop cuts. Bypass and
other non-parameter document changes still require the outer revision guards.
The next live-release consumer must retain/revalidate the complete take when a
receipt is Pending/Invalid; a submitted cancellation never authorizes release.

No API/schema, stored project format, parameter identity or Undo representation
changes. AU/VST3/built-in control processing uses the shared mechanism; this
does not add AU non-flushing state observation or complete live restoration.

## New acceptance source

`editor/Tests/ParameterPreviewTests.cpp` adds:

- Clean/no-op receipts, wrong-owner rejection and invalidation by later writes.
- Pending immediately after publication, after `applyPending()` and after an
  empty process; Processed after actual positive-frame rendering.
- Grouped accepted-baseline restoration, exact saved-state preservation and
  unrelated preview preservation at 44.1/48/96 kHz and 17/128/511-frame blocks.
- Already-clean targets with newer accepted values still pending at the vendor.
- Invalid later IDs and a queue with only one free cell refusing a two-target
  cancellation without restoring a prefix.
- Delayed cancellation behind 128 separate accepted batches: a block rendering
  earlier traffic does not acknowledge the later restoration.
- Provider replacement invalidating a retained receipt despite stable song ID.
- Direct native-facade zero-frame/failed-process refusal and exact serial
  acknowledgement. Existing callback allocation/free/lock assertions cover
  the new rack rendering paths on their supported platforms.

`mac/Tests/ProcessingStateObservationTests.inc` additionally exercises the real
VST3 fixture. After preview, host queue consumption must leave observed processor
state unchanged and the cancellation Pending. Actual audited rendering must
restore the accepted bytes and make the receipt Processed. This complements
the preceding sample-offset/process-count and original-input retention checks.

## Bounded qualification at the next coordinated checkpoint

Do not rebuild the existing `kept-navigation` candidate. At 10:28 UTC its Mac
ARM build had succeeded and Intel/Windows builds were still running; no job was
restarted. The earliest next checkpoint is **11:02:24 UTC**, subject to fresh
actual build starts and completion of preceding jobs.

1. Freeze this batch with the preceding adapter and Python context correction.
   Inspect previous receipts/CI starts, then use one local Release build and
   one matching PR-head push. Include app/scanner, `parameter-preview-tests`,
   `live-parameter-tests`, `recorded-automation-tests` and workspace/precise-note
   targets in that build. Do not rebuild for test selection changes.
2. Run shared `parameter-preview`, live-parameter and recorded-automation gates;
   the native parameter-preview/recording workspace gates; and the exact
   corrected composing/Record/Keep/Undo/Redo/reopen/Discard app journey. Require
   callback audits and existing assertions; diagnose failures without weakening.
3. Mac ARM and Intel must compile and pass `parameter-preview`, `native-plugins`
   (including the real VST3 observation cases), native take/controller tests,
   and their required shared/API gates. Existing CI also protects routing,
   presets and persistence against changes in the shared facade.
4. Inspect a fresh isolated Windows QA process using Computer Use: silent
   playback, precise-note insert/delete/immediate fields, Params slider,
   Record/Stop/Keep and the retained lane action. Exercise typing, Backspace,
   Tab/Shift+Tab and Save focus. Preserve the musician's process/audio routing.
5. Retain valid unchanged screenshots/layout evidence, but recheck this binary's
   composing/recording behavior because the shared parameter path changed.
   Package only with passing receipts and foreground checks. Cross-platform
   merge/parity still needs the outstanding reciprocal project/native audio
   gates and complete live-release capability; this receipt is one prerequisite.

The delivered precise-note executable remains
`~/Downloads/ScreamSeq-Windows-ARM64-precise-cc5553aab/ScreamSeq.exe`, SHA256
`940df0bfe7263f1d60527a50ec679740674b5b6d2b80fdc05b117654c1f3ba33`.
