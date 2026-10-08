# Windows arrangement and timing — preimplementation plan

Implementation now exists in the native tools and workspace integration. See
[ARRANGEMENT_PROGRESS.md](ARRANGEMENT_PROGRESS.md) for current qualification.
The audit below records the earlier source boundary and planned acceptance
criteria; its "not implemented" descriptions are historical.

Source audit and proposed implementation boundary, 2026-10-07. This is the next
UI checkpoint after [recording](RECORDING_PROGRESS.md), not a claim that these
Windows controls are implemented or qualified. Preserve the recording checkpoint's
evidence when implementing this checkpoint.

The musician can choose an existing pattern or order in Windows, but cannot
create or duplicate a pattern, arrange orders, select a song sequence, or edit
tempo and groove through the native workspace. Most required musical operations
already exist behind guarded Windows APIs. Connecting them to retained native
tools closes a basic composition workflow without introducing a second musical
implementation.

## Current source boundary

| Workflow | Mac source | Current Windows source |
|---|---|---|
| Complete order list and insert/assign/move/remove | [OrderEditor.swift](../mac/App/OrderEditor.swift), wired by `arrangeOrders` in [main.swift](../mac/App/main.swift) | [WorkspaceView.inc](App/WorkspaceView.inc) creates pattern/order combos; choosing an order navigates to its pattern but does not retain an independent selected order. [WorkspaceCommands.inc](App/WorkspaceCommands.inc) has no arrangement commands. |
| New and duplicate patterns | Shared [Document::addPattern](../editor/TrackerDocument.cpp), exposed by the Mac application | [DocumentOperations.cpp](Session/DocumentOperations.cpp) already exposes `pattern.create`; there is no native entry point. |
| Selected sequence | Mac order editor's sequence picker | `sequence.select` exists in [DocumentOperations.cpp](Session/DocumentOperations.cpp); [DocumentController.cpp](Session/DocumentController.cpp) publishes current sequence and sequence names. No native chooser invokes it. |
| Tempo, ticks, signature and groove | [SongTimingEditor.swift](../mac/App/SongTimingEditor.swift), [SongTimingIntegration.swift](../mac/App/SongTimingIntegration.swift) | [TimelineOperations.cpp](Session/TimelineOperations.cpp) implements reads, validation, preview and one-Undo Apply. [WorkspaceDraw.inc](App/WorkspaceDraw.inc) only draws tempo/speed text. |
| Named sections, annotations and arrangement matrix | [OrderEditor.swift](../mac/App/OrderEditor.swift), [ArrangementMatrix.swift](../mac/App/ArrangementMatrix.swift), [TrackerSessionAPI.inc](../mac/Bridge/TrackerSessionAPI.inc) | Stable entities exist in shared native data, but Windows does not expose `song.annotate`, `arrangement.get`, `arrangement.matrix` or `arrangement.copyBlock`. These are a separate extension below. |

This scope follows the [UI philosophy](../doc/RESONANCE_UI_PHILOSOPHY.md): keep the
pattern reachable, distinguish inspected target from focus/playhead, retain
drafts, and make every operation keyboard-accessible. Native UI and device work
stay in `windows/`; edits and storage continue through shared
[document primitives](../editor/TrackerDocument.cpp) and
[song timing](../editor/SongTiming.cpp).

## Existing API contract to reuse

All writes below carry `expectedRevision` in the ordinary request envelope. The
adapter and document worker both guard it. The revision includes document
generation, musical revision, selected sequence and plugin revision. A sequence
change therefore invalidates a previously prepared timing/order request despite
not allocating musical Undo. Success returns the ordinary `revision`, `changed`,
`playbackStopped`, `documentId` and `data` envelope. Do not send an old draft with
the latest revision simply to bypass a stale failure. See
[SessionAdapter.hpp](Api/SessionAdapter.hpp) and
[DocumentController.cpp](Session/DocumentController.cpp).

| Method | Parameters in addition to the write revision | Current behavior and limits |
|---|---|---|
| `document.get` | `{}` | Publishes `orders` as pattern numbers, `patterns` with index/rows/stable ID/name, current `sequence`, and `sequences` with index/name. It currently omits order stable IDs and annotation/color fields. |
| `pattern.create` | Required integer `rows`; optional integer `source` naming an existing pattern | Uses format-specific row/capacity limits, the first available pattern slot and shared `Document::addPattern`. Appends one order in the current sequence; it does not insert beside the selected order. Duplicate copies up to the requested row count, assigns a fresh pattern identity and uses the shared native-cloning logic. Returns `data.pattern`. No `dryRun` parameter. |
| `order.edit` | Required integer `order`, required `operation`; `pattern` is optional in the API and defaults to 0 | Operations are `before`, `after`, `assign`, `up`, `down`, `remove`. The UI must supply its chosen pattern explicitly for insert/assign. It must name a valid pattern; this API does not insert End/Skip markers. Order must already exist, including for insertion. Moving beyond either edge and removing the last remaining order fail. Insertion creates an order identity, moves carry it, and assign preserves it. Same-pattern assign is a no-op preserving playback, history and redo. No `dryRun` parameter. |
| `sequence.select` | Required integer `sequence` in the current catalog | Selects an existing sequence, stops playback only when changed, and retains music/Undo/Redo. It requires an editable document. It does not create, rename or remove sequences. Returns empty `data`. |
| `document.timing.get` | `{}` | Returns `mode`, `tempo`, `speed`, `rowsPerBeat`, `rowsPerMeasure`, normalized `groove`, selected `sequence`, `patternOverrides` indices and `grooveActive`. |
| `document.timing.set` | Optional `mode`, `tempo`, `speed`, `rowsPerBeat`, `rowsPerMeasure`, `groove`, `dryRun` | Mode is `classic`, `alternative` or `modern`; finite BPM 32..512, integer ticks 1..31, rows/beat 1..32, rows/bar 1..128 and at least rows/beat. Groove is empty or one finite weight 0.25..4 per row of a beat, at most 32 entries, normalized by shared code. Groove requires modern timing; an unchanged existing legacy groove is tolerated, but switching to legacy requires explicitly clearing it. Returns `before`, `after`, `wouldChange`, `dryRun`. |

Timing BPM/ticks belong to the selected sequence; mode, beat/bar and groove apply
to the song. Pattern overrides remain authoritative and must be reported, not
silently replaced. Changed Apply stops playback and makes one document
transaction. Preview and no-op Apply preserve transport, revision and history.
Structural operations validate an exact candidate before touching live playback;
the UI must retain that ordering by using the existing worker path.

The existing `document.patch` also changes title/tempo/speed/channel count, but
channel resizing and a general song-properties tool are not needed for this
checkpoint. Do not mix its broader controls into the timing form.

## Proposed native UI

Add retained modeless **Arrange orders** and **Tempo and groove** tools using
[NativeToolWindow.hpp](App/NativeToolWindow.hpp), with direct workspace entry
points and searchable palette commands. Reopening raises the existing controls
without replacing their fields. The tools need not wait for a new docking
topology; preserve pattern access and make later docking possible.

### Proposed workspace entry points and fit

These placements are source-review proposals, not implemented or measured UI.
Use the left sidebar's **PROJECT** caption row for adjacent **Arrange…** and
**Timing…** native buttons, retaining the project title and channel count below.
Keep the existing pattern/order choosers at their full sidebar width. This gives
both tools a visible entry point without extending the crowded transport/header
rows or taking width from the pattern and lower editors. Do not append another
row below the Sound chooser: at minimum height it would compete with the footer.

Qualify this arrangement at the usual 1057×719 workspace and the actual minimum
client produced by the current main-window outer minimum (900×620 DIPs without
an editor dock; docking has a larger height requirement). Those are different
measurement spaces. All proposed design-DIP bounds must be measured after
[NativeControls.hpp](App/NativeControls.hpp) applies its sizing rules, with real
fonts, native child HWNDs and the actual DPI. If the two labels do not fit that
caption row, revise their measured widths rather than shrinking the native hit
targets or overlapping the title. No exact button coordinates are qualified yet.

Add searchable palette commands **Song / Arrange orders** and **Song / Tempo and
groove**, initially without new default key bindings. **Pattern / New…** and
**Pattern / Duplicate…** should open the retained arrangement form with an explicit
captured source/row-count draft; opening a command must not create a pattern.
Reopening an existing form must preserve its draft rather than silently replace
it with the newly focused pattern. Keep these tools modeless and floating for
this checkpoint; adding another side/lower dock would reduce the existing native
editors' usable width.

The workspace tempo label currently converts BPM to an integer in
[WorkspaceDraw.inc](App/WorkspaceDraw.inc). Display fractional BPM truthfully when
connecting timing edits, so the main workspace agrees with the saved timing form.
Preserve the current title/status space in both header rows.

Arrange orders should use an owner-data native list with Order, Pattern/name and
Rows columns, sequence picker, count, and a clear selected-order indication.
Below it place a pattern picker and Assign, Insert before/after, Move up/down and
Remove. A compact New/Duplicate row contains row count, source pattern and two
explicit actions. State clearly that creation appends an order, including when
duplicating; do not secretly compose several edits into a misleading single
action. Format limits come from the document/worker rather than hard-coded UI
assumptions. If current snapshots lack them, add bounded read-only capability
fields without changing musical storage.

Use native End/Skip labels for existing sentinel orders and a dash for unavailable
pattern rows. Those entries may be selected and rearranged, but must not attempt
invalid pattern navigation. Do not offer creation of unsupported sentinel orders.
Large lists must remain scrollable and keyboard-navigable without creating one
child HWND per order. A proposed starting client is 860×650 DIPs with a responsive
760×600 minimum; these are design targets to verify, not measured qualification.
Keep the sequence/count row above a flexible-height list, followed by three
bounded action rows: pattern/assign/insert, move/remove/play-selected, and
rows/source/new/duplicate. Reserve a wrapping error/status area independently of
the list and action rows rather than growing the window for long errors.

Tempo and groove should retain the mode, BPM, ticks/row, rows/beat, rows/bar,
comma-separated groove durations and local swing-percentage draft. Use Preview,
Apply and Reload, visible scope/override information, and bounded wrapping error
text. Match Mac's local Set swing behavior: even rows/beat 2..32, first-row share
12.5..87.5%, alternating weights `percent/50` and `2-percent/50`, and modern mode.
Straight clears only the local groove draft until Apply. A proposed 660×560
client is the starting point; verify real HWND bounds before choosing the minimum.
Use labelled mode/BPM/ticks/beat/bar fields, a full-width wrapping groove field,
a local swing/Straight row, and separate normalized-preview/scope and error areas
above the Preview/Apply/Reload row. Set the eventual minimum using client bounds
and clamp initial floating placement to the monitor work area.

## Retention, selection and worker boundaries

- Keep document ID, sequence, selected order, pattern ID, captured revision and
  pending generation explicit. A pattern may occur in several orders; its
  pattern number cannot identify the selected occurrence. Add a bounded read-only
  order-ID catalog from existing shared native sequence entities, or use an exact
  revision-bound ordinal and reject stale selection. Do not infer an occurrence
  by always choosing the first matching pattern. Stable order IDs are preferable
  for retaining selection after moves and unrelated edits.
- Replace the current chooser's incidental ordinal retention with shared,
  explicit selected-sequence/order identity. The order handler in
  [WorkspaceView.inc](App/WorkspaceView.inc) navigates to a pattern, while
  [EditingView.inc](App/EditingView.inc) currently restores the old combo ordinal
  after rebuilding it. Neither identifies a repeated pattern occurrence after
  rearrangement. Resolve both the arrangement list and workspace order chooser
  from the retained identity, independently of the selected pattern. Existing
  End/Skip entries can retain selection without navigating to an invalid pattern.
- Own-command selection is deterministic: insert selects the inserted order,
  move follows that order, assign keeps it, remove chooses the nearest surviving
  order, and creation selects the appended order. Undo/Redo re-resolve the retained
  identity or visibly fall back when it no longer exists. Sequence changes choose
  a valid order in the new sequence without reusing an old ordinal blindly.
- Keep order selection, edit cursor, playback order and keyboard focus distinct.
  Selecting a valid order may deliberately navigate the pattern and detach follow;
  polling/playback must not keep changing that selection or steal focus. Preserve
  repeated-pattern occurrence through native chooser updates. An explicit
  **Play from selected order** can use the existing `transport.play` `order`
  setting. Do not silently change Windows' current Play-from-beginning behavior.
- Preserve raw numeric/groove text, native caret/selection, list scroll and pending
  edits across polling, resize, hide/show and rejected requests. A timing draft
  remains bound to the captured sequence/revision. Another edit, sequence change
  or replacement document makes it stale; show Reload instead of auto-rebasing.
  Reload is the explicit draft replacement action.
- Validate all fields before dispatch; disable only the relevant pending actions
  and reject repeated commands while busy. Keep captured document/target/request
  generation across pumped waits, ignore obsolete callbacks, and restore focus
  only when the original visible/enabled control and active tool still qualify.
  Preserve native edit/list/combo keyboard and context-menu behavior.
- Opening either tool should return from its branch in the workspace command
  handler before the generic final `SetFocus(window)`, as existing retained-tool
  open commands do. Reopening preserves the focused native field and its caret.
  Proposed local bindings are Enter on the order list for deliberate navigation
  and Ctrl+Enter in timing for Apply; Play selected remains an explicit action.
  Delete/move commands belong to the order list and must yield to text fields,
  combos and open combo popups. Route global transport/Undo and shortcut prefixes
  through the existing tool shortcut owner; do not add musical typing to these
  forms. F6 or an explicit Return to pattern must first reveal Tracker when a
  compact dock covers it. Escape may hide the tool and restore only a still-valid,
  visible captured focus target, never a covered pattern canvas.
- Continue through the root document-operation path for playback/recording
  boundaries. Structural/timing edits can make a retained take incompatible;
  preserve it and its diagnostics. Do not implicitly Finish, Discard, rebase a
  take, restart transport or retarget a queued recording response.

## Separate extensions

Full Mac order-editor parity also includes section names, pattern notes/color and
previous/next section navigation. These require exposing existing entity metadata
and porting the guarded `song.annotate` wrapper around shared document annotation.
Plan them explicitly after the basic order/timing checkpoint; blank editable
placeholders would falsely imply that workflow works.

The arrangement matrix is a further scope: paged order/track density, block
selection, copy/merge/mix/make-unique and stale clipboard guards. Reuse shared
[ArrangementTools](../editor/ArrangementTools.cpp) after adding the missing
Windows read/write wrappers. Do not claim matrix parity from an order list.

Independent right/bottom/secondary dock groups, general pattern-transform review,
formula-completion refinements and custom-canvas accessibility remain other UI
gaps. Mac [DockWorkspace.swift](../mac/App/DockWorkspace.swift) has three groups;
Windows [WorkspaceDocking.inc](App/WorkspaceDocking.inc) currently gives automation
and instruments one active shared rectangle. This plan does not expand that
topology or replace the ongoing qualification gates.

## Qualification plan

Reuse the existing [document-operation native tests](Tests/Editing/DocumentOperationsTests.cpp),
[timing tests](Tests/Timeline/TimelineTests.cpp),
[sequence/timing tests](Tests/Timeline/PreciseNoteTests.cpp) and
[native reopen application case](Tests/test_editor_app.py). Add UI tests for the
new behavior rather than duplicating backend parsing tests.

1. Actual HWND navigation and actions: repeated pattern occurrences, long order
   lists, existing End/Skip markers, multiple sequences, insertion/moves/removal,
   first/last edges, last-order removal rejection, new/duplicate with format
   limits, and exact single Undo/Redo. Verify native metadata and precise/native
   pattern payloads through duplicate and save/reopen; never assume legacy cells
   alone prove a complete pattern copy.
2. Retention: raw invalid numeric/groove text and caret survive polling, pending
   Preview, rejection, resize and reopen; native Tab/F6/Enter/Escape and combo/list
   navigation act on the visible focused control. Wait for actual enabled controls
   before one test action. Verify document replacement/sequence change invalidates
   prepared drafts and never applies them to a new target.
3. Timing: fractional BPM, normalized groove/swing/straight, pattern-override
   scope, invalid mismatch/legacy-mode rejection, read-only behavior, Preview and
   no-op preserving revision/redo/playback, and one changed Apply stopping playback
   with exact Undo/Redo. Include second-sequence persistence and stale preview.
4. Integration: selected-order Play starts the selected occurrence while edit
   cursor/focus stay independent. Use only owned silent qualification for actual
   transport checks. A retained take survives an incompatible arrangement/timing
   edit and remains reviewable; no automatic commit/discard is introduced.
5. Review source-matched real rendering/native-control views at default/minimum
   clients and actual available large display scale. Include long names, hundreds
   of orders, sentinel row, repeated pattern, multi-sequence, invalid timing,
   normalized preview and stale draft. Enumerate every visible native child for
   positive bounds/non-overlap; inspect all images at original resolution. Record
   any composited capture limitations, DPI, executable hash and source manifest.
6. Run the affected isolated app cases, native suites and full app suite once
   source is frozen; preserve failed runs and exact candidate identities. Strict
   outer foreground/clipboard/desktop checks are separate from inner test passes.
   No foreground takeover, musician-session replacement or system-audio default
   changes. Mac source agreement alone is not reciprocal Mac runtime qualification.

Completion of this checkpoint means these native workflows are usable with exact
targeting, retained drafts, reversible edits and qualified persistence. It does
not close the broader Windows/Mac parity goal.

## Proposed document snapshot additions — read-only audit

These additions are **not implemented**. The smallest useful extension to
`document.get` is an `orderMetadata` array containing `{id: "n…"}` objects,
aligned one-for-one with the existing current-sequence `orders` array;
`sequences[].id`; and
`formatLimits: {patternRowsMin, patternRowsMax, patternsMax, ordersMax}`.
Populate IDs from `native.sequences[sequence].orders` and `.info`, and limits
from the existing `song.GetModSpecifications()` reference in
[DocumentController.cpp](Session/DocumentController.cpp). Keep the numeric
`orders` array and existing sequence indexes compatible. Mac already exposes
`orderMetadata` and sequence IDs in
[TrackerSession.mm](../mac/Bridge/TrackerSession.mm); full annotation payloads
can remain part of the later annotation scope above.

[NativeSong::reconcile](../editor/NativeSong.cpp) assigns a stable entity to every
raw order slot, including Stop (`65535`), Skip (`65534`) and missing-pattern
references. Preserve the complete untrimmed list, including slots after Stop.
Display those entries distinctly and retain their selection without navigating
or attempting selected-order playback through a nonexistent pattern. Current
`order.edit` assign/before/after require an allocated pattern; move/remove can
operate on existing sentinel slots, with edge and last-order removal guards.
See [DocumentOperations.cpp](Session/DocumentOperations.cpp) and
[TrackerDocument.cpp](../editor/TrackerDocument.cpp).

Use actual format specifications, including the engine's extended IT/XM/S3M
specifications, rather than constants inferred from the displayed extension.
Reusable pattern slots are `Patterns.GetRemainingCapacity()`, which counts
allocated patterns below `patternsMax` and permits holes. Available order slots
for the current APIs are `max(0, ordersMax - Order().size())`. Do not substitute
`Order().GetRemainingCapacity()`: that shared helper excludes trailing Stop
entries, whereas current create/insert guards use raw size. The proposed four
limits plus existing catalogs suffice to derive capacity; explicit remaining-slot
fields are optional. Existing sequence selection selects an existing sequence,
changes the opaque revision, and creates no musical Undo; it remains guarded
against read-only documents.

Two publication details need implementation and regression coverage. The current
`catalogRevision` comparison only sees numeric `orders`, so swapping two
occurrences of the same pattern can leave it unchanged. Include order identities
and selected-sequence/catalog changes when invalidating arrangement controls.
Also update `sequence.select` growth preflight: its current estimate of
128 bytes per order plus 8 KiB does not include the proposed per-slot JSON
objects. Charge that metadata before mutation, preserving atomic rejection of
an over-budget sequence switch.

Extend [DocumentControllerTests.cpp](Tests/DocumentControllerTests.cpp) for exact
published native IDs, repeated-pattern moves, assignment retaining identity,
distinct insertion IDs, remove/Undo and save/reopen, current-sequence projection,
immutable old views, holes/full capacity, trailing Stops, MOD's fixed 64 rows,
and atomic low-budget rejection. Existing
[DocumentOperationsTests.cpp](Tests/Editing/DocumentOperationsTests.cpp) already
covers most backend order/creation/sequence guards. New arrangement UI tests must
verify the selection contract; existing whole-document no-op equality checks
should continue comparing the added deterministic fields.
