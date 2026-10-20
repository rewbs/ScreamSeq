# Shared recorded-gesture preparation

Status: **source and regression checks prepared, not compiled or executed**.
This is an internal capture/preparation primitive, not an enabled Windows
recording feature. It now supports atomic preparation/publication across several
targets. Both native adapters, public arm/status controls, recovery, native
gesture grouping/draining and UI integration remain required.

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

## Several targets in one transaction

`prepareBatch` receives current retained owners, each provider's current
incarnation and its parameter catalogue. Every owner must share the gesture
token, original arm/release boundaries, document revision, timeline object,
ordered rack identities and bit-exact starting points. Targets must be distinct.
Different plugins may have different incarnations; targets of the same plugin
must agree on incarnation and both the initial/current provider loss counter.
Use one immutable shared point baseline for all targets, avoiding one project
copy per parameter.

The helper overlays all captured targets into one candidate, validates the
combined 100,000-point cap and retains each target's loss report. It bounds the
batch to 128 targets, 100,000 raw events and 64 MiB of retained owner/baseline/
candidate storage. The latter is an admission bound, not a process peak-memory
guarantee: temporary per-target frame indices are separately bounded by the
point cap, and native coordinators must enforce intake limits before creating
additional owners. Duplicate provider loss counts describe the same queue;
status/UI must group them by provider, not sum them as independent losses.

`publishBatch` takes the current owner membership again. A changed membership,
provider, event log or final Stop mapping refuses the entire batch. Each owner
has a nonreusable instance identity, so recycling its address cannot revive an
older candidate. One callback publishes the complete candidate and stages one
native Undo entry. Refusal/exception retires none; success retires every member
without another allocation. Keep all owners alive, serialized and nonreentrant
through this callback, and enforce the native document revision guard there.
Do not commit each owner's full snapshot sequentially.

A target first observed after Stop can still be constructed against retained
coverage if its original arm time precedes the final boundary. A new arm at or
after that boundary is refused. This permits delayed first notifications; it
does not authorize reuse after playback restart or provider replacement.

## Native notification-source identity

`PluginEditSource` in `PluginTypes.hpp` now carries a process-local provider
incarnation and that provider's cumulative dropped-event count.
`NativePlugin::editSource()` and `popEdit(event, source)` take one snapshot of
the actual published vendor. The latter returns source identity even for an
empty queue. An identity is assigned at native processor construction, off the
audio callback, and never wraps or reuses a previous value.

The stable rack facade reports the replacement vendor's incarnation after
preset adoption; preparing a replacement does not change the active source.
`PluginChain` forwards the paired read while preserving its existing live
parameter queue/cached-value delivery, and Mac `AudioDevice` exposes the same
pair. An unavailable slot/device returns the zero-incarnation sentinel.
Existing ID/value and timestamp-only readers remain supported.

This supplies the source binding needed by both adapters. It does not arm
recording or change project identities/storage, and the existing native
consumers have not yet switched to recorded capture. Use the paired pop result,
not a stable plugin ID or two independently observed provider pointers.

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
- Multiple plugins merge into one candidate and one actual timeline admission;
  late sibling input, membership/provider changes and publisher failures retain
  the complete group. Combined offline PCM checks both plugin values.
- Different capture baselines/tokens/clocks and overlapping targets reject;
  two independently legal additions that together exceed capacity reject
  without touching the original points or retiring either owner.
- Shared-provider loss coherence, exact no-op under acknowledged loss,
  recycled owner addresses, maximum target count and aggregate input capacity.
- First notification arriving after Stop retains its original pre-Stop time.
- Native processor recreation and live preset adoption distinguish actual
  notification providers while retaining the stable project identity.
  Replacement events are rejected by an earlier capture owner. Existing
  Windows/Mac vendor notification tests now check the paired source identity
  alongside original callback timestamps and audible delivery.

Only source inspection and `git diff --check` have run. Build and execute these
checks with the pending clear/Undo/snapshot repairs in the next coordinated
batch, **not before 11:48 UTC**. In addition to the existing shared recorded-
automation target, retain both native plugin-notification targets and the
existing live rack/preset checks: source pairing changes the rack drain adapter.

## Native integration still required

Use the adapter boundaries in
[manual automation follow-up](MANUAL-AUTOMATION-FOLLOWUP-2026-10-20.md).
Do not expose a Record toggle ahead of the following:

1. A native coordinator retaining all targets touched by a vendor gesture.
   Supply one shared baseline and per-provider arm-time loss counter to the
   batch helper above, enforce its intake budgets before allocation, drain all
   sources to the release barrier, and call one existing recorded-only native
   history transaction. The helper does not determine gesture boundaries or
   replace native lifetime/revision guards.
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
