# Shared recorded take ownership

**Windows ARM64 qualified on `7fb99d0b2`; Mac/x64 CI still running.** See the
[batch results](TAKE-BATCH-QUALIFICATION-2026-10-20.md) for exact source,
binary and test scope. This is a control-owner layer over
`RecordedGestureCapture`, not a user-facing Record feature.
No API, project format, native UI or audio callback behavior changes yet.

### Review corrections and native-input checks

The batch also fixes two source-level defects found before exposing this
owner through either session:

- A capacity/storage/allocation refusal is reviewable, but a later document,
  playback-clock or provider identity failure must supersede it permanently.
  Otherwise partial-take consent could mask that later failure. Four ordered
  overflow-then-identity regressions now exercise actual refusal and ensure
  the publisher is never called, even when the original arguments are restored.
- Ordinary provider polls resolve pending original timestamps while their
  presentation interval is still available. Waiting until release could lose
  valid early events when a long take overwrites the bounded clock ring.
  A regression supplies delayed coverage, resolves it on an empty provider
  poll, evicts all original intervals, and checks the exact saved point.
  Resolution runs once per provider poll/drain, not once per raw notification
  in a dense batch; the raw intake path only validates and appends.

`windows/Tests/Plugins/EditNotificationTests.cpp` now routes real fixture VST3
callbacks through the take. It retains two ordered events until after release,
excludes pre-arm/post-release events, requires the explicit owner barrier plus
empty paired pop, checks 96 kHz to canonical 48 kHz mapping against original
callback timestamps, and publishes once. The existing overflow and PCM/audit
checks remain.

`mac/Tests/NativePluginTests.mm` now feeds the fixture's actual NSSlider action
through this same shared owner after release. It checks exact original-time
publication and leaves the existing audible callback assertion intact.

The Windows native notification, shared and worker checks **passed on ARM64**.
Mac `native-plugins` and x64 execution remain in the running CI batch. These
checks use controlled presentation coverage; they do not prove device latency,
session arming, manual-state exclusion, recovery or the final native Record
workflow. They remain in existing registrations, not separate build targets.

## Why another ownership boundary is needed

The per-parameter capture and atomic publication already exist. A native
session still needs one owner for all parameters between arm and release,
including parameters whose first notification arrives after Stop. Checking
the aggregate budget only at final publication would permit excessive intake;
calling each target's completion separately would split history and risk a
partial take.

`editor/hosted/RecordedGestureTake.hpp` owns one token, full document revision,
original arm/release times, retained timeline, ordered stable rack identities,
one shared immutable point baseline, and explicit notification-provider scope.
It retains identity-only rack records, not vendor blobs. Mac and Windows can
use the same owner while retaining their native worker, UI and device barriers.

## Intake and completion contract

- Feed every paired `PluginEdit`/`PluginEditSource` before coalescing. The
  captured provider incarnation and monotonic loss count must agree. Missing
  timestamps remain visible as unmapped; worker delivery time is never used.
- New targets are created on first input. An event delivered after release can
  still create a target if its original timestamp precedes release. Events
  before arm or at/after release are outside the take.
- Enforce 128 targets, 8,192 events per target, 100,000 aggregate events and
  64 MiB retained/candidate accounting during intake. Failed admission retains
  existing input, records the failure and rejected-event count, and continues
  to own its providers. Native code must not fall back to manual capture for
  a failed/full recorded take.
- Provider loss is reported once per provider. Every target from that provider
  retains the same counter baseline. Loss from a provider with no successfully
  delivered events still blocks an ordinary completion.
- Seal preflights every target before changing any release bound. An empty
  poll while recording cannot satisfy a drain. After seal, each provider needs
  the native input barrier and an empty paired pop. Late input invalidates its
  previous drain acknowledgment.
- `commit` reconstructs current member pointers at the native commit boundary.
  Its synchronous callback uses the existing atomic batch publication path;
  acceptance must retire every target, refusal must retire none. Exceptions
  retain the take. Empty successful takes retire without invoking publication
  or adding history.
- Explicit loss review can admit the retained valid portion after source loss
  or capacity/storage/allocation refusal. It cannot bypass changed document,
  playback/provider identity or an invalid target. This explicit consent must
  also reach native `commitRecordedGestures(..., acknowledgeLoss)`.

`preparedOwners` is a temporary observation/preparation view, not a captured
transaction to cache across native input. Native completion must use `commit`.
All operations are serialized outside audio; no concurrent or reentrant intake
or message pumping is permitted inside its publication callback.

## Checks prepared for the next existing targets

`RecordedGestureTakeTests.inc` joins `recorded-automation-tests`; no new build
target or duplicate suite is introduced. It covers same-frame input retention,
outside scope/time, delayed target creation and clock coverage, all-provider
drain, atomic failed seal, stale prepared candidates, late input, one
publication, empty/no-op retirement, cumulative provider loss including a lost
first event, stale context/provider, exact target/aggregate/per-target limits,
and missing timestamps.

The existing `windows/Tests/RecordedGestureCommitTests.inc` now feeds this take
owner into the real `PluginOperations` completion. Its saturated real timeline
queue, no-partial-publication, exact saved/manual state, one Undo/Redo,
save/reopen, no-op Redo preservation and PCM checks remain. Controlled
notifications are not evidence of native slider/vendor intake.

The shared and worker groups passed in the coordinated ARM64 build together
with the graph-selector repair. Both running Mac jobs must compile and execute
the shared tests and protected-source history fixture correction. The prior
candidate's successful reciprocal exchange does not qualify this new header.

## Remaining native work

1. Session-owned arming captures current revision, provider/catalogue,
   original input time and retained playback lifetime; keep one take owner.
2. Route native slider and paired vendor notifications before coalescing.
   Establish actual main-thread/vendor release and Stop barriers; merely
   observing an empty queue is insufficient.
3. Exclude recorded movement from manual opaque-state flushing and recovery
   overlays; use transient audible previews and restore accepted manual
   baselines on cancellation/failure.
4. Serialize unfinished take input and failure evidence separately from
   committed recorded points, protect departure, and hydrate a stopped review
   owner without inventing clock continuity or reusing process-local provider
   identities.
5. Add both-host API/schema/guide and native arm/status/review controls,
   then qualify real native gestures, Stop/drain, recovery and F04 save/reopen.

Do not expose a Windows Record toggle ahead of those protections or equate
this prepared ownership layer with complete recording parity.
