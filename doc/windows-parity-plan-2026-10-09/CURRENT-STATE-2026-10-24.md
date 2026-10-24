**ScreamSeq Windows/macOS parity: current-state report**

24 October 2026, 2:15 p.m. Pacific. Based on current source, local receipts and CI observed at 21:15:03 UTC. Supersedes the earlier snapshot in this file.

**Overall: substantially implemented; not yet production-qualified or integrated.**

The active goal is to complete navigation/composing workspace, mixer/graph/plugin editing and the precise-note timeline, then deliver a production-ready build without unrelated refactors. Those streams now have working implementations, passing targeted checks and fresh Computer Use evidence. We are well beyond P0b. Remaining work centres on a concrete window-placement defect, audio/presentation qualification and cross-platform integration.

The original 117-row Mac parity programme is broader and has not been fully requalified by this batch. Completion of these streams must not be called complete macOS parity. A numerical percentage would mix implementation, acceptance and delivery.

| Stream | Implemented and evidenced | Remaining closure work |
| --- | --- | --- |
| Navigation and composing workspace | Pattern-centred navigation, graph directly beside Pattern, explicit Float/Dock, contextual hideable sidebar, retained inspectors and clearer panel/window actions. Fresh UI checks exercised navigation, maximize/restore, Float, main-window Dock and hiding the context panel. Targeted docking/focus cases passed. | Envelope bank can open partly off-screen from the right instrument panel. Correction and regression assertions are written but unbuilt. Final-candidate placement inspection remains. Exhaustive DPI/accessibility coverage is not established. |
| Mixer, graph and plugin editing | Live gain/balance/pre-balance/width; node creation/removal; socket connections; selected-wire Backspace; working parameter sliders, ten-row pages and search. Fresh UI checks exercised these during playback. Routing/modulation/preservation cases passed. Instrument plugin assignment and sample/plugin layering have earlier evidence. | Sustained audio/presentation remains unqualified. Floating-field typing has a Computer Use targeting limitation. Broad third-party plugin/device certification is outside this closure batch. |
| Precise-note timeline | Double-click insertion, selected-hit Backspace, playback cursor, automatic field/drag edits and Save without Write note or playback restart. Fresh UI checks covered insertion, drag, deletion, velocity typing, Save caret, text Backspace and Tab/Shift+Tab. Live composing/history and Save cases passed. | Recheck affected behavior on the final candidate; reuse unchanged evidence. Still screenshots cannot establish absence of flicker. |
| Recording and complete composing journeys | Record/Stop/Keep/Discard, original-time capture, conflict refusal, take retention/recovery, history and persistence are implemented. Latest composing/recording case passed, including cross-domain edits, exact history/reopen and export. Recorder entry/focus and automatic instrument-bank handoff checks passed. | Preserve stopped/closed-editor safeguards for opaque recovery. Native instrument property edits still intentionally stop playback, confirmed in source/tests and observed in QA. This is a remaining ergonomics limitation, separate from the precise-note requirement. |
| Shared architecture and compatibility | Musical operations, history, routing and persistence remain shared; UI/audio-device hosting remains native. Earlier reciprocal edited-project exchanges passed both Mac architectures and Windows return legs. | Required final platform checks must pass. Reuse reciprocal evidence only where relevant inputs are unchanged. Stable identities, plugin state, unknown metadata, project compatibility and OpenMPT attribution remain requirements. |

**Latest validation**

Frozen implementation: `946c61af7a3dde87c87500a9837cdc81d69292d9`.

- Windows ARM64 Release compiled successfully once, 20:49–20:53 UTC.
- **All five targeted native groups passed:** automatic edits, plugin search, short instrument docking, native tool-window behavior and audio export.
- **All eleven actual-app cases passed:** four prepared recording/instrument cases and seven closure cases covering precise notes, native Save, graph wiring, live mixer and workspace placement/focus.
- Fresh isolated Computer Use ran at 200% DPI against that exact binary. API snapshots through precise-note, graph and mixer edits showed advancing frames, unchanged playback epoch 3 and no transport fault.
- QA used a disposable project and silent playback, exited normally, did not alter system audio defaults and did not start microphone capture.
- Current **Mac ARM CI job passed**. Mac Intel and Windows x64 were still building at the observation time. Pending is not passing. See [Mac CI](https://github.com/rewbs/ScreamSeq/actions/runs/40131518375) and [Windows CI](https://github.com/rewbs/ScreamSeq/actions/runs/40131518388).

The earlier statement that this candidate had only compiled is superseded. Local functional passes do not make the foreground or production result green.

**Remaining blockers and limitations**

1. **Envelope-bank placement:** opening from the right instrument panel can hide actions beyond the monitor. `windows/App/EnvelopeBankWindow.hpp` now uses the existing work-area clamp at creation. Assertions in `windows/Tests/Workspace/AutomaticEditsApplicationTests.inc` cover edge placement and preserve a retained bank's user-chosen position. These 19 product/test lines are uncommitted, unbuilt and untested; this is not yet a verified fix.
2. **Audio deadline overruns:** latest QA recorded 36 overruns, zero starvation indicators and zero device errors at 48 kHz / 480 frames. Maximum callback time was 14.4005 ms against a 10 ms period. The 504.6-second session includes stopped inspection after an instrument edit, so it is not uninterrupted playback duration. `realtimeAuditPassed=false`; sustained presentation is also unqualified. Earlier diagnostics indicate engine-rendering time dominates spikes but do not identify a processor or distinguish CPU work from scheduling delay. A focused investigation is needed before a production claim.
3. **Cross-platform acceptance and integration:** finish Intel/Windows CI, inspect failures without weakening assertions, preserve shared/reciprocal gates, and review the accumulated changes. The latest implementation is outside main. PR 7 needs a final-scope description before integration.
4. **Production package:** no package was produced from this candidate because foreground qualification is not passing. Package the accepted binary with scanner, runtime, notices, source provenance and verified hashes.

Computer Use has bounded coverage. Main-targeted typing can reactivate Main while a floating tool appears as related UI; that neither proves an application defect nor qualifies floating typing continuity. Some docked child controls are absent from the helper tree, so visible caret observations are combined with native assertions. Still screenshots do not qualify temporal flicker. Narrator and exhaustive accessibility are not qualified.

Earlier failed evidence remains retained: Windows live-startup failures with no usable audio endpoint, an export assertion failure, a Mac retry assertion failure, and an Intel cabinet timeout/cancellation. Current corrected local gates and current ARM success supersede only relevant coverage. Intel and Windows results remain required.

**Repository and delivery state**

| Item | Verified state |
| --- | --- |
| Remote main | `0fe8f34008fe6517d7598affc31a4948a2ab14b8` |
| Remote implementation branch | `codex/mac-gesture-intake` at `946c61af7a3dde87c87500a9837cdc81d69292d9` |
| Primary HEAD | `ca3b3d0d1`, documentation after the frozen implementation |
| Working tree | Two modified product/test files for bank placement; this report and production-closure document are untracked |
| Integration | [PR 7](https://github.com/rewbs/ScreamSeq/pull/7), last inspected draft/unmerged |
| Latest compiled executable | `C:/Users/P14/.codex/worktrees/parity-ci-layout/ScreamSeq/bin/windows-parity-p1/Recording-Root-Release/ScreamSeq.exe` |
| Executable SHA256 | `e30e936e307dbd3ecb0f29bb7fd5ef98472aa53a40b763461f75924590c33dac` |
| Existing usable package | `C:/Users/P14/Downloads/ScreamSeq-Windows-ARM64-composing-9c5c6ebb1/ScreamSeq.exe` — verified present; includes precise-note interactions but predates later recorder/export/instrument corrections. Not the completed production release. |

**Recommended completion sequence**

1. Finish observing existing CI; do not rebuild to inspect its results.
2. At the next permitted checkpoint, build the focused placement correction, run affected bank/owner checks and inspect placement with Computer Use. Reuse unchanged passing evidence.
3. Resolve or adequately characterize playback overruns using a bounded representative composing workload, then establish sustained audio/presentation acceptance. Avoid unrelated DSP/architecture rewrites.
4. Complete the final platform/compatibility gate, update and review PR 7, merge to shared main and package the exact qualified Release.

Latest observed coordinated build start: 20:50:05 UTC. Next checkpoint must be no earlier than **21:50:05 UTC / 2:50:05 p.m. Pacific**, subject to later starts and avoiding overlap. Continue at most one coordinated checkpoint per hour, including CI.

No product decision currently requires user input. The goal remains active and incomplete. The concrete closure list is much narrower than the original programme, but audio and pending platform results prevent a defensible completion date.

This reporting turn started no builds/tests, changed no product code and performed no push or merge.

**Evidence**

- Local receipts: `bin/mac-gesture-20261022/recording-root/qualified-checks.json`, `closure-checks.json`, `foreground-check.json`, `ci-checkpoint.json`, `build.json` and `freeze.json`.
- [Three-stream closure criteria](PRODUCTION-CLOSURE-2026-10-24.md), [original parity matrix](parity-matrix.csv), [earlier detailed status](STATUS-REPORT-2026-10-24.md), [precise-note implementation](PRECISE-NOTE-LIVE-2026-10-23.md), [instrument-bank handoff](INSTRUMENT-BANK-HANDOFF-2026-10-24.md).

The Mac reference defines the target, with live screenshots, supplemental renders, verified behavior and untested claims kept distinct. This is a current status assessment, not a new audit of every matrix row. Source identities and executed receipts take precedence over dated claims. Local ignored `bin/` receipts must be retained separately when handing off qualification.
