# Plugin gesture recording: recovery prerequisite

Status: source and regression cases drafted on 21 October 2026 after navigation
candidate `86538cb42` passed automated qualification. **Not built or executed.**
The navigation executable does not contain these changes. No Record control,
public API, timing-parity, Mac execution or complete recording-lifecycle claim.
The initial gate was 23:07:42 UTC. The approved navigation merge subsequently
started main CI at 23:53:08 UTC; the next batch gate is now **22 October
00:53:09 UTC**. Build receipts, not this historical timestamp, govern later work.

## Observed gap and scope

`RecordedGestureTake` already retains original-time vendor input, stable rack
identities, canonical mapped frames, a before-points baseline, provider loss and
drain readiness. `PluginOperations::beginRecordedEditors` remains an internal
adapter with no production arming caller. Recovery excluded the owned vendor's
transient state correctly, but `DocumentController::recoverySnapshot` serialized
only MIDI recording and accepted manual state. It had no archive of the retained
plugin take. A future Record control must not introduce that loss path.

This prerequisite archives **input already consumed into the take**. It does
not claim to capture unconsumed native callback queues at crash time. Native
intake barriers, original-time standard-slider input, worker arming, multi-target
preview/restoration, Stop/admission, review/commit/discard API and UI remain part
of the lifecycle described in the navigation report.

## Source changes and ownership

- `editor/hosted/RecordedGestureTake.hpp`: `Review` is a detached data snapshot,
  not a restorable live take. It copies the exact rack order and point baseline,
  original event order/timestamps, cached canonical frames, ranges, per-provider
  original/current loss, drained flags, failure, completion and time bounds.
  It neither consumes input nor seals, publishes, acknowledges or mutates the
  live take. A shortened stopped-clock bound clears invalid frames in the copy
  while retaining their raw events; clock-ring eviction does not clear valid
  previously mapped frames. Existing capture/storage limits remain unchanged.
- `editor/hosted/RecordedGestureRecovery.hpp`: shared JSON/plist projection
  and strict detached review decoder; the Windows header is a compatibility include.
  Decimal strings preserve 64-bit time/counter precision; unmapped frames are
  absent, never zero or plist null. Stable identities and baseline rack order
  allow a future review decoder to resolve slots explicitly. No vendor blobs
  are duplicated into the take archive.
- `RecordedPluginEditors.inc`, `PluginOperations.hpp/.cpp`: copy a live owner's
  snapshot into a separate project's `screamseqPluginGestureRecovery` array.
  Repeated snapshots replace only matching take/revision evidence. Earlier
  archives survive; a 64-archive cap refuses additional captures without
  discarding live input. Existing incompatible archive shapes also refuse.
- `RecoveryOperations.inc`: include that archive in recovery bytes/fingerprints
  and unsaved-work protection. Manual opaque state remains protected by the
  existing exclusion path. Recovery replacement requires explicit discard when
  such input is retained.

The archive is version 1 with `reviewOnly:true`. It is an opaque preservation
extension, not playable song automation and not an API request. The project
container and native musical metadata versions do not change. Existing native
preservation paths retain unknown top-level fields; Mac reciprocal execution is
still required before claiming that compatibility qualified. Module export's
existing unknown-extension rejection protects these archives from silent loss.

There is intentionally no deserialization into live providers. The shared bounded
review decoder validates types/counts/ranges/references and distinguishes
mapped, unmapped and rejected input. It must not reuse host timestamps or
provider incarnations in a new process, silently accept identity failures, or
offer a second commit for `completed:true`. Until review/commit/discard UI and
API are complete, production recording arming must remain unavailable.

## Prepared regression coverage and bounded validation

`editor/Tests/RecordedGestureTakeTests.inc` adds snapshot checks for independent
copies, exact baselines/identity, frames surviving clock-ring eviction, missing
timestamps, provider loss without a target, provider failure, completed no-op
takes and shortened Stop coverage without mutation of the live capture.

`windows/Tests/RecordedEditorOwnershipTests.inc` adds native fixture checks for
recovery after refused preview, raw timestamp/value order, mapped/unmapped frame
preservation, repeat-snapshot idempotence, preservation of an earlier archive,
native plist recovery reopen, and exact accepted automation/manual vendor state.
Existing preview-refusal, 128-event drain, provider ownership, restoration,
history and native-save assertions remain in place.

At the cohesive recording build gate:

1. Windows ARM64 Release: build the app, `document-controller-tests` and
   `recorded-automation-tests` with the real retained VST3 fixture/scanner.
   Run `recorded-automation-tests`, `document-controller-live-recorded-publication`
   and the recovery suite's native manual-editor fixture (`--recovery-manual`,
   through `windows/Tests/test_recovery.py` on its isolated desktop). Include the
   targeted recovery/controller and recording regressions because admission and
   fingerprint protection changed. Do not rerun all navigation gates for this
   header/worker-only prerequisite unless subsequent UI changes overlap them.
2. Mac: build the shared `recorded-automation-tests` target and run CTest
   `recorded-automation`. Reopen/save a recovery archive with Mac's preservation
   adapter, then reopen it on Windows and compare archived input, plugin opaque
   bytes, stable identities and accepted automation. No new Mac audio/UI source
   has been changed by this prerequisite.
3. Once lifecycle wiring exists, actual-app API and native UI checks must cover
   delayed polling, standard-slider and vendor input, Stop, full preview queues,
   provider replacement, recovery/review and atomic multi-target commit, followed
   by Undo/Redo, reciprocal save/reopen and canonical point/PCM comparison.
   Realtime audits remain necessary for later publication/intake changes; this
   snapshot executes on the worker and never in the audio callback.

## Next candidate: shared recovery, atomic restoration and reachable parameters

Source changes prepared after the navigation merge, still unbuilt:

- `editor/Tests/RecordedGestureRecoveryTests.inc` runs the same decoder on both
  platforms. It checks exact uint64 strings, original event order, same-frame
  input, shortened Stop, unknown timestamps, provider-only loss, independence
  from the live take, completed no-op evidence, invalid metadata and aggregate
  bounds. Unknown versions/fields refuse review while leaving the original tree
  untouched. This is not permission to replay recovered timestamps or commit
  a completed take. Both CMake targets include the existing JSON dependency.
- `PluginChain::cancelParameterPreviews(span<ParameterPreviewTarget>)` validates
  and prepares every target before one atomic queue publication. The old
  single-plugin overload delegates to it. Queue refusal cannot restore a prefix
  of a multi-plugin take. Accepted manual values remain authoritative. Tests in
  `ParameterPreviewTests.cpp` cover two-target restoration with one free queue
  cell, invalid later targets, duplicates, unrelated previews, newer durable
  input and actual PCM at three rates and three callback partitions. No new
  allocation or lock is introduced in the audio callback.
- `edited_metadata.py` adds a detached archive to the existing paired F04
  preservation oracle. It must remain byte/type-exact through title/graph edits,
  rack move/remove, Undo/Redo and reopen, including when captured providers no
  longer exist in the current rack. No expected output is normalized or removed.
- Windows command 335 opens the existing retained ten-parameter window directly
  from the Plugins context rail, command palette and Separate windows menu.
  The rack Editor action retains the VST3 vendor editor; built-ins offer Params.
  Deferred openings capture the stable plugin target. Opening controls never
  enables routing, changes musical history or redirects another plugin's draft.
- Rack Tab order now leads from search to parameter to its numeric/choice/toggle
  editor. The ten-parameter window follows visible header/row order and retains
  its last editable focus/caret when raised. Incomplete values remain protected;
  failure text describes correcting unsaved values instead of requiring Apply.
  `PluginSearchApplicationTests.inc` and the actual-app
  `SongRoutingTests.test_rack_parameter_window_without_graph_retains_and_saves`
  cover reachability, retained owners/drafts, focus, automatic save, Undo/Redo
  and reopen. This adds no public musical API; existing parameter APIs are used.

### Evidence before building

Computer Use inspected owned PID 40828 running the unchanged `86538cb42` ARM64
binary with inspection/automation and no hardware audio, around 00:06–00:15 UTC
on 22 October. It confirmed the built-in rack Editor was disabled, the sidebar
Plugin editor action only reported that built-ins use the panel, and Tab from
Gain selection jumped backward to Parameters page. Search typing and arrow-key
selection worked. The process was closed after inspection. These are baseline
findings, not qualification of the new source or temporal flicker evidence.

`bin/plugin-take-lifecycle-20261021/archive-baseline/` passed the new paired
archive preservation oracle against that same retained executable/hash. The
report pins binary source, executable hash, tool hashes, immutable fixture and
all edits/results. Its passing scope is Windows opaque preservation; it does not
qualify the new decoder, new UI or a reciprocal Mac leg.

### Bounded candidate gate

Build once after the gate, from a frozen clean checkout, into a new Release
directory. Include ScreamSeq, workspace-restore-tests, document-controller-tests,
recorded-automation-tests, parameter-preview-tests and plugin-edit-notification-tests
(with native fixture/scanner dependencies). Run shared recorded/preview gates,
native notification gate, controller recorded-publication/recovery/recording/
live-parameter/recipe-preview gates, and workspace plugin-search, context-menu,
menu-bar, parameter-preview and worker-presentation gates. Run the native recovery
manual fixture on the private desktop and the actual-app direct parameter test,
retained navigation and parameter-slider smoke checks. Broaden only for new failures.

Then inspect the exact candidate with Computer Use: direct built-in controls,
Separate windows destination, rack search/parameter/value Tab and Shift+Tab,
typing/Backspace, slider response and retained field focus on reopening. Preserve
the musician's process and audio route. No screenshot-only temporal claim.

The shared changes require Mac compilation plus recorded-automation and
parameter-preview execution before integration. The modified F04 archive must
also pass reciprocal Mac save/reopen; reusing the Windows preservation leg is
valid only while its adapter and fixture inputs remain unchanged. A successful
Windows candidate is not a complete recording-lifecycle or cross-platform pass.

Only static diff/whitespace review and the retained-binary preservation check
have run so far. None of the navigation candidate's 36 passes establishes that
this new recording/UI source compiles or passes.

## Audible editor delivery: subsequent source batch for the same build checkpoint

Current source inspection found a concrete backend difference. Windows retained
two 8192-entry FIFOs: original UI notifications and audible scalar updates. Even
when the document worker consumed every notification, stopped audio accumulated
obsolete audible updates until the second FIFO faulted the provider. Mac already
coalesced audible values independently while retaining ordered notification input.

`editor/hosted/LatestParameterEdits.hpp` now extracts that Mac algorithm into a
shared prepared mailbox. Both VST3 adapters use it. One atomic float per prepared
parameter retains the latest audible value; a separate ordered notification FIFO
still preserves original timestamps. Generation/carry state belongs to the single
render consumer, or a quiescent control-owner flush. Refused native-list delivery
retains the value, unless a newer producer value has arrived. Construction is
bounded to 4096 catalog entries; callback publication/consumption allocates,
frees and locks nothing in the host. These are source properties, still awaiting
the runtime audit, not a performance qualification.

Windows preflights its native parameter-change list before consuming editor input.
A full list defers that editor value until the next block instead of faulting
scheduled automation. Ordinary scheduled-value capacity failures remain strict.
State capture has a 64-turn flush bound and refuses continuing vendor input rather
than waiting indefinitely. Mac's native device/UI integration stays unchanged;
only its existing audible mailbox implementation moves to shared code.

**Retained platform distinction:** Windows still faults when the original-time
notification FIFO itself fills, and rejects invalid nonfinite/out-of-range vendor
values. Those assertions are unchanged. Relaxing raw-notification loss requires
the manual and recording owners to reconcile missing input explicitly; applying
Mac's tolerant policy alone could publish an obsolete manual value to the live
rack. This batch removes the independent audible backlog failure without silently
changing that loss contract. It does not complete native recording intake barriers.

Prepared checks add maximum catalog, 20,000 stopped updates, refusal with newer
publication, producer/consumer overlap, signed zero and allocation/lock audit in
`LatestParameterEditsTests.inc`. The real Windows VST3 notification fixture adds
20,000 separately drained notifications without rendering, final PCM/state reopen,
and deferred editor delivery beside 32 scheduled offsets. Existing exact FIFO
capacity/loss and invalid-value failure tests remain. Mac's existing
`plugin-robustness` fixture already checks 40,000 stopped editor events, timestamp
order, exact loss, state flush and actual PCM; it is required for this extraction.

To avoid another build cycle, validate these two source batches together at the
00:53:09 UTC gate after freezing the combined commit. Add Windows `vst3-native-tests` and
CTest `vst3-native` to the earlier bounded selection. Mac qualification must include
`plugin-robustness`, native-plugins, musical-automation, recorded-automation and
parameter-preview. Main CI triggered by the prior navigation merge is not evidence
for this new backend. At 00:26 UTC its Mac ARM64 job had passed; Mac Intel and
Windows x64 were still running. Keep new source on the feature branch until the
new candidate's required checks pass.

The legacy standalone `windows/Tests/Plugins` project imported engine libraries
from an old build directory and asserted ARM64 unconditionally. The grouped root
build now registers that same native PCM/scheduling suite as `vst3-native-tests`,
linked to the candidate's actual `TrackerHosted` and fixture/scanner. Its native
architecture assertion remains strict, selecting ARM64 or AMD64 at compile time;
emulation is still rejected. This makes the existing broader provider assertions
usable in both local ARM64 qualification and x64 CI without an extra build.

## Main CI follow-up, 22 October 00:43 UTC

Navigation main `4e4862bcc` Windows run `39696261383`, job `119554252323`,
built successfully but failed the broad application step: 87 tests, 14 failure
records and two skips. These are retained failures, not a successful gate.
Its audio probe explicitly reports `devices: []`; transport has no active
endpoint or playback clock. Thirteen failure records involve required audio
startup, including secondary audition teardown failures. Starting the runner's
audio services did not provide a device. Do not skip these assertions or replace
native-device evidence with offline rendering to turn this run green.

The remaining failure was a stale Precise Notes saved-layout assertion: version 5
adds `mixingGraph` to the exact location set. The test now requires that complete
contract. On the retained qualified ARM64 executable `86538cb42`, the corrected
test passes with its draft, caret, compact-layout and persistence assertions.

A private-desktop silent-audio recheck on that same retained binary passed the
two affected transport tests and all four recording and three audition cases.
The shortcut case reached its actual assertion locally and exposed another stale
fixture: Ctrl+Alt+P now belongs to Pattern navigation. The fixture explicitly
unbinds command 647 before assigning it to playback, preserving conflict rejection
and all live-playback, text and focus checks. That corrected case passes.
The first local selection skipped eight opt-in audio cases; those skips are not
counted as evidence. `live-app.log` records seven audio passes and the shortcut
conflict; `shortcut-corrected.log` records its subsequent pass. No build was run.

Evidence is under `bin/plugin-take-lifecycle-20261021/main-ci-recheck`, including
the CI audio probe, logs and binary/tool hash receipt. This establishes bounded
local ARM64 behavior, not Windows x64 device qualification. The hosted runner
still requires an available WASAPI endpoint or an additional suitably equipped
Windows qualification host. Mac ARM64 main CI passed; Intel was still running at
the last observation. These main results do not qualify the new plugin source.

Include both corrected fixture cases in the next candidate's bounded application
selection (seven actual-app cases total), with explicit silent-audio opt-in.

## First combined candidate, 22 October 00:53 UTC

Frozen source `2ff11b8ae` ran exactly one coordinated checkpoint: local ARM64
started 00:53:09 UTC; draft PR #6 started Windows/Mac workflows at 00:53:34.
Next build must wait until **01:53:35 UTC**, or later if another actual build
start supersedes that gate. Local app and workspace executable compiled, but
the batch failed compiling `DocumentControllerTests.cpp`: its recovery test
used the shared decoder without including its header. Add the direct include;
do not remove the decoder roundtrip assertion. Later test targets were not built.

The resulting app SHA256 is
`1a766736ca49c69f232fd029ef64ed2e8251c4df6a96c3243f8b41d79f673948`.
Partial evidence lives in `candidate/partial.json`: all five compiled workspace
registrations passed. Five of six app cases passed; direct parameter-window
invocation initially failed in the Python helper because command 335 is a menu
command without a child HWND. Pass LPARAM zero for such commands. The corrected
case then passed on the same executable, retaining parameter edit, history,
draft and save/reopen assertions. This is not a pass of all required gates.

Computer Use inspected isolated inspection/automation PID 30260 on this binary.
Add Gainer, rack Params, the contextual Parameter controls destination and retained
reopening worked. Dragging Gain changed 0 to -13.656000137329102 and displayed
the successful one-Undo status. This exposed numeric presentation noise: shortest
round-trip float text now replaces promoted-double JSON text in generated rack
and window fields. Actual double metadata retains double precision; user draft
text is untouched. Added tests check scalar roundtrips and genuine double values.
These corrections are source-only until the next build.

The Computer Use helper only listed Main as a targetable window. Related-window
coordinates worked, but related accessibility indexes were rejected even after
refresh. Main field focus observations returned Main rather than an edit after
clicks; do not claim typing, Tab or floating-field continuity verified from those
observations. Native focus/caret/Tab assertions passed in the workspace harness;
fresh Computer Use focus verification remains required. No temporal flicker or
audio-performance claim is made. Candidate is not packaged or merged.

## Original-time native cuts: next source batch, not yet built

`editor/hosted/PluginEditQueue.hpp` replaces the duplicated Windows/Mac VST3
8192-notification FIFO with one bounded SPSC implementation. It retains every
accepted raw value and original timestamp; audible coalescing remains separate.
Native validation and each platform's exact overflow policy remain unchanged.
Fixed drain positions prevent later input from extending a Stop drain forever.
Stale/future bounds refuse without consuming an event; 64-bit publication cursors
do not become persisted song identities.

`PluginBackend::editFence()` captures position, host time and loss on the actual
native owner (`pluginMainCall` on Windows STA / Mac main). The shared facade's
`editBoundary` adds provider incarnation and refuses a changed vendor. Its
`popEditBefore` rejects another incarnation before touching the FIFO. The Mac
factory forwards this only for VST3; AU and built-ins explicitly remain unsupported
at this layer. Ordinary edit polling is unchanged.

`PluginEditQueueTests.inc`, included in the existing recorded-automation target,
covers fixed cuts with later input, identical/unknown timestamps, invalid bounds,
exact 8192 capacity over three wraps, callback allocation/lock audit where available,
and 50,000 overlapping producer/consumer events. Windows `EditNotificationTests`
and Mac `PluginRobustnessTests` add real-provider boundary checks: native capture,
pre-cut exact event, retained post-cut event, stale cut and replacement rejection.
Existing queue-loss, PCM, state, ordering and capture assertions are unchanged.

These are prerequisites for the recording session, not its finished lifecycle.
A cut does not freeze a vendor, restore its state or decide the fate of later
gestures. Worker arming/Stop/review/API, standard-control original-time intake,
AU integration and Mac session adoption remain outstanding. Next session work
must retain post-cut input during restoration, rather than blindly draining it.
Do not expose recording controls until those admission paths are complete.

Batch this with the first-candidate corrections at the next hourly build gate.
No C++ compilation or execution has run for this new source. Main Mac Intel CI
eventually reported cancelled; ARM64 passed. Neither result validates this batch.

## Worker Stop boundary (source only, next checkpoint)

`PluginOperations::sealRecordedEditors` now captures all owned VST3 input cuts
in one private-STA call, without vendor catalog/state calls that could pump input
between cuts. Allocation and context checks precede sealing. The exclusive time
end is the last cut clock quantum plus one; queue positions exclude later input,
including input with indistinguishable timestamps. Repeated Stop retains the
original boundary. A provider/context/preparation failure leaves ownership intact.

`pollRecordedEditors` drains at most 128 original notifications per provider per
call and acknowledges the fixed cut only when reached. Later gestures stay in
their native queue, rather than extending Stop or entering the audible preview.
Restoration refuses a changed post-cut position/loss before calling its audible
restore hook. Explicit stopped discard with the vendor closed can abandon that
transient input; normal completion cannot. The documented native restoration
barrier remains required: a cut alone does not prevent vendor reentrant input
during state restoration, and this primitive is not exposed as a finished UI/API.

The real-provider ownership test now uses the fixed boundary for successful
one-Undo/save/reopen recording, then adds 130 queued gestures, Stop, and a later
gesture before draining. It checks the 128-event work bound, exact 130-event
retention, original preview, idempotent polling/Stop, refusal before audible
restoration, and explicit closed-provider discard retaining state/history.
These added assertions have not been compiled or run yet.

PR #6 source `2ff11b8ae` CI update at 01:24 UTC: Mac ARM64 completed all configured
steps successfully; Intel remained in its build. Windows x64 failed compilation
on the same missing recovery-decoder include as local ARM64 (plus its cascading
comparison error). The direct include correction is already in `ddefc311c` and
will be validated with this combined source. Do not rerun the old CI candidate.

## Correction checkpoint: 22 October 01:53 UTC — failed compilation

Frozen `d1c8fddff13686884f8c7ba66d216fa718edd54e` configured successfully,
then failed in `RecordedPluginEditors.inc:74`: `pluginMainCall` was not declared
in the `PluginOperations.cpp` translation unit. The direct native-owner header
`windows/Plugins/UiOwner.hpp` is now included there. This correction has not been
compiled. The local attempt ran 01:53:51–01:55:34 UTC; retain
`bin/plugin-take-lifecycle-20261021/correction/build.json` and its log.
No ScreamSeq app was produced, no candidate checks or Computer Use qualification
were run, and no package or merge is justified from this attempt.

Updated PR CI began at 01:54:08 UTC: Mac run `39708338698`, Windows run
`39708338863`. The next coordinated build must not start before **02:54:09 UTC**.
Do not restart this failed local build or push another PR update before that gate.
Both Mac architectures passed the preceding `2ff11b8ae` CI run; that result does
not qualify the new shared queue. Source corrections should continue on the local
branch and be pushed to the non-CI backup branch between checkpoints.

## Pending-input arming guard (source only)

The next batch adds native-owner unread counts to shared/VST3 input cuts.
`beginRecordedEditors` checks these after catalog preparation, which can pump
vendor callbacks. A pending callback refuses arming without consuming input,
claiming the take or changing project/history. The real-provider worker test
queues a manual gesture between preparation and arming, requires refusal, then
verifies ordinary capture still produces the exact value and one reversible
history edit. Shared queue and both native provider tests cover pending counts.
The original native barrier requirement remains; this does not expose a finished
recording workflow. These additions have not been compiled or executed and join
the next hourly build after 02:54:09 UTC.

## Recovery captures its fixed pending prefix (source only)

`DocumentController::recoverySnapshot` now asks the recorded owner to retain the
native queue prefix before creating the archive. Arming, Stop and recovery share
one group-cut helper. Recovery drains at most 65 rounds of 128 notifications per
provider, bounded by the real 8192-entry native queue; later callbacks cannot
extend that work. A previously sealed Stop cut remains authoritative. A live
snapshot neither seals the take nor acknowledges Stop.

Recovery intake never calls audible preview or reads vendor state into saved
manual state. Preview remains pending for normal polling, including when its
queue refuses. All provider/context checks precede consumption; already failed
takes remain archivable. The archive remains detached review-only evidence.

Added real-provider assertions cover 130 queued callbacks, an injected callback
after the recovery cut, exact raw values/timestamps, no history/manual/Stop or
preview changes, deferred preview refusal, repeated snapshots, and existing Stop
boundaries. The prior normal 128-event poll assertion remains intact. These
changes and tests are unbuilt; use the next combined hourly checkpoint.

## Restoration preparation recheck (source only)

Baseline preparation calls vendor constructors/catalog/state methods and can pump
native callbacks. Release now rechecks each fixed Stop boundary after that work,
before invoking the audible restore callback. Later input remains queued and
ownership remains retained on refusal. This closes the preparation window; the
exclusive restoration barrier contract is still required for restoration itself.

The real VST3 fixture has a one-shot state-read callback probe. The worker test
requires the injected later gesture to block two release attempts before audio
restoration, then verifies explicit closed-editor discard preserves the baseline.
The arming test's manual value is also corrected to .375: fixture attachment
already sets .625, so reusing .625 would incorrectly expect a no-op to add history.
Assertions remain strict. These changes await the next hourly build.

## Native restoration input leases (source only)

Windows VST3 now implements an incarnation-bound control-owner input lease.
`windows/Plugins/EditorInputBarrier.hpp` subclasses the existing editor HWND tree
on its native STA, disables its root during restoration, blocks keyboard/mouse,
commands and timer input, and permits programmatic presentation. It refuses an
active mouse capture. Cleanup restores the original enabled state and field focus
only within the still-active owned editor, without activating another app. New
editor opening is refused while the provider lease is held. Explicit Close
releases the lease; retrying the last completed token is safe. Mac backends retain
the unsupported default and their existing behavior.

`releaseRecordedEditors` acquires every vendor lease after fallible baseline
preparation, rechecks the fixed Stop cuts, restores the audible preview on the
worker, and restores open vendor parameter baselines on the native owner. No
native thread waits for a reciprocal worker call. Every exit attempts lease
cleanup; a transient cleanup refusal retains its token with the take for retry.
The session must still own admission of ScreamSeq's standard controls.

Removed the former blind restoration-notification drain. Input from vendor code
outside ordinary HWND messages is possible even under a lease: changed native
queue positions/loss refuse completion and retain both the take and queue.
Explicit closed-editor discard remains the way to abandon this transient input.
This is not a guarantee against arbitrary vendor-created windows or callbacks.

New unexecuted regressions cover keyboard blocking and restoration, focus,
original disabled state, active-drag refusal, nested/token/incarnation rejection,
Close/retry cleanup, and a vendor callback injected during restoration. The last
case requires a second release attempt to see the same retained input, unchanged
saved baseline/history and successful explicit closed-editor discard. Existing
preview refusal, one Undo, opaque-state and save/reopen assertions remain intact.
They are included in the prepared notification and private-desktop recovery-manual
gates; no additional build is needed. All additions are unbuilt until the combined
checkpoint no earlier than 22 October 02:54:09 UTC.

## Worker recording session wiring (source only, not publicly advertised)

`DocumentController` now owns a parameter-recording session tied to its retained
playback host, presentation-frame timeline, musical revision and native providers.
`ParameterRecordingOperations.inc` implements internal start/get/stop/commit/discard
operations. Native callback intake uses the existing fixed queue cuts and transient
preview publisher. Start refuses pending manual changes, other takes/previews,
missing timing and invalid provider selections. Musical writes/save, replacement
(including recovery) and playback restart refuse while the take is retained.
Unselected vendor callbacks are deferred during capture instead of background
inspector reads advancing the take's captured musical revision.

Commit publishes one shared recorded-automation transaction, then restores its
transient controls/vendor baselines. A committed take with refused restoration
remains completed and retryable; retry cannot publish the music twice. Failed
context/clock takes remain readable. Explicit discard after stopped playback and
closing affected editors can release their retained input without committing it.

The new private-desktop real-provider controller fixture covers timed intake,
preview, protective recovery, mutation/restart rejection, Stop, commit without
stopping playback, discard, failed-clock review/discard, one Undo/Redo and native
save/reopen. It is registered inside the existing recovery-manual gate and is
unexecuted. This worker path is intentionally absent from the application's API
catalog and UI until native admission, original-time standard-control intake,
paginated take review, schema/documentation and UI controls are connected. Those
are required next steps, as are Mac session adoption and AU/builtin capture.

The internal worker also accepts one original-time standard-control event and
returns whether that event was retained independently of preview delivery.
Invalid values/identities/timestamps fail before changing take counters. A shared
validation helper keeps target/envelope/pattern-command rules aligned. Mixed
vendor/control preview selects the newest original timestamp, rather than the
last worker arrival. Take review pages contain at most 512 raw events for one
stable target, retaining decimal host-time/frame identities and unmapped values.
Regressions now cover delayed older vendor input after a newer control, invalid
input without poisoning the take, exact paginated timestamps and frame mapping.
These methods remain internal until native event handlers and the public contract
are connected; no running application exposes this unfinished workflow.

Both Mac architectures passed the preceding `d1c8fddff` run `39708338698`.
That result protects the earlier shared inputs only; the subsequent queue,
restoration and session changes still need the next combined checkpoint.

Mixed-source review also found a shared same-frame collision issue: worker arrival
order could let an older event overwrite a newer event when both map to one
canonical frame. `RecordedGestureCapture::overlay` now tracks each frame's newest
original host time; equal timestamps keep the existing last-accepted tie-breaker.
Baseline point order and all raw events remain intact. Added portable checks cover
44.1/48/96 kHz, reversed delivery, equal timestamps and unrelated baseline points.
This change is source-only and needs both platforms' recorded-automation gates.

## Follow-up checkpoint and subsequent built-in work

Frozen `fb228cb3e019c1040f3f73c556313fce4de90a92` began its single ARM64 build
at 22 October 02:54:09.923 UTC. Receipts/logs are under
`bin/plugin-take-lifecycle-20261021/followup`. Windows CI run `39713958236`
started at 02:54:47 UTC and Mac run `39713958110` at 02:54:46 UTC. The next build
must not start before **03:54:48 UTC**. Completion/check/UI/package status must be
read from those receipts; starting the build does not qualify this candidate.

The primary checkout subsequently adds control-only built-in recording support,
outside that frozen build. A built-in `NativePlugin` has no vendor callback queue
or HWND. Its adapter supplies the native host time for an identified empty cut;
paired reads validate the incarnation and acknowledge only that empty source.
Its no-editor lease has token zero and still checks incarnation on explicit end.
Native backends retain their own queue/barrier contract; an unsupported backend
does not become a built-in. Windows worker cuts now supply the host-only time.

Added unexecuted portable checks cover absent/injected host time, exact source,
empty drain, rejected vendor positions and no-editor lease identity. A worker
fixture exercises built-in control capture, Stop, live commit, preserved manual
state, one Undo/Redo and save/reopen in `document-controller-recording`. This work
remains internal along with the vendor lifecycle and awaits a later checkpoint.

## Follow-up results and next corrections (22 October, after 03:00 UTC)

The frozen `fb228cb3e` ARM64 build succeeded, ending at 02:58:23.067 UTC.
Its SHA-256 is `34f25ccbebbbfc08c54d58674ab94fa1c19920d2eb502ef2a633cccb00d634d3`.
The bounded run selected **17 native registrations** (earlier counts of 16 were
incorrect) and eight actual-app checks. **16/17 native and 7/8 app checks passed**.
This is a failed qualification, not a production candidate. Do not package or
merge it. Receipts and the exact logs remain in the `followup` directory above.

- `workspace-pattern-accessibility-tests` failed: external UIA focus resolves to
  Main after setting focus on the native Song title field. The existing assertion
  remains mandatory. Additional diagnostics now distinguish native HWND focus,
  active/foreground HWND, and UIA focus/property results.
- `test_manual_vendor_editor_overlay_and_late_protection_guard` passed its manual
  editor and recorded-ownership portions, then failed with `Expected an object`
  in the new worker lifecycle fixture. Its `automation.get` and
  `automation.recording.get` calls used JSON null (`{}`), not an empty object.
  Corrected these to `Json::object()`, including the later built-in fixture. No
  lifecycle assertions were removed; the previously unreached assertions still
  require execution on the next build.

Fresh Computer Use inspected this exact executable in disposable PID 444 with
`--inspection --automation --seconds 1800`. No audio device was opened. Observed:
Song Settings opens with the title caret visible; rack Add creates a built-in
Gainer; parameter search retains typed `gain`; Backspace produces `gai`; Tab
focuses the parameter selector, Down selects Gain by its filtered identity, and
Shift+Tab returns the caret to search. UIA reports Main throughout these native
focus changes even when typing reaches the correct field. This confirms a focus
reporting defect; it does not demonstrate lost keyboard input. Floating-window
typing continuity and temporal flicker were not qualified by this check.

The candidate accessibility correction delegates focused native controls to
Windows' MSAA-to-UIA bridge from the custom pattern fragment root, restricted to
this UI thread and Main's owned HWND tree. It preserves the standard text/value
provider. A direct fragment check now requires the exact Song title HWND, in
addition to the existing external-client assertion. This is **unbuilt and not yet
confirmed**. Microsoft documents the bridge's purpose in
[UiaProviderFromIAccessible](https://learn.microsoft.com/en-us/windows/win32/api/uiautomationcoreapi/nf-uiautomationcoreapi-uiaproviderfromiaccessible);
the host-only provider is explicitly unsuitable for returning arbitrary control
properties ([UiaHostProviderFromHwnd](https://learn.microsoft.com/en-us/windows/win32/api/uiautomationcoreapi/nf-uiautomationcoreapi-uiahostproviderfromhwnd)).

The primary checkout also adds native host admission for the still-internal
parameter lifecycle. `Application::ParameterRecordingInputLease` holds the existing
`NativeInputGate` across Start/Stop/Commit/Discard worker calls, completion and error
readback. It refuses active capture, another state transition and (on Start) raw
rack/parameter-window drafts. It releases musical keys before filtering queued
input, protects docked and floating HWNDs without disabling their fields, and
rejects nested API writes/workspace changes. RAII releases the lease on failure.
The existing `workspace-plugin-search-tests` registration now additionally checks
raw text/selection/focus, synchronous and queued input, nested writes, drag/draft
refusal and a real rejected worker Start. These additions are **unbuilt**.

The full original-time UI capture queue, parameter recording review window and
public schema/catalogue remain next work; this admission change alone does not
expose or complete recording parity. Both platform CI runs for `fb228cb3e` were
still building at 03:12 UTC. Re-read their terminal results without restarting.
No additional build may start before **03:54:48 UTC**. Reuse passing evidence only
for unchanged inputs; rerun the now-corrected lifecycle and accessibility checks,
the expanded native admission fixture, and existing shared built-in/gesture gates
on the next frozen source.

## Original-time native controls (source after 03:30 UTC; not built)

`windows/App/ParameterControlDelivery.hpp` now retains a bounded 1,024-event
UI-owner queue. Each original event carries document/revision/take/plugin/parameter,
native value and exact decimal host timestamp. Submission uses the serial worker's
future and per-call receipt without a native wait or message pump. Collection is
nonblocking, checks completion identity, and can reconcile a lost callback from its
receipt. Refused/unknown events remain reviewable; they are never blindly resent.
Overflow/allocation loss is counted separately and blocks commit. Commit/Discard
wait for pending submissions; only explicit completed take release clears the queue.

The independent ten-parameter rack windows and main rack fields/switches use this
path during an internally armed take. Ordinary manual editing retains its existing
path. Valid typed values preserve raw formatting/caret; partial numbers stay drafts,
and Retry cannot create a later timestamp. Slider movements are not coalesced into
the final thumb position. Discrete controls suppress their hidden numeric EN_CHANGE
to avoid submitting twice. The native timer services worker hooks and completion
receipts; it does not allocate or wait on the audio callback.

`windows/Tests/Workspace/ParameterControlDeliveryTests.inc` adds deterministic future/
receipt tests (delayed and out-of-order completion, lost callbacks, identity mismatch,
late receipts, explicit refusal, exact timestamps above 2^53, capacity and pagination),
plus actual native parameter-window/rack event-handler checks for text, caret, dense
sliders, switches, failed drafts, held takes and unchanged manual state/history.
They join `workspace-plugin-search-tests`; they are source assertions, **not executed
evidence**. The window tests substitute only capture delivery, not controls; worker
timing/publication is independently covered by the lifecycle fixtures.

This still-internal workflow must not be exposed in menus or `api.describe` yet.
Remaining prerequisites: durable review of native refused/unknown/lost inputs;
fresh take status/presentation after commit/discard; native recording review UI;
schema/catalogue/guide; actual-app end-to-end recording checks. The current native
queue is process memory, not crash recovery. Worker-accepted gestures already have
the separate shared recovery archive. Do not call this complete recording parity.

The `fb228cb3e` Windows x64 CI run `39713958236` has now failed: all 158 worker
registrations passed; 48/49 native UI registrations passed, with the same external
UIA focus failure. The audio probe returned an empty device list. Audio-dependent
actual-app cases then failed to start WASAPI/a valid recording clock (with secondary
process-liveness assertions). Keep these failures visible; do not weaken or skip
their required assertions. The owned vendor/lifecycle step also failed and needs
the corrected-fixture checkpoint. Mac ARM in run `39713958110` passed; Intel was
still executing the interface harness when last inspected around 03:28 UTC.

## Native delivery recovery (next source batch; not built)

`editor/hosted/ControlInputRecovery.hpp` adds a shared, bounded review-only archive
for native inputs whose completion is pending, refused or unknown. It preserves
original double values and decimal 64-bit host times/counters, distinguishes known
accepted count from delivery loss, and refuses unsupported versions/types/fields.
It has no replay or live-take constructor. A pending event may also be present in
the worker's accepted-gesture archive: these are two observations of an uncertain
delivery, not two musical events to insert. Diagnostic text is capped independently
of original values and identities.

Windows `ParameterControlDelivery::recovery` makes an immutable snapshot without
consuming input. `RecoveryIntegration.inc` captures it before its pumped worker wait;
the worker FIFO includes all already-submitted controls. `captureRecovery` validates
the original document/take/revision and overlays only the protective project copy.
The new top-level `screamseqPluginControlRecovery` array retains prior takes and
replaces only compatible matching evidence. It is an optional opaque preservation
extension, with no container/native musical metadata version change. Explicit song
replacement remains blocked by the live take; this does not bypass that admission.
Autosave now considers an internally armed built-in-only take even when the song is
otherwise clean and no vendor window is open.

Prepared checks in `editor/Tests/ControlInputRecoveryTests.inc` run through the shared
recorded-automation registration on both platforms. They cover exact roundtrip,
independence, repeated events, bounded pages/types, future-version refusal and
preservation of earlier/unrelated evidence. The native delivery fixture checks that
later completion cannot mutate an earlier recovery snapshot. The existing worker
vendor/lifecycle fixture additionally checks exact native project reopen, fingerprint
changes, foreign take/document refusal and unchanged accepted automation/manual state.
All these new assertions remain **unexecuted** until the next checkpoint. Reciprocal
Mac save/reopen of this new extension is also required before calling it qualified.

This supersedes the process-memory-only limitation of the preceding source entry,
but imported-archive review UI and end-to-end native capture/review are still pending.
No public recording entry point has been enabled. The latest completed Mac CI for
the older frozen `fb228cb3e` passed on both Intel and ARM; it does not qualify these
new shared/native changes. The hourly gate remains **03:54:48 UTC**.

## Native take review component (source before the next hourly gate)

`windows/App/ParameterRecordingWindow.hpp` implements a retained Win32 review
component with Record / Stop recording / Keep take / Discard take, explicit loss
acknowledgement, a recovery-copy action, native scope/target selectors and paginated
input inspection. Musical song time is presented in seconds; exact original host
times remain in its diagnostic snapshot. The host supplies parameter names, worker
calls, delivery evidence and completion reconciliation. Close hides the component
without finishing or discarding the take. Controls follow visible keyboard order;
ordinary polling retains the selected page and native list focus.

`ParameterRecordingWindowTests.inc` exercises the real native controls with
controlled worker replies: stable scope/target and revision, two-page raw evidence,
unmapped input, retained focus, closing while recording, explicit loss consent,
recovery copy, a post-commit failure and receipt-based review without resubmission.
The component is included in the existing native plugin-search registration and
will compile in the next candidate. **No test execution is claimed yet.**

This component is not connected to a public menu/API yet. The next implementation
batch must provide Application ownership and protected Close/document departure,
fresh native parameter presentation when the take ends, exact completion repair,
native-input loss acknowledgement at admission, API catalogue/schema/guide and
actual-app recording checks. Do not expose Record simply by adding a button before
those ownership paths are connected. Imported detached recovery review is also still
separate work; the new live-review component does not rearm old-process timestamps.

## Control review checkpoint and Application ownership (2026-10-22)

The frozen `ffe4a330c34147ea9ea907e3783901a2b8d792a7` ARM64 build started
03:54:48 UTC and failed at 03:56:22 before producing an executable. Main's new
recording admission called `PluginParametersWindow::retainedDraft`, which had not
been exposed. The failed build receipt/log are in
`bin/plugin-control-review-20261022/`; no native/app/foreground qualification or
production package is claimed for this candidate. The source correction exposes
the complete draft/preview/pending predicate; it does not remove the admission check.

Windows CI run `39719480632` and Mac run `39719480594` qualify that same frozen
head. Their results must be recorded separately. The observed Windows build step
began at **03:55:42 UTC**, so the next coordinated build must not start before
**04:55:42 UTC**. Do not rerun the failed local build or push another CI candidate
before that gate. Backup branch pushes remain build-free.

The subsequent source batch connects `ParameterRecordingIntegration.inc` as an
Application-owned retained review window. Song close/save/departure checks inspect
the parameter take as well as MIDI/sample takes under native input protection.
Receipt-based completion repair reads the current worker state, rejects another
document or a later take, and repairs presentation without resending a write.
Explicit boolean loss consent admits only already-completed native deliveries;
pending submissions still block Keep/Discard. Pre-admission refusals now carry
NotCommitted outcomes instead of appearing as uncertain writes.

Keep/Discard invalidates native parameter catalogues even when Discard leaves the
document revision unchanged. Clean parameter windows re-read the accepted baseline
on their timer; partial numeric text remains retained until explicit Reload.
This is source, not yet qualified behavior. Native assertions were added to the
existing plugin-search registration for Application ownership, close inspection,
receipt repair, loss admission and same-revision refresh/focus.

The paired edited-metadata fixture now includes `screamseqPluginControlRecovery`
alongside the gesture archive. Its exact typed oracle requires preservation across
edits, rack reorder/removal and Undo/Redo, including repeated original timestamps,
pending/unknown/refused status and values beyond JavaScript integer precision.
Use those opaque saved outputs for the reciprocal Windows→Mac→Windows and
Mac→Windows→Mac retained-app legs. Python syntax was checked; these native save/reopen
legs have not been executed for the new archive.

The public menu/API recording entry point remains disabled pending the native/app
checkpoint, complete catalogue/schema/guide and actual-app workflow checks.
Imported detached-archive review is still outstanding. Never treat this internal
owner or its unexecuted fixtures as completed recording parity.

Additional source review found a completion-ownership edge case: after every
original input had been accepted, no queued entry remained to bind accepted/loss
counters to their original take. `ParameterControlDelivery` now retains a separate
document/revision/take owner through entry removal. New input, recovery snapshots
and completion release must match it; a later take cannot inherit an earlier
take's counters, and an earlier receipt cannot clear later evidence. Tests cover
accepted-only and loss-only states, foreign release, foreign recovery and attempted
cross-take submission. Completion repair checks this owner before updating the UI.

The review UI also revokes loss consent when the reported evidence changes. A take
whose music has already committed but whose native preview restoration is pending
offers **Finish keeping**, disables Discard and explains the state. Restoration
errors survive the following status read. Clean parameter readback begins through
the existing focus-preserving load path, rather than pre-emptively disabling the
focused HWND at the take-finished notification. These assertions are prepared,
not executed; they belong to the next hourly native checkpoint.

## Detached input review preparation (source, not yet qualified)

`editor/hosted/PluginInputRecoveryReview.hpp` provides a shared read-only catalogue
and paginated review of imported gesture and native-control archives. Catalogue
headers are explicitly unverified; selecting an entry validates that whole archive
with the existing strict shared decoder. An unsupported collection/future archive
remains visible and preserved without hiding independent valid entries. Pages keep
target/event order, repeated original timestamps, exact native doubles, unmapped
events and decimal 64-bit counters. They never consult the current rack or clock,
rearm a take, coalesce input or publish automation.

Windows `PluginRecoveryReviewOperations.inc` has internal
`automation.recovery.list` and `automation.recovery.get` read boundaries. Get
requires the current document revision before resolving collection kind/index;
the original recorded document/revision remain separate evidence in the summary.
These operations are not yet in the public catalogue. A native recovered-input
window, corresponding Mac adapter and public schema/documentation are outstanding.

`PluginInputRecoveryReviewTests.inc` runs in the shared recorded-automation target
on both platforms. It covers cross-target pagination, equal timestamps, precision,
source loss, opaque/future archive preservation, malformed independent collections,
invalid bounds and immutable input. The existing native worker lifecycle fixture
now reopens an actual protective copy and checks both archive kinds, original
identity, stale/invalid request refusal, no live take and unchanged music/history.
Those new checks have not run; keep them in the next coordinated checkpoint.

## Native imported-input review (next source batch)

`windows/App/PluginRecoveryReviewWindow.hpp` adds the retained, read-only Win32
view over the shared catalogue/pages. It has separate bounded archive/input pages,
an ordinary native report list, and selectable exact original input or archive
details. Unmapped events and unsupported collections/versions remain explicit;
an unsupported first entry does not prevent opening the window or inspecting an
independent valid archive. Reading has no mutation/replay callback. The captured
document and revision guard page requests. Native input is protected during a
pumped read; unchanged refresh retains the selected row and detail-text caret.

Application owns this separate window and retires it with its document. The live
recording review opens it via **Recovered inputs…** without changing a live take.
This is internal navigation until the recording entry point/catalogue is exposed.
`PluginRecoveryReviewWindowTests.inc` joins the existing plugin-search registration:
future/malformed and valid archives together, 130 input rows, 68 archived entries,
exact counters/timestamps, read-only Backspace, retained focus/caret/page, failure
and retry, and document change during a read. These are source assertions only.

CI observation at approximately 04:25 UTC for the older `ffe4a330c` checkpoint:
Windows x64 job `119629368462` in run `39719480632` failed compilation on the same
missing `PluginParametersWindow::retainedDraft` member in both Main and the native
workspace fixture. The corrected source was already committed in `225ddb62a`.
Mac ARM job `119629368448` in run `39719480594` passed; Mac Intel job `119629368438`
was still building. This does not qualify any source added after `ffe4a330c`.
The next build gate stays **04:55:42 UTC**; do not restart the failed run.

## Public Windows workflow integration (source, awaiting the hourly gate)

Commands 661/662 now open the live plugin take and detached recovered-input
windows directly. They are registered in the command palette, separate-windows
navigation and Tools → Automation; the live take also appears in Playback.
These are document-wide window commands with deferred-open handling. They retain
the Pattern/docked editor layout and existing window ownership. The healthy take
automatically selects its first captured parameter; subsequent polls respect an
explicit parameter or delivery-review choice, page, keyboard focus and caret.

`PluginOperations::reads/writes` exposes the existing worker lifecycle through the
actual Windows pipe. `SessionAdapter::describe` advertises the request schema and
the revision requirement for detached recovery Get. The new
`windows/Api/plugin-recording.schema.json` and API README cover nine operations,
original QPC 100 ns timestamps, bounded paging, stale/type rejection, loss
acknowledgement, uncertain receipts, no replay, Keep/Discard, and recovery copies.
The take Get now exposes a decimal host-clock sample and stable original
start/end/epoch fields. Empty, oversized and NUL-containing detached revision
tokens reject before indexing the archive. Shared music/history/project encoding
is unchanged. **Mac API exposure of these new lifecycle/recovery names remains
outstanding**; the Windows guide states that limit explicitly.

`windows/Tests/test_plugin_recording.py` adds three actual-app checks:

- Direct window navigation, catalogue/schema exposure, strict recovery request
  validation, unchanged document/history and retained editor layout.
- A future archive beside an independent valid original-input archive, exact
  decimal counters above 2^53 and native doubles, retired plugin identity,
  native selection of the valid archive, read-only review, no current-clock take,
  and save-copy preservation without touching the recovered source.
- Silent owned playback, native Record and standard rack value-field capture,
  original timestamps, stopped-take review without stopping playback, Keep as
  one Undo step, Redo, unchanged manual state, exact API capture replay, a second
  discarded take, and native save/reopen of the committed music.

The native plugin-search fixture now opens through the public commands and checks
that window navigation preserves the dock configuration. Its recording review
fixture checks initial event visibility and explicit delivery-page retention.
Windows CI includes the actual-app class. The local checkpoint adds ApiTests and
SessionCacheTests for the adapter change and the three new app cases. The prepared
close-drain test selector was corrected from the nonexistent
`RecoveryControllerTests.test_close_drains_pending_native_recovery_write` to the
existing `RecoveryTests` method; the assertion itself is unchanged.

At this source checkpoint only Python/JSON parsing and `git diff --check` have
run. No native tests, builds or foreground qualification are claimed for these
changes. The correction build script still enforces **04:55:42 UTC** and a clean
frozen source commit. Before running it, inspect its receipts; after a successful
build run the bounded checks, then use Computer Use on that exact executable for
navigation, parameter controls, recording/review and keyboard focus. Keep failure
evidence; do not package or merge an unqualified candidate.

Latest bounded observation of older `ffe4a330c` Mac Intel job `119629368438`:
build, model/host tests, Swift tests and native interface harness passed; socket
conformance was still running. This is supporting evidence for the older inputs,
not qualification of the newer shared recovery helper or Windows integration.

## Shared detached recovery reads on Mac (source, awaiting qualification)

The earlier public-read asymmetry is now addressed in source. Mac
`PluginRecoveryReviewAPI.inc` exposes the same `automation.recovery.list/get`
contracts, paging limits and stale-document guard as Windows. It delegates
archive validation and event projection to `PluginInputRecoveryReview.hpp`.
`ProjectMetadata::importedMetadata()` provides a const view of detached imported
metadata, excluding owned module/plugin state. Reads do not serialize the current
audio/plugin graph or merge old input into current music. Mac's socket cache now
classifies recovery List as a read, so repeating a read request cannot return a
cached earlier document's catalogue. Both public schemas/guides describe this.
The seven live `automation.recording.*` methods remain Windows-specific; this
batch does not silently replace the Mac's existing native recording lifecycle.

Mac's existing parameter-activity registration now includes a native bridge test
for a future archive beside valid input, exact counters/doubles, independent
selection, invalid/stale requests, unchanged music/history, save-copy preservation
and untouched source. The shared project-preservation target checks that the
const imported view excludes owned state and survives current-model saves.
The existing paired edited-metadata conformance workload now calls these reads
on both platforms at initial import, after deleting the original plugin, and
after reopening. It requires unchanged original timestamps, values, loss/epoch
counters and stable target IDs; those requests are recorded in the same evidence
journal as the save/Undo/Redo checks. This adds execution coverage to an existing
build and fixture workload, not another build cycle.

These additions remain unbuilt/unexecuted at this source checkpoint. Local
qualification adds the shared project-preservation target to the same hourly
build. Mac native and actual-socket checks require the next coordinated CI run;
prior Mac results cannot be reused for these changed adapters or conformance
inputs. Reciprocal exchange must use the new saved opaque fixture outputs.

At approximately 04:47 UTC the older Mac Intel job `119629368438` completed
successfully, including its socket conformance and interface checks. Together
with the older ARM result this closes that old Mac checkpoint only. Both Mac
jobs must execute again for the now-changed shared metadata and native adapter.

Final source review found a trapped-take UI path: a failed clock/context could
leave `capturing:true` because normal Stop could not seal input, even after
transport stopped. The worker already supported explicit Discard in that state,
but the native button required `capturing:false`. Get now exposes authoritative
`transportPlaying`/`discardable` fields, and the native review labels the stopped
failed take correctly and enables its guarded Discard path. Pending delivery and
completed-take guards remain. The existing native failed-clock worker test now
asserts that admission state, and the review fixture exercises the matching
button/identity/close-barrier transition. These assertions join the same pending
build; no additional build or relaxed failure gate is introduced.

## 22 October 04:55 UTC checkpoint — compiled, startup qualification failed

Frozen candidate: `bcd10d8a8edf0b2e5409c8ef4e7c42f15f3aab25`.
The one local ARM64 Release build started at 04:55:46 UTC and completed at
04:59:44 UTC. Executable SHA-256:
`b6970550f4746d22121e912dbea6d5c46e8ec5b731cfba1b1b774f6c7db3ac78`.
Receipts: `bin/plugin-control-review-20261022/correction/{build,checks}.json`,
with `native.log`, `native.xml`, `app.log` and `app-launch.log` alongside them.

Thirteen of 24 native registrations passed, including the recorded-take worker,
failed-clock discard admission, plugin notifications, VST3, project preservation,
API and reply-cache checks. Eleven native application registrations failed during
startup with **Duplicate native menu command**. Eleven of 12 actual-app cases
could not start; the separate controller/vendor-ownership case passed. No claim
is made that the public recording workflow passed. The private-desktop wrapper
also recorded a foreground-preservation failure (before HWND null, afterward an
unrelated PID); preserve that evidence separately from the deterministic menu
failure. No foreground Computer Use pass or production package is claimed for
this executable: it cannot construct its application menu.

Source correction removes the duplicate `parameterRecordingCommand` registration
from Tools > Automation, retaining Playback > Plugin automation take and the
separate-window navigator. The uniqueness assertion remains strict. Recovered
plugin inputs remains under Tools > Automation. Both document-global review
commands now tolerate playback/cursor movement while the native menu is open;
the document identity and native focus guards still apply. The existing menu
fixture now invokes both after cursor movement and requires preserved musical
state and dock layout. This correction is source-only until the next checkpoint.

Coordinated CI at this source: Mac run `39725114919`, Intel job `119647127828`
and ARM job `119647128083`; Windows run `39725114915`, x64 job `119647127634`.
All were compiling at the 05:02 observation. Inspect terminal results and retain
valid Mac evidence if its relevant source inputs are unchanged by the menu fix.
Do not restart these jobs. The latest build start was Mac Intel at 04:56:14 UTC,
so **no next build or build-triggering push before 05:56:14 UTC**.

Next checkpoint must rerun the affected application checks against a rebuilt
candidate, without weakening any assertions, then inspect the exact executable
with Computer Use for real navigation, parameter editing/recording and keyboard
focus. Worker-only passing results are bounded reusable evidence if their inputs
remain unchanged; they do not substitute for the unexecuted public workflow.
Packaging and main integration remain gated on qualification. Backup-only branch
pushes do not trigger the coordinated build workflows.

## Pending completion review correction (22 October 05:30 UTC)

Source review found that the new recording window's Refresh action called
`NativeWriteCompletion::finish()` even without a returned completion. It reported
an unverified outcome but dropped the original receipt. If a Keep had made the
current take empty before its completion arrived, this also enabled Record and
released the window's close barrier. Present state alone does not establish the
outcome of the earlier request.

Refresh now retains an unresolved receipt and its protection. A matching late
completion must pass application reconciliation and successful readback before
the receipt is released. A foreign receipt or a failed reconciliation cannot
release it; nothing resends the earlier write. Proven rejection still follows
the existing `NativeWriteCompletion` admission behavior.

The existing `applicationParameterRecordingReview` native fixture now injects
a missing completion after Keep, reads the empty current take, attempts a new
write, supplies a foreign completion, fails one readback, and finally supplies
the matching successful completion. It asserts retained close protection,
disabled repeat/new-write controls, exact submission count, and Refresh focus
through recovery. These assertions are **source-only**, included in the existing
`workspace-plugin-search-tests` gate at the next hourly build. No build or
native pass is claimed for this correction.

## 22 October 05:56 UTC checkpoint — two native focus failures

Source `7154b9f323def1c448a7948eb27b6933c6a40021` built once locally from
05:56:28.8304205 to 05:58:28.4394101 UTC. Executable SHA-256:
`8abb2a711c2a1e6233a67f46b35a27dd87a6dcb9d34b60ccf022aecb2c08be57`.
Terminal receipts are under
`bin/plugin-control-review-20261022/startup-correction`; never rerun its build.
The 05:58:37–06:01:18 check run passed **22/24 native registrations and 13/13
actual-app cases**. Source and executable hashes remained unchanged.

Passing public workflows include real Record/Keep/history/reopen/Discard,
detached recovery, parameter search and later-ID editing, preview/close barriers,
and hidden-context MIDI sound selection through native keyboard input. These
passes do not erase the two separate native failures:

- `workspace-pattern-accessibility-tests`: Main's custom fragment root omitted
  its focused native Song settings field, before the external-client focus check.
- `workspace-context-panel-tests`: the immediate post-scan sound-choice fixture
  failed its combined draft/focus/history assertion. Its existing layout case
  passed. The successful actual-app case waits for idle, unlike this immediate
  native call; it is not evidence that the transient state is safe.

Prepared corrections, **not yet built**: `RecordingIntegration.inc::rescanMidi`
  now clears `midiBusy` before publishing the completed scan's UI snapshot. Its
  guard still handles exception cleanup. The native case requires enabled fields
  and acquired focus immediately, then separately checks layout, draft, caret,
  take/history and cursor/selection preservation. No assertion was relaxed.
  `NativePatternAccessibility.hpp::nativeFragment` now starts at the native
  parent root and follows explicit `GW_OWNER` links, with thread and bounded-chain
  guards. The previous `GA_ROOTOWNER`/`GetParent` path misses ownership of the
  application's overlapped tool windows. The native regression retains direct
  bridge HWND and external MTA-client focus assertions, verifies the production
  owner chain, and rejects an unrelated focused window on the same GUI thread.
  This source explanation still requires runtime confirmation of the full bridge.
  Relevant Windows contracts: [GetParent](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getparent)
  and [GetAncestor](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getancestor).

PR #6 head is the frozen candidate. CI merge `4c40b3e0b792f73953b71d2727dd8580548e696e`
has the same Git tree `d41c9221ae92a1615aa5eb79c22ba52a862d9185`. Windows run
`39730905720`/job `119665472145`; Mac run `39730906060`, ARM job `119665473046`,
Intel job `119665473245` were still building at 06:12 UTC. The latest build start
was Windows at 05:56:56 UTC: **no next build or build-triggering push/merge before
06:56:56 UTC**. Backup-only pushes remain separate from this gate.

Foreground diagnostic process PID 20700 used this exact executable, silent audio
and a disposable F04 copy. Its API observed active playback with a valid clock;
this is not visual or audible-performance evidence. Computer Use found Main but
Windows denied input twice (`GetCursorPos: Access is denied`). Input stopped and
an unlock request remains pending. The returned wallpaper image is not an app
visual pass. Never reuse this PID/handle after exit. No foreground qualification
receipt, package or main merge is authorized by these failing results.

Previous `bcd10d8a8` CI is terminal: Windows passed its build and all 158 worker
tests, but only 20/49 native UI registrations; the duplicate menu prevented the
public workflows from starting. Both Macs passed native/interface checks and
failed the stale inventory baseline. A separate retained old Mac ARM run passed
5 original fixtures, 41 API and 21 codec cases after tooling review; it does not
cover the current source or the extended edited-fixture reciprocal gate.

### Cooldown investigation: native focus handoff and test scope

An uncompiled, hidden-HWND Python/ctypes contract probe confirmed that an owned
overlapped tool's `GetParent` is null and the field's `GA_ROOTOWNER` stops at that
tool, while `GW_OWNER` reaches Main. The same probe then reached the former bridge
call: Windows returns `E_INVALIDARG` for the OleAcc standard EDIT proxy. The owner
correction alone would therefore reveal another deterministic failure. Probe
source/results live beside the checkpoint as `owner-probe.py`/`.json`; the first
ownership result is retained, while the later bridge attempt ended with the
explicit HRESULT exception rather than producing a success receipt.

The prepared source now uses an HWND-hosted focus fragment with override-provider
semantics. It supplies the exact native HWND and defers native Value/Text/selection
behavior; it does not install a replacement `WM_GETOBJECT` handler on the edit.
Its subclass caches one provider per control and invalidates/disconnects it before
native destruction, preventing a retained provider from following a reused HWND.
The existing exact-HWND assertion now reads the handoff's native-handle property
instead of trying to unwrap an unsupported MSAA bridge. The external-client
global-focus equality remains mandatory, with additional Value/Text/selection
and retirement assertions. **This implementation remains unbuilt and unqualified.**

A second synthetic control probe (`focus-contract-probe.py`/`.json`) runs in a
separate child on a never-switched private desktop, including an ordinary EDIT
with no ScreamSeq provider. `ElementFromHandle` returns that exact EDIT with native
Value and Text patterns, but global `GetFocusedElement` returns the desktop
(`class #32769`, PID 1728) rather than that child's locally focused field. This
also happens with the null-root and proposed HWND-handoff variants. Thus this
environment cannot establish global foreground focus even for the control-only
baseline. Do not classify that observation as a ScreamSeq focus failure or a pass.
The synthetic handoff node exposes its exact HWND and Value support, but that
deprecated core-node probe does not establish the full COM Text/focus contract.
No test assertion has been disabled on this basis; the real native fixture and
unlocked Computer Use checks remain outstanding. Earlier generic failure text
“reported Main” is not proof of the returned HWND without the diagnostic IDs.

At the 06:24 UTC observation, current Mac ARM job `119665473046` is terminal
success: 134/134 native tests, 35/35 interface groups, and its full requested
conformance step passed (including the extended editing workloads). Windows x64
has compiled and is running worker checks; Mac Intel is still building. These
results belong to `7154b9f323`/identical-tree CI merge `4c40b3e0`; they do not
qualify the subsequently prepared Windows focus changes. No jobs were restarted.
