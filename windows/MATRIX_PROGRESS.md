# Windows arrangement matrix

Implementation and qualification, 2026-10-07. Qualification is in progress;
this report does not declare a packaged checkpoint or full Windows/Mac parity.

## Implemented behavior

Song → Arrangement matrix and block copy, the command palette and Arrange's
Matrix button open one retained native window. It shows 64 order occurrences by
12 tracks, with paged orders/tracks, section and pattern labels, sixteen density
bins per block, and pitched-note/all-event counts. Precise-note-only and extra-FX
blocks are visible. Repeated pattern occurrences retain distinct order identities;
End and Skip slots cannot be opened or copied as musical blocks.

Selection stays independent of the editing cursor and playback. Open navigates
to the selected occurrence, row zero, selected track and note column, detaches
follow, reveals the tracker and returns keyboard focus. Arrow keys, Enter,
Ctrl-C/V, F5 and F6/Escape support navigation, copy/paste, refresh and return.
The native list also supplies text descriptions of each block's event layers.

Copy retains document, revision, sequence, occurrence and track identity. External
edits or document replacement mark that source stale without replacing it.
Refresh, polling, resize and hide/reopen preserve captured source and paste
options. Explicit recopy captures a new source. Successful Paste clears the source,
matching Mac behavior; rejected requests preserve it. Track paging reveals the
new selected track. Failed reads are quiet for an unchanged context until an
explicit retry or changed document/revision/sequence.

Overwrite, Merge and Mix use the shared arrangement operation. Independent copy
clones a destination used by multiple occurrences or sequences, preserving exact
engine signature/groove, name/color and native pattern metadata. Copy now handles
precise note-on/off events and all unified FX columns as well as tracker cells.
The existing format remains container 6 / metadata 17. Changed copies are one
document Undo and survive native save/reopen.

The shared operation builds and validates the full prospective document before
stopping playback or modifying the live song. Windows adds exact view-cache
admission at that boundary. Native-only changes count as changes even when no
tracker cell changes. Exact same-pattern/same-track copies, including different
occurrences of that block, preserve revision, IDs, Redo and playback. Collision
and clipping rules are documented beside `ArrangementCopy` and in
[the API guide](../mac/AUTOMATION.md).

Windows and Mac adapters use one shared native-aware density summary. API pages
are bounded to 128 orders × 32 tracks and an explicitly checked serialized
response budget. Event counts and density bins use unsigned 32-bit values;
distinct-pattern summaries are reused for repeated occurrences. Mac source has
matching adapters/schema and tests, but Mac runtime has not been exercised here.

## Candidate identity and measured evidence

The isolated worktree is `arrangement-matrix-core`; the separate ARM64 build is
`bin/windows-matrix-core`. The candidate-3 source snapshot is rooted at
`062f9d2de77830b2bc70285224e385dbcdd1d9d0` plus the retained matrix diff. A subsequent
label-only UI correction puts selected/copied order and track identity before
long optional names. Current candidate-4 application SHA256:

`0944532EC88D6C2B3D63185E974258A410C28D4F084543BF5CA402846F4E9C10`

Candidate 3, used by the initial native and focused actual-app runs, is retained
with SHA256 `73CE124D290FF81188FE4C27024E019DE89FEAA0089B0424AEAA95ACD3EF9A45`.
The controller-fixture correction and render-probe build did not change that
application. Final controller SHA256:

`A69612BB0D0FFA5BC59897C11D5AF41EAE894FF832659AC2BAE6D9EC1C9B447C`

| Evidence | Result | Retained log |
| --- | --- | --- |
| Shared copy/density/renderer, candidate 2 | 9/9 groups | `bin/windows-matrix-core-tests-2.log` |
| Complete native suite, candidate 3 | 44/44, 32.18 seconds | `bin/windows-matrix-native-3.log` |
| Actual app API and HWND matrix cases | 11/11, 11.802 seconds; strict outer exit 0 | `bin/windows-matrix-focused-1-app-tests.log`, `-isolation.log` |
| Corrected controller matrix admission/history | 3/3 groups | `bin/windows-matrix-controller-4.log` |
| Separate document Editing suite | 15/15 groups, six exact nonzero PCM comparisons | `bin/windows-matrix-editing-tests-2.log` |
| Candidate-4 matrix HWND fixture | 6/6 groups, including long-name identity visibility at 192 DPI | `bin/windows-matrix-label-native-4.log` |

Shared rendered comparisons use native-only notes, extra FX and an independently
cloned target at 48 kHz, 24,000 stereo frames, and 128/511-frame buffers. Source and
copy match exactly: peak 0.103498, maximum difference 0. The no-FX control differs
by 0.0444261, demonstrating an audible effect rather than two silent renders.
Both buffer partitions match. This is offline renderer evidence, not device output.

The actual-app cases exercise named-pipe dispatch/serialization, replay/stale
guards, all copy modes, clipping, counts over 65,535, native-only independent
copies, one Undo, save/reopen, minimum 900×620-DIP geometry, native keyboard use,
retained options/source, track paging, and tracker reveal/focus. A silent-output
case verifies advancing callbacks/frames through navigation and exact self-copy;
a changed Paste stops playback through the existing musical publication path.

The separate Editing comparisons cover 32/64/96-row native pattern duplicates,
each at 128/511-frame buffers, 48 kHz and 24,000 frames: peak 0.190726 and maximum
difference 0. Full application and source-matched visual evidence are still
pending. Packaging and primary-checkout integration are also pending.

## Retained earlier outcomes

The first shared fixture passed 6/9 groups. Two fixtures used `CMD_VOLUME`, which
the MPT format rejects, and the audio fixture omitted explicit precise-note
preparation. Supported tracker commands and precise preparation corrected the
fixtures; nonzero peak, exact copy, FX-control and buffer-invariance gates stayed
in place. The original logs, source snapshot and test executable are retained.

The first controller run passed two groups and failed the cache-budget group.
After intentionally setting a one-byte-short budget, the fixture attempted
`document.save` to inspect native IDs; save independently reserves 256 KiB of cache
headroom. The corrected fixture reads an immutable recovery serialization instead,
then compares decoded native state and unchanged view/stop/history state. The
one-byte rejection and exact-byte admission assertions remain unchanged. The
original failure, executable, source and scratch data remain retained.

The first separate Editing run passed 14/15 groups and all six PCM comparisons.
Its strict API inventory expected the old read/write lists. Adding the two matrix
methods to those expected lists corrected the fixture; the next run passes all
15 groups. The earlier inputs, executable and failure remain retained.

The first focused-run launcher stopped before launching an app because its unused
renderer-probe environment path did not exist yet. The launcher now constructs
the explicit path, and the matching probe was subsequently built with
`SCREAMSEQ_BUILD_RENDER_PROBE=ON`. No failed test was relabeled as a pass.

The first fifteen source-matched matrix views passed geometry/state checks but
failed visual review: very long pattern/section names hid the copied track number.
Candidate 4 puts order, track and pattern numbers before optional names and adds
a native-font minimum-width regression. The original views remain marked as a
visual failure. The first full application run was intentionally interrupted for
this correction, with its partial log and interruption record retained; it is
not a completed or passing full run.

## Boundaries and next work

All app runs use owned, never-switched private desktops, explicit PID pipes and
disposable projects/recovery storage. They preserve foreground and clipboard;
the musician's existing arrangement-checkpoint process is not changed. Native
control and private rendering checks do not establish foreground presentation,
accessibility, multiple-scale behavior or sustained frame timing.

Local drag-copy is not implemented; the buttons and keyboard provide block copy.
Mac runtime reciprocity, x64, broader plugin/hardware and long-session gates remain
open. The next workspace implementation is
[independent dock regions](INDEPENDENT_DOCKING_PLAN.md). Full parity remains active.
