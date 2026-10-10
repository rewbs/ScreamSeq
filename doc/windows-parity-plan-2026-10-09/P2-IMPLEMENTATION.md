# P2 named/grouped tracks — source implementation in progress

This temporary branch starts from P1 commit `a320bd9da`. P1 remains frozen in its
own worktree for checkpoint 02. These changes are not included in that checkpoint
and have not been compiled or executed. P0b/P0c and P1 gates remain open; this
document does not claim that their prerequisites or P2 parity are complete.

## Shared candidates and Mac adoption

`editor/TrackLayout.hpp/.cpp` now declares typed `GroupNoteTrack`,
`CreateNoteTrack`, `UngroupNoteTrack` and `SetNoteColumnMute` requests, combined as
`NoteTrackEdit`. `prepareNoteTrackEdit(native, song, edit)` returns an owned
`PreparedNoteTrackEdit`: candidate native song, affected stable ID, appended
column count, changed/no-op and mixer-change classification. Preparing a candidate
does not mutate the input song, allocator or history. Existing `groupNoteColumns`
and `effectiveColumnMute` remain the shared grouping and mute helpers.

The extracted logic covers format-dependent append limits, stable column/group
allocation, reconciliation of appended track buses, grouping admission, ungroup
with routing retained, imported-mute override removal, and candidate validation.
Append validates projected mixer identities; its native adapter must resize the
embedded song and adopt the candidate in the same document transaction, which
performs final whole-song validation. Plugin capacity remains adapter-owned.

`mac/Bridge/TrackAPI.inc` now decodes its existing public requests to those typed
intents and consumes the shared candidate. Foundation/JSON interpretation,
revision guards, plugin capacity, stopping structural playback, document history,
renderer mute publication and public layout/result dictionaries remain native.
The existing API fields, aliases, storage identifiers and metadata version are
unchanged. Windows dispatch and presentation do not yet consume this interface.

One confirmed bug is repaired in the underlying shared grouping helper:
`commonOutput == 0` previously meant both uninitialized and disconnected. With a
disconnected first column followed by a connected column, the implicit grouping
could accept differing outputs. An optional destination now distinguishes these
states, requiring an explicit output for mixed destinations in either order.
All-disconnected grouping still requires an explicit valid group/return/Master,
as before. The public guide describes this contract.

## Prepared validation and required next checks

`editor/Tests/NoteTrackEditTests.cpp` is registered as `note-track-edit` on both
platforms; Windows includes it in the `portable-tests` build dependency. Authored
cases cover pure/repeatable candidate preparation, stable IDs and pattern data,
one-step grouping and append history, ungroup routing retention, invalid adjacent
column sets, destination admission including disconnected ordering, imported
mute true/false and no-op/override removal, and append/capacity across all five
supported module formats. These tests are unexecuted.

The P2 build checkpoint must include `note-track-edit-tests` and both native
applications. Mac must retain its existing `note-track` provider/audio/history/
five-format persistence suite and note-track interface checks; those assertions
were not weakened or removed. Both platforms need the applicable mixer graph,
native song, channel resize and pattern scope regressions. Changed shared
candidates require new evidence; P1's unchanged-input evidence cannot qualify
this extraction. No P2 build or test has been run, and the user-requested build
cadence applies across worktrees.

## Next implementation boundaries

1. Add Windows request decoding/dispatch, `api.describe` coverage and the same
   public layout/result fields, preserving typed validation and revision guards.
   Use the shared candidate and normal document-worker history. Verify dryRun,
   no-op, invalid/stale/capacity failure, unrelated data and reciprocal saves.
2. Implement prepared live column-mute publication, including Undo/Redo and NNA/
   plugin note ownership. `HostedProject::prepareNativeUpdate` currently rejects
   column-mute and note-track changes. Do not simply remove that guard or claim
   parity from a stopped-only fallback. Structural appends may retain the
   established safe stop/prepare/adopt path.
3. Add native group/name/color headers, column mute and create/group/ungroup
   commands with captured selection, focus, keyboard access and retained editors.
   Preserve the existing stable-bus note-track scope in `PatternTransform.inc`.
4. Qualify against A03/F04 and reciprocal fixtures after P0/P1 prerequisites pass;
   both platforms converge on shared main through temporary review branches.

## Windows adapter prepared, application dispatch still pending

`windows/Session/TrackOperations.hpp/.cpp` now decodes the five existing track
methods into the shared candidate interface and produces the Mac-compatible
layout/result fields. Numeric booleans, fractional/out-of-range counts, unknown
keys and noncanonical stable identities are rejected. The name and native-ID
rules follow the current adapters; no public schema extension is introduced.

Result JSON is prepared before document adoption. DryRun and successful no-op
avoid publication/history; grouping/append retains the established structural
stop path. Ungroup retains routing. Persistent column mute uses a prepared
publication callback passed to `Document::annotate`, so admission failure leaves
history and the native document unchanged. Host hooks reserve plugin/cache
capacity validation and live mute publication for the document controller.

`track-operations-tests`, included in `worker-tests`, exercises that adapter
offline. Its authored cases cover result projection, strict input failures,
dryRun/no-op, refused publication with unchanged history, mute Undo, and in-memory
native codec round trips of grouping/mute/append/ungroup in all five module
formats. These are unexecuted functional checks, not actual pipe, device, realtime,
or reciprocal evidence. Add this target/test to the eventual P2 checkpoint.

The adapter is intentionally not yet wired into `DocumentController` or the
Application method inventory; no newly advertised method runs with empty live
hooks. Complete that wiring together with renderer publication and history.
At that adapter checkpoint, `prepareNativePublication` ignored column-mute
differences in its fast no-change path, and `HostedProject::prepareNativeUpdate`
refused them. Both paths need the same prepared contract for writes and Undo/Redo. The existing
renderer applies imported/native mute flags and releases owned plugin/NNA notes
at render boundaries, but its per-channel atomic setters do not by themselves
establish all-column transactional publication. Preserve imported mute baselines
when removing overrides; renderer `ChnSettings` is updated by playback and is not
a safe replacement for the document's imported state. No build/test ran for this
adapter; P1's frozen source is unchanged.

## Prepared renderer mute publication — source only

The shared Renderer now offers `prepareColumnMuteUpdate` and
`publishColumnMuteUpdate`. Preparation resolves stable native column IDs against
an immutable copy of the imported mute flags captured at renderer construction.
It produces one bounded 192-column `ColumnMuteFrame`. The existing
`RealtimePlan<ColumnMuteFrame>` queue admits up to three pending frames, rejects
saturation before history commit, and preserves a rejected caller's ownership.
At the render boundary the consumer adopts the newest complete frame before
the existing sample/plugin/NNA mute logic. Allocation and retired-frame deletion
remain on the control producer. No device defaults or vendor integration changed.

Windows `HostedProjectPlayback::PreparedNativeUpdate` now has a ColumnMutes kind
with the existing owner/generation/single-publication guards. Only a pure mute
change uses this path; combined mute plus graph/automation/topology changes are
refused rather than partly published. The document controller's native-history
fast path now includes `columnMutes` in its equality check. Structural channel
changes still require a new renderer; logical grouping follows the established
safe structural path. Legacy Renderer mute setters remain for startup, audition
and existing Mac use; a live owner must not mix those setters with pending
persistent-frame publications. Mac migration to the new admission mechanism is
not claimed by this Windows integration step.

The existing shared `note-track-edit` test gains renderer-boundary and imported
mute true/false restoration cases. The Windows hosted-project suite gains native
owner/generation/refusal checks at 17/128/511-frame blocks, independent-column
flags, full-queue rejection and retained-frame retry, newest-frame adoption,
audible sample mute/unmute and source immutability. Sustained sample loops make
the mute/unmute oracle independent of natural sample decay. These render calls
run through the suite's existing C++ allocation/deallocation audit scope; they
do not establish direct malloc/free, lock or vendor-private behavior. The old
unsupported-mute assertion is replaced by required successful pure publication
and rejection of an unsupported combined mute/automation edit.

All these checks remain unexecuted. Application dispatch, worker/pipe history
tests, queued-frame NNA/plugin ownership scenarios, both platform builds and the
affected Mac realtime/note-track regression gates are still required. No builds
or tests ran for this source batch. P1 remains frozen at `a320bd9da`.
