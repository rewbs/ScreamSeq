# Precise Notes editing parity — 2026-10-07

This phase extends the retained native editor qualified in
[PRECISE_NOTE_HOST_PROGRESS.md](PRECISE_NOTE_HOST_PROGRESS.md). The source is
integrated and independently reviewed. Corrected candidate 3 passes **50 native
tests, 82 focused application tests and 404 full application tests**. Eighteen
source-matched private-desktop views pass mechanical checks and two independent
original-resolution reviews. This completes the bounded editing phase; full
Windows/Mac parity remains open. See [the stopping-point report](PARITY_HANDOFF.md).

## Implemented behavior

- Offset accepts finite musical fractions, including signed/exponent numeric
  components and surrounding whitespace. Hit editing, unit conversion and
  Details use the same parser. Zero denominators, nonfinite values and offsets
  outside the current row fail without changing the song. Other numeric fields
  retain their existing grammar.
- An empty row captures the selected compatible sample or instrument by stable
  catalogue identity. Later chooser movement cannot redirect the pending draft.
  Existing ordinary and precise events retain their own instrument, including
  zero; a sample selection is not reinterpreted as an instrument slot.
- Reload retains the selected timestamp/pitch when the same document, stable
  pattern, stable track and row are recaptured. A different target cannot borrow
  that selection. Failed or superseded reads retain the prior draft.
- The bottom focus indicator uses the existing readable editor titles.

All musical writes continue through the existing revision-guarded
`pattern.notes.set` path. Check is nonmutating; Apply preserves untouched events,
creates one Undo step, and uses the shared native project format. This phase
does not change the API schema, document model or serialization version.

## Source and planned qualification

Base commit: `dfeb8548c6734fb44d5b58b8c5c1ffb78c3462fe`.
Eight integrated files exactly match the reviewed proposal after-images. Five
base files required only CRLF/LF normalization after committed-content and
Git-clean identity checks. Original proposal bytes and both applied patches are
preserved under `bin/precise-note-parity-integration/`; the integration receipt
has SHA256 `3F6B4A40B7476B4D009D18E4D38DB24EAFBA3E5018E9DB82893B1A66B49DCB93`.

Independent static inventory finds **404 application methods** across 58 modules
and 62 classes, **82 methods** in the existing ten focused modules, and **14
native Precise Notes groups**. These are inventory counts, not pass results.
The seven previous precise-note app methods retain all original assertions;
the additional method covers captured sound, history and native persistence.

Candidate 1's exact 5,242-input source snapshot is
`bin/precise-parity-source-1-9d01f7b960a946ce9f9cb3aff58749f7/`, with manifest
SHA256 `AF09A1E5469EB75168A824F6BBC5345BCD1ACF246F1877C6CF4F13604D49FFC5`.
Its build passes without source drift; App SHA256 is
`6F8F6D5F2E63B236A481532105B230C3A2CA7E23C4D43E9F9D2B18D63F6D5B75`.
The full native run passes 49/50 in 101.61 seconds. Precise Notes passes its
first thirteen groups, then fails the new same-row Reload scroll assertion.
Foreground, clipboard, musician process and source/binary identity guards pass;
the outer result correctly fails. The complete run and all 56 candidate binaries
are preserved under `bin/windows-precise-parity-native-1-full-*` and
`bin/precise-parity-build-1-binaries/` before any rebuild. No application or
visual run qualifies this candidate.

Candidate 2 preserves scroll during pending focus restoration, but the focused
native rerun fails earlier: its new diagnostic observes top 600 becoming 601
when the focused list is disabled at request entry. All thirteen preceding
groups pass; all isolation and identity guards pass. Its App SHA256 is
`F2CBFDDF47C763EA1FA87B783AF845B0652F21930533875020C6DBDFF27D5B70`.
The exact snapshot, focused run and 56 binaries are preserved under source/build
label 2, `bin/windows-precise-parity-native-2-focused-*` and
`bin/precise-parity-build-2-binaries/`.

Candidate 3 extends the same narrow viewport guard across pending entry as well
as completion; it changes no tests or musical semantics. Exact before/after
images and the patch are in `bin/precise-parity-pending-scroll-fix/`. The unchanged
scroll assertion passes: top is 600 before, during and after Reload, selected hit
602 survives and the same native list regains focus. All fourteen owner groups
pass, then the full native suite passes **50/50 in 101.85 seconds**, with strict
outer isolation and zero source/binary drift.

Candidate 3 source: `bin/precise-parity-source-3-7dc9888a1433408fb7b502651f804d38/`.
Its 5,242-input manifest SHA256 is
`BA5D93C056547A799BD675844D37D529327DCAB02815C06DEF28645E37BC8BCB`;
App SHA256 is `7295FB90A4A27875E4E331A347DE7E57A324823F46EE87905CE4F1EC68FB1008`.
Focused application run 2 passes **82/82 in 150.469 seconds**, with strict outer
success and unchanged production, Python and compiled-source bytes. Full
application run 1 passes **404/404 in 1,027.617 seconds**, without skips or failures,
with strict outer success, zero source/Python/binary drift and the musician's
process unchanged.

Focused application run 1 finishes 81/82 in 150.777 seconds. The additional
captured-sound case reaches its second save after the musical/history assertions,
then the API correctly rejects replacement of the same owned temporary file
without `overwrite:true`. The fixture now supplies that documented flag for its
second save; all assertions and production bytes are unchanged. Before/after
Python, patch and correction receipt are preserved in
`bin/precise-parity-save-fixture-fix/`, and focused run 2 uses the corrected bytes.

## Visual qualification and checkpoint

The final capture is `bin/ui-capture/evidence-precise-parity-3b/`, using the frozen
`precise-parity-build-3-first` scratch executable with SHA256
`B825B1FF7D41C70C139917F5264C3EF09E53E6C1AAEA2A7C44D39F8823B13089`.
It retains the prior sixteen scenes and adds fraction timing Details and captured
empty-row sound. All eighteen state/geometry checks and reproduction archival
checks pass; the strict runner reports unchanged foreground, clipboard, musician,
production/capture executables and all input hashes. Both reviewers verified all
final PNG bytes exactly match the directly viewed original-resolution images from
the first attempt. The combined review SHA256 is
`E9E72D72320BA855C25534C9F78A09DFAC74B7254302D19B4A8C3EF00B8D46B9`.
The unchanged validator accepts that final review with no open visual issues.

Two failed capture attempts remain history. The first completed all runtime
checks, but reproduction archiving hit a 263-character Windows path; an attempted
exact-byte long-path copy did not complete validation. The next short-path run
rendered its scenes but failed strict foreground isolation as the observed
foreground changed from Windows Terminal to Codex. Its child exited and its other
identity/clipboard/musician guards passed. Neither attempt qualifies the final
build. A new short-path run using the same frozen executable independently passes.

Qualification uses a separate build, disposable projects and owned private
desktops. It does not replace or control the musician's running application or
change system audio defaults. The images cover recorded sizes at **192 DPI**;
the original short/floating Details selection is mechanically retained but its
heading is scrolled out of view. The added fraction scene visibly highlights its
heading. Mac runtime, other display scales, physical MIDI/audio and sustained
foreground presentation remain separate claims.

Checkpoint destination:
`bin/windows-checkpoints/precise-note-parity-20261007/` in the primary checkout.
Its completed manifest and independent verification receipt establish immutable
publication. Failed source/build/test/capture history, exact compiled source,
test inputs, helpers and reviews are retained. The unintegrated Mixer proposal
and later note-track audit remain separate preparation, outside this package.
