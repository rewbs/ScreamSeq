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
- Partial sample settings preserve omitted rate, volume, pan and loop groups. Windows and Mac adapters retain field presence before filling saved defaults; a name/volume edit cannot silently recalculate MOD/XM tuning or enable a sample-panning override. Explicit pan enables the override. See `windows/SAMPLE_SETTINGS_PROGRESS.md` for the regressions and native settings/import controls.
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

Preview notes carry an optional raw channel (`UINT16_MAX` means inspector).
Pattern entry captures that channel and instrument for the complete held-key
lifetime. The renderer keeps separate held-pitch slots per destination. Stopped
audition prepares the native routing plan while pausing the pattern clock.
Sample-based inspector voices enter after the final mixer adapter; sample
instrument graph assignments have one additional independent inspector copy.
That copy retains instrument envelopes and graph processing without inheriting
channel/Master processing. All preview buffers, routing and plugin copies are
prepared off the callback, within existing processor/storage/adapter budgets.

### Parameter activity diagnostics

`editor/hosted/ParameterActivity.hpp` is the shared bounded parameter monitor.
Each PluginChain prepares metadata for rack processors and the independently
instantiated graph copies. Selection is an immutable control-to-audio handoff;
applied values and graph contributions cross an SPSC ring into bounded retained
control-thread history. Sample-rate ramp endpoints, direct parameter sets,
recordings, envelope edits and pattern-command provenance are observed at the
host delivery path. A per-buffer musical-clock history maps late-rendered plugin
values back to their actual pattern/order/row, including a pattern boundary
inside a callback. The audio path never allocates, frees, queries catalogs or
calls UI code. Capture is optional (one selected parameter), about 1 ms with
extrema and explicit drops; it is not an automation recorder or a report of
vendor-internal DSP modulation.

The macOS Parameter activity inspector/API supplies source navigation and a
revision-guarded recorded-point editor. Recorded point mutations reuse existing
absolute automation storage and plugin Undo; no metadata version is added.
Monitor identities are document scoped externally and distinguish graph roles,
bus uses, sample-instrument channels and the independent inspector copy.
The shared monitor is available to Windows hosting; a Windows-native panel/API
adapter has not been added by this macOS UI change.

### Prepared live-routing transitions (partially enabled)

`editor/RealtimeTransition.hpp` provides a bounded single-producer/render-consumer
exchange which retains outgoing and incoming plans until explicit render-thread
retirement. Superseded pending preparations can be reclaimed on the control
thread while a fade is in flight. Revisions reject stale preparations and expose
requested versus rendered state. This differs from `RealtimePlan`, whose old
plan can be reclaimed immediately after the next consume.

`reusableMixerProcessors` in `MixerGraph.cpp` compares complete input expressions
for old/new compiled mixers. Stable processor identity alone is insufficient:
sidechains, fan-in order, gain, pre/post taps, latency compensation and changed
upstream state all propagate. Explicit reset identities cover changed recipes
and plugin state. Instruments retain their source identity when only destinations
change. Comparison uses collision-free expression interning on the control thread.

Neither component enables live topology publication by itself. The executor must
retain equivalent delay/fader histories, cache shared processors so they advance
once, prepare separate affected copies, handle latency/warm-up/tail transitions,
and integrate MIDI routing, failure retention, pending UI state and chronological
Undo. Most structural mutation paths still stop playback. Do not infer a complete live
repatch capability from the ownership or dependency unit tests.


`MixerTransitionReuse` also matches delay histories and smoothed bus controls.
`MixerRuntime::retainHistory` is control-thread preparation: source bindings and
ownership must remain stable until render activation. Compatible delay lines
share a prepared output-chunk cache, so old/new plans cannot advance them twice.
`activateHistory` runs after the outgoing plan begins a chunk and before either
plan processes it; it copies only small ramp records. Both plans must use exactly
the same chunk boundaries. A retained plan rejects rendering before activation.
The hosted mixer now uses this activation path for supported live reroutes.

`hosted/RenderOnce` similarly shares one processor's current main output across
compatible plans. Auxiliary buffers are consumed before either plan starts the
next chunk. The wrapper itself must be shared; sharing only the vendor processor
would still process it twice. Multi-bus and latency fixtures cover real AU/VST3
processors. `mixer-transition` covers exact reference PCM, active ramp histories,
PDC, sidechain/auxiliary fan-out, concurrent preparation/cancellation and retirement.
`MixerTransition` owns the old/new plans, fills the incoming latency history from
real ongoing sources, applies a 10 ms sample-clock linear fade, and collects
retired plans off the callback. It bounds the combined host storage and rejects
an incompatible catalog or total-latency change. A failed candidate retains the
outgoing plan; failed request revisions are distinct from rendered revisions.
`commitStopped` is only for a fully quiescent device (e.g. the existing latency
refresh path); it must never be used concurrently with rendering.

The native `PluginChain` uses this executor for normal mixer playback, preserving
the fixed OpenMPT adapter/source indices. macOS mixer API edits and document
Undo/Redo publish a prepared plan when existing bus kinds, source assignments,
total latency and all processor input dependencies can be retained. Group/return
buses can be added, removed or reordered within that constraint; track source
adapters remain fixed. Bus meter maps belong to each prepared plan and resolve
by stable identity. The bounded append-only signal-port catalogue publishes
immutable identities and meter slots together, allowing telemetry readers and
audio observation to continue while a new bus is prepared. Existing
ordinary/row/persistent and sample-instrument graph copies are also retained
when their processing and bus note-envelope membership stay unchanged. The
control owner compares against the newest published graph-control snapshot;
a combined routing/control Undo cannot silently skip restoring parameters.
The rendered fixture reroutes a dry channel into another channel while an
unaffected VST3 insert and all these graph copies keep running, with reference
PCM checks at 17/512/4096-frame blocks and the realtime allocation/free/lock audit.
Preparation finishes before the document transaction;
publication is allocation-free. Busy transitions reject without editing the model.

This is deliberately a partial host path. Changed processor inputs, recipe
instances, source adapters, auxiliary port activation and differing latency
still use stopped preparation. Affected copies, their automation/MIDI,
full latency transitions and requested/rendered UI remain unfinished. Windows
shares the executor and build sources; its API adapter has not enabled the live
publication path. `mixer-publication` covers direct executor fades, failure
retention and concurrent ownership; `native-mixer` covers actual hosted reroute
and reverse transitions against continuously rendered reference audio.
