# ScreamSeq delivery — 21 September 2026

ScreamSeq is published as [rewbs/ScreamSeq](https://github.com/rewbs/ScreamSeq), a true fork of OpenMPT. The Mac application, shared editor/audio work, branding, agent skills and Windows handoff are preserved in Git. The graph editor now includes direct connection editing, graph-owned pattern automation and sample-instrument processing before channel effects. Seamless structural graph replacement during playback remains unfinished.

The usable checkpoint is `bin/mac-checkpoints/2026-09-21-screamseq/ScreamSeq.app`. The same directory contains a graph tutorial song, rendered WAV, guides and qualification evidence. Save and quit the older application before switching builds. The user's running Resonance process and its project were not replaced or terminated.

## Repository, branding and handoff

- The fork retains OpenMPT history. Commit `db496a399012650096e21980a8ec91a480a65392` layers all accumulated application work, the rename and initial handoff assets onto upstream `f83cedb0cd5446e4dfaa83ac97e3087107e26767` as **one import commit**. The graph completion work follows in a separate commit on the default `codex/screamseq` branch.
- The application name, menu, executable and icon are ScreamSeq. The icon derives from the supplied spectral/squid artwork; both the source reference and generated asset are retained under `assets/branding/`.
- New Save As operations use `.screamseq`; new native plugin presets use `.screamseq-preset`. Existing Resonance files, preset formats, recovery/cache directories and stable plugin identifiers remain compatible. Projects using the new graph extensions require native metadata **13**; earlier builds cannot read those extensions.
- Four maintained skills live in `.agents/skills/` and are installed in the local Codex skills directory: development, realtime audio, agent API and qualification. They cover architecture, past failure modes, transaction/draft ownership, graph timing, plugin hosting, compatibility and safe testing beside a musician's running app.
- [Windows agent kickoff prompt](WINDOWS_AGENT_PROMPT.md) is ready to paste into the new agent. It specifies a separate checkout/branch, a native Win32/Direct2D/DirectWrite/DXGI starting architecture, WASAPI/optional ASIO, native Windows VST3 hosting, a portable project codec, shared musical semantics and measured performance gates. Windows implementation has **not** been started here.

## Graph work delivered

The reusable graph library, song overview and pattern graph lanes form one workflow. Definitions have stable identities, names/numbers, saved positions, clone/remove operations, Undo and API access. Instances remain independent per destination and per row/persistent/ordinary role.

- Individual audio input/output sockets and named parameter sockets support direct dragging. Clicking a wire selects it and exposes its ports, gain, enabled state or modulation range/base. Updating it preserves neighboring connections. Selectors provide a keyboard alternative.
- Zoom, Fit, dependency-based Arrange, node/wire keyboard navigation, deletion and drag cancellation are implemented. Wire geometry is cached outside drawing. A crowded graph can float beside the pattern rather than occupying a small dock.
- Channels, groups, plugin instrument outputs, sample instruments, inserts, sends, sidechains and auxiliary outputs appear in the shared overview. Filtering a channel retains its routing dependencies. Live badges report active stack order and releasing tails.
- **Automation source nodes** contain a curve for each pattern. Their lower editor supports point dragging, numeric edits, snapping, zoom, nine outgoing curve types and the same bounded scripted expressions as pattern automation. The graph and curve remain visible together. Drafts retain their original graph/source/pattern/revision, including edits made while Apply is in flight.
- Curves repeat with their pattern, clone with it and survive native save/reopen and Undo. Missing or disabled curves output zero. Scripts receive the true pattern end and beat signature. Step boundaries remain discrete; continuous modulation uses bounded evaluations and sample-offset host ramps.
- **Sample-instrument graphs** process before channel graphs. Each instrument/raw-channel pair gets its own processor history. A continuing NNA voice retains its original instrument and channel when a new instrument starts on that channel. Plugin instruments continue to use output-bus routing because one multitimbral plugin can mix its voices internally.
- Row-only and persistent graph commands stack in activation order. Repeating Start updates the existing instance. The order is sample-instrument graph → row graphs → persistent graphs → ordinary channel graph → inserts. Amount and Wet are separate controls.
- Row copies expire at the next row. Stop cuts the named copy's output by default; optional tails are retained. Inactive processors advance on silence, including silent auxiliary inputs, while transport continues. Already-received audio inside downstream processors is not erased.
- Latency tests now cover stopping and restarting a delayed A/B stack into B→A order. Dry compensation remains continuous and reserved graph latency stays fixed.
- New agent methods are `graph.automation.get/set` and `graph.instrument.assign`; existing graph catalogs, activity, schema and API discovery were extended. Curves use 256 units/row; precise notes and graph commands use 65536. All new musical writes have validation, revision guards, Undo and persistence.

See [the graph workflow guide](../mac/GRAPH_WORKFLOW.md) and [API guide](../mac/AUTOMATION.md) for controls and examples. `mac/Tools/create_graph_demo.py` creates a tutorial in its own disposable host through the public API.

### Graph limits still open

1. Structural, recipe, assignment and curve changes stop playback to prepare safely. Names, numbers and layout preserve playback. An uninterrupted handover needs prepared-plan publication/retirement, plugin-state ownership and latency/tail transition work.
2. The library is currently song-owned. Cross-song subgraph presets, nested reusable subgraphs, individual sample-zone graphs and instrument graph-switching commands remain future extensions.
3. Existing P/L parameter-command bindings address stable rack plugin instances. Graph recipes use their own parameter connections, automation sources and Amount/Wet commands; direct P/L bindings to individual graph copies are not implemented.
4. Implicit zero-delay cycles are rejected. This is not an arbitrary feedback graph. Host-owned graph storage and processor counts are bounded; private allocations inside third-party plugins are outside that budget.
5. The graph's hard Cut behavior is deliberately not a click-free crossfade. Plugin-specific smoothing and audible quality still require real-plugin/listening qualification.

## Verification and evidence

| Check | Result at this checkpoint |
| --- | --- |
| Full native CTest suite | **70/70 passed** after the graph/instrument engine changes |
| Final AppKit interface suite | Passed, including connection updates, geometry, instrument assignment, curve draft ownership, in-flight Apply preservation and launch-argument filtering |
| Actual application socket API | Passed: schema/discovery parity, revision/retry guards, history and existing musical workflows |
| Startup and workspace | Passed; real 127-channel startup also includes an existing plugin path as a command-line option to prevent accidental song loading |
| ASan/UBSan | **5/5 passed**: signal graph, hosted graph, graph session, sample routing and precise notes; leak detection disabled for system/plugin runtime objects |
| Stock OpenMPT fidelity | **30/30 bit-exact comparisons** at 44.1/48/96 kHz; zero intercepted callback allocations, releases or locks |
| Instrument graph rendering | Independent audio references across 44.1/48/96 kHz and 17/128/4096-frame blocks, including multiple instruments, channels and NNA voices |
| Graph automation rendering | Linear/step/script behavior, exact step boundaries, missing curves, pattern cloning and callback partition tests passed |
| Actual Core Audio output/capture | Dry, AU effect/instrument, VST3 effect/instrument with automation, and graph cases all matched reference PCM exactly |
| Expanded graph loopback | **60 seconds**, 48 kHz/512 frames, **2,880,000 stereo frames**, exact PCM, no capture discontinuities and **zero callback overruns**; includes graph curves and sample-instrument graphs |
| Tutorial project | Reopened, saved, reopened again without graph changes and exported successfully; 610,397 stereo frames at 48 kHz, peak 0.06156, finite unclipped output |
| Live graph inspection | Earlier in this run, a source curve was dragged and applied, its new position/value verified through the socket, and a selected modulation wire's range edited and verified |
| Sustained displayed 60fps | **Not qualified.** Final attempts reported `measurementStarted: false`, inactive/occluded windows and no presented frames. Computer-control clicks returned `noWindowsAvailable`, including with a uniquely identified QA bundle. AX/cached window images are not a presentation pass. |

The BlackHole tests select the virtual device explicitly. They neither record the microphone nor change the default speaker route, and do not establish physical DAC latency or speaker sound. Commercial plugin custom interfaces and Battery 4 require further live qualification.

Sixteen simultaneously modulated copies were also benchmarked over 1,000 callbacks, 128 frames each. In the run with no concurrent build/test workload, the measured p99 times were:

| Processor | 48 kHz p99 | 96 kHz p99 |
| --- | ---: | ---: |
| Built-in gain | 0.268 ms | 0.215 ms |
| VST3 gain fixture | 0.080 ms | 0.082 ms |
| Apple AU low-pass | 0.592 ms | **1.432 ms** |
| Callback budget | 2.667 ms | 1.333 ms |

The 96 kHz AU case exceeded its budget, with a 4.678 ms maximum. Loaded runs also showed overruns of the theoretical budget. All benchmark cases passed the host realtime-operation audit, but this is **not** a claim of reliable sixteen-AU operation at 96 kHz/128 frames. The larger-buffer Core Audio fixture passed as described above. Both loaded and idle measurements are retained rather than discarding unfavorable results.

Reproduction entry points: `mac/build.sh`, CTest in the selected build directory, `mac/test-interface.sh`, `mac/Tests/test_automation.py --app`, `mac/Tests/test_startup.py`, `mac/Tests/test_workspace.py`, `mac/test_audio.py`, `loopback-tests` and `signal-graph-performance`. For visible qualification use `SCREAMSEQ_BUILD_DIR=bin/mac-screamseq bash mac/ui-test.sh 60 --no-build --vst3 --graph --device 'BlackHole 2ch'`. Omit `--graph` for the separate 127-channel/1024-row display workload. The script makes a uniquely identified QA bundle and terminates only that process.

## Current status of the earlier priority list

This table supersedes the old dated “Pending” labels where work has since landed. “Implemented” describes the stated scope, not complete Renoise equivalence or commercial-plugin qualification.

### Pattern editing A01–A15

| IDs | Implemented | Remaining |
| --- | --- | --- |
| A01 | Keyboard note entry, clipboard/selections, transpose, scoped row insertion/deletion | Broader live interaction/format qualification |
| A02 | Named polyphonic tracks made of adjacent note columns, shared track processing, column mutes | Broader track hierarchy/reordering workflow |
| A03 | Up to eight native parameter/pitch effect subcolumns; precise-note timing/volume | Independent pan/delay/volume grid fields and arbitrary extra legacy effect columns |
| A04 | Timestamped MIDI recording, 1/65536-row precise notes | More physical MIDI/latency qualification |
| A05 | Mono handoff, polyphonic column allocation, quantization | Richer chord-entry/performance workflow |
| A06 | Field masks, scoped operations and mix paste with guarded previews | Extend unified tools to every native lane/event kind |
| A07 | Flip, expand/shrink, rotate/nudge and instrument remapping | Unified transforms across newer precise/graph events |
| A08 | Interpolation, scaling and seeded randomization/humanization | Comprehensive timing-aware transforms for precise hits |
| A09 | Source-format command picker, descriptions and effect-family colors | Additional contextual guidance as new commands arrive |
| A10 | Format-specific tracker effects, per-hit effect onset/handoff | Broader live musical qualification; no Renoise command-dialect promise |
| A11 | — | Deterministic probability, exclusive triggers and explicit ghost-note semantics |
| A12 | Fractional BPM, rows/beat, ticks/row, signatures and imported timing modes | Dedicated tempo-envelope automation |
| A13 | Saved normalized global groove and swing | Groove preset/workflow extensions |
| A14 | — | Metronome, count-in, beat/bar accents and monitor/export policy |
| A15 | Independent editing cursor, playhead, Follow and agent navigation | Sustained live presentation qualification |

The enhanced precise-note inspector is already implemented: beat fractions, containing-beat/row views, constrained timing/volume dragging, individual occurrences, retrigger generation and one effect per hit. Parameter slides use VST3 sample-offset queues inside ordinary blocks; sample pitch bends are interpolated per sample. Plugin pitch uses MIDI pitch bend and depends on the instrument's configured bend range.

### Mixing and plugins E01–E13

| IDs | Implemented | Remaining |
| --- | --- | --- |
| E01–E03 | Mixer strips, gain/balance/width/mute/solo/meters, inserts, groups | Continued dense UI and live workflow qualification |
| E04 | Pre/post sends and return buses | Dedicated multiband sends/splitter workflow |
| E05 | Plugin and graph sidechain routing | Wider real-plugin bus-layout testing |
| E06 | AU/VST3 auxiliary outputs and graph multi-port routes | Separate hardware output-pair routing |
| E07 | Native macOS AU/VST3 effects/instruments, state, custom/generic interfaces | Battery 4 regression qualification, broader commercial coverage and AUv3-specific extensions |
| E08 | Multiple tracker instruments sharing one plugin on separate MIDI channels | MIDI-controlled effect workflows |
| E09 | — | Plugin MIDI-output routing into other instruments |
| E10 | Cached discovery/rescan, search/favorites/categories/hidden entries, native presets and factory programs | Vendor-specific preset interchange/databases |
| E11 | Safe stopped preparation and current lifecycle checks | Auto-suspend and seamless dynamic bus/parameter/latency changes |
| E12 | Isolated scanning/validation | Runtime plugin process isolation and crash recovery |
| E13 | Fixed-plan graph delay compensation and track offsets | Safe dynamic latency transitions and wider hardware/plugin qualification |

Plugin cache/rescan is implemented. The earlier Battery 4 crashes are **not claimed resolved** by fixture and Apple-AU tests. Runtime plugins still execute in the app process.

### Automation G01–G06

| IDs | Implemented | Remaining |
| --- | --- | --- |
| G01 | Hosted parameter gesture recording, playback/export | Generalized mixer/instrument macro targets and broader live recording qualification |
| G02 | Searchable graphical lanes, zoom/pan/snap, nine curves, outgoing node type and scripted segments | Apply the richer curve representation to other envelope domains where appropriate |
| G03 | Pattern-relative automation, now also graph-owned source curves | Per-order-occurrence overrides/muted-clip semantics |
| G04 | Curve/instrument-envelope clipboard, repeated/insert paste, transforms and generators | Some transforms deliberately reject scripted curves; broader native-event unification |
| G05 | Pattern/order curve evaluation and chase, cursor-start/range playback | Complete historical note/plugin-state reconstruction and all seeking/muted-clip combinations |
| G06 | Stable rack parameter bindings, P/L commands, last-touched target selection | Direct graph-copy parameter bindings and broader target registry |

## Other requested areas

| Area | Present | Still to build or qualify |
| --- | --- | --- |
| Arrangement B01–B07 | Orders/reusable patterns, sections/annotations, matrix/block copying, selection/pattern/song playback and loops | Per-track block aliases, occurrence mutes, queued sections, section-range workflow and complete track/group rearrangement |
| Sampling C01–C04 | Waveforms, import, clipboard/mix paste/drawing, processing, snapping, crossfades and forward/reverse/sustain loop work | Advanced loop-finish/chase workflow and broader asset/format qualification |
| Sampling C05–C12 | Exact native sample snapshots and sample-instrument live graphs | Slicing/aliases, beat sync/time stretch, microphone/line recording, processed monitoring, offline chain-to-sample baking, 24-bit/float sample storage and voice path |
| Instruments D01–D04 | Filename-based pitched-family detection and multi-sample import, keyboard maps, tracker note actions, instrument envelopes/tools | Rich graphical zones, velocity/release/round-robin layers and explicit choke groups |
| Instruments D05–D14 | Shared graph modulation sources, sample-instrument pre-channel graph, basic plugin aliases | Per-voice modulation sets, zone chains, full macro bank, tuning/scales, hybrid hardware instruments, phrases/generators and complete instrument preset/asset management |
| Built-ins F01–F03/F07 | 15 devices: Gainer, DC Offset, Stereo Expander, Digital Filter, EQ5, EQ10, Mixer EQ, Comb Filter, Distortion, Cabinet, LofiMat, Compressor, Bus Compressor, Gate, Maximizer | Analog/exciter variants, additional EQ/filter presentation and more listening qualification |
| Built-ins F04–F06 | Can use hosted effects | Native delay/multitap/repeater, reverb/convolution, chorus/flanger/phaser/ring modulation |
| Control devices F08–F12 | Amount macro, LFO, random, follower, note envelope, MIDI CC, scripted automation and reusable parallel graphs | Full macro/MIDI panels, XY/Meta Mixer/Hydra-style tools, key/velocity tracking, specialized mid/side/frequency splitters and nested racks |
| Browser/workspace J | Fast folder/name search and filtering, preview/bulk import, multi-sample review; retained docks/floating panels, Follow/Pin/Return, command palette/shortcuts; autosave/recovery | Cross-song instrument/graph libraries, analyzers/scopes, broader theme/font/layout options, continuous neighboring-pattern view, templates and richer activity views |
| MIDI/integration H | CoreMIDI note capture, local revision-guarded JSON API/Python client, graph MIDI-CC source | Hardware MIDI output/returns, general expression recording/learn, clock/Link/OSC and controller/tool ecosystems |
| Rendering/audio I | Whole-song offline stereo WAV, plugin latency/tails, device settings and callback telemetry | Stems/range-to-sample/freezing/plugin sampling, more export formats/quality controls, real-time hardware export, multichannel I/O and measured multicore execution |
| Interchange K | OpenMPT module compatibility and native project persistence | Native MIDI-file import, qualified SFZ subset/export, XRNS/XRNI conversion and a portable Windows native-project codec |

## Suggested next order

1. Finish visible frame-pacing/custom-editor qualification and diagnose the smallest-buffer AU stress outliers on a controlled desktop.
2. Implement safe live graph replacement, then dynamic plugin bus/latency handling and Battery 4 qualification/runtime isolation.
3. Add the remaining high-priority pattern features: metronome/count-in, probability and independent effect-field workflow.
4. Unify mixer/graph/instrument parameter targets and precise-event transforms for the agent API.
5. Build the Windows sibling against the shared fixtures and project format; then expand sampling/layered instruments and the remaining native effects.

No design answer is required to use this checkpoint. The remaining work above is explicit; the full original feature catalogue is not marked complete.
