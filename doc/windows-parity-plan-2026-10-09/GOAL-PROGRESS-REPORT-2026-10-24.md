# ScreamSeq: progress against the active goal

Snapshot: **24 October 2026, 23:27 UTC / 4:27 p.m. Pacific**. Based on current Git state, execution receipts, source checks and live GitHub job metadata. This report supersedes earlier status snapshots for the facts below; their failed and historical evidence remains retained.

**The three priority streams are substantially implemented and locally validated. The production-ready delivery goal is not complete: corrected Windows x64 acceptance, final platform review, main integration and packaging remain. Complete macOS parity is a wider, unfinished programme.**

The active goal is to complete navigation/composing workspace, mixer/graph/plugin editing and the precise-note timeline without unrelated expansion, then deliver a production-ready Windows build. We are well beyond P0b. Remaining work is primarily release qualification and integration, rather than another broad feature implementation phase. A completion percentage would obscure the distinction between implementation, verification and delivery.

## State of the requested workflows

| Area | Implemented and supported by evidence | What is still open |
| --- | --- | --- |
| Navigation and composing workspace | Pattern-centred connected layout; directly reachable adjacent mixing graph; explicit Float/Dock; contextual, hideable sidebar; clearer panel/window actions; retained inspectors. Current foreground checks cover compact and maximized layouts, direct graph access and restoration. Prior unchanged tests cover docking and envelope-bank placement. | Final release acceptance. Exhaustive multi-monitor/DPI and accessibility coverage is not established. No new navigation redesign is needed to close this batch. |
| Mixer and graph editing | Immediate mixer gain, balance, pre-balance and width; node removal, connection gestures and selected-connector Backspace; readable graph framing and connected parameter/automation navigation. Routing, history and persistence have targeted tests. Current foreground checks exercised all four mixer controls and graph-to-parameter navigation during playback. | Detailed wiring evidence is reused from unchanged inputs, not freshly re-executed in every checkpoint. Sustained flicker absence is not established. |
| Plugin and instrument editing | Effective parameter sliders, pages of up to ten editable parameters, search and retained edits; instrument plugin assignment and sample/plugin layering. The current ARM64 complete layered journey passed, including independent audible layers, exact reopened PCM and full-song export. | The same corrected journey must pass on the exact x64 executable/provider. Broad third-party plugin certification is not claimed. Instrument property changes still stop playback under the current contract. |
| Precise-note timeline | Double-click insertion, selected-hit Backspace, playback cursor and automatic field/drag edits without Write note or transport restart. Text Backspace remains text editing. History, Save/reopen, border handles and unrelated-note preservation have automated evidence. Fresh UI checks verified insertion/deletion, velocity typing, Save caret and Tab/Shift+Tab. | No outstanding failure in the locally tested requested gestures. Final integrated acceptance remains; every possible layout/input combination has not been qualified. |
| Plugin recording and full composing journeys | Record/Stop/Keep/Discard, original-time input capture, conflict refusal, retained recovery and chronological Undo/Redo across note and plugin edits. Current actual-app tests include normal capture, recovery refusal, combined history and layered composing. Fresh native Balance-slider recording followed by Stop/Keep passed without restarting playback. | Corrected x64 replay and final review. Opaque recovery deliberately retains stopped/closed-editor safeguards. Unsupported live opaque restoration is not silently attempted. |
| Immediate editing | Routine composing fields apply on edit; Save/history/navigation paths finish valid queued edits and preserve invalid drafts. Explicit acceptance remains for previews and recovery decisions. | This is not a claim that every Apply button throughout the entire application has been removed or should be removed. Remaining flows require individual assessment. |
| Shared architecture and compatibility | Shared model, validation, editing/history, timing, routing, hosted helpers and project preservation remain under `editor/`; UI, device and plugin-host integration remain native. Reciprocal edited-project fixtures have passed on retained binaries. | Final evidence-reuse review must confirm relevant inputs are unchanged. Stable IDs, opaque plugin state, legacy compatibility identifiers and OpenMPT attribution remain release requirements. |

## Latest corrections and evidence

The built implementation is **`b0375de637dd940cce225a0aba15fab69ee2f50f`**. Two defects found during final review are now corrected and passed on ARM64:

1. Windows allowed API Discard of an already-kept take to bypass explicit recovery safeguards. `windows/Session/ParameterRecordingOperations.inc` now refuses that operation before restoration side effects, matching Mac behavior. Native and actual-app regressions check retained music, later input, baseline/revision and explicit recovery/history.
2. `sample.renderSelection` inherited a five-second API reply deadline and timed out in the complete x64 layered journey. `windows/App/ApiDispatch.hpp` now gives an admitted offline selection render the same bounded 120-second total reply budget as export. Queue admission remains five seconds; replay, cancellation and uncertain-outcome protections remain intact. The actual named-pipe regression exercises a slow render and same-ID replay. The former x64 failure still needs closure on the corrected x64 artifact.

The bank-placement correction is also included and has prior targeted acceptance. Earlier reports describing these three fixes as unimplemented or unbuilt are obsolete.

| Validation | Current result |
| --- | --- |
| Local Windows ARM64 Release | Passed; built once at 22:52–22:54 UTC. Executable SHA256: `77fb8a0e046ea4b23f0e379eb591c05b01688beb0313cbb32bd776b4c3d31039`. |
| Latest local automated checks | All **three selected native groups plus mandatory scratch setup**, and **five actual-app cases**, passed. Cases include recovery protection, kept-take Discard refusal, normal recording lifecycle, interleaved precise/plugin history and complete sample/VST3 layering. These are targeted checks, not a claim that the entire suite was rerun locally. |
| Fresh Computer Use on that binary | Functional pass at 200% DPI: precise gestures and keyboard/Save focus, connected/compact workspace, graph/Params/Recording navigation, a real slider capture with Stop/Keep, all four mixer controls and maximize/restore. Four snapshots retained playback epoch 3 while frames advanced. Disposable QA process exited normally. |
| Current Mac ARM CI | **Passed**, including model/host, native interface, plugin lifecycle and socket/typed gates. [Run 40140424287](https://github.com/rewbs/ScreamSeq/actions/runs/40140424287). |
| Current Mac Intel CI | **Still compiling** when inspected. Pending is not passing. Both architectures passed the preceding candidate; unchanged Mac inputs support reuse but do not turn this running job green. |
| Current Windows x64 CI | Build passed; **162/162 portable/worker tests and 53/53 native groups passed**. Vendor ownership and typed-pipe steps passed. The 125-case integration suite reported **23 failures, four errors and one skip**; some diagnostics are repeated/teardown failures rather than distinct cases. The runner reports `devices: []`, and live-startup failures are present. The workflow remains **failed**. [Run 40140424174](https://github.com/rewbs/ScreamSeq/actions/runs/40140424174). |
| Corrected x64 real-endpoint replay | **Not yet executed.** Prepared verification/replay tools must use the exact current CI binary and matching provider. Earlier 21 affected cases passed on a prior exact candidate; reuse requires an explicit comparison of relevant changes and the current failure set. x64 execution locally is emulated on ARM64, not physical x64 hardware. |
| Reciprocal preservation | Retained-source Mac ARM save and Windows return passed fixtures 01–05 plus rendered/imported, opaque-state, composing-history and bank projects. Receipt pins source `946c61af7`. Subsequent corrections do not change codecs; final reuse assessment remains. Earlier Intel exchange evidence is separately bounded. |

The first local check attempt stopped at an inventory preflight because it omitted CTest's mandatory scratch setup. No tests executed in that attempt. The inventory was explicitly corrected, the failed preflight retained, and checks ran once without another build or weakened assertions.

## Limits that remain visible

- **Dense-effects audio timing:** the latest 265.6-second foreground workload at 48 kHz / 480 frames recorded 22 deadline overruns, zero starvation indicators, zero device errors and no processor fault. A matched prior-main comparison reproduces this overload class. It is a known pre-existing limitation, not a clean realtime qualification or proof against every timing regression.
- **Temporal presentation:** still screenshots and functional gestures cannot establish that white flashes or flicker are completely eliminated. Sustained presentation remains unqualified.
- **Accessibility and floating focus:** some docked controls are omitted from the inspection helper's accessibility tree. Visible caret evidence and native assertions support the checked focus paths. Main-targeted helper input can reactivate Main while a floating tool is related UI; floating typing continuity is therefore not claimed. Narrator and exhaustive keyboard/accessibility coverage remain open.
- **Review size:** PR 7 has 204 commits across 236 files. Focused review has found and corrected real defects, but final review and integration remain material work. Mergeability alone does not establish readiness.

## Source versus delivered software

| Item | Current identity/status |
| --- | --- |
| Integrated main | `0fe8f34008fe6517d7598affc31a4948a2ab14b8` |
| Built implementation and PR head | `b0375de637dd940cce225a0aba15fab69ee2f50f` on `codex/mac-gesture-intake` |
| Local HEAD before this report | `6741a083785a9f018d7cb120e293564834f4f4ea`; only three documentation files differ from the built implementation. Working tree was clean. |
| Integration | [PR 7](https://github.com/rewbs/ScreamSeq/pull/7) is open, draft and unmerged. Both native platforms continue toward shared main. |
| Latest compiled candidate | `C:/Users/P14/.codex/worktrees/parity-ci-layout/ScreamSeq/bin/windows-parity-p1/Render-Reply-Release/ScreamSeq.exe` — qualification executable, not the final packaged delivery. |
| Previously delivered package | `C:/Users/P14/Downloads/ScreamSeq-Windows-ARM64-composing-9c5c6ebb1/ScreamSeq.exe` — predates the later recording/export/instrument corrections. |
| Final corrected package | Not created. No successful final integration receipt exists. |

## Shortest path to completion

1. Inspect the terminal Windows failure set and verify/download the current x64 artifact and provider. Run the prepared five changed-path cases on a real endpoint; reconcile the remaining failures against valid unchanged evidence. This requires no new compilation.
2. Observe the existing Intel job to completion without restarting it. Close platform, compatibility and focused review gates; investigate any new failure before proceeding.
3. Integrate the reviewed source on shared main. Coordinate any merge-triggered CI with the hourly build limit; avoid a documentation-only PR rebuild immediately followed by another merge build.
4. Package the exact accepted ARM64 Release with scanner, runtime, notices, provenance and verified hashes. Deliver its usable executable path and the bounded audio/presentation limitations.

The latest recorded coordinated build start is 22:53:17 UTC, so another build checkpoint cannot begin before **23:53:17 UTC / 4:53:17 p.m. Pacific**, subject to later starts and still-active jobs. Continue at most one coordinated checkpoint per hour, including CI. No additional build is needed merely to report status or replay an existing artifact.

The wider [117-row parity matrix](parity-matrix.csv), [command/API inventories and acceptance recipes](README.md) remain the overall programme baseline. They mix historical and more recent evidence; they must be reconciled row by row before declaring complete parity or a specific feature absent. Beyond this release, unresolved coverage includes full keyboard/accessibility, multi-monitor/DPI, wider plugin/device behavior, remaining automatic-edit flows and sustained performance/presentation. These are not reasons to expand this three-stream release into unrelated redesigns.

**No product-design decision currently requires user input.** The remaining decisions are evidence-based engineering and release acceptance. This report ran no builds/tests, changed no product code and performed no push or merge.

## Evidence anchors

- Current local receipts: `bin/mac-gesture-20261022/render-reply/build.json`, `checks.json`, `native.xml`, `app.log`, `foreground-check.json`, `review-progress.json` and `composing-layered-instrument/audio-replay/report.json`.
- Reciprocal evidence: `bin/mac-gesture-20261022/recording-root/reciprocal-exchange.json`.
- Audio comparison: `bin/mac-gesture-20261022/recording-root/main-ancestor-comparison/receipt.json`.
- Scope and implementation: [production closure](PRODUCTION-CLOSURE-2026-10-24.md), [live precise notes](PRECISE-NOTE-LIVE-2026-10-23.md), [kept-take guard](KEPT-TAKE-DISCARD-2026-10-24.md), [render reply correction](SELECTION-RENDER-REPLY-2026-10-24.md).

Execution receipts under ignored `bin/` are local artifacts and must be retained separately for a handoff. Source/hash identity and relevant unchanged inputs govern evidence reuse; dated progress prose is supporting context, not a substitute for execution evidence.
