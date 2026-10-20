# Shared recorded-gesture preparation

Status: **source and regression checks prepared, not compiled or executed**.
This is an internal single-target capture primitive, not an enabled Windows
recording feature. Both native adapters, public arm/status controls, recovery,
multi-target vendor coordination and UI integration remain required.

## Ownership and behavior

`editor/hosted/RecordedGestureCapture.hpp` owns one bounded input log and its
captured point-set baseline on a serialized control thread. Its key binds the
gesture token, document revision, stable plugin/parameter, playback generation,
provider incarnation and original arm time. It retains the absolute recording
timeline independently of the device so delayed Stop drains can still resolve.

The caller supplies the original `PluginEdit` timestamp. Zero is retained as
unmapped, never changed to the worker's current time. Events outside the arm/
release interval are excluded. Each input keeps its original time and, once
available, its canonical 48-kHz frame. A later `resolve()` maps only pending
events; already mapped events survive clock/device retirement.
`RecordingFrameTimeline::stoppedEndTime` exposes the final bound for its original
generation. A normal Stop can shorten earlier presentation coverage, so
preparation/publication rechecks cached mappings against that final bound.
Affected raw events remain visible as unmapped and invalidate older candidates;
ordinary ring eviction does not erase a previously valid mapping.

The input log has the existing vendor queue's 8,192-event bound. Source-loss
counter deltas and local overflow remain observable. A decreasing loss count
or another provider/playback incarnation rejects. Normal preparation refuses
lost/unmapped events; an explicit reviewed partial result carries those loss
counts. Native adapters must distinguish coverage still pending from a terminal
clock loss, and must not automatically acknowledge either.

Preparation requires a sealed gesture, the same revision and ordered stable
rack identities, a writable unchanged parameter range, and no enabled pattern
lane or parameter-command conflict. It produces a complete candidate without
modifying the saved points. Last accepted input wins at the same target/frame;
other points and their order remain unchanged. New points respect the existing
100,000-point limit, replacement remains possible at capacity, and ambiguous
existing duplicates at the edited time reject. Unchanged points retain their
representation and report no musical change.

A candidate is immutable to its caller and bound to its owner, key and input
generation. Publication refusal/exception retains the input log; a successful
native atomic publication retires it. Late input or newly resolved coverage
invalidates an earlier candidate. The callback contract excludes reentrant
input/message pumping and requires staging all history/persistence allocation
before publishing. The helper does not call a plugin, change the manual
baseline, own document history, allocate from audio, or touch project formats.
It stores rack identities, not copies of vendor blobs.

## Prepared checks

`editor/Tests/RecordedGestureCaptureTests.inc` is included by the existing
`RecordedAutomationTests.cpp`, already built/scheduled on both platforms.
There is no extra native build target.

- Original timestamps at 44.1/48/96 kHz, delayed coverage, shortened Stop bounds,
  invalidation of pre-Stop candidates and exact canonical frames.
- Wrong playback/provider/parameter, invalid values, pre-arm/post-release input.
- Whole candidate and unrelated-target preservation, no-op, same-frame order.
- Stale revision, rack reorder, unavailable catalogue, enabled pattern lane
  and bound parameter-command conflicts.
- Publication refusal/exception, stale candidate and duplicate publication.
- Explicit source loss, unmapped timestamps, exact event/point capacity,
  replacement at capacity and duplicate existing-point refusal.
- Prepared points enter the real shared recorded timeline and render through
  the existing host allocation/free audit (plus Mac lock audit where enabled).
  This is offline PCM, not original-time native event ingestion.

Only source inspection and `git diff --check` have run. Build and execute these
checks with the pending clear/Undo/snapshot repairs in the next coordinated
batch, **not before 11:48 UTC**.

## Native integration still required

Use the adapter boundaries in
[manual automation follow-up](MANUAL-AUTOMATION-FOLLOWUP-2026-10-20.md).
Do not expose a Record toggle ahead of the following:

1. A coordinator for all targets touched by a vendor gesture. Single-target
   candidates share a captured revision; committing one cannot silently stale
   or overwrite another. Merge their deltas into one staged point set/history
   transaction, bound aggregate storage, and reject ambiguous overlap.
2. Windows native UI timestamps and vendor notifications before value
   coalescing; equivalent Mac native/vendor original-time ingestion.
3. Independent manual baseline restoration and suppression of ordinary opaque
   editor-state/recovery capture for the recording. Keep live preview and
   persistent automation ownership distinct.
4. Fully drained release/Stop/closure barriers, retained queue failures,
   document-departure protection and a recoverable pending take representation.
5. Shared public arm/status/failure semantics, native controls and exact Undo/
   Redo/save/reopen acceptance. The newer clear-all API is already a separate
   source batch; do not implement a competing clearing path.

This primitive intentionally has no API or UI of its own. It must not be cited
as completion of A13, Windows gesture recording or Mac timing accuracy.
