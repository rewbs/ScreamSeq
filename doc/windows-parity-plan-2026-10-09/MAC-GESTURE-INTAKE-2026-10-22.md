# Shared capture: Mac AU intake continuation

## Stopped control handoff, 22 October 12:57 UTC — unbuilt

The retained timeline path already called AudioOutputUnitStop before adopting
stopped work, but AudioDevice's catalog read, preview cancellation, graph preview
restoration, parameter activity initialization and latency refresh still used
`!active()` as proof of quiescence. The callback sets playing_ false on a fault
before reading processor tail state and, on natural completion, before closing
its final timeline interval. These native control paths must not adopt work
while that callback is still finishing.

`AudioDevice::joinStoppedProcessing` centralizes the existing checked native
output-stop handoff. Those paths and recorded-timeline preparation/publication
now use it. It returns immediately for an active device. It never calls the
user-facing `stop()` method, resets processors or closes the retained clock.
All joining remains on the serialized control owner; the audio callback has no
new work, allocation, lock or logging. See Apple's
[AudioOutputUnitStop contract](https://developer.apple.com/documentation/audiotoolbox/audiooutputunitstop(_:)).

The native-plugin regression extends stopped review to eight repeated preview,
catalog read, cancellation and latency-refresh cycles, requiring the effective
value to restore while the accepted manual value, provider, editor and timeline
owner remain unchanged. It now runs the stopped timeline/preview checks for
both process-local AU and VST3 fixtures; the existing owned VST3 editor path
still checks window retention. No output device is opened by these cases.
They require the next Mac ARM/Intel build and native-plugin ordinary/owned-UI
checks plus unified-history. They do not simulate an in-flight Core Audio
callback: natural-end/fault/device and sustained realtime qualification remain
separate runtime requirements. Full retained-take session/API/UI remains open.

## Reciprocal and Intel update, 22 October 12:57 UTC

Intel ab5 job 119813994178 also completed 133/134 native registrations; its sole
failure is the same obsolete plugin-trust assertion corrected in source.
All 35 interface groups and the recovery, native editor, private socket and
shared conformance stages passed. The full logs were inspected; excerpts are
`admission-followup/mac-intel-result-excerpts.log`.
Windows→Intel run 39781599519 and Intel→Windows run 39782231757 passed all ten
fixture legs with build jobs skipped. Together with the ARM return above, this
qualifies the exact ab5 project exchange on both architectures, not the pending
source corrections or complete parity.

## Execution update, 22 October 12:45 UTC

The ab5f677c1 Mac ARM job 119813994591 (run 39775712080) completed:
133/134 native registrations passed, including native-plugins,
recorded-automation and unified-history with the stopped publication/history
checks below. Recovery/picker, private socket admission/replay, owned AU/VST3
editor lifecycle, sample library, all 35 interface groups and fixture/shared
conformance stages passed. Intel job 119813994178 is still building; no Intel
result is inferred. Excerpts are under `admission-followup/mac-arm-result-excerpts.log`.

The one failing native test, plugin-trust, still required a failed manual-state
commit to discard the accepted edit and report Save success. That conflicts with
the deliberately transactional prerequisite below. Its source assertion is now
replaced with repeated Save/recovery refusal retaining manual input, exact file
bytes, revision and history; restoration of the same trusted provider must allow
retry, exact Undo/Redo and reopen. Trust-location assertions remain intact.
This correction is **unbuilt**, and the candidate remains failed until it passes.

Windows-to-Mac ARM retained exchange run 39779951063 passed all ten fixture
legs using this candidate, with native build jobs skipped. The Windows return
leg 39780723571 also passed all ten without building. Full Mac retained-take API/UI
integration is still open; passing prerequisites do not supply those operations.

## Retained stopped publication, 22 October 11:34 UTC — unbuilt

Current source inspection found another concrete restoration boundary:
`TrackerSession::restorePluginGraph` used `AudioDevice::setPlugins` whenever
transport was stopped, including recorded-point edits and their history. Clear
also took this path. That replaced native processors and closed their editors.
The live path already had a bounded recorded-timeline publication, but its
AudioDevice adapter refused stopped use. Retained take review must not change
provider identities merely to clear or restore a timeline.

`AudioDevice::prepareRecordedAutomation/publishRecordedAutomation` now also
support stopped processors. Before control-owner adoption they join the output
callback with `AudioOutputUnitStop`: `active()==false` alone is insufficient
after natural completion or a processor fault. This join deliberately does not
call `stop()` or shorten the retained presentation clock. Preparation adopts
previous pending work so repeated stopped edits cannot exhaust unconsumed
publication slots. Live adoption remains on the callback. Each plan captures
`pluginEpoch_`; a replacement rack refuses it before hosted pointer checks,
including potential allocator address reuse.

The session uses this path only for an available, unchanged rack and unchanged
native song. Structural/opaque-state changes retain their existing native rack
path; unresolved plugins retain their fallback. Clear restores explicit saved
manual baselines in the same publication, then records its existing single Undo.
No project format or API changes are introduced.

Prepared checks (not executed): `native-plugins` exercises stopped publication
without opening an audio device, eight consecutive publications, unchanged
provider/host identity, manual-value exclusion, newer manual-write refusal,
outdated timeline refusal and replacement-rack refusal. The existing `--ui`
fixture additionally requires the same native editor to remain visible.
`unified-history` adds the real session/API stopped point edit, Clear dry run,
Clear/Undo/Redo/no-op and exact full-tree save/reopen checks with a second
unrelated processor. The existing live clear/custom-editor and realtime hosted
tests remain necessary; this source does not establish their new execution.

Full Mac retained-take API/UI integration remains open. In particular, Mac
custom editors act on the audible vendor, unlike Windows' isolated editor
provider. Numeric timeline restoration cannot prove restoration of opaque
program state; input leases and explicit vendor restoration must remain part of
that integration. This batch fixes the existing stopped path without claiming
that the seven public lifecycle operations have been ported.

## Manual-state preservation prerequisite, 22 October 11:01 UTC

Source inspection found that Mac `commitManualParameters` moved and cleared
the accepted deltas before constructing temporary providers, then dropped any
refused values or failed slots and continued Save/recovery/Play with a warning.
This is unsafe as the baseline for a retained gesture take: accepted manual
values must survive a provider failure independently of live automation.

The new control-only `editor/hosted/ManualParameterState.hpp` stages every opaque
state before swapping the rack and clearing deltas. Its factory constructs an
isolated native provider from saved state. A queue refusal gets the existing
single drain-and-retry; a repeated refusal, construction or serialization failure
preserves the entire original rack and pending set. Project-owned identity,
assignment, layout, aliases and bypass are preserved exactly. The Mac bridge
uses this helper; Windows' existing transaction ownership is not replaced.

All save/recovery/play and rack mutation callers already catch standard
exceptions and return failure. Export's existing `serializedData` fallback
returns empty data on failure. No file is written by this helper, and no playing
vendor or audio callback is touched. Save/recovery now refuses instead of storing
a partial manual baseline. Pending values remain in memory and Undo snapshots;
this does not add an on-disk detached manual-delta recovery format, so failed
recovery still needs explicit visibility and retry in the future lifecycle UI.

Prepared tests in `ManualParameterStateTests.inc` cover second-provider refusal,
construction and serialization failure after the first provider succeeds;
preflight of invalid slots/nonfinite values; successful retry, queue drainage,
last-write ordering, exact metadata/unrelated state, and real builtin
serialization/reopen. These join `recorded-automation-tests`. Existing Mac
`UnifiedHistoryTests.mm::exactParameterHistory` covers folded pending state,
interleaved Undo/Redo, opaque fields and save/reopen. Run both and Mac actual
socket save/history checks at the next shared checkpoint. **Unbuilt and untested**;
the 97bd596a4 review build stays frozen. Full Mac retained recording remains open.

## Current execution and next source, 22 October 10:43 UTC

The `3cefb9ab9ff0a892586c21442e5c8d78746fbd03` Mac ARM job
`119763197802` (run `39760505711`) passed its build, all 134 native
registrations, recovery/picker, owned AU/VST3 notification lifecycle, interface,
sample-library and requested shared editing/conformance stages. Result excerpts
are retained in `bin/mac-gesture-20261022/clock-followup/ci/`. Intel Mac and
Windows x64 were still running. This updates the foundation's execution status;
it does not implement the missing Mac retained-take session/API/UI.

The next source extracts grouped arm/stop validation to
`editor/hosted/RecordedGestureBoundary.hpp`, now called by the Windows owner.
It preflights complete provider membership, incarnation/loss counters, bounded
pending input and original clock boundaries before mutating any take. Arming
requires empty queues; Stop permits pending input and reviewable loss but still
requires the actual fixed-cut drain before Keep. The half-open stop time includes
the final native clock quantum and rejects overflow. Native owners still capture
the grouped cuts, recheck context and own their leases/restoration.

Shared regressions cover partial/foreign/extra providers, stale/zero/overflow
times, source changes, oversized pending counts, nonempty arm, loss, repeat Stop
and a cut preceding retained input, preserving the entire take on refusal.
The real Mac VST3 slider test now uses this shared policy and reads its exact
stop fence rather than an unbounded queue. These changes are **unbuilt** and
belong after the frozen Windows review candidate `97bd596a4`.

Required next validation: recorded-automation and Windows plugin notification/
recording worker gates; Mac native-plugin ordinary and `--ui` paths on both
architectures. Include these targets in one later coordinated checkpoint.
Do not change the prepared 11:06 review-only freeze to absorb this source.
No public method, project format, manual baseline, native restoration policy or
audio callback changes here. The full Mac lifecycle migration below remains open.

**Execution update, 22 October 08:06 UTC:** the AU/input-lease foundation is frozen
at `352ee334b` in PR #6. Its Windows ARM64 build, seven selected native checks and
six app cases passed with the import-order follow-up described in the
[current queue](CONTINUATION-QUEUE-2026-10-20.md). Both Mac CI builds and Windows
x64 CI remain in progress. No Mac provider pass or foreground UI pass is claimed.
The next build gate is **09:00 UTC**. The subsequent raw-rack adapter below is
source-only and is not part of that running candidate.

## Original preparation record (before the 07:57 checkpoint)

Status at preparation: source prepared, **unbuilt and unqualified**. This temporary feature
branch follows the Windows candidate; it is not a separate platform integration
branch. Both platforms continue toward shared main. The Windows `313893b9f`
candidate and its qualification checkout remain unchanged and available for the
pending Computer Use/global-focus check. No build or CI was started for this work.
The next coordinated build cannot start before **2026-10-22 07:57:09 UTC**.

## Observed boundary

`mac/Bridge/TrackerSession.mm::collectPluginEdits` still consumes the legacy
id/value overload; `acceptPluginParameter` records at current telemetry while
playing. It does not yet use the shared retained-take lifecycle or the seven new
Windows `automation.recording.*` operations. This is a shared architecture/API
convergence task; Windows already has public Record/Stop/review/Keep/Discard.
Recipe-copy recording is not a Mac reference feature either.

`MacPluginBackend` forwarded VST3 notification fences but returned Unsupported
for AU. Its AU observer held a separate 1,024-event array, read/write cursors and
loss counter. Closing the editor deleted that observer and added its unread
events to the loss count. Consequently already-delivered values/timestamps could
not be drained after close. The observer itself belongs to the main run loop;
audio rendering must not consume it or allocate its storage.

## Prepared implementation

- `editor/hosted/PluginEditQueue.hpp` now provides a capacity-parameterized
  implementation. `PluginEditQueue` remains an alias with its exact existing
  8,192-event VST3 budget; AU uses 1,024. Fence/cursor/order semantics are shared.
- `mac/Audio/AudioUnitEditIntake.hpp` retains original delivered events, a
  monotonic saturating overflow counter and fixed cuts. This is owned by the AU
  backend, independently of its editor/listener. Its prepared storage is included
  in the provider's storage accounting; no VST3 queue size changes.
- `mac/Audio/AudioUnitHost.mm` forwards actual AU callback values/timestamps to
  that intake, retains unread events across editor close/reopen, and captures
  fences on the native main owner using the existing host-time domain. Event
  registration errors now fail editor attachment explicitly rather than silently
  providing incomplete observation. No new public API or project format is added.
- The existing process-local `mac/Tests/FixtureAU.mm` has an opt-in Cocoa slider
  which uses `AUParameterSet`, exercising the real AU notification API. Default
  multi-bus/audio fixtures retain their previous no-editor behavior. The fixture
  is never installed or registered outside its process.
- `mac/Audio/PluginWindow.hpp` and the owned `RSPluginEditorWindow` in
  `AudioUnitHost.mm` now provide a native-main-owner input lease. It gates normal
  events, key equivalents and user close on that specific host window, retains
  each native control's previous enabled state, and restores those states without
  making a window key or replacing its first responder. Active field-editor
  drafts, tracking/modal run-loop modes, mouse drags and attached child/dialog
  windows refuse admission before control mutation. Enumeration is bounded at
  8,192 views. These conservative admission rules still need ergonomic testing.
- Both AU and `mac/Audio/VST3Host.mm` implement the existing backend lease
  interface: exact provider/token identity, no nested lease, no editor opening
  while protected, retryable cleanup and explicit host-close cleanup. This does
  not discard programmatic vendor notifications; the shared take owner must
  retain/recheck unexpected input before publishing a restored baseline.

The notification API choice follows Apple's [Audio Unit view/event guide](https://developer.apple.com/library/archive/documentation/MusicAudio/Conceptual/AudioUnitProgrammingGuide/TheAudioUnitView/TheAudioUnitView.html)
and [AUParameterListenerNotify reference](https://developer.apple.com/documentation/audiotoolbox/auparameterlistenernotify(_:_:_:)).
The test is intended to qualify actual delivery, not infer it from the API name.

## Required execution at the next checkpoint

1. Shared `recorded-automation` runs the complete queue ordering, fixed-prefix,
   overflow/wrap, allocation and concurrent producer/consumer checks at both
   capacities. The original 8,192-event assertions remain intact.
2. Mac `plugin-robustness` adds AU-budget overflow/loss retention, unknown original
   timestamps, fixed-cut suffix preservation, stale cuts and allocation/lock
   checks. This is a pure intake check, not proof of AU listener delivery.
3. `native-plugin-tests ... --ui` exercises actual AUParameterSet/listener delivery,
   original-time retention, fixed cut versus later input, close/reopen with unread
   events, preserved native value, exactly-once drain, and provider replacement.
   The existing VST3 custom-slider-to-audio test also runs. The ordinary CTest
   invocation lacks `--ui`; the Mac CI workflow now explicitly schedules this
   bounded 120-second owned-editor check from the same build, retaining its log.
   This is not a physical display or Core Audio loopback qualification.
   It also protects actual AU/VST3 fixture windows, checks native close refusal,
   unchanged values/geometry/focus, native control reenable, and original-time
   programmatic delivery while protected. The ordinary native-plugin test covers
   closed-editor leases for both providers: wrong/foreign/stale tokens, nested
   refusal, blocked opening, retryable release and explicit-close cleanup.
4. The shared queue header is a Windows provider input: rebuild the relevant
   Windows host/recorded-automation targets once, retain real VST3 notification,
   audible latest-value, overflow and callback-allocation assertions. Do not
   reuse the old binary as proof of this changed shared header.
5. Both native Mac architectures must compile and run the changed AU path.
   Existing native provider/PCM tests cover the storage-accounting change and
   unchanged audio/property behavior. API inventory/project format are unchanged;
   the final paired history/persistence/exchange gate still remains required.
6. `plugin-window-geometry` retains its existing geometry assertions and adds
   owned, offscreen native input checks: key dispatch and key equivalents before,
   during and after a lease; independent-window input; enabled/disabled controls;
   close notification; raw text/focus retention; and refusal of an active field
   editor or child window. This exercises AppKit directly, not OS-level input or
   Computer Use. Live keyboard/IME and real-vendor behavior remain separate gates.

Cheap checks so far are source/diff review only. No compilation, native test,
new AU runtime observation, or passing CI is claimed.

## Remaining work and limits

A fixed notification cut alone is not an exclusive native input lease. AU events not
yet delivered by Apple's listener when it is disposed are outside this queue's
retention guarantee. The newly prepared native AU/VST3 barriers are unqualified;
they protect the existing host window, not arbitrary programmatic vendor activity
or a separately created vendor top-level window. Late vendor windows/input must
be accounted for by the session's admission/recheck policy before enabling this
lifecycle publicly. Fixed release cuts, manual/vendor-state restoration, retained recovery and Mac session/API/UI
integration are still required before exposing the shared take there. Merely
adding the seven API names would not preserve these semantics.

In particular, the legacy Mac consumer still chooses record-versus-manual
behavior at poll time. This source is a dependency of the lifecycle migration,
not evidence that close/stop races, original-time musical placement or complete
Mac recording parity are solved. Keep that end-to-end migration and its tests
on this branch before claiming or releasing complete shared recording behavior.

The final migration should delegate capture/timing/validation/recovery to the
existing shared `RecordedGestureTake`, `RecordedGestureCapture` and recovery
types. AppKit input ownership and Core Audio/provider adapters remain native;
no cross-platform UI/audio framework, stable-ID rename, plugin-state rewrite
or format migration is proposed.

## Following source batch: raw rack reads

Current `PluginChain::popEdit` does more than consume an event: it queues an
observed manual write, changes the rack's accepted parameter catalogue, clears
preview flags and updates activity values. Using it for retained-take intake
would overwrite the independent manual baseline before Keep/Discard. This is
visible in `editor/hosted/PluginChain.cpp`, not inferred from a progress report.

The prepared `editorInputBoundary`, `popEditorInputBefore`, and native barrier
forwarders expose the existing provider guards through the shared chain without
those manual side effects. `mac/Audio/AudioDevice.hpp` forwards them for the future
session owner. Missing slots return no boundary/lease or InvalidFence with a
cleared source; provider incarnations guard wrong/stale cuts and lease tokens.
These are serialized control-owner operations. The future session must retain
stable identity and pluginEpoch guards, prevent competing legacy consumption,
own preview/restoration, and handle chain replacement before exposing Record.
No existing Mac consumer is switched yet and no public API is added.

`editor/Tests/PluginChainCaptureTests.inc` supplies one set of regression assertions
to actual Windows and Mac VST3 fixtures: protected callback capture, original
timestamps, fixed-cut suffix retention, foreign/missing/stale boundary rejection,
unchanged accepted manual value, retryable lease cleanup, and the existing later
manual drain. Both native tests render the final .625 gain; Windows retains its
host allocation audit. Mac drives an owned actual NSSlider in the `--ui` check.
These new tests have not run. The legacy manual path remains unchanged.

The same next batch fixes the recording test module's import order so it can be
selected independently. A standalone import passed without launching the app;
the candidate's three recording cases passed through the explicit-path follow-up.

## Following source batch: shared bounded drain policy

`editor/hosted/RecordedGestureDrain.hpp` extracts Windows' provider preflight,
128-events-per-provider poll, fixed-stop-cut handling, paired empty-read
acknowledgement and latest-original-timestamp preview selection. The Windows
adapter now delegates those rules while preserving its existing API errors,
preview-publication retry and native baseline restoration. Missing/partial
catalogues or fixed cuts are rejected before consuming notifications.

The callbacks are serialized control-owner operations: observation returns an
optional exact provider identity/loss counter; reading returns Event, Complete
or a rejected boundary and always supplies its paired provider source. A native
adapter may throw its own lookup error. Native cut creation, UI leases, provider
hosting, stable slot mapping and audio preview remain platform-specific. No UI,
device, project format or public API is introduced by this extraction.

`editor/Tests/RecordedGestureDrainTests.inc`, registered through the existing
recorded-automation executable, covers exact-budget continuation, a later
provider failing before any pop, changed revision, fixed suffix retention,
stale/unsupported/partial cuts, original out-of-order timestamps, equal-time
last delivery and loss on an empty provider. These are prepared source tests,
not executed results. Mac still requires a session take owner and guarded
preview/baseline restoration before replacing its legacy recording consumer;
the raw rack adapter and shared drain are prerequisites, not API parity closure.
