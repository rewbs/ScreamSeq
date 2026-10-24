**Production closure for the three requested streams**

The active goal is now to complete navigation/composing workspace, mixer/graph/
plugin editing, and the precise-note timeline, then deliver a production-ready
build. The full 117-row parity programme remains background context, not a reason
to add unrelated features before shipping these streams. Preserve native UI/audio,
project compatibility and the one-hour coordinated build cadence.

| Stream | Closure requirements | Evidence to use and finish |
| --- | --- | --- |
| Navigation and composing workspace | Pattern is central; mixing graph is directly reachable beside it and can float/dock. Panel versus window actions are clear. Default and compact layouts stay usable after resize/maximize/restore. Sidebar can hide and is contextual. Navigation retains selection/drafts and text focus; Tab/Shift+Tab/Backspace work in their intended controls. | Existing navigation, contextual-panel and native workspace tests cover much of the behavior. Fresh Computer Use on the final candidate must verify the visible routes and focus. Fix concrete failures, without a new navigation redesign. |
| Mixer, graph and plugin editing | Mixer controls apply during playback. Nodes and wires are discoverable and editable; connector Backspace, connection gestures and node removal work. Parameters expose effective sliders, up to ten editable rows and search. Instrument assignment reaches plugin management and preserves sample/plugin layering. Ordinary edits need no redundant Apply; explicit previews have clear acceptance. Undo/Redo, Save/reopen and rejected edits preserve unrelated music. | Reuse unchanged graph, mixer, plugin and layering evidence; run affected native/app cases and a fresh visible composing journey. The instrument-bank correction is included as an existing composing handoff, not a new feature stream. |
| Precise-note timeline | Double-click inserts; selected-hit Backspace removes; text Backspace remains text editing. Playback cursor advances. Field/drag edits apply without Write note or restarting playback. Border handles, focus, Save, Undo/Redo and reopen retain exact notes and unrelated channels. | Earlier live journeys and foreground receipts are valid only for unchanged inputs. Recheck the final app with playback, exact epoch/state evidence, and native focus assertions where the UI helper omits child controls. |
| Production integration | One reviewed source on shared main builds for both native platforms. Targeted functional gates pass; required shared/compatibility checks remain intact. Windows playback and sustained UI use have no unresolved release-blocking regressions. Package the exact Release binary with scanner/runtime/notices, source and verified hashes. | Current failures remain failures. Resolve the queued recorder/export/interface corrections, determine the Mac Intel cabinet timeout/cancellation outcome, and investigate measured Windows audio overruns only as needed to establish release readiness for these composing workloads. |

The single `recording-root` checkpoint started on24 October at approximately
13:49 Pacific (20:49 UTC). It builds frozen `946c61af7` once, with five native
groups and four actual-app cases; PR7 was advanced to that same source. Subsequent
builds must be at least one hour after the latest actual start in this coordinated
checkpoint, including CI. Inspect receipts before any launch or retry.

Do not expand this pass into arbitrary vendor certification, a new profiling
framework, another architecture extraction, or every historical parity row.
An unsupported platform-only plugin is not a reason to emulate its native API.
Existing failure/preservation assertions stay strict. A genuine defect in a
requested journey gets a focused correction and the cheapest meaningful checks.

This is a closure boundary, not a completion claim. Record final executed results
against these requirements and provide the usable executable path when qualified.

**Audio regression triage, 24 October21:30 UTC.** A matched ARM64 comparison
used the unchanged fifteen-effect fixture,48kHz/480frames and60s per process.
Previously integrated97bd596a4 produced14 deadline overruns,15.240ms maximum
callback and zero starvation/device errors. Candidate946 produced4 overruns,
11.8364ms maximum and zero starvation/device errors. Both exited normally with
unchanged binary/project hashes. The retained97 build is an ancestor of main;
its NativeEffects, CabinetSimulator, Oversampler, HostedAudio, Windows audio
device and HostedProject rendering sources are unchanged through current main.
This establishes that overruns on this workload predate this batch, not that
every timing regression is excluded or that the candidate has a measured speedup.
See `recording-root/main-ancestor-comparison/receipt.json` and both audio reports.

Keep the original workload and its failed timing measurements. The cabinet
isolation experiment does not replace it. Avoid an unrelated DSP rewrite merely
to eliminate an existing main limitation while closing these UI/editing streams.
Final qualification must still verify continuous playback, accepted live edits,
transport/history preservation and no new processor/device failures; disclose
the dense-effects timing limit. Broad sustained realtime/presentation performance
remains unqualified. No test assertion or measured threshold is changed by this
regression classification.
