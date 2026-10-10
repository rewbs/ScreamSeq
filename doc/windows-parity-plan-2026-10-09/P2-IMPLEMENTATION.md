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
