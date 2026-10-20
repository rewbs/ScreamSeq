# Native recorded vendor ownership

**Source only, unbuilt and not exposed through the session/UI/API.** This
continues [shared take ownership](RECORDED-TAKE-OWNER-2026-10-20.md).
The qualified `7fb99d0b2` candidate predates these changes.

## Implemented boundary

`windows/Session/RecordedPluginEditors.inc`, included by `PluginOperations.cpp`,
adds an internal worker scope holding one shared `RecordedGestureTake`,
current full revision/playback-epoch callback, transient preview callback and
captured parameter catalogs. `beginRecordedEditors` validates the captured
ordered rack/point baseline and actual provider identities, refuses pending
manual input, and adopts the scope only after preparation and revalidation.

The session caller must flush earlier manual input and hold the real native
input barrier while arming. The helper does not manufacture that barrier.
There are currently no production callers of this arming method.

- Ordinary editor polling feeds original paired native notifications to the
  take before coalescing. Each provider gets at most 128 events per poll;
  another poll is required if it has not returned an empty paired pop.
- Transient preview is reconstructed from retained raw input. Queue refusal
  leaves the raw take and preview retry intact. Recorded providers are excluded
  from manual opaque-state flushing and recovery overlays even after failure,
  after commit, or after their window closes.
- Plugin/graph/automation mutations and history movement are refused while
  ownership is retained. Unowned vendor notifications can remain pending;
  ordinary opaque history capture waits for release so it cannot change the
  take's captured full revision.
- Completion accepts only the complete take's actual target owners. A scoped
  exception permits its atomic recorded publication through the normal history
  owner; refusal restores the ordinary mutation guard.
- Release first restores playback preview, then restores open vendor parameters
  and checks complete opaque state/audio-layout equality against the accepted
  baseline. Old vendor audio queues are flushed before restoring parameters.
  Restoration-generated notifications are drained while ownership is retained.
  A failed hidden-state restoration requires explicit editor close and retry;
  a closed provider can be replaced from its saved baseline. Ordinary successful
  restoration keeps the open native window and notification incarnation.

These functions run on the worker/control owner, never the audio callback.
They add no metadata or public API and change no behavior without an armed scope.
Shared additions are bounded provider/baseline inspection on the take; musical
capture, original-time mapping and atomic publication remain shared.

## Prepared validation

`windows/Tests/RecordedEditorOwnershipTests.inc` joins the existing
`--recovery-manual` document-controller fixture. It uses actual VST3 fixture
callbacks, not synthesized notification input. The checks cover:

- Rejected arming without partial ownership; pre-arm manual state accepted.
- Raw intake retained across preview refusal, latest-value retry, idle polls,
  and the 128-event poll boundary; all original timestamps resolved after seal.
- Exact saved/manual state and recovery-overlay protection before and after
  polling, commit, restoration refusal and fatal context change.
- Mutation/history refusal and rejection of a substituted target owner.
- Refused recorded publication retaining input/history/protection, followed by
  one successful publication, exact Undo/Redo and save/reopen.
- Successful in-place restoration without another manual Undo entry; explicit
  close/discard after failure with baseline and Redo retained.

The Windows workflow now explicitly schedules
`test_recovery.RecoveryControllerTests.test_manual_vendor_editor_overlay_and_late_protection_guard`
through `run_isolated.py` on a never-switched private desktop. It reuses
`plugin-edit-cache.json` produced by the preceding native notification test;
missing cache/fixture fails rather than skips. The complete worker group is
included, so adjacent ordinary manual/recovery behavior is exercised too.
Its log is retained as `screamseq-vendor-ownership.log`.

At the next permitted batch, build the app, document-controller test and shared
recorded-automation target together; reuse the deterministic provider/scanner
only if their inputs remain unchanged. Run the ownership group once, the shared
take tests, and the existing live recorded-publication/preview groups. Any
failure expands investigation to its affected owner, not an automatic full
rebuild. Both Mac builds must compile the shared header in the coordinated CI
batch. This source review is not an execution pass.

## Required next integration

DocumentController must own arming, current revision/epoch, native UI/vendor
input leases, transient playback publication and Stop/disarm drain. It must
also guard Save/departure/topology/history/preview conflicts before flushing.
Status needs publication independently of musical edits. Restore all accepted
manual preview baselines, retaining scope if the audio queue refuses.

Serialize unfinished input and loss/review state separately from committed
points and opaque manual plugin state; restore it into stopped review without
inventing provider identity or clock continuity. Add shared recovery encoding
and invalidation before exposing recording controls. Mac's native intake must
adopt the same take semantics with its own UI/audio barriers. Both-host API,
schema, guide, native controls and F04/reciprocal acceptance remain required.
An internal worker scope is not completion of recording parity.
