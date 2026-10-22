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
