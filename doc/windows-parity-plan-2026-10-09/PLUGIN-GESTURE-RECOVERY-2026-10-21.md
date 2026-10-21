# Plugin gesture recording: recovery prerequisite

Status: source and regression cases drafted on 21 October 2026 after navigation
candidate `86538cb42` passed automated qualification. **Not built or executed.**
The navigation executable does not contain these changes. No Record control,
public API, timing-parity, Mac execution or complete recording-lifecycle claim.
The next build may start no earlier than **23:07:42 UTC**, and should combine
the remaining coherent recording work rather than build this prerequisite alone.

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
- `windows/Session/RecordedGestureRecovery.hpp`: native JSON/plist projection.
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

There is intentionally no deserialization into live providers. A future bounded
review decoder must validate all types/counts/ranges/references and distinguish
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

Only static diff/whitespace review has run for this source. No build was started
inside the hourly gate, and none of the navigation candidate's 36 passes is
evidence that this new recording source compiles or passes.
