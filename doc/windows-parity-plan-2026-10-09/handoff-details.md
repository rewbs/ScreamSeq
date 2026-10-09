# Implementation handoff details

This supplements the [implementation plan](README.md). It specifies future work. This review read source, fixtures, images and retained logs; it did not modify product code, launch the app, build, run tests, dispatch workflows or integrate a branch.

**Historical checkpoint:** the candidate discussion below records `e7f165eb4` and earlier source. The [latest planning review](planning-review-refresh.md) pins the inspected checkout to `d5b7a09eb50a89b775841da14d8d81f49362c122` and supersedes candidate and evidence status here. The eight-file list records the earlier `61a28b489` checkpoint. The P0b owner and admission design remains future work; cable, stage-role and subsequent layout/fixture repairs already have candidate implementations and bounded local evidence. Do not implement those fixes twice.

## Earlier source and candidate checkpoint

Remote main was rechecked on 9 October 2026 and remains `ffe81aa4bfa83bc89b1e1dd92db06c67819c247a`. There is no post-reference main delta. The current checkout was clean at `61a28b4892b89e6c1e0445dd3b16f975fd222c9e` when this review began. Its eight changed product/test files relative to main are:

| Existing source change | Scope and future qualification |
|---|---|
| `windows/App/RecordingIntegration.inc`, `SampleCaptureIntegration.inc`, `Main.cpp` | Separate MIDI/sample guards, aggregate guard with MIDI recheck after the sample worker read, and close-failure fallback. Preserve these candidate fixes; qualify both take types, reentrancy, native close/open/save and API entry points independently. |
| `mac/Audio/CaptureEndMonitor.hpp`, `SampleRecorder.mm`, `mac/Tests/SampleRecorderTests.mm` | Atomic stop request and joined thread, with lifecycle tests. Source inspection does not establish actual AudioUnit disconnect/limit behavior or supported-toolchain execution. |
| `mac/App/WorkspaceIntegration.swift` | Explicit typed menu accumulation replaces the expensive combined Swift expression. Review order, optional-window visibility, context actions and weak ownership; the new candidate still needs a supported native build. |
| `windows/Tests/Workspace/WorkspaceRestoreTests.cpp` | Adds take guards and fixes normalized strength versus percent display expectations, retaining exact raw strength/offset/duration checks. This corrects a display expectation rather than relaxing musical fidelity. |

The [historical implementation receipt](IMPLEMENTATION.md) reports a local ARM64 build and focused tests. Read-back of retained logs found: six recording/MIDI cases passed with four initially skipped, those four subsequently passed with explicit silent output, the full workspace check passed in 80.75 seconds, and three worker recording/recovery checks plus scratch-directory setup passed. These are pre-existing results, not tests run for this plan. They do not qualify all of main, x64, Mac or physical input devices. Before reusing any result, match the original executable, full relevant source/dependency inputs, toolchain, fixture and test source; a matching executable hash alone is insufficient to prove the reported source association.

Read-only CI inspection found [Apple Silicon job 113750668968](https://github.com/rewbs/ScreamSeq/actions/runs/37909410789/job/113750668968) failed at the Swift expression. Its checkout line is **PR merge `b6566c0` of candidate `605c7c688` into main `ffe81aa4b`**, not local HEAD `61a28b489`. Subsequent native/Swift tests were skipped. Windows job `113750669560` and Intel Mac job `113750669330` were still in progress at the read-only snapshot; they are neither passes nor failures in this plan. Do not rerun or cancel them as part of planning. An implementer should inspect their eventual results before deciding which remaining gates need execution.

This evidence changes P0a's handoff from “write the fixes from scratch” to “review the existing candidate, address remaining exact-candidate qualification failures, then integrate both platforms together.” It does not mark P0a complete.

## P0b native draft protection

**Observed:** `EditingView.inc::protectUnsaved` checks retained takes, flushes plugin editors and examines `view->dirty`. It does not aggregate raw native editor drafts. `openFile` can pump messages during checks and the file chooser. `Main.cpp::documentOperation` awaits the worker and refreshes state. `RecoveryIntegration.inc::restoreRecovery` separately captures the saved/recoverable project and calls `controller->recover`; that snapshot does not represent every raw inspector draft. `NativeToolWindow` has no universal document-draft admission contract.

**Proposed owner interface:** add a small native `DocumentDraftRegistry`, without moving HWND lifetimes or text buffers into `editor/`. Owners expose a value summary containing owner ID, document ID, stable target, captured revision, generation, dirty/pending/uncertain state, and a human-readable target label. Registration lifetime follows the owner, including hidden windows. `review()` raises the existing owner. Discard is a deliberate owner operation guarded by the captured generation. Do not inspect HWND text from a document/audio worker. Share the value-token/state-transition rules with Mac, not the Win32 registry implementation.

The owner audit must cover all of these families; testing three representative editors alone cannot establish complete aggregation:

| Owner family | Concrete source | State to include |
|---|---|---|
| Inline and retained pattern edits | `PatternEditor.inc`, `PatternNudgeEditor.inc`, `PreciseNoteWindow.hpp`, `PreciseNoteOwnerModel.inc` | Raw typed fields, cell/hit selection, precise collection draft and in-flight Apply; hidden FX fields count. |
| Instrument and curves | `InstrumentEnvelopeWindow.hpp`, `ParameterAutomationWindow.hpp`, `GraphCurveWindow.hpp`, `GraphCurveOwnerModel.inc`, `AbsoluteAutomationWindow.hpp` | Raw fields and curve edits, keymap/settings, stable instrument/plugin/source/pattern, dirty drag and pending lane Apply. |
| Nested formula and bank owners | `FormulaWorkbenchWindow.hpp::retainedDraft`, `FormulaApplyState.hpp`, `EnvelopeBankWindow.hpp` and parent child-owner files | Formula raw text and invalid/valid generation, pending Use, parent target, unsaved song-template edits. An app-catalogue-only preference change is not a song mutation; classify its lifetime separately. |
| Samples and scratch | `SampleDetailWindow.hpp`, `SampleDetailWorkflows.inc`, `SampleWorkflowDraft.hpp`, `ScratchGestureWindow.hpp`, `ScratchEditorState.hpp` | Loop/settings/drawing/processing preview and paired motion/fader/formula drafts; preview is not a saved PCM edit. |
| Main Mixer/plugin/graph editors | `MixerEditor.inc`, `PluginEditor.inc`, `GraphEditor.inc` | `mixerDirty/mixerPending`, plugin draft/preset request, graph definition and raw-field dirtiness, active cable/node gesture. Vendor editor flushing remains a separate boundary. |
| Routing, trims and graph forms | `SongRoutingWindow.hpp`, `GraphTrimsWindow.hpp`, `GraphWorkflowWindow.hpp`, `GraphCommandsWindow.hpp` | Layout draft, wire/source/trim fields, command target and tail flag; retained fields can exist even when the main graph draft is clean. |
| Arrangement, timing and activity | `ArrangementWindow.hpp`, `ArrangementMatrixWindow.hpp`, `SongTimingWindow.hpp`, `ParameterActivityWindow.hpp` | Order/annotation fields, timing preview and recorded-point fields. A selected block or observation trace alone is not dirty. |
| Import/record/recovery tools | `MultisampleImportWindow.hpp`, `PatternSampleRenderWindow.hpp`, `SampleRecordingWindow.hpp`, `MidiRecordingWindow.hpp`, `RecoveryWindow.hpp` | Distinguish configured-but-uncommitted imports, active jobs, retained PCM/MIDI takes and read-only browser selection. Reuse take protection; do not count the same take twice or equate a meter with a draft. |

Some files already expose snapshots/guards, others need a narrow read-only draft summary. Audit each family explicitly before declaring the registry complete. Do not make a generic “all windows dirty” fallback that prompts on ordinary navigation or local preferences.

**Proposed replacement protocol:**

1. Capture a departure token containing document identity/revision plus every affected owner generation. Review raises the selected dirty editor; Cancel preserves raw text, caret, selection, scroll, pins, captured target and song. Invalid/stale drafts cannot be silently applied.
2. A deliberate Discard choice authorizes departure but does not erase drafts yet. If Open's chooser is cancelled, loading fails, a protected recovery write fails, or the user cancels later, retain the original document and its drafts.
3. Stage and validate replacement, then recheck the departure token at admission. Message pumping can create another draft or take. Any new generation/identity/pending write invalidates the token and requires renewed review; the old consent cannot discard newer work.
4. Commit the replacement once, then retire only the captured old-document owners. No callback with a borrowed canvas/control reference may survive a reentrant worker call. A late successful write with an uncertain reply must be reconciled before departure.
5. Apply the same admission boundary to native Open/New/Demo/Close, API `document.open`, `recovery.restore`, and shutdown handling. API replacement reports structured pending-owner information rather than opening a modal dialog on the pipe thread. Do not let an existing `discard:true` request silently discard unrelated native drafts without an explicit documented contract. OS session-end handling must be bounded and cannot auto-Apply an invalid draft.

**Future acceptance:** table-driven owner registration checks for every family, then actual app cases for native Open and Close, API Open and recovery restore. Cover clean/dirty/invalid/stale/hidden/nested/pending/uncertain combinations; Cancel, failed load, cancelled chooser, deliberate discard and a second draft created during a worker read. Include both retained take types and existing plugin-flush/close failure behavior. Check exact raw text and target retention, not only a boolean `dirty` marker.

Cheapest checks are pure registry/token transition cases and direct native owner summaries. Build the Windows app/workspace target once after the cohesive implementation, then run affected workspace/retained-editor, recovery and recording cases on the same executable. Common message/admission changes trigger the broader workspace/recovery set. Mac's unchanged native draft code can reuse valid evidence; any shared state token/history change requires the relevant Mac editor-draft/session target and app build.

## P0b role-safe stages and modulation edits

This subsection describes the main-baseline defects and intended contract. Existing candidates `d59c24c8c` and `e7f165eb4` implement the cable/default and role-protection slices; review their current-source disposition before selecting work. Broader graph UI, exact-copy observation and aggregate departure protection remain outstanding.

**Stage identity:** `SongRoutingCanvas.hpp::build` gives Row/Persistent/Ordinary cards distinct display IDs but the same bus ID. `SongRoutingWindow.hpp::inspectFields`, `assignGraph`, `insertAction` and layout enablement derive capabilities from bus/instrument presence. Proposed `StageRole` and capability values must survive canvas projection, selection, snapshots and mutation dispatch. Check capabilities again immediately before issuing a write; merely disabling a button is insufficient. Row/Persistent selection may inspect/edit the shared definition or choose a live copy, but it cannot rewrite the bus's ordinary assignment or insert chain. Keep aggregate auxiliary stage routing separate from individual copy observation.

**Fresh modulation:** `GraphCableEdits.hpp::connect` currently initializes 0..1 depth with base 0. The Mac socket gesture in `SignalGraphRouting.swift::connectPorts` uses zero depth and `modulationBase`. Resolve a new Windows base from the accepted target-wide peer, preferring an enabled peer, or normalize the target's manual catalog value into 0..1 when no peer exists. Preserve the target-wide quantization contract. Zero depth must not reset the manual base or change an existing saved cable.

`GraphEditor.inc::graphMouseUp` currently keeps a reference into `graphCanvas.sockets`. If catalog acquisition requires `documentOperation`, first copy the endpoint values and capture graph/target/revision/generation. That worker wait pumps messages and may rebuild the canvas. Recheck the copied target after the response; never retain the socket reference across that boundary. Prefer an already valid immutable catalog snapshot where available.

**Field preservation:** the Add/update form at `GraphEditor.inc:243` reconstructs the modulation edge and omits `quantized`. Start from the captured existing edge for an update and change only explicit fields. Propagate base/quantization according to the target-wide validator; minimum/maximum remain per wire. Preserve enabled state, sibling sources, audio gain/ports and any permitted untouched fields. Rewiring must distinguish moving a selected cable from adding a fresh one and reject collisions without altering either existing cable.

**Future checks:** extend `windows/Tests/GraphOperations/FanConnectionOperationsTests.inc`, which already exercises gain-preserving rewires, collision refusal and enabled-peer quantization. Use F03/F04 for role cases in `test_song_routing.py` and `test_graph_editor.py`. Compare dry-run/model/history first, then a bounded nonzero render for a new zero-depth cable. Test missing/changed catalog, stale endpoint, disabled/enabled peer ordering, discrete target, selected unrelated wire and range-only edit retaining `quantized:true`. Build only the graph test target first; use one Windows app checkpoint for the related native changes. Mac-hosted `windows-graph-document` is portable adapter evidence, not Windows desktop proof.

## Preserve live recorded automation

**Observed on main, unchanged in this branch:**

- `windows/Session/AbsoluteAutomation.inc` implements stopped-readable saved points and point add/move/remove. It validates the persistent rack instance and catalog, preserves other lanes, and rejects conflicts with enabled pattern envelopes/native parameter commands.
- `PluginOperations.cpp:133–155` prepares recorded-only publication before history/model commit. Its history path also prepares recorded-only Undo/Redo.
- `DocumentController.cpp:191–197` prepares the immutable timeline and performs guarded publication while active. A full queue, stale playback generation or failed preparation leaves accepted state intact.
- `mac/Bridge/ParameterActivityAPI.inc:73–89` routes the same edits through plugin graph application; `TrackerSession.mm:1801–1805` chooses prepared recorded publication when plugins/native routing are unchanged.
- `windows/Tests/LiveGraphPublicationTests.inc:31–61` already expresses the live queue/refusal, dry-run, Undo/Redo, opaque baseline and save/reopen invariants. It is a source-inspected test, not a newly run one.

P4/P5 must preserve this behavior. The Activity panel's prepared-copy enumeration and the Absolute Automation editor's stopped rack-lane access are different UI entry points. Improve stopped availability/labels and retargeting without turning observation requirements into a new musical restriction. A12 should run both entry points, including no prepared copy, stopped prepared copy, active copy, retired copy, missing vendor and queue refusal. Manual gesture **recording** remains a separate P4b gap; point editing does not implement capture/arm semantics.

## Core command mapping

The command inventory retains all 419 Mac entries. The following maps core reference rows to concrete Windows identifiers in `WorkspaceCommands.inc`/`WorkspaceShortcutDispatch.inc`, with execution in `Main.cpp`, `EditingView.inc` and the named editor integrations. These are source entry points, not runtime passes or proof that every context has the same meaning.

| Reference rows | Existing Windows entry or gap | Future work |
|---|---|---|
| 3, 6, 8–12, 16–20 | `audioSettingsCommand`, `openCommand`, `sampleBrowseCommand`, `sampleRecordCommand`, `recoveryCommand`, `saveCommand`, `saveAsCommand`, `undoCommand`, `redoCommand`, focused cut/copy/paste commands | A01/A07/A13–A17; focused selection semantics and dirty protections. |
| 1, 2, 5, 7, 13–14 | No dedicated About/Keyboard-Display/New/Demo/user export command found in the inspected registry; underlying timing/import/render infrastructure is not the missing command | P3 preferences/discoverability, P6 protected replacement/export/About and notices. |
| 4, 15, 87 | Native window close/minimize and Alt+F4 are platform equivalents; close routes through `Main.cpp::canClose` | Preserve Windows ownership/activation; add menu reachability and draft protection, without copying macOS Quit/window-menu behavior. |
| 21 | `selectAllFocusedCommand` is registered for sample frames, not a complete tracker Select All | P3 selection-aware dispatch; local text Ctrl+A retains precedence. |
| 22–28 | `parameterAutomationCommand`; local API via explicit launch flag; `togglePlaybackCommand`; play accepts API region/loop arguments but cursor/selection/live-loop UI contracts remain gaps | P3 scope/loop commands and protected context; no blanket “Windows cannot loop” claim. |
| 29–33 | No dedicated previous/next instrument, octave or use-cursor instrument command in the inspected registry | P3 add native command semantics over existing musical input ownership. |
| 34–41 | `songTimingCommand`, `notesCommand`, `recordingFinishCommand`, `recordingDiscardCommand`, `effectsCommand`, `scratchGesturesCommand`, `patternRenderSampleCommand`, `patternRenderInstrumentCommand` | Preserve existing owners and take/API behavior; complete discoverability and pending-state reasons. |
| 42–46 | Comprehensive Pattern Tools/near-cursor picker and note-track creation/grouping commands are gaps; effect search and fixed transforms already exist | P2 track operations; P3 preview/picker and complete scopes. |
| 47–58 | `patternPasteMix`, `patternPasteMerge`, `arrangementCommand`, `arrangementMatrixCommand`, `newPatternCommand`, `duplicatePatternCommand`, transpose/clear/delete-channel-row/insert/delete commands | A04/A05 plus preview, all FX/native layers and stable order identity. |
| 59–71 | `notesInspectorCommand` is read-only context; `notesCommand` opens precise notes. Sample/instrument/plugin/mixer/graph/lanes/automation/activity windows exist; effect browser is chiefly `pluginBrowse` | Register local actions, distinguish show/focus/retarget, and add destination-aware graph effect browsing. |
| 72–85 | `focusPatternCommand`, `nextPanelCommand`, `pinCommand`, `cursorCommand`, `returnCommand`, `followCommand`, `liveKeysCommand`, Compose/Sound/Pattern and saved-layout commands | Focused-region scope and floating/docking eligibility differ. Preserve user shortcuts; expand retained ownership without equating the existing four regions with arbitrary docking. |
| 88–99, 158–164, 384–390 | Precise Notes, Instrument and Automation have independent region owners and local placement/follow/return actions | Register these specific local actions in the palette; do not claim they are absent because they lack global IDs. |
| 136–142, 183–189, 204–210, 330–350, 357–363, 404–410 | These panel-management entries span Main-bottom or floating surfaces outside the four-region model | P3 retained-owner expansion according to composing workflow; per-panel exceptions must name the equivalent navigation. |
| 190–203 | Mixer group/remove/reload, draft mute/solo, sends and SongRouting forms exist; return creation, strips and Route Here need UI completion | P1; preserve existing `mixer.bus.set(preview:true)` rather than adding a second audio control path. |
| 211–329 | Graph recipe canvas, Graph Workflow forms, Song Routing, trims, signal observation and provenance collectively cover many operations | P5 per-action mapping and native contexts; distinguish existing form/API support from absent direct gestures. Preserve recipe/stage/live-copy identity. |
| 351–356 | `graphCommandsCommand` and `GraphCommandsWindow.hpp` provide tails, insert/remove/open/reload; lane focus uses `graphLanesFocus` | P3/P5 captured cursor/return, follow behavior and command registration. |
| 364–383 | Pattern automation has bank/formula/tools/curve controls; Record gestures and Clear all recorded automation are separate missing capture workflows | P4a ergonomics; P4b recording plus API/history/persistence. |
| 391–403 | `ParameterActivityWindow.hpp` has refresh/freeze/clear/fit/zoom, source navigation, saved-point editing and pagination | P5 last-touched/capture/mapping entry points, names-first selection and prepared-copy transitions; retain live point editing. |
| 411–419 | `ArrangementWindow.hpp`, `arrangementMatrixCommand`, `previousSectionCommand`, `nextSectionCommand` | Existing UI reorder is distinct from missing Mac-compatible `order.edit(move,destination)` API arguments; add adapter conformance, not a second arrangement engine. |

For local commands outside this summary, the inventory's owner and reference navigation delimit discovery; unresolved specific reachability is explicitly a question. A matching verb, a present API string or a handler for another editor must never upgrade that row to parity. At P3/P5 completion, add the actual native semantic command ID and availability predicate to every row, or a demonstrated Windows-equivalent disposition.

## Windows API extensions outside the Mac inventory

The 232-row API inventory deliberately follows the supplied Mac `api.describe`. The following 16 additional Windows method names must also be preserved and covered by future conformance checks; they are not missing Mac methods. These are observed dispatch/contracts, not runtime passes.

| Methods | Source owner | Required contract / native boundary |
|---|---|---|
| `audio.devices.get`, `audio.settings.get`, `audio.settings.set` | `windows/App/AudioSettings.inc`; `windows/Api/audio-settings.schema.json` | Enumerate Windows endpoints and guard settings changes with their settings revision. Preserve explicit silent output and failure/recovery state; device identifiers are platform-local, not portable song identities. |
| `midi.devices.get`, `midi.settings.get`, `midi.settings.set`, `midi.test.inject` | `windows/App/RecordingIntegration.inc` | Preserve revision-guarded input settings, device loss/retained take behavior, and the restricted test-injection contract. A synthetic event is not physical MIDI qualification. |
| `document.open` | `windows/Api/SessionAdapter.hpp`; `windows/Session/DocumentController.cpp`; native `Main.cpp::documentOperation` | Absolute path, expected revision and explicit unsaved-work discard policy; P0b must cover raw native drafts as well as accepted document dirtiness. Failed/cancelled replacement must preserve the current document and drafts. |
| `plugin.editor.open`, `plugin.editor.close` | `windows/Session/PluginOperations.cpp` | Native STA/editor ownership, guarded slot selection and editor-state flush; closing a vendor window is not equivalent to deleting the plugin. |
| `plugin.path.get`, `plugin.path.scan`, `plugin.path.set` | `windows/Session/PluginPathOperations.inc`; `windows/Api/SessionAdapter.hpp:157` | Stable plugin ID; scan/set revision guards; set verifies the expected module SHA-256. Preserve opaque state and plugin history; do not substitute the Windows path into a cross-platform identity. |
| `graph.plugin.path.get`, `graph.plugin.path.scan`, `graph.plugin.path.set` | Same path-operation implementation; `windows/Api/SessionAdapter.hpp:158` | Graph/node identity, document history, recipe state, ports and routing survive location repair. Cover missing/wrong architecture/vendor binary and stale scan results. |

P0c should generate the combined method census from dispatch/describe contracts and label intentional native extensions. P6/P7 must retain these semantics during codec/provider extraction; a shared schema does not require identical native device or vendor-editor APIs. Inspect `windows/Api/README.md` alongside the source rather than treating the Mac method list as the complete Windows API surface.

## Delivery and validation boundaries

P0b's registry, role and cable work may be split into reviewable commits, but share one final Windows app build when their source inputs are frozen. Pure helpers can be checked separately before that expensive checkpoint. P0c supplies the typed corpus before codec migration; P1 Mixer and P2 track APIs then adopt small shared operations with both adapters in the same integration PR. No platform branch remains long-lived.

Do not weaken assertions to lower cost, claim skipped tests passed, or schedule full audio/hardware suites for a label-only UI change. Conversely, a passing pure helper cannot replace actual named-pipe/socket, native HWND/AppKit, foreground accessibility or device qualification. The parent plan's per-batch triggers and final A01–A18/B01–B14 reciprocal gate remain mandatory.

No user decision is needed for this planning handoff or to specify P0a. Later qualification needs the must-support physical devices/commercial plugins, and explicit agreement only if the assumed x64/ARM64/Apple Silicon/Intel support scope is reduced. Optional provider consolidation, device modes and automatic recovery pruning are not prerequisites.
