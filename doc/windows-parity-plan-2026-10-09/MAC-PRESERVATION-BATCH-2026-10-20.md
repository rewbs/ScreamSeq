# Mac session ownership of preserved project metadata

**Mac source prepared; Mac compilation/execution remain open.**
The later [B3 qualification](SHARED-CONTRACT-QUALIFICATION-2026-10-20.md)
passes the shared codec/preservation/metadata-owner tests on Windows ARM64.
Those checks do not qualify this native Mac session integration.
This follows the [typed foundation](PROPERTY-TREE-BATCH-2026-10-20.md) at
`5f278e7c5`. B3 executed after the 05:53:34 UTC hourly gate. No codec observation
baseline is changed by this source work; the next build gate is 06:57:04 UTC.

## Implemented source behavior

`editor/project/ProjectMetadata.hpp` owns an immutable imported tree and
canonical unedited baseline. It uses the existing exact typed comparator,
canonical promotion and three-way merge. Native keyed-map paths remain relative
to the native metadata root. Plugin extensions follow `instanceID`; indexed
entities follow their stable IDs. An intentional deletion does not resurrect
the deleted entity. Undo can restore its original extension from the immutable
baseline. An ambiguous edit to an unkeyed array still refuses serialization.

The module snapshot, current plugin-state payloads and recording take are
supplied by their existing owners for each save. They are not retained as a
second long-lived comparison baseline. A discarded take cannot reappear from
the imported tree. Existing inactive recovery archives remain retained.

Mac's `Bridge/ProjectMetadata.inc` extracts the known-property encoder formerly
inside `TrackerSession::projectDataForRecovery:`. Staged Open and Save share that
encoder, including plugin location hints, assignments, buses and automation.
Open captures the typed source and baseline before stopping/replacing the
current document. `_projectMetadata` is published beside `_document` only after
the candidate is accepted, and reset on New. Saves, recovery snapshots and
Undo/Redo do not replace that baseline.

Ordinary binary input goes directly through the portable codec. Unknown
binary/date/UID/wide-integer/NaN payloads never pass through a textual JSON
conversion. XML admission remains supported through Foundation's typed parse.
A binary representation unsupported by the portable parser is **not**
silently converted through Foundation: the complete original file bytes become
inactive recovery data, the known model is recovered, and source protection
remains active. Parser and output limits are unchanged. A recovery copy that
would exceed those limits fails explicitly before replacing a destination.

When recovery actually drops invalid data, or canonical promotion cannot match
an unknown-bearing array safely, the accepted musical model becomes active and
the original metadata remains under a collision-safe `screamseqLoadRecovery`
key. This deliberately archives optional metadata together rather than trying
to transplant extensions onto partially recovered entities by position.
Original plugin states and rejected recording data remain in that archive;
the embedded module and older archives are not recursively duplicated there.
Opaque root extensions and existing archives stay retained. Legacy NF/NR
duration migration applies only to the promotion candidate, so consumed row
units cannot reappear beside canonical beat duration; a failed promotion still
archives the pre-migration original.

Save encodes the merged tree and validates its actual known musical fields,
plugin capacity, automation and stage-plugin references before the existing
atomic file write. Unknown-field tolerance is narrowly scoped to file
interpretation; API mutation validation stays strict. Offline native-project
export uses the same known-field interpretation. Failed Save leaves source
protection in place; only successful publication clears it.

The shared property-tree library is linked by `TrackerMac` and by all three
explicit Swift link commands (app, automation host and shutdown host).
Static ordering, encoder-use, link-command and diff checks are recorded in
`bin/mac-preservation-20261020/source-checks.json`; none executes the application
or compiles these changes.
No version, storage identifier, stable identity, plugin format, framework or
OpenMPT attribution changes.

## Prepared checks and bounded validation

The expanded shared `project-preservation` test covers current owned payloads,
track/plugin reordering, deletion and restoration, metadata-relative keyed
promotion, immutable repeated saves, failed staged capture, inactive recovery
archives, archive-name collisions, ambiguous promotion and pre-migration
original retention. It runs on both platforms in the existing target.

Mac `project-recovery` now also covers:

- Exact opaque binary/date/UID/float32-NaN/large-integer/signed-zero/Unicode
  values at root, native and track levels through unrelated edit/save/reopen.
- Undo and Redo after Save, failed Save, failed Open and New-document reset.
- XML import without source-file rewriting.
- Unavailable VST3 state, stable identity, original location hint and typed
  plugin extension through edit/save/reopen, without hosting that provider.
- Exact inactive original sections after partial recovery, plus the existing
  strict API, legacy-duration, malformed-rack and source-protection checks.

The old assertion that a future-field recovery copy reopens with no warnings
would assert that those fields disappeared. That case now explicitly requires
retained root fields, exact archived invalid sections and renewed compatibility
warnings. Ordinary recovered copies still require a clean reopen; no numerical,
history, source-integrity or malformed-input assertion was relaxed.

At the next cohesive build, include the shared preservation/codec tests with
the pending API integer/replay and corrected native-metadata targets. Reuse
unchanged B158 UI evidence; this batch does not change Windows UI handlers.
Mac requires the normal app and automation/shutdown host links, targeted
`project-recovery`, `native-property-tree`, shared preservation/codec tests,
recording recovery and unavailable-plugin tests. Then run the existing F01–F05
roundtrip and codec corpus on both hosts and inspect exact differences before
updating observations. Reciprocal edited outputs remain mandatory:
Mac→Windows→edit→Mac and Windows→Mac→edit→Windows, including opaque graph-node
and plugin extensions.

## Remaining risks and gates

- Mac compilation and native Mac assertions are unexecuted. Source inspection
  and `git diff --check` are not substitutes; Windows helper passes have a
  narrower scope.
- Large-file peak memory and save latency need bounded measurement: Foundation
  and portable parsing coexist during staged import/serialization, although
  module/plugin payloads are excluded from the retained baseline.
- Unsupported binary encodings may require a large complete-file recovery
  archive. Save can refuse at the existing size/resource limit; it must never
  omit that archive silently.
- Partially recovered optional metadata is retained inactive as a whole.
  More selective activation can be a later change with explicit identity
  evidence; positional guessing is not acceptable.
- Native graph-node edits, real plugin reorder/removal, current Mac/x64 builds,
  reciprocal edited fixtures and final shared-main integration remain open.
  Passing a helper test alone cannot close those gates.
- Computer use remains stopped after the user's Escape. This batch provides
  no live visual, accessibility, audible or physical-device qualification.

No new product-design decision is needed to qualify this batch.
