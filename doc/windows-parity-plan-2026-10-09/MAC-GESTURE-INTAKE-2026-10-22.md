# Shared capture: Mac AU intake continuation

## Mac Intel terminal result, 22 October

Intel job 119842551391 in run 39784093814 also passed: 134/134 native tests,
34 named interface groups plus final editor/keyboard checks, recovery/picker,
private socket, owned AU/VST3 lifecycle, sample library and fixture conformance.
Its terminal log was inspected; excerpts are in
`focus-followup/mac-intel-result-excerpts.log`. Both Mac architectures now qualify
the 1fb prerequisite source within these fixture bounds. Full retained-take
session/API/UI integration and physical device/realtime qualification remain open.

## Mac ARM checkpoint, 22 October 13:37 UTC

Source `1fbfebd7346013926b908250390b8dd0b5af31a7` passed Mac ARM job
119842551604 in run39784093814. Its terminal log confirms **134/134 native
tests**, including native-plugins, plugin-trust and unified-history. The full
native interface harness passed (34 named groups plus the final editor/keyboard
checks); private socket, owned AU/VST3 editor lifecycle, sample library,
recovery/picker and fixture/shared editing conformance stages passed too.
Excerpts: `focus-followup/mac-arm-result-excerpts.log`.

This qualifies the stopped-control prerequisite below on that architecture's
fixtures. It does not add the unfinished retained-take session/API/UI or prove
an in-flight Core Audio natural-end/fault callback and sustained realtime
behavior. Intel job119842551391 is still running at this observation. No job
was restarted. Later scratch-refresh source changes are Windows-only.

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

## Following source batch: atomic Mac take preview restoration — unbuilt

`AudioDevice::cancelPluginParameterPreviews` now accepts a complete set of
stable plugin IDs, parameter IDs and process-local provider incarnations,
plus the captured whole-chain epoch. It resolves current slots on the control
owner and delegates the all-or-nothing cancellation to the existing shared
`PluginChain::cancelParameterPreviews`. This avoids restoring one plugin and
then failing on another, and leaves accepted manual baselines independent of
transient preview values. The existing single-parameter preview API is unchanged.

Source inspection found that `publishLiveRack` can reorder the control rack
without incrementing `pluginEpoch_`; that epoch protects whole-chain replacement,
not positional identity. The new adapter therefore resolves stable IDs and
checks native incarnations both before the stopped handoff and after applying
already-accepted pending work. An old take cannot use a new epoch to bypass a
replaced provider's identity. It never calls `pluginStates()` (which stops the
device), reconstructs a vendor, opens an output device or closes a native editor.
Active publication uses the existing bounded parameter queue. Native input
leases, fixed Stop cuts and provider rechecks remain required in the future
session owner; this helper alone does not establish that lifecycle.

`NativePluginTests.mm::stoppedTakePreviewRestoration` adds one mixed AU/VST3
case to the existing `native-plugins` executable. It checks two-target restoration
in reverse request order, stale epoch, missing later plugin, foreign provider,
invalid later parameter, unchanged manual baselines and clock/provider identity,
idempotent restoration, and a reordered replacement rack with a new preview.
The shared `ParameterPreviewTests.cpp::takeCancellation` already covers queue
capacity refusal, invalid suffixes, duplicates, unrelated preview ownership and
rendered PCM; those assertions remain unchanged. No new build or test has run
for this adapter. Both Mac architectures must execute the native case before
this source is qualified; active callback/natural-end behavior and full
Keep/Discard UI/API/history/recovery integration remain open.

This follow-up is on the backup branch after the Windows palette freeze077e.
Do not silently include it in the prepared `palette-availability/` candidate or
start another build for it. Group it with the subsequent coherent Mac take
owner batch and its required Mac gates, preserving the hourly build cadence.

## Following source batch: native Mac take intake adapter — unbuilt

`mac/Bridge/NativeParameterTake.hpp` now coordinates the Mac control-owner
portion of a retained take. It delegates fresh-arm/Stop validation to shared
`RecordedGestureBoundary`, bounded original-time polling and latest-value
selection to `RecordedGestureDrain`, and event/time/range/conflict/recovery
state to `RecordedGestureTake`. Native catalogues, stable-ID-to-slot lookup,
Mach clock reads and Core Audio preview publication stay in the Mac adapter.
The owner is noncopyable and rejects reentrant entry, changed document/clock
context and a replaced audio host. Arming rechecks context after fallible
catalogue work and refuses earlier queued manual input without consuming it.

Live polls capture a bounded native prefix; Stop retains one fixed cut and
repeated Stop cannot move it. Later native notifications remain queued. A
preview queue refusal retains the original take and retries its latest values
on the next poll. Recovery reads a bounded prefix without sealing a live take
or publishing preview values. Explicit host controls retain their original
past Mach timestamp and reject future times before intake. Audible restoration
uses the identity-guarded multi-target adapter above.

The native-plugin fixture adds closed AU/VST3 host-control cases and a visible
VST3 slider case to `--ui`. They assert original-time mapping after delayed
clock coverage, full-queue refusal/retry without duplicated input, repeated
Stop, changed-context refusal, a post-Stop suffix, refusal to arm over that
suffix, baseline restoration under a native lease, and retained provider/window
identity. These are prepared assertions, not executed results. Original native
and rendered shared queue assertions remain mandatory.

The session does **not** instantiate this owner yet. It still needs exclusive
manual-consumer admission, grouped native restoration leases and opaque-state
verification, commit into the existing unified history, recovery storage, the
seven API contracts and native controls/review UI. `restorePreview` alone is not
release: the caller must hold input leases, recheck fixed cuts and verify vendor
state before dropping ownership. In particular, Mac's rendering vendor is also
its editor vendor; copying Windows' isolated-vendor state replacement would be
incorrect. This source must not be described as completed recording parity.

## Following source batch: guarded native restoration — unbuilt

`NativeParameterTake::restoreRetained` now acquires the full group's native
input leases before restoring audible previews or invoking the session's saved
state verification callback. It checks the immutable Stop queue positions,
provider incarnations, loss counters and empty pending prefixes before leases,
after acquiring them, after verification and after releasing them. A later
native edit refuses restoration without consuming it. Even explicitly consuming
that later edit elsewhere cannot silently rebase the original Stop position.

Partial lease acquisition cleans up acquired tokens; a failed cleanup retains
its token in the take for retry. Opaque verification failure retains the take
and releases ordinary input where possible. The owner is never reset by this
method: session history/recovery publication and explicit ownership release
still follow successful native verification. It rejects reentrant intake while
restoration runs and never replaces Mac's shared editor/render vendor. The
low-level `restorePreview` remains separately documented for a caller already
holding verified native leases; it is not a release operation.

Prepared `native-plugins` assertions cover mixed AU/VST3 partial group lease
refusal without prefix restoration, successful retry, live-take refusal,
verification failure/retry, provider/value/input retention and a post-Stop suffix
that remains protected even after an explicit fixture-only consume. The `--ui`
fixture additionally covers a successful retained VST3 window with its slider
disabled during verification and enabled after failure and success. Existing
original-time, queue-capacity, fixed-Stop and shared rendered assertions remain.
No build/test has run for this source. Both Mac architectures and the visible
native fixture must pass at a future coordinated checkpoint, no earlier than
16:09:04UTC on22October. The077e Windows package deliberately excludes it.

Remaining session work: supply real opaque saved-state/manual-baseline
verification (not just the test callback), exclude manual consumers and state
mutations while the take owns a provider, coordinate unified Keep history and
recovery publication, and connect all seven lifecycle API methods/native controls.
Closed-editor explicit abandonment needs its own retained-evidence policy; this
strict helper does not implement it or silently drop queued vendor callbacks.

## Following source batch: atomic Keep adapter — unbuilt

`NativeParameterTake::commit` now performs the native orchestration around
`RecordedGestureTake::commit` and `RecordedGestureCapture::prepareBatch` /
`publishBatch`. It validates the captured rack identities and point baseline,
refreshes native catalogues and provider loss observations, rechecks context
after vendor calls, and submits the complete membership to one synchronous
session publication callback. The callback owns staged history, persistence
and Core Audio publication; false/exception retires no target. A completed
retry returns accepted without invoking publication again. An empty take
completes without creating a history callback. No UI/device model is shared.

The mixed native AU/VST3 fixture now prepares assertions for stale point and
revision rejection, whole-take refusal/retry, unrelated point preservation,
reentrant intake refusal, actual stopped `AudioDevice` timeline publication,
all-target retirement, completed retry after revision advancement, retained
restoration, and empty no-op. These are source assertions, not executed proof;
the test callback does not establish real session Undo or persistence.

Next integration step is to call this adapter from TrackerSession's unified
EffectSnapshot history owner, reserving history and point storage before
`publishRecordedAutomation`, then no-throw swapping accepted model state.
Session admission must exclude legacy `collectPluginEdits` and manual state
capture for owned providers before exposing any lifecycle API. The existing
legacy recording path still timestamps at polling time and remains a confirmed
gap until that migration is complete. No public lifecycle claim is added yet.

Catalogue observation additionally pairs the native slot/provider before and
after each vendor query. A live rack reorder does not change the whole-chain
epoch; resolving only the stable ID after a pumped catalogue call could pair
another plugin's range metadata with the original target. Arming and Keep now
reject that changed observation before using its catalogue. This guard remains
part of the unbuilt Mac batch and does not change the frozen077e candidate.

## Following source batch: session ownership, Keep and recovery — unbuilt

`TrackerSession` now owns a `NativeParameterTake` through the internal
`ParameterTakeSession.inc`. The internal start path validates distinct stable
plugins, rejects a concurrent arm, drains preceding manual input, rechecks the
captured revision, folds accepted manual values, requires an actual playing
Core Audio frame timeline, and installs the owner only after native catalogue
and fresh-cut checks. Its context reader uses a weak session reference and
the exact retained timeline; it does not keep the session alive in a cycle.

When this owner exists, `collectPluginEdits` uses its bounded raw-input path
instead of the legacy manual consumer. Transport Stop captures the fixed native
cut before stopping audio. Internal Keep requires an already stopped take,
prepares a complete recorded timeline plus `EffectSnapshot`, reserves Undo
capacity before native publication, and adopts points/revision/history only
after that publication succeeds. One complete take creates one history step;
a completed retry retains ownership and cannot publish it again. It does not
release ownership or claim native state restoration is finished.

Recovery captures available original input without sealing a live take. A
refused further drain records the warning while preserving already retained
evidence. The typed archive overlay runs **after** `ProjectMetadata::merge`,
so imported opaque recovery arrays are not replaced by a Foundation projection.
Ordinary save/export is refused while a take is retained. Saved-baseline reads
and recovery no longer capture live `pluginStates()` for that owner. New/open,
device/playback restart, rack changes, manual parameter writes, history and
preview/audition entry points have native guards; API mutations are admitted
conservatively until the lifecycle methods are installed. Shutdown drops the
owner before its audio reference, after the application's recovery phase.

The recovery-array overlay is now shared with Windows in
`editor/hosted/RecordedGestureRecovery.hpp`. It stages the complete array,
replaces only the same take/revision, preserves independent/future archives and
binary payloads, refuses incompatible collections, and enforces the existing
64-archive bound. Windows retains its existing API error code. Prepared shared
tests cover replacement, separate revisions, typed preservation, full-capacity
update/refusal and unchanged incompatible input. `SessionTests.mm` adds real
session admission refusal checks (empty/duplicate/missing/stopped/stale), with
unchanged revision and serialized baseline. Neither set has run yet.

**Not exposed or complete:** no public API or native Record control calls the
new internal start yet. The old poll-time recording flow remains in place for
existing users. Native opaque-state verification/restoration and explicit
post-Stop suffix abandonment remain prerequisites for exposing the seven API
operations and replacing that UI flow. The direct session edit paths still
need the final admission audit when capture/review controls are connected.
Do not treat internal ownership and a prepared history callback as successful
session lifecycle qualification.

Next required tests at a coordinated build: both Mac architectures' existing
native-plugin, session and recorded-automation gates; Windows recorded-automation
and actual-app recovery tests for the shared overlay; then new real session and
socket cases covering multi-target Keep, no-op, queue refusal, original-time
input, single Undo/Redo, recovery while live, save/reopen and protected vendor
restoration. Those end-to-end successful session cases await the release/API
integration. No new build was started for this source; the next eligible start
remains 2026-10-22 16:09:04UTC, with no obligation to build an incomplete batch.

## Following source batch: real stopped vendor verification — unbuilt

Source inspection found a specific live-release hazard: Mac
`VST3Plugin::state()` flushes pending changes with a zero-frame `process` call
and synchronizes controller values. Calling it concurrently with the same
rendering vendor is unsafe. `AudioDevice::pluginStates()` additionally stops
transport. Neither is suitable for an unnoticed live release.

`AudioDevice::restoreStoppedPluginBaselines` now provides the quiescent part of
release. It rejects active playback, joins a naturally stopped callback without
changing the retained frame clock, validates the entire group's native sources,
prepares canonical accepted state and writable values using isolated providers,
then restores those values in bounded batches. It rechecks all stable slots and
provider/loss identities across vendor calls, pending work and each publication.
The actual retained vendors' opaque bytes and audio layouts must match the
canonical accepted baselines. A failure retains the take even after a partial
vendor restoration; saved plugin blobs and musical history are never rewritten.
The outer take still owns native leases and fixed-cut checks before and after.

Shared `PluginChain::state(slot)` supplies the existing quiescent capture policy
for one provider, avoiding serialization of unrelated vendors. `states()` uses
the same policy; transient preview exclusion and the audio-block guard remain.
The captured rack entry is retained across vendor calls. Prepared shared preview
tests check per-provider accepted-state capture and missing-slot refusal.

`NativeParameterTake::restoreStopped` composes this with grouped leases, and
the internal session releases ownership only after success. Mixed AU/VST3
fixtures now include real saved-baseline verification after Keep, opaque AU
hidden-state mismatch, retained ownership after refusal and successful retry.
They remain unexecuted. Existing visible lease, suffix, timing and history
assertions are retained, with both Mac architectures and shared Windows/Mac
parameter-preview/recorded gates required at the next source checkpoint.

This is a prerequisite, **not a substitute for the requested final behavior**.
Release during playback, closed-editor opaque replacement/explicit suffix
abandonment, and full native controls/API/session qualification remain open.
Public Mac recording is still the legacy flow; no new lifecycle UI is exposed.

## Following source batch: direct native admission and real session gate — unbuilt

The API guard alone does not protect direct AppKit editing. `TrackerSession.mm`
now refuses retained-take edits at the native pattern/cell/order/sequence,
song settings, sample processing/import/settings, instrument import/settings
and device refresh entry points, before stopping audio or touching the song.
Previously prepared native history/rack/manual guards remain. Read-only
inspectors and ordinary live note input remain available. These conservative
guards apply only while the new internal owner exists; the legacy public
recording flow remains unchanged.

New Song now has `createSong:error:`. It prepares the new document and identity
before replacing audio/document state and reports refusal through NSError.
`main.swift` uses the throwing Swift bridge, so failure does not run the success
completion that clears the document URL, recovery identity and edited status.
The old `newSong:` convenience entry remains nonthrowing for existing native
callers, preserving the old song and surfacing the failure in `pluginWarning`.
The existing note/sample departure policy is unchanged. Full UI admission and
departure integration is still required before exposing retained parameter takes.

Internal `captureParameterTake` retains original control timestamps and returns
acceptance before any fallible preview/poll. `parameterTakeReview` returns a
detached shared review without consuming input. These remain internal methods,
not additional advertised API operations.

`mac/Tests/ParameterTakeSessionTests.inc`, included by `native-plugin-tests`,
adds an actual session/clock/vendor path: two native VST3 fixture providers,
direct-mutation refusal, original-time control capture, recovery during playback,
fixed Stop while transport continues, whole-take Keep, completed retry, refusal
of stopped verification during active playback, stopped native release, exact
manual/opaque state, one-step Undo/Redo, save/reopen, empty no-op and Discard.
It uses the existing **explicit** `--ui --loopback-record` path, selects
BlackHole 2ch and reuses that device's current buffer size. It neither opens a
default physical output nor changes a system default. Ordinary `--ui` CI does
not execute this new device case. Initial clock readiness has a bounded retry
only for the specific pre-arm timing absence; other failures are not retried.

No compilation or execution evidence yet. At the next coherent source checkpoint
run both Mac architecture builds, the existing session/history/native-host gates,
and this explicit virtual-device case on a provisioned Mac; a missing device is
an unmet qualification requirement. New/shared Windows gates from the preceding
sections also remain necessary. The socket and native take UI are still to be
connected; this test does not replace their coverage or qualify live release.

PR#6's earlier077e tree is integrated as main0fe8f3400. Its push CI is the current
hourly checkpoint (Windows39811540714, Mac39811541170). Latest build start
16:09:52UTC gives **17:09:52UTC** as the earliest following checkpoint. Source
work and backup pushes on this temporary branch do not dispatch another build.

## Following source batch: explicit failed-context discard — unbuilt

`NativeParameterTake::abandonStopped` adds a separate, explicit stopped Discard
path. Every owned native editor must already be closed, and the host and actual
provider incarnations must still belong to the take. It can seal an otherwise
unsealable take after a clock/document-context failure without accepting any
music. It preserves an existing Stop boundary. Ordinary Keep/restoration still
requires the original fixed cut and never invokes this path implicitly.

The complete group is protected against native editor input. Saved baselines
are restored and verified before consuming any abandoned notification. The
discard uses one bounded native cut per provider, checks for intervening input,
and never routes that input into the legacy manual consumer. Source changes,
reopened editors, unsupported barriers and opaque-state mismatch retain the
owner. A failure after an explicitly authorized drain may have consumed a
prefix of abandoned input; it cannot promise to restore those notifications.
The owner and its already captured review remain until successful release.
`discardStoppedParameterTake` resets the session owner only after success.

Prepared native AU/VST3 checks cover changed clock/revision, refusal before
touching an open editor, a real post-Stop slider suffix, missing saved baseline
without consuming notifications, successful closed-editor restoration, retry,
unchanged provider/host identities and refusal of a replaced host. Device-mode
session checks also cover the explicit release's history/state contract.
They are unbuilt. The native checks join the existing `native-plugins` and
owned `--ui` gates on both Mac architectures; the real-session case still
requires the explicit virtual-device mode described above.

Remaining release work is substantive: hidden state that cannot be reconstructed
through parameters still refuses, even with an editor closed. A safe opaque
replacement path must preserve unrelated processors and routing. Live release
must avoid concurrent VST3 state serialization/zero-frame process calls and
retain editor usability; a stopped-only escape does not close that requirement.
Full native controls, seven API contracts and socket qualification remain open.

## Following source batch: prepared stopped opaque replacement primitive — unbuilt

`NativePlugin::prepareStoppedReplacement` prepares a fresh native backend from
the accepted opaque blob without serializing or resetting the old vendor.
It rejects an open editor, an unsettled preset handoff, changed persistent
identity/assignments/bypass, ports, latency or parameter catalogue. It captures
the current provider and canonical replacement state before returning.
`adoptStoppedReplacement` exchanges backend ownership behind the existing
scheduling facade, changes the native notification incarnation, and retains
the old backend in the plan. Its input lease must be released through that
retained old provider. Stale and double adoption are refused. All of this is
quiescent control-owner work; playback must be prepared afresh before rendering.
It is **not** an audio-boundary/live-preset replacement.

This is an internal building block with no session caller yet. Before wiring
closed-editor opaque Discard, add complete-group preparation and validation,
combined retained/prepared storage accounting, revision/queue guards and
retryable old-provider lease cleanup. No successful prefix may escape a failed
group. A new provider cannot inherit the old take's input cut. The ordinary
scalar restoration path and public recording behavior are unchanged.

The shared parameter-preview registration now covers builtin state/identity
preservation, unrelated/stale/double-adoption refusal, reopened PCM, and repair
behind a completed live preset. `mac/Tests/StoppedPluginRepairTests.inc` adds
native VST3 effect/instrument and AU opaque-hidden-state coverage, old-lease
cleanup against the correct provider, save-state reconstruction, effect PCM
comparison and the existing host realtime allocation/free/lock audit. These
are source assertions, not execution evidence. Run them with the next coherent
both-Mac-architecture/shared-Windows checkpoint; no build was started for this
source batch. It does not close live release, native take UI or socket coverage.

The following unbuilt group layer, `PluginChain::prepareStoppedRepair`, prepares
every selected provider before adoption, rejects an outstanding preview, and
guards the retained rack entries, manual queue fence, graph/recorded revisions
and settled routing plan. It bounds one complete preparation plus retained host
storage at 256 MB, conservatively counting shared graph/preset storage again.
A native owner must retain at most one such preparation; this is not a general
unbounded draft cache. `MixerTransition::stoppedStorageBytes` collects completed
transitions and refuses unfinished ones before the final guards. Adoption then
has no native call or fallible operation between providers. It swaps the complete
group's backend/manual catalogue and retains old backends and queues in the plan
for explicit native cleanup. No unrelated rack entry or routing identity changes.

Prepared shared checks cover invalid later targets without a successful prefix,
intervening writes, retained previews, manual catalogue restoration, unrelated
providers, stale/double adoption and reopened PCM. A mixed AU/VST3 test adds an
invalid later AU blob, real hidden-state repair and old-provider lease cleanup.
These have not run. Session integration still needs a retained repair/cleanup
receipt so cleanup failure after adoption cannot cause duplicate replacement or
lose the take; input-cut checks remain the native owner's responsibility. No
public lifecycle/API changes or live-release claim follow from this layer.

## Next source batch after frozen da8c89f07 — unbuilt

`AudioDevice::StoppedPluginRepair` now carries the shared group preparation,
audio-owner identity, host epoch and original native provider identities. Its
prepare/adopt methods require stopped processing and closed original editors,
join the output callback, validate complete saved membership, and reject a
replacement audio host or repeated adoption. Old native backends remain in the
receipt for lease cleanup. `play()` always prepares a fresh renderer/chain,
which meets the primitive's prohibition on resuming old scheduling state.

The device-free Mac native fixture prepares AU/VST3 repairs without configuring
an output device, checks stale epoch/missing baseline refusal without a prefix,
complete restoration, retired lease cleanup, no audio activation, repeated
adoption refusal and a host replacement. It has not run and is not part of the
da8c89f07 checkpoint currently building. The retained take still needs its
retryable post-adoption cleanup state before this wrapper can be wired to
explicit opaque Discard; do not release ownership based on preparation alone.

## Explicit opaque Discard integration after 93cec81c3 — unbuilt

`NativeParameterTake::abandonStopped` now uses the complete stopped repair
receipt. This is the explicit **stopped, closed-editor Discard** path already
owned by `discardStoppedParameterTake`; normal Keep/verification does not
silently choose replacement. It cancels owned parameter previews, prepares the
entire accepted AU/VST3 group, checks every original input cut again, and adopts
only the fully prepared group. Stable project identities, accepted blobs,
unrelated processors, routing and history remain unchanged. Notification
incarnations intentionally change; old cuts must never be used on the new ones.

The take retains `StoppedPluginRepair` after adoption. Its old vendor backends
still own the abandoned queues and input leases. Cleanup resolves each lease
by stable identity **in the retained receipt**, never by a current rack slot.
Successful cleanup entries are erased individually; a later refusal leaves the
remaining entries and old backends available for retry. A repeat Discard finishes
cleanup and validates the adopted receipt instead of rebuilding a second group.
Changing the accepted saved state, replacing/activating the host, reopening an
editor, or delivering new input on a repaired provider refuses release and
retains the original take. No new-provider input is consumed as old take input.
The internal session stops polling the original providers after adoption;
autosave serializes the original detached review without attempting intake.
The existing session admission guard prevents playback/replacement/model edits
until ownership is released.

`AudioDevice` now captures both original and prepared-provider input fences.
Adoption checks the original queues again after native editor observation;
completion checks the prepared provider's original empty fence. The shared
`PluginChain::StoppedRepair::Item` retains the exact requested state for retry
validation separately from canonical native state and the swapped baseline.
Its extra blob capacity is included in the existing combined 256 MB bound.
This changes no project format, public API or callback work. Reopening playback
still constructs a fresh chain; the old scheduler is not resumed.

New source checks cover a genuinely divergent hidden AU setting alongside VST3
and an unrelated vendor; invalid later state rejects the group without changing
an earlier provider; successful Discard restores opaque/manual state, leaves
take evidence and original Stop time intact, and changes only owned native
incarnations. Retry keeps those new incarnations. An owned native editor check
delivers an event on the repaired provider and confirms cleanup retry preserves
it instead of discarding or attributing it to the old take. Device-receipt tests
exercise a successful first lease release, a wrong later token, successful
retry and unknown identity refusal. Reopened mixed-vendor PCM is compared with
the accepted baseline under the existing allocation/free/lock audit.

These are **source assertions, not passing evidence**. Run shared Windows
parameter-preview, both Mac `native-plugins` and owned native-editor `--ui`
gates in the next coordinated checkpoint, no earlier than 18:11:23 UTC. The
real-session BlackHole lifecycle test must still run separately; it exercises
the unchanged no-history/save-state contract through the updated session path.
Both prior Mac jobs failed the same private-helper test call at da8c89f07; the
2f89631d3 correction has not been compiled. Windows x64 job119969865402 finished
building at 17:39:38 UTC and is still qualifying; do not restart it.

Remaining work includes live native release, full UI/API/socket contracts and
the session-owned resolution of **new** input arriving during an adopted
repair's failed cleanup. This path preserves/refuses such input; it does not
yet present a new recovery/review choice for it. Do not claim complete recording
parity or expose an unconditional successful Discard UI from these changes.

## Shared take reads and Mac API lifecycle after ac9d672e6 — unbuilt

`editor/hosted/RecordedGestureRead.hpp` owns the summary and bounded event-page
projections used by both native API adapters. Original host times and mapped
48 kHz frames remain decimal strings. Events at/after the playback Stop cut
remain visible with null projected frames; reads never rewrite retained input.
Pagination validates the target and uses subtraction before adding a bounded
page length. `RecordedGestureReadTests.inc` covers times above 2^53, exact Stop
boundaries, empty/end pages, invalid limits/targets and unchanged raw evidence.

`mac/Bridge/ParameterRecordingAPI.inc` exposes get/events/start/capture/stop/
commit/discard with strict fields, original mach timestamps, stable plugin and
parameter identities, take tokens and current revision guards. Get returns the
mach timebase and bounded intake warnings. Stop seals capture without stopping
music. Keep publishes one musical/history transaction; a successful publication
with failed native restoration returns completed/retained plus restorationError.
Retry cannot publish the points twice. Release remains stopped-only and explicit
opaque Discard requires closed owned editors. These limits are advertised in
api.describe and the guide; they are not completed live-release parity.

The AppKit request dispatcher bypasses its preliminary manual drain for these
lifecycle requests. Otherwise a refused intake could prevent Get or Discard from
ever reaching the retained owner. The API handler remains responsible for its
own intake, errors and retry status. Native Record controls still use the legacy
flow; they have not been connected to this API or advertised as qualified.

Required checks: shared recorded-automation on Windows and both Mac architectures;
native session strict/stale/empty admission; actual private socket admission and
schema inventory; explicit BlackHole real-session two-target capture, exact event
timestamps/frames, live Keep with deferred release, stopped retry, one Undo/Redo
and native save/reopen. The latter is in `ParameterTakeSessionTests.inc` and only
runs through the explicit virtual-device gate, not ordinary native-editor `--ui`.
Socket live success, native controls, live release and new-provider cleanup input
review remain outstanding. No API presence count closes those requirements.

Cheap source checks parsed both changed Python fixtures and the JSON schema,
confirmed all seven methods agree with the dispatcher, and passed git diff
whitespace validation. The local Python runtime lacks jsonschema, so full schema
validation was not claimed. None of the new C++/Objective-C++ assertions has run.

Read-only CI inspection at 17:57 UTC found Windows job119969865402 terminal
failure after a successful build: its recorded audio catalogue has zero devices,
and 12 unique live playback tests account for 15 failure entries (three teardown
failures repeat failed launches). Preserve these failures; no skips or weaker
assertions were added. Both Mac jobs failed the same already-corrected private
helper call. No CI rerun was dispatched. Next coordinated build remains no
earlier than 18:11:23 UTC, subject to rechecking actual build starts.

### Completion and client retry correction — source only

The Mac API now refuses Discard of an already-kept take. Commit retries finish
restoration; musical reversal uses Undo. A failed restoration recomputes the
summary after cleanup so `discardCleanupPending` describes the current owner.
The real-session fixture checks that refusing Discard does not change playback,
revision or the kept take.

The AppKit lifecycle audit also found that graph, graph-envelope, scratch and
pattern-envelope request helpers treated every -32002 as pre-dispatch busy.
`NativeRequestOutcome.swift` now requires the existing explicit
`data.writeOutcome:notCommitted` receipt before retrying a write. Captured
revision/target/payload stay unchanged; unknown, committed, noChange and missing
outcomes report their original error and retain the draft. Pattern automation's
previously unbounded loop is limited to five retries. Read-only parameter and
port polling retain their existing bounded behavior. Other native clients still
need a separate admission audit before a blanket retry-safety claim.

Native interface regressions cover accepted busy receipts, unchanged captured
payload, bounded exhaustion, unknown graph/curve/scratch results and uncertain
envelope deletion. The detector-routing refusal fixture now requires exactly
one call, not six repeated attempts at a native precondition failure. It still
requires unchanged cables and a persistent, readable reason. The interface
compile command explicitly includes the new helper. These checks are unbuilt;
run the signal-graph, scratch, and automation-tools groups with the next normal
Mac interface checkpoint. No standalone compiler invocation was made early.

### Following frozen 019803275 — sample-loop retry source only

The next source batch applies the same explicit pre-dispatch receipt to Mac
sample-loop writes and bounds each automatic chain at five retries. Unknown
outcomes cancel queued automatic loop saves, preserve raw fields and captured
revision, and show the original error. New interface cases require no replay
after an unknown outcome and bounded exhaustion with an editable draft. This is
not part of frozen 019803275 or its upcoming build. Mixer request/reconciliation
still needs its own retained-draft audit; a plain code check is not sufficient.

### 18:12 UTC checkpoint failure and conformance correction

Frozen 019803275 was pushed to PR#7 alongside the local ARM64 build, which
started 18:11:42 UTC. Both Mac jobs stopped **before compiling**: the API
inventory preflight correctly found the seven newly shared schema methods
missing from `doc/api/platform-differences.json`. This was an omitted expected
inventory update, not an unavailable Mac compiler or passing native result.
Keep failed run39829213878/job120000567927 (ARM) and job120000567435 (Intel).
Do not rerun the build or alter the frozen checkout to hide this failure.

The following source correction reviews exactly the seven additions, updates
Mac expected reads/writes, recomputes each platform's schema delta and the LF
schema hash, and preserves earlier binary receipts. The Mac review is explicitly
source-only. Two existing take read vectors now require the implemented success/
strict-unknown-field outcomes. Six additional vectors cover empty arming and
missing-owner events/capture/Stop/Keep/Discard without changing music. Matching
read/admission expectations are historical regressions, not an open method-name
gap; the contract remains `implemented-pending-qualification-live-release-open`.
Live release/UI/input cleanup and full lifecycle/socket/device checks remain.

All six portable inventory tests passed on the corrected source in 3.144 s,
including refusal of unreviewed methods before any document access. Runtime
vectors and both native Mac builds still need the next hourly checkpoint.
This correction is subsequent to 019803275 and is not included in its binary.

Local 019803275 compiled and linked `ScreamSeq.exe` and its scanner, then failed
building `workspace-restore-tests` at NavigationApplicationTests.inc:121.
The new local name `small` collided with a Windows RPC header macro expanding
to `char`. The following source correction names it `overviewSnapshot` and
preserves every selection/no-edit/readable-focus assertion. No second compile
was run. Build receipt remains exit1 with no qualifying executable hash.
Separate diagnostic app/foreground receipts may inspect the completed product
link, but cannot substitute for the failed native test gate or permit packaging.
All three CI jobs stopped on the API inventory preflight; their build steps are
skipped. Conservative next checkpoint is **19:12:11 UTC / 12:12:11 Pacific**
from the recorded CI timeline; no early rerun or PR-head push is permitted.

### 18:25 UTC bounded diagnosis and recording action correction

The completed product link (SHA256
`86fedb64c50422a29aac6de732a86b79abd3884ebf48997f2fb87577068bd816`)
was used **only for diagnosis**. `take-api/diagnostic-app.json` preserves the
failed build and exit1. Eight of ten selected app cases passed: both dense graph
cases, all four scratch cases, detached recovery review, and retained editor
keyboard/focus. Two failures remain recorded: immediate recording-window
visibility after its opening command, and native Stop leaving a one-event take
capturing. No broad suite or second build was run.

Foreground Computer Use on private silent F04 copy/PID36764 confirmed the
Windows menu entry, one-click opening, empty-take Record/Stop/Keep with playback
continuing, default readable graph nodes, complete compact Group/Return labels,
and readable/overview menu framing. The process exited normally with original
fixture and executable hashes unchanged. Detailed limits are recorded in
`take-api/foreground-diagnostic/observations.json`: this did not establish a
captured-parameter lifecycle or field typing continuity. Ctrl+A after the gain
click selected Pattern; the helper exposes Main rather than child edit focus,
so neither a field-focus pass nor an application defect is inferred from it.
Screenshots cannot qualify temporal flicker. The 354.84-second silent run at
48 kHz/480 frames reported 22 deadline overruns, max callback 12.289 ms, no
processor/device faults, and `realtimeAuditPassed:false`; not a performance pass.

Current-source corrections (unbuilt):

- The discoverability fixture waits for the actual view-opening queue and
  visible window, without resending the command or relaxing the result.
- `ParameterRecordingWindow.hpp` previously dropped every action while its
  polling read pumped messages. It now retains one lifecycle intent during
  that read, including original document/revision/take, scope and explicit loss
  acknowledgement. It submits once after a successful refresh. A changed take,
  capture state, revision or loss evidence refuses that intent; failed reads
  never authorize it. Existing completion receipts still block uncertain
  resubmission. No audio callback, musical model, or persistence format changes.
- `Workspace/ParameterRecordingWindowTests.inc` deterministically injects Stop
  during a read, duplicate clicks, replacement take, changed revision, failed
  refresh and new loss before Keep. It requires exact write counts, unchanged
  targets and visible refusal. These native regressions and the failed actual
  app lifecycle must run on the next hourly build; foreground empty-take success
  does not replace them.

No package or merge from this candidate. Preserve the 19:12:11 UTC gate. Required
Mac native/interface/socket/device and reciprocal project gates remain open.

### Subsequent source batch — retained Mac mixer outcomes

`mac/App/MixerEditor.swift` completes the previously noted mixer admission
audit. A busy code alone no longer permits write replay: retries require the
shared Swift `NativeRequestOutcome` helper's explicit `notCommitted` receipt,
preserve the original payload/revision and stop at the configured bound. The
old display-tick path restarting exhausted chains has been removed. Reads keep
their bounded retry behavior. A failed/uncertain write preserves raw controls,
stable bus, queued newer values and revision, blocks dependent writes, and
shows a contextual **Reload saved state** action. Normal successful gestures
still apply immediately with one durable commit.

Explicit Reload now reads the accepted mixer before cancelling preview values.
It never sends pre-request saved values after a commit that might have succeeded.
Failed reads/cancellation retain the draft and cleanup keys; a later explicit
Reload reads again. Fields changed while readback is in flight survive its late
completion. This is native AppKit presentation/reconciliation only; no model,
DSP, metadata, or public protocol change. Existing Windows receipt guards remain.

`EditorDraftInterfaceTests.swift` covers exact bounded retry payloads, no
display-tick retry restart, absent/unknown/committed/noChange error receipts,
fresh-baseline cancellation, failed readback/release and newer raw fields.
`MixerStripsInterfaceTests.swift` requires an uncertain successful width commit
to remain visible and survive reconciliation. Earlier assertions expecting
blind resends or stale baseline rollback have been replaced with stricter
single-commit/retained-state requirements. Existing normal coalescing, prior
commit preservation, typed values, stable bus and bounded strip allocation
assertions remain. **Unbuilt**: include editor draft and mixer strip interface
groups on both Mac architectures at the next coordinated checkpoint.

### Following source batch — retained-take autosave admission

The next candidate is frozen at `69fe8bf206049c163742ad10105a1de87afde24b`
in the clean qualification checkout. `bin/mac-gesture-20261022/take-review/`
contains prepared, source/time-pinned build/check/package scripts; none has run.
Do not include later source implicitly or start before 19:12:11 UTC.

Source inspection found two application-level gaps above the already implemented
session recovery serializer: autosave considered only MIDI ownership, so plugin
input could be skipped at an unchanged musical revision; strict preliminary
plugin polling could prevent recovery after a provider refused further intake.
`TrackerSession.parameterRecordingTakeID` now exposes retained ownership through
Stop and kept-but-unrestored completion. `RecoverySnapshotPolicy` schedules each
retained snapshot and one after release. `RecoveryIntegration` lets the existing
best-effort session serializer collect recovery input, while ordinary manual
plugin edits retain their strict preflight. Recovery API status/list/save remain
available during retained-input failure. Restore and native close/replacement
reject a retained plugin owner before replacing the song.

No audio callback, musical transaction, file format or public method change.
The existing `hasRecording` metadata still means resumable MIDI; plugin input
remains detached `automation.recovery.*` evidence after reopening. Tests added
to `RecoveryTests.swift` exercise unchanged-revision input and final-release
scheduling. The explicit-device `ParameterTakeSessionTests.inc` now checks owner
identity, consecutive recovery evidence at the same musical revision, detached
save/reopen with preserved plugin baselines, and kept-but-unrestored lifetime.
**Unbuilt and unexecuted**: these changes are after frozen69fe8 and belong to the
following coordinated checkpoint. Required checks: both Mac builds, recovery
store tests, actual-app recovery regression, native parameter take admission,
and the explicit BlackHole real-session lifecycle. A real-app timed recovery
check with captured plugin input remains necessary; the pure scheduling and
session tests do not establish the full app timer/socket path.

### Following source batch — native Mac take review

`mac/App/PluginRecording.swift` adds an AppKit review component for the existing
shared lifecycle API. `PluginRecordingIntegration.swift` retains one window;
Window → Plugin Automation Take, the plugin action menu, and the command palette
reach it without navigating away from Pattern. Close/replacement protection
raises this review. Closing only the review window leaves input retained and
does not implicitly Keep, Discard or stop transport. The window polls only while
visible; the existing session intake and recovery remain independent of it.

Record captures stable selected/all-rack identities and a revision. Stop ends
the take; Keep uses one shared history transaction; committed-but-unrestored
results expose **Finish restoration** and disable Discard. A lifecycle action
clicked during polling retains one intent. The completed read must match its
original document, take, revision, capture/completion state and acknowledged
loss before submission. Read failure cancels the unsubmitted intent. Writes
are never replayed automatically; errors retain the review and require an
explicit readback before another action. Malformed readback cannot re-enable
Record. Incompatible context cannot be bypassed by acknowledging loss. Provider
loss is shown even when no parameter event survived. Unchanged reads preserve
target selection and native table focus; ordinary polling keeps action controls
enabled rather than blinking them off.

The `plugin-recording` interface group covers those ordering/failure cases,
scope retention, duplicates, exact write counts, new loss, completed restoration,
all ambiguous receipt variants, source-only loss, native focus and compact
layout. It is registered in the full isolated interface runner and compile list.
The actual-app workspace fixture requires menu/command-palette discoverability.
**Source only; after frozen69fe8, not built or run.** Required later gates include
both Mac architectures, the new interface group and the complete interface run,
actual-app workspace, real native AU/VST3 input and Keep/restoration/reopen, and
visible keyboard/resize checks on Mac. Counts and offscreen interface tests do
not qualify real vendor input, presentation or accessibility.

This window currently describes recording from native plugin windows. Generic
Mac rack controls still use their older immediate/manual delivery path and
cannot safely join this retained take until original timestamps, stable targets,
pending/refused/uncertain input recovery and worker completion are wired through.
That is the next required integration step; do not package or call this a
complete Mac recording workflow yet. Windows's equivalent retained native input
delivery already exists in `App/ParameterControlDelivery.hpp`. Live Mac release
without stopped transport, native device evidence and reciprocal project gates
also remain open.

### Following source batch — original native-control delivery journal

`mac/App/PluginControlDelivery.swift` prepares the missing main-owner handoff.
Each input retains stable plugin/parameter, native value and original UInt64
host timestamp under a fixed document/base-revision/take owner. It holds up to
1024 entries, counts overflow without replacing older inputs, submits one event
at a time when the caller makes the session available, and never retries a
refused/unknown event. Completion must match the original callback, revision,
take and a strict Boolean accepted result. Duplicate or late callbacks cannot
count twice or retire a different event. Accepted entries leave the pending
queue; loss, refused/unknown original inputs and preview warnings remain owned
until explicit completion/discard and a guarded release. Recovery uses the
shared `ControlInputRecovery.hpp` version-1 detached format with exact decimal
times/counters and bounded UTF-8 diagnostics. This is native request scheduling,
not a second musical recording implementation; capture, timing, validation and
commit still use the existing shared engine/API.

The session's new internal `recoveryDataWithNativeInput:error:` validates that
archive through the shared decoder, matches the native document ID, take token
and original take revision, and overlays it on the existing typed recovery copy.
It does not mutate imported preservation state or replay the evidence. The
virtual-device session case now verifies all three owner guards, unchanged music
and event count, and detached archive/plugin-state preservation across reopening.
The new `plugin-control-delivery` interface group covers waiting and serial
submission, timestamps beyond 2^53, exact payloads, duplicate/late completion,
receipt variants, numeric-Boolean rejection, bounded overflow, owner isolation,
plist serialization and UTF-8 diagnostic bounds. **Not built or run.**

`PluginEditor.onOriginalParameter` now offers each snapped slider/text value,
stable identity and `mach_absolute_time()` to an optional retained-input owner
before the existing busy/coalescing path. Handling it prevents a duplicate legacy
manual write. The new interface case exercises three distinct busy-time inputs,
exact timestamps beyond 2^53, stale-row rejection and ordinary manual fallback.
This hook is not connected to AppController yet.

Still to wire before qualification/package: use a main-owned cached
take context rather than reading session internals while its worker is busy;
submit through the existing serialized application API; wait for pending native
input before Stop/Keep/Discard; bind loss acknowledgement to the delivery review;
include an immutable delivery archive in `RecoveryIntegration` snapshots; release
only after the matching session take is resolved. The existing `onGesture`
manual-history callback must yield to retained take ownership rather than
touching worker-owned session flags during delivery. The journal's document must
be `_automationDocumentID` (the first component of the API revision), not the
separate AppController recovery-file grouping UUID. Existing generic controls
must keep their ordinary non-recording behavior. Frozen69fe8 and its hourly gate
are unchanged; this later source belongs to a following coordinated checkpoint.

### 19:12 checkpoint and unlocked foreground diagnosis, 22 October

Frozen source `69fe8bf206049c163742ad10105a1de87afde24b` was built exactly once
at 19:12:27 UTC, completing 19:15:20. Executable SHA256:
`91fbd8d47637cd373c65a0394b4e4a60f76c8626816833ec62ef2e66e44fe2bb`.
The original `take-review/build.json` and `checks.json` are retained. Native
registrations passed 12/13: departure, scratch editor, parameter-preview workspace,
context/navigation, automation dock, graph workflow, graph curve, short instrument
dock, plugin notifications, parameter preview, recorded automation and hosted
project. `workspace-mixer-hosted-tests` failed **Stale assignment was replayed or
redirected**. The bounded check script correctly stopped before app checks.

An explicitly separate `diagnostic-app.ps1` then exercised the already-built
product: 29 cases, **28 passed / 1 skipped** (optional installed Surge XT).
The full routing, scratch-editor and plugin-recording classes plus retained graph
navigation and global field-focus cases passed. This includes actual parameter
capture, Stop/Keep/Discard, exactly-one Undo, save/reopen and detached input review.
These results do not overwrite or qualify the failed native receipt. Diagnostic
source and executable fingerprints remained unchanged.

Source diagnosis of the failed assignment fixture found a timing ambiguity: the
clean chooser's normal 250 ms refresh can adopt the new revision while the
worker pumps messages, before the test edits the supposedly old presentation.
`InstrumentPluginApplicationTests.inc` now checks that clean refresh explicitly,
then stops/drains only the chooser's own refresh timer and proves the old revision
is still presented before editing. Original refusal/no-redirection assertions
remain, with a full failure snapshot. This is a **test-premise correction**, not
proof that the failure is resolved; the corrected fixture is unbuilt.

After the user unlocked the desktop, a new persistent diagnostic launcher started
owned PID27232 with the exact frozen binary, copied F04, isolated recovery directory,
silent output and 600-second duration. Computer Use observations:

- Windows menu clearly exposes Plugin automation take. Record, close review,
  click the actual Gainer Enabled control, reopen review: one captured input,
  parameter Enabled, value 0.0, song time 56.8616 seconds, no reported input loss.
  Stop retained it and playback continued; Keep released the take and enabled Undo.
- Rack search accepted `Gain` without losing text; Backspace produced `Gai`.
  Tab visibly focused the parameter selector and Shift+Tab returned the caret
  to search. The helper's accessibility focus string remained the root window,
  so this evidence uses actual text/caret/focus-ring changes, not that string.
- Docked graph started at readable scale. Whole overview and Home restored
  readable scale; explicit Float/Dock and maximize/restore retained the graph.
  One tiny-body overview click did not yield sufficiently clear selection
  evidence; retain native/app hit-target assertions, do not count that click
  as a foreground selection pass. Compact Group/Return labels were readable.
- Ctrl+Alt+P returned to Pattern, Down moved the edit cursor from row 0 to 1
  and detached following. The scratch phrase editor opened from Windows.
  Dragging its middle motion point left the curve rendered after release.
  Read-only `workspace.get` confirmed position 36864, value
  0.7172503242542153, `dirty:false`, `pending:false`, `saveQueued:false`,
  and 1024 preview/rendered samples for both lanes. Snapshot is retained under
  `take-review/foreground-partial-diagnostic/workspace-after-scratch-drag.json`.

The process exited 0 normally; original F04 and executable hashes are unchanged.
Its silent run reports 600.042 seconds, 48 kHz/480 frames, 59989 callbacks,
35 deadline overruns, maximum callback 12269.2 microseconds, no starvation/device
errors or processor faults. `realtimeAuditPassed:false` and
`sustainedPresentationQualified:false` remain explicit failures/limits. This was
loaded interactive diagnosis while CI was running, not an idle benchmark.
Screenshots do not qualify temporal flicker, and floating-field typing continuity
was not established by this helper. No package or main merge was made.

Fresh diagnosis also found two presentation defects, now corrected in **unbuilt
source**:

1. The narrow Pattern header positioned three fixed buttons relative to a grid
   narrower than their combined width; Pattern FX used the wider host including
   graph lanes. This let controls overlap one another and the context sidebar.
   `WorkspaceView.inc` now owns every header placement. At compact width a native
   More menu retains mute, FX count, FX panel, graph-command and scratch actions;
   direct controls return when space permits. `WorkspaceDraw.inc` uses the same
   reserved title width. `WorkspaceNavigation.inc` captures document/revision/
   selection and rejects a menu whose target changed. Musical commands continue
   through existing shared edits/history; no project format changes. A newly
   hidden focused action yields to the same header's visible replacement.
   The context-panel native group adds control containment/non-overlap, target
   refusal, one-step FX-count Undo, cursor/selection and resize-focus checks.
2. Main retained “Recording original parameter input” after Stop/Keep even though
   the review was correct. `finishDocumentOperation` now advances only those
   recording-specific status messages on confirmed lifecycle results, preserving
   unrelated errors/status. Existing actual-app recording checks assert the stale
   recording prompt is gone after Stop and Keep.

CI run IDs remain Windows39838064564 and Mac39838064576. At 19:26 UTC all three
native jobs had passed portable preflight and were still building. Latest actual
build start was Intel 19:13:13 UTC: **next build no earlier than 20:13:13 UTC**.
Inspect existing run/receipt state before any follow-up; do not restart them.
The new source needs the next bounded context-panel/mixer-hosted native groups,
affected actual-app routing/recording checks and fresh compact-header Computer
Use. Mac autosave/window/input-journal source after frozen69fe8 still needs its
own compile/interface/session gates and the remaining AppController wiring above.

### 22 October 19:52 UTC — native control integration, unbuilt source

The next source batch now wires `PluginRecordingInput.swift` into AppController.
The adapter owns native lifecycle admission and the existing bounded
`PluginControlDelivery` journal on the main thread; musical capture still uses
the shared `automation.recording.capture` operation on the serial session worker.
Generic rack controls retain each original stable plugin/parameter/value/host
timestamp while busy. Stop waits for earlier input completions and prevents
new controls from extending the queue. Refused/uncertain completions are retained
without replay. Keep captures the exact native-loss census and refuses a changed
census before dispatch. A release must identify the same take/document before
retiring evidence. Known validation/conflict/pre-dispatch refusals preserve
ownership; unknown lifecycle completion requires explicit readback.

`PluginEditor` restores a refused row's prior value. Manual gestures outside a
take synchronize their begin/end state only when the worker and inspector reads
are idle. Start refuses an active/deferred manual gesture instead of letting old
manual input enter a new take. AppController processes pending original input
at worker/read completion and on its normal tick. Collected take-input counts
do not mark the song dirty; Keep's actual revision change still does.

The native review displays accepted/waiting/in-flight/refused/uncertain/overflow
counts and provides **Save recovery copy**. Native loss participates in the
existing explicit acknowledgement and captured intent. Recovery captures an
immutable journal on main, then uses the existing guarded native-input recovery
bridge on the worker. Save, close, replace and recovery-restore paths protect an
unresolved journal even when a malformed/uncertain reply leaves its shared take
unverified. No new musical format or live replay of recovered input is introduced.

Added coordinator cases join `plugin-control-delivery`: FIFO original timestamps,
Stop behind input, stopped-control refusal, matching release, unknown outcome,
no-take/wrong-take readback, new loss during Keep, and shared validation ownership.
`plugin-recording` gains native-loss acknowledgement/recovery/compact-layout
checks; the actual-app workspace socket test also invokes the existing strict
recording admission contract (the windowless host alone cannot cover this).
`mac/test-interface.sh` explicitly includes the new source and tests.

Existing checkpoint CI was inspected, not restarted:

- Windows run39838064564/job120031512710 compiled successfully. The merged
  private-desktop suite ran93 tests, with15 failures and2 skips. Its audio probe
  reports `devices: []`; failures include fixture startup/playback-clock admission,
  recording, loop/shortcut/audition checks and failed cleanup of exited fixtures.
  Preserve this failure; no-device results are not native device qualification.
- Mac run39838064576 ARM job120031513056 compiled successfully, then its graph
  interface group failed “Port reads retry busy rejection…”. Source inspection
  found `requestGraph` applying the write-only explicit-receipt rule to catalogue
  reads. The correction permits bounded busy retries for an explicit read-only
  method list, while writes/preset saves retain the strict no-uncertain-replay rule.
  The existing assertion is preserved; its offscreen wait now uses the established
  short-turn run-loop pump with the same150ms deadline.
- Intel job120031513179 was still building at19:44 UTC. Latest actual build start
  remains19:13:13 UTC; **no next build before20:13:13 UTC**. Inspect its final state
  before starting a coordinated checkpoint.

Only source review, `git diff --check` and Python syntax inspection are appropriate
before that gate. This new Mac source is **not compiled or qualified** yet.
Next checkpoint must include the existing13 Windows registrations (especially
context-panel and mixer-hosted), affected actual-app classes, fresh compact-header
Computer Use, Mac plugin-recording/plugin-control-delivery/signal-graph interface
groups and parameter-take/recovery session gates. Actual Mac generic-control
worker contention, manual Undo grouping, recovery timer, native playback and
reciprocal typed save/reopen remain required before a parity-completion claim.
No production package or main merge follows from this source checkpoint.

### 22 October 20:51 UTC — composed-song recording and original-input review

The coordinated checkpoint froze **337fcfdac7b6056684e9b5fa7280d83d70a481e8**.
Local ARM64 Release compiled once at20:31:08–20:33:11 UTC, SHA256
`341c7ecfe05a709f7b925e75cedb099636b2ccf041f8bb1bb606f7cc84f3657b`.
Receipts are in `bin/mac-gesture-20261022/native-control-integration/`.
The13 native registrations passed12 and failed `workspace-context-panel-tests`:
the new Pattern-header containment assertion failed at200% DPI. Do not overwrite
`checks.json` or treat the separate diagnostic runs as a qualification pass.
The preceding stale instrument-assignment regression now passed.

`diagnostic-app.json` records28 actual-app passes plus one optional installed
Surge XT skip, with frozen source/binary unchanged. The required CI runs are
Mac39849557952 (Intel120071747522, ARM120071747873) and
Windows39849557976/job120071747426. At20:45 UTC all three were still building.
Latest actual build start20:31:35 UTC makes **21:31:35 UTC /14:31:35 Pacific**
the next allowed coordinated checkpoint; inspect current jobs before proceeding.
PR#7 is at337fcfda; the backup branch may advance without another CI checkpoint.

The new `test_composed_fixture_records_without_losing_cross_domain_edits` first
runs the exact shared F04 six-domain composing contract: note, sample loop,
instrument pan envelope, plugin value, graph edge and mixer send; dry runs,
stale/no-op behavior, full chronological Undo/Redo, branch fork and typed
save/reopen comparison. It then opens that edited project in an owned silent
WASAPI process and exercises native parameter recording, Keep, one-step Undo,
Redo, a second-take Discard and reopening. Its final typed comparison permits
only the expected recorded-automation change; plugin state and all other music
remain exact. Outputs persist beside the receipts for reciprocal exchange.

Two initial runs (`composing-journey` and `composing-diagnosis`) failed because
the reused blank-song test incorrectly expected Gain capture to succeed: F04
has an enabled Gain envelope. The diagnostic preserved the real refused event,
original value/timestamp and shared reason “Disable this parameter's envelopes
before recording gestures”. This is the intended shared conflict guard, not
permission to disable existing music. The corrected journey explicitly asserts
that refusal and unchanged song/plugin/automation state, stops/discards that
take, then records the unoccupied Balance parameter. `composing-conflict.json`
passed against frozen337fcfda in23.9s; all typed unrelated-data comparisons passed.
The current source additionally requires the new editor-level refusal message,
so that new assertion awaits the next executable. No assertion on conflict or
data preservation has been relaxed.

Fresh Computer Use on owned PID41072 observed Windows→Plugin automation take,
Record during silent F04 playback, opening Gainer Params, a gain-slider drag,
the same envelope refusal with exact retained input in the review, and Stop
preserving playback. This exposed misleading editor text: the parameter window
still said Recording after asynchronous refusal. Search/Tab observations reported
root focus, so this session does **not** qualify keyboard continuity. The process
exited normally after600s; original fixture and binary hashes are unchanged.
`foreground-diagnostic.json` is explicitly failed/diagnostic. The audio receipt
reported43 deadline overruns at48kHz/480 frames, no starvation/device errors and
no processor fault. Concurrent diagnostic work and screenshots do not qualify
realtime performance, temporal flicker or sustained presentation.

Source following337fcfda, **unbuilt**:

- Windows `PluginParametersWindow.hpp`, `ParameterControlDelivery.hpp`,
  `MixerEditor.inc`, `Main.cpp`: asynchronous refused/uncertain input is surfaced
  in the retained parameter editor without rewriting text/caret/focus. A native
  **Recording…** footer action opens the existing take owner directly. Queued
  input is labelled queued, not accepted. Original values are never replayed.
  Native cases cover foreign-document refusal, focus/caret retention and opening
  review without another input. The actual composed-song case checks the real
  worker refusal reaches the editor.
- Windows `WorkspaceView.inc`: explicitly sizes the owner-drawn FX combo's
  closed selection to16DIP while preserving22DIP popup rows, to fit the23DIP
  header controls. `NavigationApplicationTests.inc` prints the offending control
  rectangle before the unchanged containment assertion. The exact failed
  rectangle was not in the old receipt; this diagnosis remains unconfirmed
  until rebuilt, not a claimed fix.
- Mac `PluginControlDelivery.swift` exposes bounded, read-only original-input
  pages with stable UUID rows and decimal UInt64 host times. `PluginRecording.swift`
  adds native review-mode and paging controls, selectable complete details,
  unchanged-selection polling and the existing captured-parameter summary.
  It is wired through `PluginRecordingIntegration.swift`. No new capture API,
  replay path, project format or audio-callback work is introduced. Tests cover
  exact timestamps beyond2^53, bounded pages, unchanged recovery evidence,
  stable selection as accepted rows retire, last-page navigation and compact
  action containment. These Swift changes are not compiled yet.

Next bounded checkpoint: retain the13 native registrations, add the composed-song
actual-app case to the28-case selection, and run Mac plugin-control-delivery,
plugin-recording and existing session/recovery gates in the coordinated CI build.
Fresh Windows Computer Use must check the rebuilt header at200% DPI and compact
width, direct Recording action, visible refusal, Balance capture and keyboard
focus. Preserve prior receipts; no package/merge until required checks pass.
Mac live Keep/Discard restoration still requires stopped transport and opaque
Discard requires closed editors: safely removing that restriction needs a
separate host-state/preview-restoration batch, not removal of its guard. Native
vendor/device checks, Mac real-control recording, reciprocal edited-project
exchange and the complete A01–A18 composing journeys remain open.

## 22 October 21:35 checkpoint: composing passes; sustained recording fails

Frozen `9195204aaceb59e38ad22007b1bd231d7da93c86` was pushed to PR#7 and
built once in the clean qualification checkout. Local ARM64 Release started
21:35:59 UTC and completed21:38:02. Executable SHA256:
`695e820a0f003bc85c08fb6272cd4c30bb1eebedf403a1982d5bc506b51fc0ba`.
`bin/mac-gesture-20261022/recording-journey/build.json` and `checks.json`
are successful, immutable evidence for that candidate. All13 selected native
groups passed, including the previously failing200% DPI header containment.
The actual-app selection passed29 cases with one optional installed-Surge-XT
skip. F04 composing and recording passed the occupied-Gain refusal, Balance
capture, Keep/Undo/Redo/Discard, exact unrelated typed-tree preservation and
save/reopen. Do not repeat those recorded runs.

That automated result **does not qualify this candidate for packaging**.
Fresh Computer Use on isolated silent PID36976 observed the corrected closed
FX header, Params → Recording, and a visible Gain-envelope refusal beside the
parameter controls with retained original delivery evidence. No existing Gain
automation was disabled. During longer playback the absolute recording timeline
became terminally invalid with `host-start-regression`, while the raw device
correlation remained valid and playback continued. Stop could not seal that
take, and the review misleadingly still said Recording. Balance Keep was not
qualified in this foreground session.

`foreground/gain-refusal.json` preserves the original readback: generation1,
48kHz, contiguous musical frame4465440, new host start3065724205794 versus
previous3065724275107, and new end3065724325794 versus previous3065724375107.
The projection moved backwards6.93ms at its start, with no raw device
discontinuity reported. This establishes the adapter/shared-clock mismatch; it
does not establish why the device correlation varied or prove physical timing.
Microsoft's [IAudioClock::GetPosition contract](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclock-getposition)
distinguishes precise/delayed results and defines the device/QPC correlation.
The native adapter must not publish a backwards estimate as valid musical time.

Main-dock search accepted `Balance`, Backspace produced `Balanc`, and visible
Tab/Shift+Tab focus moved between search and parameter selection with text
retained. Computer Use's accessibility report still named the root window;
there was no fresh exact native/global focus observer, so these observations
are not complete focus qualification. Compact More/resize and floating typing
continuity remain open on this executable. PID36976 exited normally after600s,
with source fixture and executable hashes unchanged. Its audio report contains
39 deadline overruns,48kHz/480-frame period, no starvation/device errors or
processor fault. Performance and temporal flicker remain unqualified.
`foreground-check.json` records **failed**, so the prepared package script
refuses it. No production package or main merge was performed.

CI was dispatched only with the same checkpoint: Mac39858573202
(Intel120103039731, ARM120103040135) and Windows39858573190
(x64120103039342). At21:54 UTC all three were building. Actual latest build
start21:36:29 UTC makes **22:36:29 UTC the earliest next checkpoint**, subject
to later starts and final job inspection. The preceding337fcfda ARM job passed;
Intel compiled but timed out one cabinet test and the run later canceled;
Windows compiled but failed its required live suite with no enumerated WASAPI
device. Those failures/skips are not replaced by local success.

### Follow-on source batch, unbuilt

- `windows/Session/HostedProject.{hpp,cpp}` admits monotonic native presentation
  intervals to the row and absolute clocks. A backwards projected interval is
  rendered normally but contributes no timing coverage. Subsequent valid
  intervals retain their actual frame origins, including frames rendered in
  the gap. No clamping, retiming, interpolation across holes or relaxation of
  `editor/RecordingFrameClock.hpp` is introduced. Proven device discontinuities
  remain terminal for the retained absolute timeline. This is a proposed
  correction pending executable validation, not a claim that sustained capture
  is fixed or that device timing is accurate.
- `PresentationClock.hpp` and `Main.cpp` add the read-only
  `transport.get.recordingClock.frameTimeline.unmappedCorrelations` diagnostic.
  It counts excluded render slices, not lost input. Existing take loss and
  acknowledgement semantics still apply to input outside valid coverage.
- `HostedProjectTests.cpp` adds deterministic backwards-start/end-only cases,
  exact prior coverage, explicit row/frame gaps, actual resumed frames, unchanged
  PCM,4096-frame slicing, and terminal hardware-discontinuity assertions under
  the existing callback allocation/free audit. No shared-clock assertion changes.
- `test_plugin_recording.py` adds real native input before and after a95-second
  recording hold, continuous context/clock checks, and the existing strict Keep,
  one-step Undo, Redo, Discard and save/reopen journey. The owned process receives
  a180-second bound; ordinary test processes retain their120-second limit.
- `ParameterRecordingWindow.hpp` explains that an incompatible live take needs
  playback stopped before safe discard/recovery. Its native regression retains
  disabled Keep/Discard until stopped; no lifecycle safety guard is removed.
- Mac `PluginControlDelivery.swift`, `PluginEditor.swift`, `main.swift` and
  `PluginControlDeliveryTests.swift` add asynchronous refusal/loss feedback
  beside parameters and a native **Review take…** action. Status changes retain
  original input, values, search text, field editor and selection. Tests cover
  bounded/refused diagnostics, repeated status publication, compact containment,
  direct review and clearing only the released take's notice. Both Mac builds
  and native interface tests are still required.

Next validation batch: one build of the candidate and existing targets plus
`recording-device-tests`;14 native groups (the previous13 plus device-clock
regressions),31 app cases (previous30 plus sustained capture). Reuse completed
919 evidence only for unaffected behavior; new clock/UI inputs require the new
candidate. Fresh isolated Computer Use must include sustained silent playback,
Gain refusal, Balance Record/Stop/Keep, Params review, compact More, resize and
exact native focus observations after typing/Tab/Shift+Tab/Backspace. Preserve
the existing failed foreground receipt. Mac native interface/session gates,
reciprocal edited-project exchange and device/audio qualification remain before
merge or complete parity claims. Keep Mac stopped restoration and closed-editor
opaque Discard guards. Backup-only source pushes between coordinated checkpoints.

## 22 October 23:14 checkpoint and recovery-flow continuation

The prior9195204 CI completed: **both Mac ARM and Intel jobs passed** in
run39858573202. Windows x64 compiled but its required private-desktop/live-audio
stage failed16 cases with two skips. The retained log confirms
`audio.devices.get` returned an empty device list; do not call that suite passed
or remove its assertions. Read-only recent-run inspection found no newer builds
or concurrent checkpoint before proceeding.

Frozen `cc6da0bd314344dd89a692b61734e2fc2fa631c2` was pushed to PR#7 alongside
one local ARM64 build at23:14:45 UTC. It compiled successfully at23:17:36;
SHA256 `9ecbe9994dbc71b9ecf6aada5afeb35f2fa24eb286f45d9e73720d4ab0e94901`.
All14 native groups passed, including strict native correlation gaps, unchanged
PCM, callback allocation/free audit and terminal hardware discontinuity. The
actual-app run was still progressing at this report entry; later receipts,
not this intermediate statement, determine its result. Evidence is under
`bin/mac-gesture-20261022/clock-recovery/`. Never repeat its build/check scripts
when their receipts exist.

Current coordinated CI: Mac39870506422 (ARM120143455474,
Intel120143455255), Windows39870506136 (x64120143454737). Actual latest
start23:15:10 UTC makes **2026-10-23 00:15:10 UTC** the earliest next build,
subject to later starts and current-job inspection. Qualification checkout
remains frozen atcc6da0bd3 while primary source work proceeds independently.

### Recovery usability source following cc6da0bd3, unbuilt

- Mac `PluginRecording.swift` now honors the API's `discardable:true` after
  stopped playback even if a failed recording cut leaves `capturing:true`.
  The prior UI required `!capturing` unconditionally and could trap a take which
  the backend explicitly permitted the musician to discard. Keep remains
  disabled for incompatible context; opaque Discard still requires closed
  editors and preserves every backend safety check.
- The same native view adds a distinct **Stop playback** action for failed
  capture or stopped-take restoration. It sends one captured, revision-guarded
  `transport.stop`, reads the take after confirmed completion, and never commits
  or discards automatically. Queued actions recheck take identity, revision,
  capture/completion state and current eligibility. Uncertain completion
  requires explicit readback without replay. `PluginRecordingInterfaceTests.swift`
  covers these transitions, changed eligibility, duplicates and compact bounds.
- Source inspection found that `AutomationIntegration.swift` performed
  `collectPluginEdits` before `transport.stop/panic`; with a retained failed
  take, that function calls `pollParameterTake` and can throw before Stop ever
  reaches the session. These two explicit transport exits now bypass that
  preliminary drain while a take owns input. Session Stop already catches a
  failed drain and stops audio while preserving the take. Actual Mac failed-clock
  AppController/device qualification remains required; the view harness alone
  does not prove that integration path.
- Windows `ParameterRecordingWindow.hpp` now includes the exact decimal
  original host timestamp in its copyable read-only details for both captured
  events and refused/uncertain deliveries. Previously the backing snapshot
  retained the timestamp but rendered only value, song time and status. Native
  assertions use9007199254740993/994, preserve read-only text and keep existing
  focus/selection checks. No timestamp conversion, replay, schema or history
  change is introduced.
- `mac/AUTOMATION.md` now describes the implemented generic-control intake,
  recovery eligibility and explicit playback Stop. It removes the obsolete
  claim that the retained native UI was unconnected. Live native restoration,
  vendor/device evidence and complete composing qualification remain open.

Next source batch gates: reuse unaffected clock/PCM receipts atcc6da0bd3;
rebuild changed native UI/application paths at the next coordinated checkpoint.
Run Mac plugin-recording/interface, session and actual AppController admission
checks; Windows workspace-departure and recording app cases; then fresh native
recovery/focus UI inspection. Preserve exact failed-clock evidence and do not
substitute UI harness success for real device/context recovery.

### 23:35 UTC: clock candidate qualified for targeted workflows; next batch unbuilt

Compiled source `cc6da0bd314344dd89a692b61734e2fc2fa631c2` passed all 14 selected
native groups and 30 actual-app cases; one optional installed-Surge case skipped.
The 95-second capture test and F04 six-domain composing/recording journey passed.
Receipts are in `bin/mac-gesture-20261022/clock-recovery/`; do not rerun them.

Fresh Computer Use targeted PID 19476 only, with the disposable F04 project and
silent WASAPI output. The process exited normally after 600.038 seconds; the
original fixture and executable hashes were unchanged. At native DPI 192:

- Params opened Recording directly. Balance slider gestures produced four
  retained inputs across a 99.566-second capture interval. Stop retained
  playback; Keep released the take and advanced musical history once. Exact
  one-step Undo/Redo and save/reopen have separate automated app evidence.
- A second Gain gesture was refused because F04 already has its enabled
  envelope. Params displayed the actionable refusal and Recording link. The
  project revision remained unchanged; existing Gain automation was not disabled.
- Dock search typed `Balance`, Backspace produced `Balanc`, Tab reached the
  selector and Shift+Tab returned. Three read-only native/global focus receipts
  passed for the actual search HWND. Computer Use's root-focus report was not
  treated as an application failure or used as the sole focus evidence.
- Compact resize exposed the Pattern More menu, with commands readable after
  its native animation settled. The closed FX selector remained inside the
  header. Maximize/restore retained the search. Floating typing continuity
  remains unverified; targeting Main can reactivate it.

The run had 22 deadline overruns at 48 kHz/480 frames, maximum callback
11754.8 microseconds, zero starvation/device errors and no processor fault.
Realtime performance and temporal flicker are **not qualified**. The foreground
receipt's pass is explicitly limited to functional UI checks. Its packaged
copy retains that scope and these limits. No main merge is justified by it.

Usable optimized ARM64 candidate:
`C:/Users/P14/Downloads/ScreamSeq-Windows-ARM64-plugin-cc6da0bd3/ScreamSeq.exe`.
SHA256 `9ecbe9994dbc71b9ecf6aada5afeb35f2fa24eb286f45d9e73720d4ab0e94901`.
Scanner, ARM64 CRT, OpenMPT/VST3 notices, source manifest and copied hashes are
included. This package excludes the later unbuilt recovery changes above.

The next source batch also adds a mandatory native VST3 composing journey to
`windows/Tests/test_plugin_recording.py`: use the bundled instrument class
`5245534F4E414E43494E535452550001`, preserve F04's sample instrument/keymap and
envelopes, assign MIDI channel 3 with Undo/Redo/no-op checks, route its main
output to a new group, then capture parameter 7 through actual Params controls.
Keep/Undo/Redo, Discard and save/reopen must preserve opaque state, aliases,
routing and every unrelated typed project field. Values are .25/.75/.5 for the
fixture's normalized parameter; existing built-in tests retain their original
negative-value assertions. This does not replace optional vendor tests or claim
layered PCM proof by checking only metadata. Pair it with native-audio-bus's
existing layered stem/ordinary/precise/audition checks and hosted instrument UI
coverage at the next checkpoint. Only syntax/static review has run for this new
test so far.

At 23:34:58, exact-source Mac ARM and Windows x64 compilation had passed;
their remaining jobs and Mac Intel were still running. CI runs are 39870506422
and 39870506136. Do not repeat/restart them. Next build is no earlier than
2026-10-23 00:15:10 UTC and only after current jobs finish and later starts are
checked. Required Mac native recovery/device checks, reciprocal edited-project
preservation and Windows native audio gates remain open before merge/parity
claims. Keep full scope active; no recurring automation is requested.

### Follow-up source: explicit Windows playback recovery and Mac CI diagnosis

The cc6da0bd3 Mac ARM application compiled, but job120143455474 failed the
`plugin-control-delivery` native interface group's compound asynchronous-refusal
assertion. `plugin-recording` passed. The failure did not identify whether
focus, selection, text, musical preservation or compact geometry failed. The
fixture now lays out the initially zero-sized editor and requires a real search
field editor before capturing focus. Separate, equally strict assertions report
each condition and actual geometry. This is an unbuilt fixture correction and
diagnostic improvement, **not proof that the Mac regression is fixed**.

Windows x64 job120143454737 compiled and completed with required live-app
failures: its audio-environment evidence reports an empty WASAPI device list,
and the app suite reports missing playback clocks/owned audio startup failures.
The required vendor ownership step also failed. Preserve those failures;
neither successful compilation nor local ARM64 evidence qualifies x64 hardware.
Mac Intel was still building at23:47UTC. Raw exact-source logs are retained in
`bin/mac-gesture-20261022/clock-recovery/current-job-*.log`.

Windows `ParameterRecordingWindow.hpp` now exposes a separate **Stop playback**
button for stopped-take restoration or failed capture context. It keeps Record,
Stop recording and playback Stop distinct, fits the compact header, and places
the new action immediately after Stop recording in keyboard navigation. A
queued action captures document/revision/take/capture/completion state and
rechecks current transport eligibility. It never automatically Keeps or
Discards. The button stays visible but disabled after playback stops so the
recovery layout remains stable until the take is resolved.

`ParameterRecordingIntegration.inc` routes this action through the native
`SessionHost::stop` owner, since `transport.stop` is not a DocumentController
worker method. It validates the captured revision and argument domain, publishes
a matching native completion receipt before fallible presentation readback,
and reconciles that receipt without stopping any subsequently restarted
playback. The existing ordinary transport API remains unchanged. This is a
native presentation/ownership change, with no new musical operation or storage.

New mandatory checks in the existing workspace plugin-search group cover
compact bounds and Tab order, duplicate Stop during a polling read, changed
transport eligibility, retained failed-clock take identity, dependent-write
blocking after a presentation failure, and explicit readback with retained
focus. The actual application fixture exercises native Stop dispatch, stale/
wrong-domain rejection and post-Stop presentation failure receipts. The full
plugin composing journey now also presses this native action after its second
take stops recording, checking stopped transport, exact retained original
events, unchanged song/history, then explicit Discard and save/reopen. The
mock failure state is not substituted for a real failed-device test.

These changes, the prior Mac recovery batch and mandatory sample-plus-VST3
journey remain unbuilt. Reuse unaffected cc6da0bd3 evidence; the next coordinated
checkpoint must rebuild native UI/application paths, run the prepared bounded
native/app selections and the full Mac interface gates, then inspect the fresh
binary with Computer Use. No build before00:15:10UTC on October23, and no overlap
with the preceding CI. Do not rerun recorded attempts or merge from partial
diagnostic evidence.

### Recording playback oracle added before the same hourly checkpoint

`editor/Tests/Conformance/recording_render.py` extends the mandatory layered
VST3 journey beyond parameter counts and metadata. On the isolated actual-app
socket, it compares saved projects differing only in the committed automation,
renders all channels of pattern0 through `sample.renderSelection`, and reads
the complete canonical 48kHz stereo PCM in bounded pages. At least64
unsaturated sample values must change by more than2 signed16 units. A second
fresh reopen/render of the recorded project must be byte-exact. Clipping is
retained in each render's `clippedValues` report, not silently normalized.

Each render is capped to the F04 evidence window of20seconds; it checks dry-run
purity and uses one Undo followed by an exact saved typed-tree comparison to
prove the appended sample did not damage the source project. Original saved
inputs are hash-checked, PCM files and failures are retained, and the test
restores the recorded song after success. The reusable helper contains no
Windows UI logic, but only the Windows journey currently invokes it. This is
unexecuted acceptance code, not new qualification. It measures pattern-local
recording replay; independent sample/plugin stems, native callback timing,
vendor-private behavior and device performance remain separate gates.

### Separate x64 vendor-gate failure identified from retained artifact

The exact cc6da0bd3 artifact12466454878 was downloaded without rerunning CI.
Its `screamseq-vendor-ownership.log` shows that native vendor notification and
recorded-editor ownership checks passed, then the worker start-refusal assertion
failed. This is **not** the missing-WASAPI failure. The fixture rendered two
identical native timestamps and still expected `host-end-regression`, although
cc6da0bd3 intentionally turns that unstable presentation estimate into an unmapped
interval while preserving earlier valid coverage.

`windows/Tests/ParameterRecordingControllerTests.inc` now requires the repeated
estimate to preserve generation41 with exactly one unmapped interval and no
shared-clock failure. It then injects an explicit hardware discontinuity and
requires a terminal invalidated timeline. Start must still return a classified
NotCommitted refusal, preserve revision and opaque baseline, and retain no take.
The previous compound failure now reports admission diagnostics separately.
No product assertion or genuine clock failure is relaxed. Rebuilt execution is
required; add `document-controller-tests` to the same next build and the owned
`test_recovery.RecoveryControllerTests.test_manual_vendor_editor_overlay_and_late_protection_guard`
case to its actual-app selection.

The CI composing selection also now receives the already-scanned deterministic
provider cache before invoking its mandatory layered journey. Missing cache is
still a failure. The old x64 receipts remain failed and immutable; the changed
test/setup has no passing execution evidence yet.

Source review before execution corrected the new render oracle's identity
expectation: `Document::undo` at `editor/TrackerDocument.cpp` preserves
`max(previous.nextID,current.nextID)`. Each sample-only render must allocate
exactly the original `nextID`; its Undo must leave exactly `nextID+1` and restore
every other typed project field. The oracle now asserts the allocated sample's
actual stable ID and that one predicted allocator change. It does not ignore
allocator values or accept arbitrary differences. Source input files remain
byte-exact. This is a correction to new unexecuted acceptance code, not a relaxed
failed gate or a change to stable-identity behavior.

The sampling guide also now reflects current Windows source: its existing
`New take limit` selector offers60/300seconds and is locked while a take is
retained. No duplicate duration feature was implemented based on the stale
guide's earlier sixty-second-only claim.
