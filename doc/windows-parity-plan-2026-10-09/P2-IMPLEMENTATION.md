# P2 named/grouped tracks — source implementation in progress

This temporary branch starts from P1 commit `a320bd9da`. P1 remains frozen in its
own worktree for checkpoint 02. These changes are not included in that checkpoint
and have not been compiled or executed. P0b/P0c and P1 gates remain open; this
document does not claim that their prerequisites or P2 parity are complete.

The sections below record successive source checkpoints. The latest worker/API
integration is described at the end; earlier statements that dispatch is pending
describe the earlier checkpoint, not the current tree. Native track UI and all
P2 execution gates remain pending.

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

## Worker dispatch and actual-pipe scenarios — source only

`DocumentController` now dispatches all five track methods using the existing
revision guard and serial worker. `Application::additionalDocumentReads/Writes`
advertises those same methods, so the real host's `api.describe` includes them
and their expectedRevision guards. `document.get.data.trackLayout` contains the
same projection as `track.get`, without replacing raw `tracks` metadata. Cache
accounting and catalog invalidation include that projection. Mixer/graph writes
also budget changes to its destination/group names before mutation.

Append preflights a private document with resized channels and candidate native
metadata, checking the complete resulting cache before stopping transport.
Other track edits validate native cache growth and plugin capacity before
adoption. Live mute uses the prepared publication callback from the preceding
checkpoint. History admission now normalizes its candidate nextID to the
document's current allocator floor, matching actual history adoption. Pure
visual ungroup history needs no renderer publication; mute history uses the
same frame admission as a direct write. Structural changes retain their normal
stop/reprepare behavior. A failed track write with unchanged song revision is
explicitly classified as notCommitted; a postcommit view/completion failure
retains the existing committed-result receipt and recovery path.

`windows/Tests/TrackControllerTests.inc` adds `document-controller-tracks` to the
native worker test modes. It covers cached projection, missing/stale revisions,
invalid/dry requests, append preserving all old musical cells, structural
Undo/Redo identities, ungroup routing, live mute and chronological alias history,
three pending mute frames/full-queue rejection, forced publication refusal,
save/reopen, and cache rejection before stop/history. It uses native prepared
playback with controlled feedback and manual render calls, not a hardware clock.

`windows/Tests/test_note_tracks.py` adds three actual-PID-pipe cases to the
existing Windows application CI batch: advertised methods/guards and strict
inputs; dry append retaining IDs and one-step Undo/Redo; grouping and ungrouping
with insert/send/plugin preservation; mute no-op retaining Redo and native
save/reopen; and mixed disconnected/connected destinations in both column orders.
It reuses only launch/read/write helpers, without inheriting another test suite.
The private inspection desktop is functional evidence, not foreground visual,
device/audio or reciprocal Mac evidence. The Windows API guide documents the
newly wired contracts and their qualification limits.

No P2 builds or tests have run. Add `document-controller-tracks` and
`test_note_tracks.NoteTrackAppTests` to the eventual P2 checkpoint along with the
previously listed portable, adapter and hosted tests. P1 remains frozen and its
evidence cannot qualify these changed shared/Windows inputs. Remaining P2 work:
native grouping/column headers and keyboard commands with retained drafts,
queued-frame NNA/plugin ownership coverage, and both platforms' required
fixture/persistence/audio/UI gates. Continue on temporary dependent branches
that converge on shared main; no long-lived platform fork is introduced.

## Native header presentation and shared ownership checks — source only

The grid now reserves a 22-DIP name/group band above the existing note/FX header.
Saved names and color accents identify ungrouped columns and logical group spans;
the note row shows group-local column numbers and explicit muted text. Text keeps
the native Windows font/palette contrast instead of using user colors as text.
Names/spans clip to the actual horizontally scrolled viewport. Clicking a group
span selects its existing columns at the current row; clicking a column header
selects that column without changing sound. A native Mute/Unmute button, palette
command and context-menu item perform the guarded column write. Ungroup is also
in the palette/context menu and retains routing. Both actions use Main's retained
command receipt/Review owner, including track readback for an unknown outcome.
No new parallel command-history or document-departure mechanism is introduced.

`NoteTrackPresentation.inc` owns this Windows presentation. The existing shared
gridHeader governs row drawing, graph lanes, hit testing, inline editing and
scrolling. `workspace.get` reports its value as `geometry.pattern.headerHeight`
and exposes visible header rectangles/data under `trackHeaders`. Existing mouse
tests now calculate row positions from that public geometry rather than the old
50-DIP constant; their focus, selection and invalid-hit assertions remain intact.
The new actual-app header case selects a group, toggles only its last/current
column, checks retained row/selection, undoes mute, ungroups without routing loss,
and restores the stable group through Undo. Real display, DPI and accessibility
qualification remain required; a snapshot assertion is not visual evidence.

`editor/Tests/ColumnMuteOwnershipChecks.hpp` supplies the same prepared-frame
scenario to the existing Mac `note-track` and Windows `native-audio-bus` native
provider targets. It uses real continued sample/plugin NNA notes in two columns,
equal and different pitches, 44.1/48/96 kHz and 17/128/511-frame blocks. It requires
only the muted parent's foreground/background plugin notes to release, the other
column to remain audible, sample NNA unmute to restore surviving voices, manual
audition to remain independent, and final plugin release without stuck sound.
Rendering uses each suite's existing realtime audit. Windows coverage remains
host C++ allocation/free only, without direct malloc/free or lock instrumentation;
the Mac audit retains its existing boundaries and sanitizer exclusions. The
existing Mac legacy-setter scenarios remain present beside these queued checks.

Source review corrected two new shared-candidate test expectations: Undo must
retain nextID's allocation high-water mark. Assertions still compare all native
song data, now also requiring a subsequent group/append to allocate fresh IDs.
No product ID-allocation rule or pre-existing assertion was relaxed.

All changes in this section remain unbuilt/unexecuted. The next P2 native/pipe
checkpoint must include the changed grid/context-menu/docking/shortcut and graph
lane scenarios, plus both native ownership targets. Native create/group forms,
their captured target/count/name/output drafts and runtime/fixture qualification
are still outstanding. P1 checkpoint 02 continues to use frozen `a320bd9da`.

The existing `workspace-command-result-tests` fixture also gains a real track
write whose native completion fails after a newer cursor move. It requires the
original receipt to survive, a second toggle to reject, Review to preserve the
new cursor without resending, and one Undo to restore mute. Its unknown-outcome
matrix now includes track mute: readback observes the current layout, a revision
change during acknowledgement retains review, and final acknowledgement cannot
claim verified success. These additions are likewise unexecuted.
