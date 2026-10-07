# Windows sections and annotations

Source audit and proposed next checkpoint, 2026-10-07. **Not implemented or
qualified by this plan.** The current arrangement/timing checkpoint is recorded
separately in [ARRANGEMENT_PROGRESS.md](ARRANGEMENT_PROGRESS.md). This scope adds
section navigation and retained annotation editing to that native workspace;
the arrangement matrix and block-copy workflow remain a later checkpoint.

## Existing semantics to preserve

[Mac OrderEditor](../mac/App/OrderEditor.swift) has a Section column, Previous /
Next section actions, a section-name field with Set section, and pattern name /
notes with Save details. Previous/Next chooses the nearest named order strictly
before/after the selected occurrence, without wrapping. Section names belong to
individual order IDs; pattern details are shared by every occurrence of that
pattern. End/Skip orders can have section names but have no pattern details.

The authoritative [Mac API implementation](../mac/Bridge/TrackerSessionAPI.inc)
already provides:

- `arrangement.get {}`: `{sequence, sequenceID, orders, sections}` for the active
  sequence's complete untrimmed order list. Each order contains
  `{id,name,annotation,color,order,pattern}` and `patternID` only when the pattern
  exists. Each section contains `{id,name,firstOrder,lastOrder,color}`; a nonempty
  order name starts a range ending before the next named order or at the final
  order. Unnamed leading orders do not create an implicit section. Whitespace
  names count as nonempty; do not silently trim stored names.
- `song.annotate {expectedRevision,id,...}` patches one existing pattern, track,
  sequence, or order entity, including entities in other sequences. At least one
  of `name`, `annotation`, `color` is required; omitted fields remain unchanged.
  Name/annotation limits are 256/4096 UTF-16 code units, color is an integer
  `0..0xFFFFFF`, and an empty name removes a section marker. Sample/instrument
  IDs are not valid targets for this method. The result is the complete
  `{id,name,annotation,color}` entity. There is no `dryRun` parameter.

[Document::annotate](../editor/TrackerDocument.cpp) stages and validates a native
metadata copy, creates one document Undo entry only when it differs, and leaves
revision/redo unchanged on a no-op. The Mac wrapper does not stop playback for
Apply. Stable IDs and structural edits already move/remove the corresponding
order entity; Undo restores it. Use these shared primitives, not new Windows
section objects or numeric-slot identity.

[NativeMetadata.cpp](Project/NativeMetadata.cpp) already encodes/decodes all four
entity fields, validates UTF-8/NUL and UTF-16 limits, and preserves them in current
native projects. [NativeProject.cpp](Project/NativeProject.cpp) requires container
6 / metadata 17; no format change is needed. Historical version-3/migration text
in the annotations section of [mac/AUTOMATION.md](../mac/AUTOMATION.md) is not the
current storage contract. Windows saves native `.screamseq`/`.resonance` projects
and rejects module destinations; do not add a lossy annotation downgrade.

## Required Windows adapter work

1. Add `arrangement.get` and `song.annotate` to
   [DocumentOperations](Session/DocumentOperations.cpp), with normal worker
   revision validation, strict unknown-key/type checks, canonical project-local
   IDs, UTF-16 limits, and atomic invalid/read-only rejection. Register both
   through [SessionAdapter](Api/SessionAdapter.hpp) / the application host so
   dispatch and `api.describe` agree. Reuse the existing shared schema entries
   in [resonance-api.schema.json](../mac/Tools/resonance-api.schema.json); document
   the newly supported Windows methods in [Api/README.md](Api/README.md).
2. Extend [DocumentController](Session/DocumentController.cpp) snapshots:
   `orderMetadata` currently contains IDs only; add name/annotation/color.
   Patterns currently expose ID/name but need annotation/color. Expose track
   entities with indices so every supported target can be discovered. Keep
   sequence IDs and prefer nonempty native sequence names over imported engine
   names, matching [TrackerSession.mm](../mac/Bridge/TrackerSession.mm); sequence
   annotations do not rename the underlying engine sequence. Include sufficient
   entity details for a client to retain and inspect its own edits.
3. Keep catalog invalidation sensitive to these fields without rebuilding cell
   or waveform buffers for text-only changes. Charge actual snapshot growth
   **before** committing annotations, including Unicode bytes and new JSON
   fields. Revisit sequence-switch, duplicate-pattern and history preflights:
   their current accounting must cover the expanded metadata and exact-fit /
   one-byte-short cases. Bound arrangement response construction and preserve
   the normal transport reply-size contract.
4. Apply must use `Document::annotate` without stopping or replacing playback.
   **Current Windows Undo/Redo stops for any native metadata difference** in
   `DocumentOperations::invoke`; uninterrupted annotation history is proposed,
   not a current guarantee. If implemented, exempt only proven presentation-only
   field changes: require identical entity IDs/container structure and every
   other native musical field. Keep the existing stop path for automation,
   precise notes, mutes, routing, graph, or mixed/structural history. Do not remove
   the broad native-change guard without a narrow, tested replacement.

## Native interaction proposal

Extend the existing [ArrangementWindow](App/ArrangementWindow.hpp) and
[SongTools integration](App/SongTools.inc), without more main-header controls.
Keep the virtualized order list and add a Section column. Put Previous/Next
section beside list navigation; use the existing stable occurrence selection
path, keeping playback independent. Sentinel selection must not navigate to a
nonexistent pattern. Expose the actions in the configurable command catalog;
native text editing retains its keys.

At the existing 760x600 minimum client, use a compact details page/tab in the
lower part of Arrangement for Section and Pattern details, retaining the
existing order/creation page and all of its drafts. Use a multiline native EDIT
for notes and explicit Set section / Save pattern details / Reload actions.
Measure real native bounds after control inflation at the actual DPI; do not
increase the crowded main header or shrink the order list to a token viewport.
Color is required API parity; a color picker is optional for this bounded UI
checkpoint because Mac OrderEditor itself does not expose one.

Capture document ID, sequence/order or pattern ID, revision and draft generation
separately for section and pattern edits. Navigation/polling/reopening must not
overwrite raw text or redirect an Apply to the newly selected occurrence.
Show a captured-target/stale indication and explicit Reload; a completion may
adopt canonical values only if target and draft generation still match.
Preserve caret, selection, scroll and focus across resize, page changes and
pending calls. Keep Return/F6 revealing Tracker, local Close/Escape behavior,
and existing creation/timing drafts intact. These draft requirements improve
on Mac's current `updateDetails()` refresh behavior rather than copying it.

## Meaningful qualification boundary

- Worker/API: all target types, partial patches, color clearing, Unicode boundary
  values, canonical/unknown/deleted IDs, invalid types/keys/NUL, stale/read-only
  rejection, no-op preserving redo/revision, and precommit cache-budget failure.
  Verify annotations leave cells, precise events, envelopes, routing and other
  entities exact. Exercise actual pipe dispatch and request-ID replay.
- Sections: repeated patterns with distinct order IDs; unnamed prefix, adjacent
  markers, last marker, End/Skip and slots after End; insert/move/remove and
  Undo/Redo; sequence switch and strict previous/next endpoints. Pattern details
  must appear on every use without conflating their section names.
- Native persistence: save/reopen full fields and stable IDs, plus annotated
  duplicate behavior; compare existing Windows codec fixtures. Reciprocal Mac
  runtime loading remains a separate platform qualification gate.
- Actual HWND workflow: independent section/pattern drafts, stale/replacement
  rejection, newer text during a pumped completion, native caret/clipboard keys,
  hide/reopen/resize, section navigation without playback relocation, and compact
  Return with a covered Tracker. Check default/minimum geometry, long Unicode
  labels and multiline notes with source-matched rendered evidence.
- Owned silent playback: changed annotation Apply and no-op keep playback epoch
  and advancing callbacks. If history classification changes, annotation-only
  Undo/Redo must also keep playback while a musical native-history case still
  stops. Run affected tests, full native/app qualification and strict outer
  isolation checks on the frozen candidate; preserve failed evidence honestly.

Existing [Mac socket regression](../mac/Tests/test_automation.py)
`arrangement_tools` and Windows
[NativeMetadataTests](Tests/ProjectNative/NativeMetadataTests.cpp) provide useful
starting coverage, not proof that the missing Windows wrappers/UI already work.
