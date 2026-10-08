# Windows arrangement and timing

Implementation and qualification record, 2026-10-07. This checkpoint adds native
composition controls after the immutable [recording checkpoint](RECORDING_PROGRESS.md).
The broader Windows/Mac parity goal remains open.

## Implemented behavior

- **Arrange orders** opens from the sidebar or command palette. It lists every
  order, including repeated patterns, End/Skip and entries after End. Insert,
  assign, move and remove use the existing guarded shared document operations.
  Selection follows the stable occurrence ID across moves and Undo/Redo, with
  nearest-surviving fallback after removal; it is independent of cursor/playhead.
- **New / Duplicate** retain row/source drafts and append an order. The source
  is a stable pattern identity; row and capacity limits come from the actual
  module format. Duplication preserves shared native musical data, exact pattern
  signature/groove and engine name/color, and creates one Undo. Another revision
  makes a prepared creation draft stale until Reload.
- **Sequence selection** uses the existing sequence catalog and guarded API.
  It chooses a valid occurrence in the selected sequence without reusing an old
  order index. Sequence selection is navigation, with no musical Undo step.
- **Tempo and groove** provides mode, fractional BPM, ticks, beat/bar, groove,
  local swing and Straight. Preview validates and shows normalized values without
  mutation. Changed Apply stops playback and creates one document Undo. Tempo
  and ticks belong to the captured sequence; mode, beat/bar and groove belong to
  the song. Existing pattern overrides remain authoritative and are reported.
- Both tools retain raw text, focus/caret, selection and scroll through polling,
  resize, hide/show and rejected requests. Reentrant completions cannot overwrite
  newer drafts or adopt a replacement document. F6 returns to a revealed Tracker.
  Initial floating placement is clamped to the owner's monitor work area; later
  user placement is retained. The normalized preview is scrollable and read-only.
- **Play selected** deliberately starts the selected order. Ordinary workspace
  Play keeps its existing song-start behavior. Arrangement/timing edits preserve
  an unfinished recording take and report incompatibility; they do not commit or
  discard it. The workspace BPM label retains fractional precision.

Read-only snapshot additions are documented in [Api/README.md](Api/README.md):
`orderMetadata`, sequence IDs, actual format limits and independent capacities.
Catalog invalidation observes stable IDs, including same-pattern occurrence
moves. Sequence-change cache preflight charges the full identity metadata before
mutation, including exact-fit and one-byte-short boundaries.

## Final qualification

Frozen ARM64 candidate 6 passes **367/367 application tests** in **898.856
seconds**, without failures or skips, with strict outer exit **0**. It also
passes **8/8 focused application cases**, **42/42 native CTests**, the separate
**11/11 document-operation groups** and six exact offline PCM comparisons.
All **15 source-matched views** pass native bounds and visual review at **192 DPI**.

- Executable SHA256:
  `83E06DAB17094A66B79C4D971C536CBC4A7BFE879BAB2D15ACF6302D38C13EB3`.
- Build: `bin/windows-arrangement-build-6.log`.
- Full run: `bin/windows-arrangement-full-3-app-tests.log`, `-isolation.log`
  and `-exe-sha256.txt`.
- Focused run: `bin/windows-arrangement-focused-candidate6-app-tests.log`,
  `-isolation.log` and `-exe-sha256.txt`.
- Native: `bin/windows-arrangement-native-5.log`, `-details.log` and
  `-executables.json`; extra document operations:
  `bin/windows-arrangement-editing-tests-2.log` and `-2-hashes.json`.
- Visuals: `bin/ui-capture/evidence-arrangement-candidate6-final/`, including
  source/helper fingerprints and scratch reproduction files. Existing narrow
  workspace pattern-heading truncation remains a baseline limitation, not a
  completed UI-parity claim.

Checkpoint destination: `bin/windows-checkpoints/arrangement-20261007/`.
Its committed-source manifest and independent verification receipt are the
authority for packaging completion. The earlier recording package is preserved.

## Retained development evidence

- Controller regression: all three groups passed in
  `bin/windows-arrangement-controller-1.log`: identity/history/reopen,
  MOD/XM/S3M/IT/MPTM limits, and atomic sequence cache budgeting.
- Candidate 2: `bin/windows-arrangement-build-2.log` built successfully after
  correcting MSVC string/JSON comparisons and one mixed-type test declaration.
  The original compiler output remains in `windows-arrangement-build-1.log`.
- Candidate 2 SHA256:
  `A3707AAD32705CD85126C62880D1C8336386A76E220C04597ED03FC4519BEF21`.
  Native tests passed **42/42** in **20.34 seconds**
  (`bin/windows-arrangement-native-1.log`).
- The initial new application run passed **7/8**, with one setup error:
  `workspace.panel` rejected the document-write helper's injected
  `expectedRevision` parameter before its return-to-Tracker assertion. The
  fixture now calls the workspace API directly. The failed run and strict outer exit
  remain in `windows-arrangement-focused-1-app-tests.log` and `-isolation.log`.
  All completed musical, retention and silent transport cases passed.
- The corrected focused run passed **8/8** in **5.763 seconds** with strict
  outer exit 0. Candidate 3 then added initial work-area placement and native
  CRLF handling for the scrollable preview: **42/42** native tests in **20.54
  seconds** and **8/8** application cases in **5.677 seconds**, outer exit 0.
  Candidate 3 SHA256 is
  `BE8F04E359DC6A0B226FF316B92A1C20FD1E70CFF14B12B3EAA5B44E9C238DCB`.
- Candidate 3's fifteen renderings exposed two issues despite those passes:
  accepted sequence changes left the selected first order off-screen after a
  long-list scroll, and the optional workspace timing summary clipped at the
  actual native minimum. Own-command completion now reveals the current selected
  occurrence while ordinary polling retains scroll. The workspace summary shows
  only the detail that fits, and omits itself when space is too narrow. The old
  executable/source and visual issues remain under
  `bin/windows-arrangement-candidate3-source` and
  `bin/ui-capture/evidence-arrangement-candidate3-final`.
- Candidate 3's full application run passed **366/367** in **898.778 seconds**.
  Cursor-only navigation repeatedly changed the unchanged order selector's
  selection and repainted it; the existing native-control regression caught
  this. Its strict outer isolation check also failed because the foreground
  HWND changed between two processes outside the owned test PID. That change
  is unattributed. Both failures remain in
  `bin/windows-arrangement-full-1-app-tests.log` and `-isolation.log`; neither
  is treated as a qualification pass.
- Candidate 4 replaces unconditional selector updates with the existing native
  compare-before-select helper. The corrected build SHA256 is
  `470081E3B72B8AB7E6954FFF8775353C8857FEE3864297422485E3C281C2FC14`.
  Native tests pass **42/42** in **18.99 seconds**, the eight arrangement/timing
  cases pass in **5.161 seconds**, and all four native-control regressions pass
  in **1.758 seconds**. Both focused application runs have strict outer exit 0.
  All fifteen candidate-4 views passed native bounds, isolation and visual
  review at 192 DPI. Its full application run passed **367/367** in **887.782
  seconds**, but the strict outer runner exited 1 after another foreground HWND
  change between processes outside the owned test PID. The actor/cause remain
  unattributed; this is not a strict isolation pass. Both logs are retained as
  `bin/windows-arrangement-full-2-app-tests.log` and `-isolation.log`.
- A follow-up shared-code audit found that `Document::addPattern` duplicated
  cells and native metadata but lost engine pattern-specific signature/groove.
  Candidate 4 therefore remains superseded despite its passing application cases.
  Its exact original executable,
  shared source and affected tests are preserved in
  `bin/windows-arrangement-candidate4-source`; its approved visual review is
  retained for that candidate in
  `bin/ui-capture/evidence-arrangement-candidate4-final`.
- Candidate 5's first correction copied the complete pattern but treated an
  unchanged-size `Resize` result as an error. The native suite passed **41/42**
  in **22.45 seconds**; the focused suite passed **6/8** in **5.145 seconds**,
  with strict isolation passing and outer exit 1 reflecting test failure.
  `windows-arrangement-native-4.log`, its details/hash inventory and
  `windows-arrangement-focused-candidate5-*` retain the failures. The original
  source/executable are in `bin/windows-arrangement-candidate5-source`.
- Candidate 6 guards Resize with an explicit row-count comparison. Build 6
  succeeds; SHA256 is
  `83E06DAB17094A66B79C4D971C536CBC4A7BFE879BAB2D15ACF6302D38C13EB3`.
  Native tests pass **42/42** in **21.46 seconds** and focused application cases
  pass **8/8** in **5.766 seconds** with strict outer exit 0.
- The separate document-operation executable is built from
  `windows/Tests/Editing` against the same final engine libraries; it is not
  part of those 42 CTests. Its **11/11 groups** pass in
  `bin/windows-arrangement-editing-tests-2.log`, with source/library/executable
  fingerprints in `-2-hashes.json`. All six Duplicate audio comparisons are
  exactly equal and non-silent: **48 kHz, 24,000 stereo frames, 128/511-frame
  buffers, 32/64/96-row duplicates**, peak **0.190726**, maximum difference **0**.
  Tests also preserve the exact already-normalized imported groove through
  Undo/Redo, snapshots and native project save/reopen. New patterns retain their
  default timing. This is bounded offline playback evidence, not hardware output.
- That separate suite's first run passed the new Duplicate tests but failed an
  old catalog equality expectation that omitted the existing `displayCode`
  field. The test expectation now includes the documented wire field. Both
  editing build/test logs and their hashes remain preserved; no production
  catalog behavior changed for this correction.

## Remaining parity and evidence boundaries

This order list does not implement section annotation, pattern notes/color,
section navigation or the arrangement matrix. The next scopes and known shared
matrix issues are in [ANNOTATION_PLAN.md](ANNOTATION_PLAN.md) and
[MATRIX_PLAN.md](MATRIX_PLAN.md). The current-display proposal for independent
dock groups is in [INDEPENDENT_DOCKING_PLAN.md](INDEPENDENT_DOCKING_PLAN.md).
General transform review, formula completion and custom-canvas
accessibility remain larger UI work. Existing native storage identifiers and
format versions are unchanged.

Private-desktop HWND tests and renderer/native-control compositions are bounded
evidence. They do not qualify foreground presentation, other display scales,
sustained presentation, physical MIDI/audio, accessibility or reciprocal Mac
runtime. The previous recording package remains immutable and independently
usable.
