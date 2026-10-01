# ScreamSeq development map

ScreamSeq is an independent tracker DAW built on the OpenMPT playback engine, developed on `main`. Both native applications are maintained here. The upstream history and attribution remain intact; [UPSTREAM.md](../UPSTREAM.md) records the retained engine and removed product components. Historical reports retain their original names. Current behavior is defined by source and tests; the original Renoise feasibility table describes the starting point, not today's completion status.

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

`NativeSong::masterID` reserves the Master identity during document construction,
before routing is materialized. Implicit graph/mixer reads reuse that identity
without allocating or adding history; unrelated graph creation, Undo/Redo and
save/reopen cannot retarget a displayed Master. Both current metadata-17 codecs
store it. When this optional field is absent, decoding adopts an existing Master
or reserves one for an implicit song. Conflicting explicit identities are rejected.
All mixer materialization paths use `NativeSong::ensureMixer`.

Sample-instrument graphs are prepared independently per instrument/raw channel and feed the ordinary mixer through sample-only OpenMPT adapters. Their NNA voices keep their original routing. `NativeSignalGraph` handles both that stage and channel/group graphs with a shared 256-processor/256-MiB host-storage budget. Graph automation sources store per-pattern curves using stable IDs, compiled formulas and the shared evaluator. Both additions require native metadata 13. See `mac/GRAPH_WORKFLOW.md` for the current signal order, activity commands, editing semantics and limits.

The current native project wrapper is a versioned binary property list containing an exact song snapshot, metadata and plugin state. Metadata and container versions are separate. `windows/Project/` implements the compatible portable codec and preservation-aware atomic saves. Plugin recipes use stable class identity; local paths are resolution hints. AU remains macOS-only. Missing platform plugins preserve opaque state and reject playback preparation rather than being replaced silently.

## Build and qualification

Windows: `./windows/build.ps1 -Architecture ARM64 -BuildDirectory bin/windows-dev -Test`.
Use `-Fresh` after a failed initial compiler configuration. The app and VST3 scanner
are in the selected build's `Release/` directory. Native sample editing, offline
hosted renders and private-PID integration tests live in `windows/Tests/`; current
evidence and remaining parity work are recorded in `windows/RESUME_PROGRESS.md`.

Build on macOS with `SCREAMSEQ_BUILD_DIR=bin/mac-screamseq SCREAMSEQ_BUILD_JOBS=4 bash mac/build.sh`. Output is `ScreamSeq.app`; `RESONANCE_BUILD_DIR` and `RESONANCE_BUILD_JOBS` remain accepted aliases. Set `RESONANCE_DEVELOPMENT_BUILD=1` for a separate development bundle identity. AppKit/Metal and Core Audio are the current native foundation; keep high-frequency drawing out of layout-heavy per-cell view trees.

`ctest --test-dir <build> --output-on-failure` runs native regressions. `mac/build-reference.sh` extracts a pinned original OpenMPT commit from local Git history for the separate stock playback oracle; see `UPSTREAM.md` and `mac/COMPATIBILITY.md`. `mac/test-interface.sh` builds/tests the AppKit editors and can save snapshots. `mac/Tests/test_automation.py` exercises the socket protocol and actual application; inspect its flags before invocation. Workspace, startup, recovery, sample-library, plugin and Core Audio loopback checks have dedicated entry points in `mac/Tests/`. The qualification skill describes safe instance handling and evidence limits.

Quit must drain document/recovery work asynchronously, then call `TrackerSession.shutdown()` on the main thread before AppKit exits. Stopping transport retains plugins; ARC teardown of the app controller is not guaranteed before vendor static destructors. `plugin-shutdown-tests` checks retained-session teardown (`--ui` also covers rack and graph recipe editors). `SCREAMSEQ_BUILD_DIR=<build> bash mac/test-shutdown.sh` checks the actual NSApplication Quit path, including pending worker calls that need the main thread and recovery writes, without audio or visible windows.

`BuildInfo.json` records source hashes in the app bundle. `mac/Tools/build_manifest.py` includes currently untracked native additions. `mac/Tools/bundle_notices.py` packages attribution and user/API guides. Required VST3 interface sources are in `mac/ThirdParty/vst3/`; do not replace them with an unpinned machine-local SDK dependency.

## Editing and API invariants

- Native IDs are stable across insert/reorder; indices are transient views. Whole-collection API writes need read/merge/write.
- Mutation requests carry `expectedRevision`; context changes also guard context revision. Document, plugin and recorded-automation edits share one chronological Undo history. The accepted `all`, `document` and `plugins` API domain names are aliases, not separate stacks exposed to the musician. Revision tokens still distinguish the underlying document and plugin revisions.
- Precise notes and graph commands use 65536 units/row. Pattern automation points use 256 units/row. Beat offsets use the current pattern signature, not a fixed assumption of four rows/beat.
- Scripted curves are precompiled bounded mathematical expressions. No general-purpose interpreter executes in the audio callback.
- VST3 automation uses sample-offset parameter queues within normal blocks. Continuing tracker effects still follow ordinary ticks.
- Sample/instrument data, note-on/off/cut semantics, NNA, routing and exact native sample payloads must survive save/reopen and Undo. The current sample voice/storage path remains 8/16-bit.
- Partial sample settings preserve omitted rate, volume, pan and loop groups. Windows and Mac adapters retain field presence before filling saved defaults; a name/volume edit cannot silently recalculate MOD/XM tuning or enable a sample-panning override. Explicit pan enables the override. See `windows/SAMPLE_SETTINGS_PROGRESS.md` for the regressions and native settings/import controls.
- The API schema retains its legacy filename for compatibility. Prefer the `screamseq_api.py` entry point; existing `resonance_api` imports continue to work.

## Workflow and source control

The root `.agents/skills/` contains the maintained project skill sources; copies can be installed under the user's Codex skills directory. Use separate worktrees/checkouts for concurrent Mac and Windows agents, and coordinate shared model/API/format edits explicitly. Neither agent should overwrite the other's platform tree or invent divergent musical semantics.

Keep binaries, build caches, private sample packs and user songs out of Git. Include reproducible fixtures and licenses. Old local qualification documents may contain absolute paths: prefer repository-relative references in new documentation. Preserve upstream copyright notices and engine identifiers. The old OpenMPT product UI and distribution build systems are no longer part of this tree.

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
UI creation then assigns the new slot as the next edit in unified chronological history.

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
absolute automation storage and the plugin-state portion of unified chronological
Undo; no metadata version is added. Mac and Windows publish a bounded immutable
recorded timeline at a render boundary. The worker prepares device-rate points
and baseline catch-up before history changes; queue refusal leaves transport and
both histories intact. A manual-queue fence orders earlier controls before the
new timeline and later controls after it. Removed lanes restore their saved
manual baseline. Viewing points does not require a prepared processor.
Monitor identities are document scoped externally and distinguish graph roles,
bus uses, sample-instrument channels and the independent inspector copy.
The shared monitor is available to Windows hosting; a Windows-native panel/API
adapter has not been added by this macOS UI change.

### Prepared live routing and graph controls

`editor/RealtimeTransition.hpp` owns a bounded single-producer/render-consumer
handoff. The control worker validates and prepares a candidate before committing
the document, publishes without allocation, and retains outgoing state until the
render thread releases it. Requested, rendered and failed revisions are distinct.
Superseded pending candidates are reclaimed off the callback. `commitStopped`
requires a quiescent device and is not a concurrent publication shortcut.

`MixerTransition` compiles the union of old/new dependencies. Retained vendors
advance once per interval, with changed inputs interpolated before processing;
changed outputs use a 10 ms sample-clock linear fade. Equivalent delay/fader
histories retain state, including a prepared per-chunk cache so a delay cannot
advance twice. `reusableMixerProcessors` and `MixerTransitionReuse` compare input
expressions, stable identities, taps, gains, ports, latency and upstream state;
identity alone does not prove two processing paths equivalent. The combined host
storage is bounded. A cycle in the transition union, incompatible latency or an
unprepared source/port change rejects before the document or transport changes.

Both native session adapters enable this path for supported effect Add/Remove, reorder,
detach/insert and bus routing, including grouped Undo/Redo. Fixed OpenMPT source
adapter slots and held instrument state remain stable while effect catalogs
change. Disconnected effects continue processing silence. Prepared built-in
detector inputs and AU input storage allow live auxiliary-input connection;
unprepared VST3 bus activation and new outputs still require stopped preparation.
Arbitrary latency changes, new instrument adapters and instrument-assignment
changes have no seamless handoff yet. A rejected live edit leaves the previous
audible plan playing and reports the reason; it does not silently stop/restart.
The Windows document worker prepares the same graph, mixer and rack plans and
publishes them through native-only document/history commit callbacks. Grouped
rack/routing Undo stages both domains before publishing. Unsupported live opaque
rack-state changes reject rather than silently retaining a different preset.
Windows-native worker tests cover publication, failure recovery and persistence;
actual CI execution is the Windows qualification gate. Mac-hosted adapter tests
do not establish Windows-native execution or desktop/device behavior.

Per-plan meter maps and the bounded append-only signal-port catalog publish
stable identities and meter slots together. Failed preparation cannot publish
orphan ports. The UI reads requested/rendered plan status and reports Preparing
while the old plan remains active. Layout, filters and presentation-only frames
never rebuild audio processing.

`GraphControlPlan` publishes a complete fixed-topology recipe update across
ordinary, row, persistent and sample-instrument copies, including inactive copies.
Parameter values, modulation source settings/depths, existing audio-edge gains
and compiled curves change together while note/envelope histories continue.
Ordinary automation lanes can be part of the same atomic publication. Producer
revisions prevent an older compound plan from overwriting a newer lane update.
The host compares against the latest accepted control/routing state, not only
its construction-time graph.

`hosted/GraphPluginEndpoint` retains a stable logical recipe-processor target.
An opaque preset update prepares replacement vendor instances off-thread for
every affected copy, validates matching descriptor, ports, latency and parameter
catalogs, then adopts all at one boundary with a 10 ms old/new vendor-output fade.
Unchanged node vendors stay intact. Parameter ramps feed both sides during the
fade, and producer-owned snapshots retain old instances until safe retirement.
This handles hidden preset state as well as exposed values; it does not enable
arbitrary recipe node/edge topology or incompatible vendor state changes. Recipe
storage, replacement buffers and conservative tails remain budgeted off-thread.

`hosted/SongModulationHost` evaluates song-level LFO, envelope, random, MIDI,
amount, scoped note-envelope and current-block audio-follower sources against
stable rack parameter IDs. Additive contributions remain separate from the
unmodulated baseline, clamp once, and explicitly quantize discrete targets.
AU/built-in delivery and VST3 sample-offset queues preserve sub-buffer timing.
The document stores source definitions and edges; control snapshots prepare all
lookup tables, curves and storage outside the callback.

`editor/ParameterProvenance` projects existing envelopes, pattern FX bindings and
recorded automation as bounded read-only source references. These retain their
base-setting semantics and cannot be cut or repatched as additive graph wires.
Both session adapters expose `graph.provenance.get`, even stopped or without a
prepared vendor. The Mac graph loads one selected parameter's source cards
lazily; exact editing links preserve a route back to the graph.

Relevant regression suites include mixer-publication, native-mixer, precise-ramp,
plugin-latency, song-modulation, signal-graph-session, unified-history and the
portable Windows model/metadata suites. Rendered cases exercise callback
partitions, held notes, opaque AU/VST presets, tails, failure retention and host
allocation/free/lock auditing. Current measured results and unsupported cases
belong in the dated qualification record; source support is not a claim that
every third-party plugin or machine has passed live UI/audio qualification.


### Pulling out a rack processor

`mixer.inserts.detach` uses the shared `detachMixerInsert` transaction to remove
one existing effect from its serial owner, heal that main path and retain the
processor as explicitly disconnected. The existing `MixerGraph.detached` storage
clocks it on silence; it cannot fall back onto Master. State, parameter bindings
and identity survive, and optional dragged position belongs to the same Undo.
Mac and Windows adapters expose the same strict guarded operation. Explicit
auxiliary input/output routes reject without dropping branches. A disconnected
multi-processor chain still needs a representation for its internal cables; this
operation therefore accepts exactly one processor rather than silently splitting
a selected chain. Recipe `graph.nodes.detach` remains the chain-capable path.
