# ScreamSeq development map

ScreamSeq is the renamed Resonance application and an independent derivative of OpenMPT. The upstream history remains intact. Historical reports retain their original names. Current behavior is defined by source and tests; the original Renoise feasibility table describes the starting point, not today's completion status.

## Shared and platform boundaries

| Area | Source | Responsibility |
| --- | --- | --- |
| Module engine | `soundlib/`, `sounddsp/`, `common/`, upstream support libraries | OpenMPT playback/format compatibility; native extensions use `OPENMPT_EDITOR_CORE` |
| Song/edit model | `editor/TrackerDocument.*`, `NativeSong.*`, editing helpers | Transactions, snapshots, stable IDs, precise notes, musical timing, shared DSP and validation |
| Routing/DSP | `editor/MixerGraph.*`, `MixerRuntime.*`, `SignalGraph.*`, `SignalRuntime.*` | Portable plans, delay compensation, modulation and bounded processing |
| macOS host | `mac/Audio/` | Core Audio, CoreMIDI, AU/VST3 hosting, plugin adapters, graph instances and export |
| macOS session/API | `mac/Bridge/` | Objective-C++ bridge, validation, API dispatch, plugin state and native serialization |
| macOS UI | `mac/App/` | AppKit controls, retained docks, Metal pattern grid, inspectors and local API server |
| Windows sibling | `windows/` (to be developed) | Native Windows UI, device/plugin adapters and local API transport; shared musical semantics |

The renderer and document are separate. Edits occur on the document worker; playback owns its prepared copy. AppKit controls belong to the main thread. Plugins and graph recipes must be prepared outside audio processing, and unsafe structural mutations must not race a live renderer. Do not move Foundation/AppKit into portable `editor/` code.

Sample-instrument graphs are prepared independently per instrument/raw channel and feed the ordinary mixer through sample-only OpenMPT adapters. Their NNA voices keep their original routing. `NativeSignalGraph` handles both that stage and channel/group graphs with a shared 256-processor/256-MiB host-storage budget. Graph automation sources store per-pattern curves using stable IDs, compiled formulas and the shared evaluator. Both additions require native metadata 13. See `mac/GRAPH_WORKFLOW.md` for the current signal order, activity commands, editing semantics and limits.

The current native project wrapper is a versioned binary property list containing an exact song snapshot, metadata and plugin state. Metadata and container versions are separate. Windows needs a compatible portable codec (or a carefully extracted shared persistence layer), rather than treating `.screamseq` as a renamed module or silently dropping native fields. Plugin recipes use stable class identity; local paths are resolution hints. AU remains macOS-only. Missing platform plugins should preserve opaque state and be reported, not replaced silently.

## Build and qualification

Build on macOS with `SCREAMSEQ_BUILD_DIR=bin/mac-screamseq SCREAMSEQ_BUILD_JOBS=4 bash mac/build.sh`. Output is `ScreamSeq.app`; `RESONANCE_BUILD_DIR` and `RESONANCE_BUILD_JOBS` remain accepted aliases. Set `RESONANCE_DEVELOPMENT_BUILD=1` for a separate development bundle identity. AppKit/Metal and Core Audio are the current native foundation; keep high-frequency drawing out of layout-heavy per-cell view trees.

`ctest --test-dir <build> --output-on-failure` runs native regressions. `mac/test-interface.sh` builds/tests the AppKit editors and can save snapshots. `mac/Tests/test_automation.py` exercises the socket protocol and actual application; inspect its flags before invocation. Workspace, startup, recovery, sample-library, plugin and Core Audio loopback checks have dedicated entry points in `mac/Tests/`. The qualification skill describes safe instance handling and evidence limits.

`BuildInfo.json` records source hashes in the app bundle. `mac/Tools/build_manifest.py` includes currently untracked native additions. `mac/Tools/bundle_notices.py` packages attribution and user/API guides. Required VST3 interface sources are in `mac/ThirdParty/vst3/`; do not replace them with an unpinned machine-local SDK dependency.

## Editing and API invariants

- Native IDs are stable across insert/reorder; indices are transient views. Whole-collection API writes need read/merge/write.
- Mutation requests carry `expectedRevision`; context changes also guard context revision. Document and plugin Undo domains are currently distinct.
- Precise notes and graph commands use 65536 units/row. Pattern automation points use 256 units/row. Beat offsets use the current pattern signature, not a fixed assumption of four rows/beat.
- Scripted curves are precompiled bounded mathematical expressions. No general-purpose interpreter executes in the audio callback.
- VST3 automation uses sample-offset parameter queues within normal blocks. Continuing tracker effects still follow ordinary ticks.
- Sample/instrument data, note-on/off/cut semantics, NNA, routing and exact native sample payloads must survive save/reopen and Undo. The current sample voice/storage path remains 8/16-bit.
- The API schema retains its legacy filename for compatibility. Prefer the `screamseq_api.py` entry point; existing `resonance_api` imports continue to work.

## Workflow and source control

The root `.agents/skills/` contains the maintained project skill sources; copies can be installed under the user's Codex skills directory. Use separate worktrees/checkouts for concurrent Mac and Windows agents, and coordinate shared model/API/format edits explicitly. Neither agent should overwrite the other's platform tree or invent divergent musical semantics.

Keep binaries, build caches, private sample packs and user songs out of Git. Include reproducible fixtures and licenses. Old local qualification documents may contain absolute paths: prefer repository-relative references in new documentation. Do not update upstream OpenMPT's Windows product branding just because the ScreamSeq sibling is renamed.
