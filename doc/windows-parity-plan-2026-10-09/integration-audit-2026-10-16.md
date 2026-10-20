# Windows parity continuation: read-only integration audit

Inspected 16 October 2026. Safety candidate `adce8a486a554c41078b7f126c2caee5e6c59eea`; existing follow-on branch `codex/windows-parity-integration` at `b95c55d57584cbb4e6f13a964f1d87b44eb894ce`. No integration files, branches or musician processes were modified. This is a bounded source/evidence audit, not a completed review of all 259 files.

## Relationship and decision

The shared ancestor is `0382a81d87c46a30896e0b2b04600167579c4d88`. The integration branch contains 98 subsequent commits and explicitly merged earlier P0 safety work before combining P1/P2/P3 preparation (`c21c6f4ee`). Its delta has 259 files: 27 editor, 28 Mac, one soundlib, 184 Windows, 18 documentation and one workflow. The only open repository PR returned by the read-only GitHub query was PR #4, whose title/body describe the P0b/P0c safety candidate and retain P1-P8 as outstanding scope.

Five files have changes on both lines: IMPLEMENTATION.md, PluginOperations.cpp, ArrangementControllerTests.inc, DocumentControllerTests.cpp and test_graph_mixer_app.py. This identifies review overlap, not proven merge conflicts. Current Mac graph/provenance corrections must remain. Integration's shared parameter preparation and queue-refusal behavior must remain when carrying the new cold-state no-op correction forward. Replaying an earlier whole file would lose one side's work.

Recommended next integration decision: retain the current bounded correctness fix on PR #4, then explicitly choose whether to qualify the existing follow-on work as a subsequent PR or expand PR #4. Do not silently import the complete branch. Fresh shared/Mac/audio validation is necessary for a combined candidate even if text integrates cleanly. Existing isolated worktrees and the musician's DirectEditsFinal app remain untouched.

## Acceptance map

| Phase | Existing implementation/evidence | Remaining gate or implementation |
|---|---|---|
| P0b/P0c | Current native/worker/application receipts; real named-pipe typed API corpus, codec observation corpus and historical reciprocal exchanges | Same-candidate final closure and reciprocal exchange. Foreground departure/accessibility evidence remains unavailable. Known API/codec differences remain defects, not parity passes. |
| P1 / A01,A02,A16 | Follow-on MixerStripsWindow, retained gesture ownership, shared MixerControlEdit, native controls and fixtures | Qualify F04's 12 stable buses, cancellation/capture loss, deletion/reorder, one final Undo, PCM and compact/focus/DPI journeys on combined Windows/Mac source. Old P1 preparation docs contain superseded failures; later scoped passes do not prove every row. |
| P2 / A03,A04,A11 | Shared note-track candidates/layout projection; Windows track API, headers and retained Create/Group owners | Current combined candidate: stable IDs, invalid capacity, live column mute without sibling mute, Undo, PCM/voice ownership and reciprocal save. Mac adapter/shared model changes need native execution. |
| P3/P3a-P3c / A04-A06,A15,A16 | Follow-on menus, command catalogue, UIA peers, native keyboard settings, ruler/transport loops, Pattern Tools, New/Demo, song properties and load reports | Current keyboard/context availability, local shortcut precedence, loop boundaries, captured transform preview, unsaved/take protection, real UIA/Narrator, theme/high-contrast and DPI acceptance. |
| P4/P4a-P4b / A07-A09,A12-A14,A17 | Direct point/field edits, recording duration choices, paged plugin parameters and choices; bounded 16-group direct-edit receipt. Shared preview foundation exists | Cold-state plugin no-op fixed separately here, pending build/test. Shared parameter preview is explicitly internal: no UI/API preview/cancel ownership protocol yet. Keep saved manual values separate from automation; test queue refusal, cancel/final, one Undo and captured ownership before exposing preview. |
| P5 / A09-A12,A16,A17 | Direct graph move/remove/cable gestures, retained parameter windows, navigation and bounded 3-native/13-application receipt | Full recipe/song group/port/provenance catalogue, same-candidate admission/telemetry and sustained presentation; do not treat static controls or private desktops as foreground proof. |
| P6 / A01,A15,A17 | Typed corpus and exact baseline differences; no blanket codec replacement | Domain-by-domain preservation/recovery convergence, export behavior and bidirectional five-fixture plus edited-output exchange. No normalizer or relaxed expected result may hide loss. |
| P7 / A11,A13,A14,A17 | Existing native hosts and controlled fixture tests | Actual endpoint/device lifecycle, reconnect, physical MIDI/audio capture, vendor architecture/editor/latency evidence. Host C++ allocation audits do not cover vendor internals or every lock/allocation API. |
| P8 / A18 | Historical scoped receipts only | All required architecture/platform gates on one integrated candidate, full composing journeys, reciprocal files, sustained frame/audio timing, foreground screenshots and separately measured physical devices. |

## Retained evidence boundaries

`integration-audit.json` contains exact source lists and selected historical receipt fields. The usable-build receipt reports eight named native groups and three app cases, not full parity. Graph-direct reports three native groups and 13 distinct application cases. Direct-edits reports 16 groups in 270.69 seconds, limited to owned private desktops and worker APIs. Instrument-layering includes shared audio changes and explicitly leaves Mac AU/VST3 execution pending. The current schema method-name sets are equal across these branches, but Windows advertised method inventories/signatures change and still require fresh conformance review.

The standing build cadence is in `doc/windows-parity-plan-2026-10-09/IMPLEMENTATION.md:5-13`, labelled a user instruction, and reiterated in P0B-CLOSURE.md: batch related changes and aim for one consolidated build cycle per hour; failed necessary checks still require correction and targeted rerun. The preceding local build ended 03:49:31 UTC. The current frozen no-op checkpoint is queued for 04:49:32 UTC.
