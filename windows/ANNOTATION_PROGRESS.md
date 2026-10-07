# Windows sections and annotations

Qualified implementation checkpoint, 2026-10-07. This report supersedes the
implementation status in [ANNOTATION_PLAN.md](ANNOTATION_PLAN.md), not the
immutable [arrangement/timing checkpoint](ARRANGEMENT_PROGRESS.md).

## Implemented behavior

Arrange has a Section column and Previous/Next section navigation. Sections are
named order occurrences, including End/Skip; repeated uses of a pattern retain
independent section identities. Navigation selects the nearest earlier/later
marker without wrapping, starting playback or redirecting the pattern cursor to
a sentinel. Both commands appear in the configurable command catalog.

The retained Section page edits an occurrence name. Pattern details edits the
shared pattern name and multiline notes. Each captures its own document, entity,
revision and draft generation; changing selection, page or window visibility
retains unfinished text. Explicit Reload captures the current selection.
Unrelated edits make drafts stale. The three pages share the existing 760×600
minimum client rather than adding controls to the main header.

`arrangement.get` and revision-guarded `song.annotate` follow the Mac contract.
Annotations support patterns, tracks, sequences and orders in any sequence, with
partial patches, Unicode limits, request-ID replay, one document Undo per change,
and native persistence. Integer-valued numeric colors follow the existing Mac
validation helper. A no-op preserves revision and Redo. Presentation-only
annotation Undo/Redo preserves playback; musical changes retain stop behavior.
Native storage identifiers and format versions are unchanged.

Document snapshots expose full entity metadata. Inactive annotations invalidate
the catalog without duplicating inactive order lists in every response. Text
edits retain immutable pattern/wave buffers. Precommit admission charges actual
UTF-8 metadata and staged structural publication sizes. The arrangement read is
bounded against the transport response limit.

## Qualification history

- Candidate 1 builds successfully. Executable SHA256:
  `E61E0DF2D4DCC5E040BFD04E922A0C8721B4D193A09979D1CF4FAE8026F36F2B`.
  The source/executable snapshot is
  `bin/windows-annotations-candidate1-source`; build evidence is
  `bin/windows-annotations-build-1.log`.
- Focused application qualification passes **14/14** in **9.663 seconds**,
  strict outer exit **0**. This includes six annotation cases (one wraps four
  controller groups) and eight existing arrangement/timing cases. Evidence:
  `bin/windows-annotations-focused-1-app-tests.log` and `-isolation.log`.
- Native qualification passes **41/42** in **20.41 seconds**. The annotation
  window fixture reports a pumped-completion draft failure. Investigation found
  the fixture used `WM_SETTEXT`, which omits `EN_CHANGE` for a multiline EDIT;
  its simulated newer input therefore did not advance the draft generation.
  The corrected fixture uses native `EM_REPLACESEL` and explicitly verifies
  generation advancement. Evidence of the original failure:
  `bin/windows-annotations-native-1.log` and `-details.log`.
- Source review identified two usability improvements now implemented:
  reveal a distant section selected through the global command, and give
  retained draft targets readable labels. It also identified structural history
  cache admission after independent plugin state grows. A shared private
  candidate reuses the actual Undo/Redo primitive; the host validates the
  restored view before stopping or committing. Its candidate retains only the
  pending history entry, as documented, rather than copying the entire stack.
- Build 2 compiles the corrected application, but its new structural-history
  controller fixture uses the local name `small`, which collides with a Windows
  header macro. The test identifier is changed to `reducedView`; production code
  is unaffected. The failed build log and original source/executable remain in
  `bin/windows-annotations-build-2.log` and
  `bin/windows-annotations-candidate2-source`.
- Build 3 succeeds with application SHA256
  `085DB141035EEA3C1EB0A8987EE65580A13C6A2A2447199BC840346828F8075F`.
  It changes only the failed test identifier from build 2. Native tests pass
  **42/42** in **20.82 seconds**; focused application tests pass **14/14** in
  **11.295 seconds**, strict outer exit **0**. The focused controller wrapper
  now includes five groups, including exact-fit and one-byte-short structural
  Undo/Redo after independent plugin growth. Evidence uses
  `windows-annotations-native-2-*` and `windows-annotations-focused-2-*` in `bin/`.
- The separate Editing build/test 1 passes 14 of 15 groups. Its linked-envelope
  fixture changed a lane point without updating the master template and was
  correctly rejected before the intended history assertion. Build/test 2 fixes
  that fixture by updating both points and retains every original assertion.
  All **15/15 groups** pass, including exact history projection for cell,
  metadata, full snapshot, sample PCM, loop geometry, splice and sample-slot
  restoration. Both runs, fingerprints and the original failing source are
  preserved under `bin/windows-annotations-editing-*`.
- All six offline Duplicate timing comparisons remain exactly equal and
  non-silent: **48 kHz, 24,000 stereo frames, 128/511-frame buffers and
  32/64/96-row duplicates**, peak **0.190726**, maximum difference **0**.
  Actual application silent-output checks also verify advancing callbacks and
  frames with unchanged playback epoch through changed annotation Apply,
  no-op, Undo and Redo; musical native history still stops playback.
- All **15 views** in
  `bin/ui-capture/evidence-annotations-candidate3-final` pass visual review at
  **192 DPI**, with no native bounds/intersection failures. The tool's actual
  initial client is **860×650 DIP**, and its minimum is **760×600 DIP**.
  The capture records 227 matching Windows/editor source files, exact production
  and scratch executable identities, all ten scene assertions and unchanged
  foreground/clipboard on a never-switched private desktop. The separate
  Editing test source was corrected afterward; it is not linked into the
  capture. Readable captured labels, raw Unicode notes, scroll retention,
  sentinel targets and stale states were inspected. Existing narrow main-header
  truncation remains visible and is not claimed fixed here.
- Full application run 1 finishes **372/373**, with one error, no failures or
  skips, in **888.429 seconds**. Its strict outer exit is **1**; foreground and
  clipboard isolation guards pass. The existing recovery document-switch test
  observes `saving=true` during immutable snapshot capture and sends
  `document.open` while the document worker is still busy. The API correctly
  rejects with `Document worker is busy; no mutation was queued`. The fixture
  now waits for `saving && !documentBusy`, placing the switch inside its explicit
  delayed disk-write phase. All completion assertions remain unchanged, and
  production/executable bytes are unchanged. The original fixture is preserved
  in `bin/windows-annotations-full-1-source`; original run evidence uses
  `bin/windows-annotations-full-1-*`.
- The corrected recovery case passes **five consecutive repetitions** in
  **9.628 seconds**, strict outer exit **0**. Evidence uses
  `bin/windows-annotations-recovery-readiness-1-*`. This supplemental result does
  not replace a passing full run.
- Full application run 2 passes **373/373** in **887.795 seconds**, with no
  failures or skips and strict outer exit **0**. The recovery case also passes
  in this complete run. Evidence is `bin/windows-annotations-full-2-app-tests.log`,
  `-isolation.log` and `-exe-sha256.txt`, all tied to the final executable above.

## Checkpoint and remaining parity

Checkpoint destination: `bin/windows-checkpoints/annotations-20261007/`.
Its manifest is the authority for packaging completion. The package retains
the application, notices, source snapshot, final qualification, original failed
runs/fixtures and visual evidence. Source provenance distinguishes exact
qualified working-tree bytes from their independently verified Git-normalized
identity; packaging does not rewrite line endings. The prior arrangement and
recording checkpoints remain unchanged.

Color editing is available through the API; this editor has no color picker.
Compact-header and native-selection visual consistency is the next small pass.
The arrangement matrix, independent dock groups, broader transform review,
formula completion and custom-canvas accessibility remain separate parity work.
Private desktop tests and renderer/native-control compositions do not qualify
foreground presentation, other display scales, sustained frame timing, physical
MIDI/audio or reciprocal Mac runtime loading.
