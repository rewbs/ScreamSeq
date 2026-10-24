# Goal status — 24 October 2026

**Subsequent source continuation:** both Mac architectures have now completed
successfully. The [selection-render reply correction](SELECTION-RENDER-REPLY-2026-10-24.md)
and its regression are now prepared in source for the same batch as the Discard
guard. Neither correction is yet qualified. The snapshot below retains the
observed state at report time, including its original failed execution evidence.

Snapshot: **22:36 UTC / 15:36 Pacific**. Refreshed from current source, Git state, retained receipts and live GitHub job/PR metadata. This supersedes earlier snapshots. Preparing this report did not run builds/tests, modify product code, push or merge.

**The three priority streams are substantially implemented and have targeted functional evidence. The production-ready goal remains incomplete. Two concrete release blockers, final platform acceptance, integration and packaging remain. Full macOS parity is a broader, unfinished programme.**

The active goal is to complete navigation/composing workspace, mixer/graph/plugin editing and the precise-note timeline without unrelated expansion, then deliver a production-ready Windows build. A percentage would conflate implemented features, tested workflows and delivered software.

## Requested experience

| Stream | Implemented behavior and evidence | Remaining boundary |
| --- | --- | --- |
| Navigation and composing workspace | Pattern-centred connected workspace; directly accessible adjacent graph with Float/Dock; contextual hideable sidebar; clearer panel/window actions; retained inspectors. Fresh Computer Use on source 828 at 200% DPI covered connected/compact layouts, maximize/restore and instrument-to-bank navigation. The bank opens on-screen and retains its chosen position. | Final corrected-candidate smoke and release acceptance. Comprehensive multi-monitor/DPI/accessibility coverage is not established. Floating-bank typing continuity was not qualified because the input helper targets Main. |
| Mixer, graph and plugin editing | Live mixer edits; node removal, connection editing and connector Backspace; effective parameter sliders, pages of up to ten parameters and search; instrument plugin assignment and sample/plugin layering. Tests cover history, persistence and unrelated routing. Fresh UI checks covered graph framing and live gain, pre-balance and width; unchanged detailed wiring, balance and parameter evidence is reused. | Complete x64 layered-instrument journey fails during selection rendering. No broad third-party plugin certification or temporal-flicker guarantee. |
| Precise-note timeline | Double-click insertion, selected-hit Backspace, separate text deletion, playback cursor and automatic field/drag edits without Write note or transport restart. Automated journeys cover exact history/reopen and unrelated music. Fresh UI checks covered gestures, typing, Save caret and Tab/Shift+Tab; four API snapshots retained playback epoch 3 with advancing frames. | No outstanding failure in the tested precise-note journeys. Final corrected-source integration remains. This does not prove every input/layout combination. |
| Plugin recording and composing journeys | Record/Stop/Keep/Discard, original timestamps, conflict refusal, retained recovery and unified history exist. Journeys combine note edits, recording, Undo/Redo and reopen. Layered VST3 journey passed on ARM64. Latest x64 journey passed its six-domain composing/history/reopen stage before offline rendering failed. | Completed-take Discard guard is source-fixed but unbuilt. Selection-render deadline remains unfixed. Opaque recovery retains stopped/closed-editor safeguards. Instrument property changes still stop playback under the current contract. |
| Shared architecture and compatibility | Musical model, editing, validation, history, timing, routing and preservation remain shared; UI, devices and plugin hosting remain native. Reciprocal fixtures passed, including opaque state, composing history and bank data. | Evidence reuse requires unchanged relevant inputs. Stable identities, plugin state, project compatibility and OpenMPT attribution remain mandatory. |

## Concrete blockers

1. **Already-kept take can bypass recovery rules through Windows Discard.** The native UI hides Discard in this state, but its API lacked Mac's refusal. Reproduced on 828. Commit `63e0f290d` adds the refusal and native/actual-app regressions; it is unbuilt. Required checks must prove rejection retains accepted music, later input, baseline and revision, while explicit recovery and exactly one Undo/Redo still work. See [finding and evidence](KEPT-TAKE-DISCARD-2026-10-24.md).
2. **Selection rendering exceeds Windows' five-second API reply deadline.** The exact 828 x64 CI executable and matching VST3 fixture ran locally with a real silent WASAPI endpoint. Composing/history/reopen progressed, but `sample.renderSelection` returned “Request began but reply timed out; outcome uncertain, inspect before retry.” Current `windows/App/ApiDispatch.hpp` grants 120 seconds only to `document.exportAudio`; selection rendering still gets five. **The source correction is not present.** Proposed fix: use the same bounded reply budget for admitted selection renders, retaining five-second queue admission and uncertain-outcome/replay protections. Requalify the unchanged full journey. The failed run did not reach a final PCM equivalence verdict.

The Discard-only build waiter was stopped before building to combine these corrections. Its receipt records `stopped-before-build-to-batch-selection-render-reply-fix`. No build/check result exists for commit 63.

## Verification status

| Gate | Result at this snapshot |
| --- | --- |
| Windows ARM64 source 828 | Release compilation passed. Changed-surface native automatic-edit group and actual-app bank handoff passed. Earlier targeted coverage, including ARM64 layering, is reused only where inputs are unchanged. |
| Fresh foreground UI on 828 | Targeted functional pass: navigation, bank placement, graph framing, live mixer controls, precise-note gestures and keyboard/Save focus. Owned QA process exited normally. Receipt explicitly says production qualification is false. |
| Mac ARM CI on 828 | Completed successfully: 135 CTests, native interface, plugin lifecycle and socket/typed gates. |
| Mac Intel CI on 828 | Compilation, model/recovery/plugin tests and full native interface harness passed. Typed fixtures/socket conformance is running; overall success is pending. |
| Windows x64 CI on 828 | Compilation, 162 portable/worker tests, 53 native groups, ownership and typed-pipe gates passed. Actual-app suite failed: 22 distinct live-startup failures plus three secondary teardown failures, with one optional skip, on a runner reporting no audio devices. Workflow remains failed. |
| Real-endpoint x64 follow-up | Earlier 21 affected cases passed on the prior exact candidate with unchanged relevant inputs. The previously missing layered case ran on exact 828 plus its provider and failed at the selection-render deadline. No case is waived or uncertain write blindly repeated. This uses x64 emulation on ARM64, not physical x64 hardware. |
| Reciprocal preservation | Mac ARM save and Windows return passed on retained 946 binaries: fixtures 01–05, rendered/imported files, opaque state, composing history and bank data. Earlier Intel evidence is separately bounded. |
| Review/integration/package | Focused review found the Discard defect. Review cannot close until corrections pass. PR7 remains draft/unmerged; no final production package receipt exists. |

Current workflows: [Mac ARM/Intel](https://github.com/rewbs/ScreamSeq/actions/runs/40136080736), [Windows x64](https://github.com/rewbs/ScreamSeq/actions/runs/40136080789). These are snapshot results, not predictions.

The fifteen-effect foreground workload still exceeded audio deadlines: **37 overruns over 232.5 seconds**, at 48 kHz / 480 frames, with zero starvation indicators, zero device errors and no processor fault. A matched comparison reproduced this overload class on an integrated main ancestor. That establishes a pre-existing limitation, not improved performance or absence of all regressions. Sustained realtime performance and temporal-flicker absence remain unqualified; screenshots cannot establish either.

## Source and delivery state

- Shared main: `0fe8f34008fe6517d7598affc31a4948a2ab14b8`.
- Built/PR head: `82834c95bd7a06fc8f9ad62ca3cf1a1ac1d0bded`.
- Local HEAD: `63e0f290d6114127e319a55922dd03e331b2fd3d` on `codex/mac-gesture-intake`, two commits ahead of the PR branch: documentation and the Discard correction. Working tree was clean before this report edit. Qualification checkout is also at 63, unbuilt.
- [PR7](https://github.com/rewbs/ScreamSeq/pull/7): open, draft, mergeable; 201 commits and 234 changed files. Mergeability is not release readiness.
- Built ARM64 executable SHA256: `e4500fdb8bbb00939fc0be0533c0f32bbfdcd0c3d0d138c8fc31c6b4b4f5acbf`. It predates the Discard correction.
- Previously delivered executable: `C:/Users/P14/Downloads/ScreamSeq-Windows-ARM64-composing-9c5c6ebb1/ScreamSeq.exe`. It predates later recording/export/instrument/bank corrections and is not the completed release.

## Shortest remaining path

1. Implement the focused selection-render reply correction and combine it with the Discard guard. Preserve fixture size, musical equivalence and failure assertions.
2. At one coordinated hourly checkpoint, qualify the combined source with dispatch/lifecycle regressions, the complete layered composing journey and necessary native platform builds. Inspect prior receipts and terminal CI first; reuse valid unchanged evidence.
3. Perform fresh isolated Computer Use on the corrected executable for composing, recording, graph/mixer navigation and keyboard focus. Preserve the musician's session and audio route.
4. Close platform, recovery, compatibility and review gates; merge reviewed source to shared main, coordinating merge-triggered CI with the hourly cadence.
5. Package the exact qualified ARM64 Release with scanner, runtime, notices, provenance and verified hashes. Deliver its executable path with the remaining performance limits.

The last recorded earliest build time is 22:52:17 UTC; later starts and active CI must be checked again. This report started no build. There is no defensible guarantee that one more build will pass.

## Broader macOS parity

This release scope covers the immediate composing priorities, not every original requirement. The [117-row matrix](parity-matrix.csv), command/API inventories and [acceptance recipes](README.md) remain the wider audit baseline. They contain historical observations and must be reconciled with current implementation before declaring remaining features absent or complete. Exhaustive keyboard/accessibility, multi-monitor/DPI, vendor-plugin/device and sustained presentation/performance coverage is not established. A current whole-programme completion percentage is unsupported.

No product-design decision currently requires user input. The immediate work is to resolve and validate the two blockers, then finish integration and delivery rather than add feature streams.

## Evidence locations

All paths below are relative to `bin/mac-gesture-20261022/`:

- `bank-closure/`: build/check/foreground receipts, Mac ARM acceptance and review progress.
- `bank-closure/ci-x64/`: verified executable/provider identity, failed cases and `layered-live-checks.json` / `layered-live.log` with the newly observed timeout.
- `bank-closure/kept-discard-baseline/`: original failing Discard reproduction.
- `kept-discard/`: frozen source and stopped waiter receipts; no completed build/check receipts.
- `recording-root/`: prior targeted acceptance, layered ARM64 checks, prior x64 replay, reciprocal exchange and matched main-ancestor audio comparison.

These are retained local execution artifacts. Source identity and relevant unchanged inputs govern reuse; dated narrative claims alone do not establish completion.
