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
| Windows sibling | `windows/` | Win32/Direct2D editor, document worker, WASAPI, VST3 provider and PID-scoped named-pipe API; see `windows/App/INTEGRATION.md` for supported features and limits |

The renderer and document are separate. Edits occur on the document worker; playback owns its prepared copy. AppKit controls belong to the main thread. Plugins and graph recipes must be prepared outside audio processing, and unsafe structural mutations must not race a live renderer. Do not move Foundation/AppKit into portable `editor/` code.

Native voice positions are published by `Renderer` through bounded atomic snapshots. UI/API readers consume sample frames and volume/pan/pitch envelope ticks without reading live engine channels or blocking audio. `pattern.timeline.get` uses the editor-only `GetLengthTarget.onRow` observer on the document worker to collect first-visit times in one engine walk; never calculate row timing in drawing or from a fixed BPM assumption. The ruler's order occurrence matters when a pattern repeats.

A non-master mixer bus may have output zero to disconnect its main route while retaining sends and processors. Such projects require native metadata 15; ordinary connected projects retain their existing metadata requirements. Deleting a disconnected group moves its retained inserts/plugin outputs to master and leaves formerly connected child main outputs disconnected.

Sample-instrument graphs are prepared independently per instrument/raw channel and feed the ordinary mixer through sample-only OpenMPT adapters. Their NNA voices keep their original routing. `NativeSignalGraph` handles both that stage and channel/group graphs with a shared 256-processor/256-MiB host-storage budget. Graph automation sources store per-pattern curves using stable IDs, compiled formulas and the shared evaluator. Both additions require native metadata 13. See `mac/GRAPH_WORKFLOW.md` for the current signal order, activity commands, editing semantics and limits.

The current native project wrapper is a versioned binary property list containing an exact song snapshot, metadata and plugin state. Metadata and container versions are separate. `windows/Project/` implements the compatible portable codec and preservation-aware atomic saves. Plugin recipes use stable class identity; local paths are resolution hints. AU remains macOS-only. Missing platform plugins preserve opaque state and reject playback preparation rather than being replaced silently.

## Build and qualification

Windows: `./windows/build.ps1 -Architecture ARM64 -BuildDirectory bin/windows-dev -Test`.
Use `-Fresh` after a failed initial compiler configuration. The app and VST3 scanner
are in the selected build's `Release/` directory. Native sample editing, offline
hosted renders and private-PID integration tests live in `windows/Tests/`; current
evidence and remaining parity work are recorded in `windows/RESUME_PROGRESS.md`.

Build on macOS with `SCREAMSEQ_BUILD_DIR=bin/mac-screamseq SCREAMSEQ_BUILD_JOBS=4 bash mac/build.sh`. Output is `ScreamSeq.app`; `RESONANCE_BUILD_DIR` and `RESONANCE_BUILD_JOBS` remain accepted aliases. Set `RESONANCE_DEVELOPMENT_BUILD=1` for a separate development bundle identity. AppKit/Metal and Core Audio are the current native foundation; keep high-frequency drawing out of layout-heavy per-cell view trees.

`ctest --test-dir <build> --output-on-failure` runs native regressions. `mac/test-interface.sh` builds/tests the AppKit editors and can save snapshots. `mac/Tests/test_automation.py` exercises the socket protocol and actual application; inspect its flags before invocation. Workspace, startup, recovery, sample-library, plugin and Core Audio loopback checks have dedicated entry points in `mac/Tests/`. The qualification skill describes safe instance handling and evidence limits.

Quit must drain document/recovery work asynchronously, then call `TrackerSession.shutdown()` on the main thread before AppKit exits. Stopping transport retains plugins; ARC teardown of the app controller is not guaranteed before vendor static destructors. `plugin-shutdown-tests` checks retained-session teardown (`--ui` also covers rack and graph recipe editors). `SCREAMSEQ_BUILD_DIR=<build> bash mac/test-shutdown.sh` checks the actual NSApplication Quit path, including pending worker calls that need the main thread and recovery writes, without audio or visible windows.

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

Envelope reuse lives in `editor/EnvelopeBank.hpp/.cpp`: song-local templates,
resolved stable target links, fitting, and bounded instrument baking. Playable
points remain materialized in ordinary instrument/automation/graph data; the
audio callback never reads a catalogue or resolves a template. Native metadata
14 stores the bank. The Mac bridge adds revision-guarded bank operations and an
atomic, separately revisioned app catalogue; `mac/AUTOMATION.md` documents the
contract. `EnvelopeBank.swift` is shared by all native envelope editors.
`CurveFormulaReference.hpp` supplies the public reference and autocomplete
snippets; `FormulaWorkbench.swift` owns the expandable, guarded script draft.

Explicitly disconnected plugin outputs use `MixerInstrumentOutput.target == 0`
and the same metadata version 15 as disconnected bus main outputs. Compilation
records the output as explicitly routed before omitting its destination, so the
instrument main-output default cannot silently reconnect it. The API adds
`disconnected:true` with `target:null`, preserving the old null-target reset
semantics for existing clients.

NC (`PatternCommandKind::NoteCut`) is native metadata 16 and schedules a release
through `PreciseNoteRuntime`. Native samples use normal cut/ramp handling; plugin
MIDI uses per-track key-off rather than the legacy cut's broad CC120/123 messages.
Cut commands at a precise-note timestamp run after the note-on. Parameter/pitch
runtimes ignore this command kind. Empty plugin trigger creation is exposed by
`instrument.create(empty:true)`, with sample-only conversion preserving mappings;
UI creation then assigns the new slot through the existing plugin history domain.

## Unified pattern FX (current native format)

The sole native project format is outer plist version 6 / native metadata 17.
Historical native wrapper, metadata and RSONGS1 migrations are removed. Original
OpenMPT module loading remains. Earlier version references in this document are
feature history, not accepted alternative encodings.

Every channel exposes 1–8 equal FX columns. `PatternCommandKind::TrackerEffect`
is kind 5, with source-format `effect` / `parameter` bytes. Columns are zero-based;
`performance.columns` counts total FX columns, default 1. For module interchange,
tracker FX 1 stays in `ModCommand`; other tracker FX and all precise commands live
in shared metadata. This storage distinction is not a UI capability distinction.
The API merges both into `pattern.effects.get/set`; `pattern.effect.set` mutates
one cell. Both precise and tracker commands occupy one `(pattern,track,row,column)`.

`NativeSong::prepareEffects` builds an immutable sparse row/channel map on the
control thread. The guarded engine executes source commands left to right with
one note trigger and shared channel effect memory. Prepare this map before
rendering or timeline walks. The Windows frontend must use these same semantics.
The Mac grid has separate code/value fields at `3+2*column` / `4+2*column`.
Clipboard payload `ScreamSeq Pattern 2` carries extra/precise FX plus bindings;
structural pattern transforms move all columns in one Undo transaction.

Manual preview notes use a 128-entry SPSC queue, with at most 32 active events
processed per render. Events carry the producer's Panic generation; the callback
releases older preview voices and skips older events while retaining notes
accepted after Panic. Preview cuts use the native volume ramp and fade-to-zero
state, keeping the moving sample alive through a tracker-tick boundary until
the ramp finishes. These preview rules do not change ordinary song note/cut
semantics. Windows typing additionally captures stable sound identity and native
input ownership on the UI thread; see `windows/MUSICAL_TYPING_PROGRESS.md`.
