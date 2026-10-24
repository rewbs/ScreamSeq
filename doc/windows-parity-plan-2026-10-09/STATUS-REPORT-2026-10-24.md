**ScreamSeq Windows and macOS parity status**

As of 24 October 2026, approximately 11:58 a.m. Pacific (18:58 UTC).

The goal remains active and incomplete. Windows has moved well beyond the
original safety-foundation phase: substantial navigation, graph, plugin,
recording, precise-note, automation and composing workflows are implemented.
Several complete composing journeys pass, including interleaved note and plugin
history. However, the newest fixes are unbuilt, sustained audio performance is
not qualified, and the current cross-platform CI results do not support a release
or merge to main. We are not still working only on P0b.

The immediate priority remains plugin recording and complete composing journeys,
with one coordinated build checkpoint per hour at most. “Implemented,” “passed
targeted checks,” “packaged,” and “integrated on main” are different states below.
There is no defensible overall completion percentage: the 117 reference rows
overlap and have not all received final behavioral and foreground acceptance.

**What exists and what remains**

| Area | Current position | Remaining work toward the goal |
| --- | --- | --- |
| Navigation and composing workspace | Pattern remains the centrepiece; adjacent/dockable mixing graph, explicit Float/Dock, panel/window navigation, contextual sidebar and retained inspectors are implemented. Prior isolated UI checks cover navigation, resizing and selected focus paths. | Complete normal/compact/DPI/accessibility coverage; verify changed paths on the next executable. Floating-window typing remains partly limited by the Computer Use helper targeting Main. |
| Mixer, graph and plugin editing | Mixer strips, node/connection editing, parameter sliders/pages/search, instrument plugin assignment and sample/plugin layering have implementation and targeted evidence. Dense graph framing and connected automation/activity routes have subsequently improved. | Reconcile every graph/reference workflow against current source and acceptance evidence; finish vendor/device coverage and sustained playback/presentation qualification. A visible control alone is not parity. |
| Precise-note timeline | Double-click insertion, selected-hit Backspace, playback cursor and automatic edits during playback are implemented. Earlier actual-app and Computer Use journeys cover live editing, Save, Undo, focus and unchanged playback epoch. | The newest recorder/instrument batch needs fresh checks; broad timing/performance and all input/layout combinations are not closed by those earlier passes. |
| Plugin recording | Record/Stop/Keep/Discard, original-time input handling, retained take/recovery review, conflict refusal, chronological Undo/Redo and save/reopen are implemented. The latest local full composing/recording case passed. Mac AU/VST3 lifecycle checks also have passing evidence. | Qualify the unbuilt recorder root-keyboard corrections; finish sustained/native-device and vendor coverage. Preserve stopped/closed-editor recovery restrictions where live opaque-state restoration is not supported. |
| Automatic editing and connected journeys | Routine fields increasingly apply without a separate Apply action; Save/history/departure paths preserve queued work and drafts. New instrument-bank handoff and clearer Use preview/Retry actions are committed. | The instrument changes are source-only. Validate invalid-field focus, exact bank capture, retained previews and separate history steps. Audit remaining workflows rather than assuming every Apply path has been removed. |
| Full-song export | Shared WAV export, native commands, accepted-song rendering and bounded long-operation replies are implemented. The latest complete composing case includes export, and the live-preview/export controller case passed. | Rebuild the corrected export oracle on all platforms. The old cross-block equality requirement contradicted independently measured stock OpenMPT behavior; exact PCM at each matching partition remains required. |
| Shared architecture and project compatibility | Shared editing/API/history, hosted processing and typed project preservation have concrete modules in `editor/`, `editor/hosted/` and `editor/project/`; native UI/device integration remains separate. Earlier reciprocal edited-project exchanges passed both Mac architectures and Windows return legs. | Reuse that evidence only for unchanged inputs. Final integrated fixtures, stable IDs, opaque plugin state and compatibility/error paths remain acceptance requirements. No JUCE or replacement cross-platform UI/audio framework has been introduced by this work. |
| Final integration | Both platforms still converge on shared main; current work is on a temporary feature branch. | Complete the relevant gates, review and merge the accumulated changes, then qualify/package the integrated source. The original 117-row inventory and A01–A18 acceptance scope remain open overall. |

**The four versions currently in play**

| Version | Verified identity and status |
| --- | --- |
| Integrated main | `0fe8f34008fe6517d7598affc31a4948a2ab14b8`, verified against the remote during this report. |
| Latest user package | `Downloads/ScreamSeq-Windows-ARM64-composing-9c5c6ebb1/ScreamSeq.exe`. It includes the requested precise-note interactions but predates later recording, export and instrument fixes. Its current SHA256 matches `72f42e436329d9581f886ed1b7dbb70f604886a8b93dddd2577d679f7c88c801`. It is a usable candidate, not a declaration of complete parity. |
| Latest built candidate / PR head | `066eb70524e6fdef18a4215d8dfd0fd03bcf30aa`. Local ARM64 compilation passed. [PR 7](https://github.com/rewbs/ScreamSeq/pull/7) is open, draft and unmerged. Its description has fallen behind the accumulated implementation and needs revision before review. |
| Latest implementation source | `946c61af7a3dde87c87500a9837cdc81d69292d9`, committed locally, backed up to `codex/mac-gesture-next-checkpoint`, and present in the clean qualification checkout. It has no build/check receipt yet. Report-only documentation changes are separate from this frozen implementation. |

PR 7 currently contains 188 commits and changes 224 files. That is a material
review and integration burden; a successful local compile alone will not justify
merging it. The local implementation also contains changes beyond its PR head.

**Latest verification results**

All results in this table concern built source `066eb7052`, except the explicitly
identified independent stock comparison. The pending fixes cannot inherit passes
for the code they change.

| Check | Result and implication |
| --- | --- |
| Local ARM64 build | Passed once, about 104 seconds. No new build was run to generate this report. |
| Local targeted native checks | 2 of 4 passed: live-preview/export controller and scratch setup. Recorder root entry and the dry export partition assertion failed. Source corrections are committed but unbuilt. |
| Local actual-app checks | Both passed: sample-recorder setup/retention and the complete composing/plugin-recording journey. These do not override the failed native aggregate. |
| Windows x64 CI | Compiled; 161/162 portable/worker tests and 52/53 native groups passed. The 122-case app run reported 25 failures and one skip: 22 distinct live-startup failures plus three secondary teardown failures, with no usable audio endpoint in the recorded diagnostics. The result remains failed. |
| Mac ARM CI | Compiled; 134/135 CTests passed, with the export assertion failing. Native plugin lifecycle and socket/typed checks passed. The interface harness failed a fixed-delay plugin retry assertion; a bounded-completion test correction is unbuilt. |
| Mac Intel CI — newly confirmed | Compiled; 133/135 CTests passed. Export failed and `cabinet` timed out at 120 seconds. All 37 interface groups passed, including plugin recording. The job was subsequently cancelled during typed/socket conformance. Cancellation cause and cabinet timeout need investigation; neither is a pass. |
| Independent pinned OpenMPT comparison | Passed 90 exact PCM comparisons at three sample rates and three block sizes, with zero intercepted allocation/free/lock calls. It reproduced the cross-block differences, supporting correction of the contradictory test expectation without changing DSP or introducing a tolerance. |

Recent Computer Use confirmed that the isolated sample recorder opens, is
readable and initially focuses Input. It did **not** qualify floating keyboard
continuity: sending Tab through the Main-targeted helper reactivated Main. Earlier
bounded UI evidence covers precise-note live gestures, Save caret, text Backspace,
Tab/Shift+Tab and native plugin Record/Stop/Keep. Screenshots do not prove absence
of temporal flicker.

Audio remains the principal release-quality concern. The latest foreground
silent run lasted 253.4 seconds at 48 kHz / 480-frame periods and recorded
34 deadline overruns, five starvation indicators and no device errors. A separate
60-second diagnostic recorded five overruns, all dominated by engine rendering
(which includes routing and instruments). This narrows the investigation but does
not identify a specific processor or distinguish CPU work from scheduling delay.
Neither run passed sustained presentation or realtime qualification.

**Next steps, in dependency order**

1. Inspect the completed Intel timeout/cancellation evidence and confirm there
   are no newer competing build starts. The old 09:05 UTC minimum gate has elapsed;
   it is not evidence that a build has occurred or that validation has passed.
2. Run the prepared single build of frozen `946c61af7`, followed by five native
   groups and four actual-app cases. This combines recorder keyboard, export,
   Mac retry-test and instrument-bank corrections. Keep all prior failed receipts.
3. Use a fresh isolated QA process for recorder keyboard entry, typing/caret,
   precise-note/plugin composing, instrument Properties-to-bank and compact/wide
   preview/retry behavior. Preserve the musician's session and audio route.
4. Investigate the measured engine-stage deadline overruns with narrower profiling;
   make evidence-supported changes and repeat the affected sustained workload.
   Resolve Windows CI audio availability without replacing real-device assertions
   with skips or simulated success.
5. Finish the broader acceptance inventory, required Mac/Windows and reciprocal
   checks, update the PR description, review the integration, merge to shared main
   and produce a fresh verified Windows package.

No product decision currently requires user input. Engineering and qualification
work remain. No build, product edit, push or merge was performed while producing
this report.

Evidence anchors: [original plan and acceptance scope](README.md),
[117-row matrix](parity-matrix.csv), [export and recording checkpoint](AUDIO-EXPORT-2026-10-24.md),
[precise-note evidence](PRECISE-NOTE-LIVE-2026-10-23.md),
[instrument handoff source](INSTRUMENT-BANK-HANDOFF-2026-10-24.md).
Machine receipts are under `bin/mac-gesture-20261022/recording-observation/`
(`build.json`, `qualified-checks.json`, `native.xml`, `completed-ci.json`,
`foreground/diagnostic-evidence.json`, `stage-profile-corrected/audio-report.json`),
`recording-root/freeze.json`, and `recorded-viewport/reciprocal-exchange.json`.
The planning README and prior continuation header contain superseded “current”
hashes; this report uses the remote heads and these receipts instead.
