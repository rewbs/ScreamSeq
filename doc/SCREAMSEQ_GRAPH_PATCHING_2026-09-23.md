# Graph patching, sidechains and group editing — 23 September 2026

## Implemented

- Named Main in/out and native auxiliary/detector sockets, with matching named port menus. Supported inactive ports remain visible as hollow sockets and activate on the playback copy when connected. Cable gestures keep the connection form on the actual endpoints. Song dB gain and reusable-graph linear gain reset to their correct unity values when changing scope.
- Channel 1 → compressor Main in moves the rack chain onto Channel 1; Channel 2 → Detector sidechain adds the separate detector signal, preserving Channel 2’s existing output. New built-in Compressor, Gate and Bus Compressor instances default to Auto (use connected sidechain). Existing instances have a graph action to select Auto. Third-party external-detector switches remain plugin settings.
- Multiple destinations per plugin output and multiple sources per input. Effect main-output taps preserve the original serial path and render the processor only once. Extra main inputs are summed immediately before the chosen insert; auxiliary detector buffers remain separate. Plugin output routes can feed track, group, return or Master buses, subject to cycle validation. Master remains the final sink.
- Shift/Command-click and marquee multi-selection, group drag/arrow movement, cancellation, and serial-chain insertion by dropping selected effects onto a highlighted audio wire. The prior main path reconnects automatically. Routing and node positions share one Undo step, preserving stable processor identities, settings, automation and unrelated modulation/auxiliary edges.
- Fresh socket drags add cables; selected wire handles reroute one cable. In the song view, normal rack Main-input dragging moves the chain, while Option-drag or “Mix into main” adds another channel to the existing insert. Rerouting an extra main-input wire retains its role instead of moving the rack chain.
- Agent API coverage: `graph.nodes.insert`; optional positions on `mixer.inserts.move`; `targets` arrays on plugin/instrument output routing; input 0 on `mixer.sidechains.set`. Shared validation, dry-run/revision guards, one-step Undo and save/reopen coverage. Both native API adapters and the schema were updated; no storage-format change.

## Routing boundaries

The song overview represents bus ownership and ordered rack inserts. Insert sidechain/extra-main sources are complete channel or return outputs. A separate plugin output must first enter a bus before feeding another rack insert’s detector. Unsupported direct gestures explain this step instead of silently substituting a different signal. Reusable subgraphs permit direct internal plugin-to-plugin patching.

Multiple mono/stereo audio buses are supported; surround layouts are not. A selection must be one serial effect chain to drop into a wire; ambiguous branches and disconnected selections are rejected atomically. Audio feedback cycles are rejected. Structural routing/port changes stop playback for preparation; layout-only changes do not.

Windows shares the operations and API semantics, but its UI and native execution were not qualified on this Mac. Commercial AU/VST3 plugins were not individually certified by this work; fixture plugins test the host bus behavior. Third-party plugins may impose additional bus-layout or detector-switch requirements.

## Qualification

- Final full CTest suite: **76/76 passed**, 40.50 seconds. Final AppKit interface suite passed, including additive sockets, named ports, automatic detector action, endpoint/form synchronization, gain-unit changes, group dragging, insertion dispatch and invalid direct-sidechain gesture rejection.
- Offline rendering verifies summed main inputs and independent detector buffers; plugin main/auxiliary fan-out; one processor invocation per block; latency alignment, mute and callback partition invariance. Covered rates include 44.1/48/96 kHz and buffer partitions from one sample through 4096 frames across the relevant fixtures. Realtime allocation/free/lock audits pass. AU and VST3 multibus fixtures validate route-driven activation without manual port-enable edits.
- Shared/API tests cover chain order, old-path healing, modulation preservation, invalid/ambiguous insertion rollback, revision guards, dry-run, one-step layout-plus-routing Undo/Redo and native save/reopen. Effect auxiliary tests now verify both main-output and auxiliary taps against exact aligned impulses, rather than assuming main taps are forbidden.
- Actual UI: dragged Track 1 Main out into Compressor Main in, then Track 2 into its named Detector sidechain. API readback verified separate sources and preserved Track 2 output. Marquee-selected two effects, moved them together, dropped them into Input → Output, then verified keyboard Undo restored the complete pre-insert graph JSON and Redo restored the chain. Saved the disposable song. A fresh final bundle confirmed the corrected From/To/port display, library Gain × 1, and arranged/fitted the chain.
- The visible 60-second combined playback/workspace workload ran on **BlackHole 2ch, 48 kHz / 512 frames**, with **295 live edits, 3 saves and 14 Undo/Redo cycles**. **5,630 callbacks, zero overruns, no renderer fault and no plugin failure.** Maximum callback was **2.389 ms** against a **10.667 ms** deadline. This is callback timing evidence, not a physical-device latency or audible-loopback measurement.
- **Display-performance gate failed:** mean presentation **55.00 fps**, p99 interval **50.00 ms**, maximum **116.67 ms**. Pattern-grid CPU p99 was **0.100 ms** and GPU p99 **1.706 ms**. The machine remained under concurrent load (load averages 18.25 / 33.90 / 23.71 during the run). This does not prove the cause of the stalls or qualify idle 60 fps. The workload measures pattern presentation while graph processing and workspace panels are active; it is not a standalone animated-canvas cadence benchmark. Thresholds were not relaxed.

Evidence: `doc/mac-native-qualification/2026-09-23-graph-patching/`. The disposable `.screamseq` fixture remains in the build’s `qualification/` directory.

## Package

`bin/mac-graph-patching/ScreamSeq.app` — development bundle, separately built from the musician’s running app. Save and quit the existing session before switching builds.

Signature verification passed (`codesign --verify --deep --strict`); all **480** recorded source hashes matched the final checkout. Base revision: `d5d376fa0ced527cc901d8555076cadbf98d75d4` (working changes are not committed by this task).

Source fingerprint: `be14f47d5f247d0ff418dcf97bad2d004a89ba7f1334fcf396fcd63e23b78694`.

Executable SHA-256: `9d8319fedac972692ef2b98ee8a2098084f3f1c3f125b24eafde03cd695e0d72`.

The musician’s existing `bin/mac-graph-rewire/ScreamSeq.app` process (PID 47040) was preserved. Manual checks and playback qualification use separately identified QA copies and disposable songs. No system audio default was changed.
