# Mac parity audit restarted from current main

Baseline: `4885bb9997393bcf072639358da3a900a20efb01`, fetched from
`rewbs/ScreamSeq` on 28 October 2026. Branch: `codex/mac-parity-current-main`.
This audit supersedes the earlier attempt based on `ffe81aa4b`. Its test
results do not qualify this branch. The original work remains preserved in
local commit `0adfbb0db` for selective reuse.

The primary checkout contains other sessions' changes and has not been reset.
The dynamic left context menu is owned by a separate session and is excluded
from this implementation. Native platform conventions remain appropriate;
Windows-specific HWND, WASAPI, WinMM and DPI implementation is not copied into
AppKit/Core Audio/CoreMIDI.

## Audit inventory

The fresh baseline contains 590 commits after the former baseline. Of these,
384 touch `windows/`; 68 also touch Mac sources and 84 touch shared sources.
These counts describe the review inventory, not completion. The local receipt
`bin/mac-parity-current/evidence/windows-commits.json` records each commit,
subject, changed files and review status. Earlier Windows features are also
checked against the current platform implementations.

| Area | Current source assessment | Work / verification remaining |
| --- | --- | --- |
| Named layouts and command search | Named storage/manager, Connected and Graph editing presets, categorized search and keyboard traversal ported. | First-batch tests and computer use passed; final integrated audit remains. |
| Independent Graph Curve | Independent pinned/dockable owner and staged capture ported. | First-batch tests, socket and computer use passed. |
| MIDI settings, capture and review | Guarded settings API, retained event review, ownership/loss handling, immediate Arm and busy intent ported. | First-batch tests/socket and Arm computer-use checks passed; remaining recording review edges are listed below. |
| Precise-note live publication | Mac now uses prepared live publication and Undo/Redo. | Offline/live/refusal tests and looped playback computer use passed. |
| Precise-note UI | Mac has automatic row editing; Windows adds cursor/edge hit testing, Save/history boundaries and queued navigation. | Port remaining behavior; retain invalid/newer drafts and exact target. |
| Arrangement and annotations | Event counts, keyboard/page traversal and captured annotation drafts ported. | Focused interface tests passed; final combined UI qualification remains. |
| Automatic pattern/graph/instrument edits | Both have automatic editing; Windows includes newer bank/Save/history handoffs. | Inspect each native boundary and port missing completion ordering. |
| Plugin recording and recovery | Current main includes substantial Mac implementation and shared input/recovery contracts. | Compare original-input review, Keep-to-edit, completion guards, loss and departure behavior. |
| Parameter Activity | Both have traces and recorded lanes; Windows has recent selection/search/keyboard/navigation fixes. | Compare field/list/canvas behavior, polling, history and target identity. |
| Mixer, routing and plugin controls | Shared live publication and native controls exist. | Compare all newer Windows interaction fixes, retained previews, graph gestures and search. |
| Samples, instruments and recorder | Mac native editors exist; Windows catch-up includes retained setup and keyboard fixes. | Verify current source and targeted UI/API behavior, including loop drawing and import failures. |
| Export, project codec and history | Main includes shared codec/streaming render, guarded exports and Mac adapters. | Check accepted-song boundaries, long replies, retained takes, atomic failure and reopen. |
| Track/row density, timing, pattern tools | Windows changes primarily implement counterparts of existing Mac tools. | Verify catalogs, stable identity, UI reachability, Undo and persistence. |
| Native menus/accessibility/theme | Platform-native implementations differ. | Verify equivalent keyboard/accessibility workflows; dynamic left menu stays with its owner. |

## Qualification state

The source baseline was refetched at 12:21 UTC on 28 October; `origin/main`
remained `4885bb9997393bcf072639358da3a900a20efb01` (zero commits ahead).

Full build 1 started at **07:17:23 UTC** and passed. Its exact source receipt,
build log and test logs are retained in `bin/mac-parity-current/evidence/`.
It passed **135/135 CTest tests**, **38/38 interface groups**, the full app socket
suite, and the Mac parity socket suite. Live precise-note tests used an explicitly
selected BlackHole 2ch output and passed edit/Undo/Redo continuity, refusal and
render-clock checks. Computer use in the isolated QA app verified retained MIDI
raw text while arming, named layouts with the independent Graph Curve, and precise
note edits/Undo while loop playback continued with zero reported audio overruns.
Screenshots are `cua-midi-arm.jpg`, `cua-graph-layout.jpg` and
`cua-live-precise-undo.jpg`. These checks qualify the first batch only.

Batch 2 adds the song-time automation editor, recorded-point automatic editing and
Activity search/freeze/navigation, ordered composing completion, instrument draft
ownership and Carry/Release Node controls, no-op instrument history preservation,
and retained revision-guarded Core Audio settings. Before its full build, Swift
whole-module type checking, Objective-C++ syntax checks, and **38/38 interface
groups** passed (`interface-batch2-frozen.log`). Full build 2 started at
**12:37:21 UTC** and passed; its frozen source hashes were verified unchanged.
The build passed 135/135 CTest tests, the complete app socket suite, the Mac parity
socket suite, and live precise-note publication/Undo/Redo/refusal checks with an
explicit BlackHole 2ch output. The live test also rejects an invalid output device
without interrupting the current renderer.

Computer use verified output selection retained across Refresh and truthful
48 kHz/128-frame readback; song automation invalid-field retention, navigation
guards, Undo/Redo, search retention and Command-S completion; instrument Carry,
Release Node and automatic edit completion before opening the bank; Activity
point relocation, saved field completion, search retention, freeze, keyboard trace
navigation and Return-to-recorded-source. API and saved-project readback confirmed
the UI writes. Evidence is in `cua-build2-*` screenshots/JSON receipts. One small
accessibility issue remains: the song automation timeline value label does not
refresh immediately after a numeric field write, although live/saved values do.
The first QA process ended during a gap in testing; it was relaunched with verified
inspection flags and history/persistence checks repeated. No crash report was
present; no musician process was replaced.
No further full build may start before **13:37:21 UTC**.

No complete current-main parity claim is made. Full builds must be at least one
hour apart, with computer-use validation after each completed build. QA uses a
uniquely identified disposable bundle and project; other running app sessions
and system audio defaults remain untouched.

## Findings from the first audit pass

The following findings were the initial implementation inventory. The qualification
section above records which have since been implemented; remaining closure and
readback edge cases continue under review.

- **Parameter Activity:** `ParameterActivity.swift` changes the watch on search;
  Windows keeps the observed parameter visible when it falls outside a name/unit/ID
  filter. Mac point fields lack automatic saving and completion ordering for
  selection, Delete, Save/history, source navigation and panel closure. Polling can
  replace recent-event row identities and selected field revisions. A clean recorded
  lane does not refresh from history when opened through Keep. Its trace has no
  keyboard source navigation, and a single constant sample draws no visible mark.
  Windows reference: `ParameterActivityWindow.hpp`, especially filterParameters,
  refreshCleanRecordedPage, deferPointSelection, queuePointEdit and flushForProjectSave.
- **Composing boundaries:** Mac pattern/graph envelope banks can open before their
  automatic save finishes. Save/export/history now drain precise notes in the first
  batch but still need equivalent pattern/graph/instrument/recorded-point ordering.
  New/Open/Quit must also preserve pending native drafts. Windows references:
  DocumentDraftRegistry, DocumentDepartureIntegration, ParameterAutomationWindow,
  InstrumentEnvelopeWindow and WorkspaceCommands.
- **Audio settings:** Mac opens a fresh basic form and closes after Apply; it lacks
  `audio.devices.get` and separately guarded `audio.settings.get/set`, retained
  choice/rescan and truthful negotiated-device diagnostics. Core Audio currently
  exposes actual sample rate/buffer through telemetry. Port appropriate native
  behavior without changing system defaults or borrowing WASAPI details.
- **Recording review:** current Mac already has original-input pagination, loss
  details, stopped restoration, captured Keep target, detached recovery and guarded
  no-replay lifecycle. Compare newer Windows first-input selection, retained scroll,
  queued Keep-to-edit during reads, failed-owner page clearing and root keyboard
  traversal. These are UI differences, not a reason to duplicate shared take storage.
- **Precise Notes:** live publication and project boundaries are in the first batch;
  keyboard timeline/list traversal, Escape drag restoration, live refresh on reveal
  and ordered hit selection still need comparison. Shared schedule PCM/audit tests
  already exist; the new Mac session test uses explicit BlackHole only when requested.
- **Plugin path repair:** Windows exposes guarded rack/graph path inspection, scan
  and same-class reconnect. Mac already resolves trusted inventory matches during
  project loading (`TrackerSession.mm`, `PluginTrust`), but no explicit repair UI/API
  counterpart was found. Verify the native trust contract and add explicit repair
  without class substitution or loss of opaque state. Rack editor API open/close and
  musical keyboard settings also have Windows API paths absent from the Mac catalogue.

A source-string API inventory found all other Windows musical method names in Mac
production sources. This establishes catalogue coverage only, not identical behavior
or qualification. Windows-only `midi.test.inject` is a test hook; CoreMIDI needs its
own bounded fixture rather than a production WinMM adapter.

## Additional source audit during build 2

- Sample recording duration is already equivalent: both platforms offer 60 and
  300 seconds (`SampleRecording.swift`, `SampleRecordingWindow.hpp`). No extra
  duration feature is needed. Endpoint duplicate names and retained setup still
  need interaction review.
- The Mac socket server already separates request input timeout from its 120-second
  dispatched-operation wait, and the bundled client defaults to 120 seconds.
  Windows' long offline-reply transport fix therefore does not require copying
  named-pipe lifetime handling. Long-running export remains a runtime check.
- Mac musical-keyboard preferences have a retained native form but still lack the
  guarded `workspace.keyboard.set` API and the Windows configuration's optional
  thirteenth upper key/custom Play-Stop binding integration. This is a confirmed gap.
- Mac plugin-location trust and automatic matching preserve saved hints, but do
  not provide explicit moved-path scan/reconnect with module-hash guards and Undo.
  Both rack and graph path repair remain open; this must not be reported complete
  merely because a trusted inventory can resolve a class on load.
- Remaining automatic-edit edges include Activity source navigation/panel close,
  mixer raw-field retention, sample settings, and failed waveform-stroke retention.
  These need source changes and focused checks after batch 2 qualification.

- **Song settings:** the Mac modal currently closes before validation/commit, losing invalid or refused title/channel drafts. Windows retains its native form and captured target. Add a retained form before declaring parity.
