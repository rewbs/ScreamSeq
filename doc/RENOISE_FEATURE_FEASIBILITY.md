# Renoise features we can feasibly build in Resonance

Research and source audit: **19 September 2026**.

## Assessment

**Most of Renoise's first-party musical workflows are feasible in Resonance.** Editing tools, browsing, visual automation, sample processing and arrangement improvements can arrive incrementally. Full track routing, layered instruments, independent note columns and runtime plugin isolation are substantial engineering projects. They should be planned as new foundations, not treated as missing buttons.

The strongest direction is a native Mac tracker combining OpenMPT playback compatibility, Renoise-style composition and sound design, and Resonance's existing external editing API. Every new musical operation should work both through the UI and through that API.

This report covers the current Renoise **3.5.4** release and the first-party feature families in its manual, including the 3.5 additions. It assesses the full application, including functions unavailable in the demo. It does not claim to inventory every keyboard shortcut, preset or community tool. It is a documentation and source-code assessment, not a hands-on Renoise benchmark. Version and recent changes: [official download](https://www.renoise.com/download), [3.5 release announcement](https://forum.renoise.com/t/renoise-3-5-and-redux-1-4-released/76590), [manual](https://tutorials.renoise.com/wiki/Welcome).

The inventory contains **106 capability groups across 11 areas**, with current support, remaining effort and implementation dependencies for each.

“Replicate” means implementing comparable musical capabilities with Resonance's own interface and implementation. Exact Renoise DSP sound, lossless Renoise project interchange and compatibility with its entire scripting ecosystem are separate, much larger targets.

## How to read the inventory

- **Present:** the capability exists within Resonance's documented limits; this is not a claim of complete Renoise parity.
- **Partial:** useful support exists, but material behavior is missing.
- **Core only:** OpenMPT has relevant machinery that the native application has not exposed or qualified.
- **New:** no complete equivalent was found in the native application.

Remaining effort is relative, not a delivery estimate: **S** is a contained extension; **M** spans a few components; **L** is a substantial subsystem; **XL** changes the song model, engine or process architecture. Ranges distinguish a useful first version from the broader feature. Dependencies are not included in a small feature's estimate. **Prototype** means the approach is plausible but performance, sonic quality or compatibility needs an early experiment before making a commitment. Other “Yes” assessments have high confidence in functional feasibility, not in a specific schedule.

## Resonance's actual starting point

| Area | What exists | Boundary relevant to this plan |
|---|---|---|
| Tracker | Native AppKit application, virtualized Metal pattern grid, pattern/order editing, clipboard, transpose, undo, independent edit/play following | One OpenMPT cell per channel and row; not Renoise's richer track/column model |
| Playback and files | OpenMPT renderer; editable MOD, XM, S3M, IT and MPTM; other engine formats previewable | Existing format semantics and limits must remain intact |
| Samples/instruments | Import, waveform selections, basic processing/loops, ITI/XI instruments, note maps and envelopes | One sample mapping per key; no velocity-layered native sampler or sample effect graph |
| Plugins | macOS AU/VST3 effects and instruments, custom editors, state, searchable parameters, cached discovery and explicit rescan | Eight total rack slots; effects run on the stereo master; runtime plugins remain in the application process |
| Automation | Plugin gesture recording, scheduled playback/export, API lane replacement | Discrete points on an absolute 48,000-frame-per-second timeline; no graphical lanes or pattern-relative automation |
| MIDI | CoreMIDI input, audition, velocity, step/live recording | Live notes are row-quantized; no exposed hardware MIDI output or clock sync |
| Persistence/API | Versioned `.resonance` projects, recovery, local JSON API for patterns, samples, instruments, plugins and automation | Document and plugin undo domains are separate; no transaction spanning both; many structural changes stop playback |
| Export | Stereo float WAV with plugin latency trimming and tails | Export is fixed at 48 kHz; no stems, range rendering or plugin multisampling workflow |

Evidence: [compatibility and qualification](/Users/rewbs/code/openmpt/mac/COMPATIBILITY.md), [automation API](/Users/rewbs/code/openmpt/mac/AUTOMATION.md), [document/renderer interface](/Users/rewbs/code/openmpt/editor/TrackerDocument.hpp), [native plugin host](/Users/rewbs/code/openmpt/mac/Audio/AudioUnitHost.hpp), [project/session implementation](/Users/rewbs/code/openmpt/mac/Bridge/TrackerSession.mm).

## A. Pattern editing and musical timing

Renoise reference: [pattern editor](https://tutorials.renoise.com/wiki/Pattern_Editor), [advanced editing](https://tutorials.renoise.com/wiki/Advanced_Edit), [note recording](https://tutorials.renoise.com/wiki/Recording_and_Editing_Notes), [effect commands](https://tutorials.renoise.com/wiki/Effect_Commands), [song options](https://tutorials.renoise.com/wiki/Song_Options), [groove](https://tutorials.renoise.com/wiki/Groove_Settings).

| ID | Capability | Resonance now | Feasibility / remaining effort | Implementation assessment |
|---|---|---|---|---|
| A01 | Keyboard note entry, octave, edit step, note-off, velocity, selections, clipboard, transpose, row insertion/deletion | Present | Yes / S polish | Build on the existing editor and format validation. |
| A02 | Multiple polyphonic note columns inside a named track; independent column mute | New | Yes / L–XL | A logical track could initially group engine channels. Full behavior also needs shared track processing and persistent column identities. |
| A03 | Independent volume, pan, delay and local effect subcolumns; multiple track effect columns | Partial | Yes / XL | Current cells have one volume command and one effect command. Simultaneous independent commands require a richer event model, not just wider rendering. |
| A04 | High-resolution note delay and accurate live recording | Partial | Yes / L | Preserve incoming MIDI timestamps; map them through musical time and input latency. OpenMPT tick delay is not equivalent to Renoise's 1/256-line note delay. |
| A05 | Mono/poly recording, chord entry and configurable quantization | Partial | Yes / M–L | Basic recording exists. Allocate chord voices across columns and retain note-off relationships; richer precision depends on A04. |
| A06 | Scoped operations with note/instrument/volume/effect masks; mix paste | Partial | Yes / S–M | Extend existing validated edit batches and selection logic. Add selection, channel, pattern and song scopes with one undo action. |
| A07 | Flip, expand/shrink, rotate/nudge and instrument remap/swap | Partial | Yes / M | Transpose and row operations exist. Add deterministic transforms, preview and collision policies; do not overwrite unrelated columns. |
| A08 | Linear/logarithmic/exponential interpolation; arithmetic scaling; randomize/humanize | Partial through API | Yes / S–M | The drum-roll client already generates a rising volume curve. Generalize to reusable commands with fixed random seeds, bounded values and UI previews. |
| A09 | Command picker, inline explanations and color-coded effect families | Partial | Yes / S–M | Present the correct commands for the current module format. A Renoise command dialect would need an explicit native-song mode. |
| A10 | Arpeggio, slides, vibrato, tremolo, retrigger, offsets, reverse and note cut | Present in format-dependent core behavior | Yes / M for workflow | Expose discoverable editing helpers. Audit each behavior before mapping Renoise command names; matching letters do not imply matching semantics. |
| A11 | Probabilistic notes, mutually exclusive triggers and ghost-note modulation behavior | No qualified equivalent | Yes / L | Add explicit event semantics and deterministic render seeds. OpenMPT's instrument memory is not automatically Renoise's ghost-note behavior. |
| A12 | Fractional tempo, lines per beat, ticks per line, tempo automation | Partial/core machinery | Yes / M–L | Unify native musical timing while preserving imported module timing. Tempo changes must also remap automation and plugin transport. |
| A13 | Global groove/swing and saved groove settings | Core only | Yes / M | OpenMPT contains `TempoSwing`; expose it, define the native groove representation and test it against note/automation timing. |
| A14 | Metronome, count-in, beat/bar accents and recording defaults | New | Yes / M | Schedule clicks in the engine, with an independent monitor level and an explicit export policy. |
| A15 | Edit independently of the playback cursor | Present | Yes / S polish | Resonance already has Follow on/off. Improve cursor/playhead visibility and navigation without presenting this as a new engine feature. |

The structural limitation is visible in [Cell](/Users/rewbs/code/openmpt/editor/TrackerDocument.hpp:15). Timing hooks exist in [CSoundFile](/Users/rewbs/code/openmpt/soundlib/Sndfile.h:508); actual editor behavior is in [PatternView](/Users/rewbs/code/openmpt/mac/App/PatternView.swift) and [main application](/Users/rewbs/code/openmpt/mac/App/main.swift).

## B. Arrangement and performance

Renoise reference: [sequencer](https://tutorials.renoise.com/wiki/Pattern_Sequencer), [matrix](https://tutorials.renoise.com/wiki/Pattern_Matrix), [transport](https://tutorials.renoise.com/wiki/Transport_Panel).

| ID | Capability | Resonance now | Feasibility / remaining effort | Implementation assessment |
|---|---|---|---|---|
| B01 | Reusable patterns; insert, move, duplicate and rearrange order entries | Present | Yes / S polish | Existing order editor is a sound starting point. Distinguish reusing a pattern from making an independent copy. |
| B02 | Named song sections, pattern annotations and section navigation | Partial | Yes / S–M | Add native project metadata and navigable section headers. Preserve identities when orders move. |
| B03 | Pattern matrix with track-by-order thumbnails, selection and drag/copy | New | Yes / M–L | Basic overview can use current channels immediately. Reusable thumbnail caches and viewport culling fit the Metal UI. |
| B04 | Per-track block aliases and per-order-slot mute | New | Yes / L | Whole-pattern reuse already exists; independent block references and occurrence-specific mute require a new arrangement layer. |
| B05 | Queue the next pattern/section and switch at musical boundaries | New | Yes / L | Add bounded scheduled transport commands, with defined note release, effect-tail and automation behavior. |
| B06 | Pattern, block and section/range loops | Partial core behavior | Yes / M–L | Expose deliberate loop controls and handle note-offs, automation chase and plugin beat position at every wrap. |
| B07 | Track duplication/reordering and hierarchical group arrangement | Partial | Yes / M for flat tracks; L for groups | Flat operations need safe remapping. Groups must refer to the same structure as the mixer, not a separate cosmetic tree. |

Evidence: [order editor](/Users/rewbs/code/openmpt/mac/App/OrderEditor.swift), [document operations](/Users/rewbs/code/openmpt/editor/TrackerDocument.cpp). Resonance also supports choosing existing OpenMPT sequences; creation and renaming are additional Resonance improvements, not a substitute for Renoise-style sections and matrix blocks.

## C. Sample editing and recording

Renoise reference: [waveform editor and recording](https://tutorials.renoise.com/wiki/Sampler_Waveform), [sampler properties](https://tutorials.renoise.com/wiki/Sampler), [sample/file import](https://tutorials.renoise.com/wiki/Disk_Browser), [high-resolution plugin sampling](https://tutorials.renoise.com/wiki/Render_or_Freeze_Plugin_Instruments_to_Samples).

| ID | Capability | Resonance now | Feasibility / remaining effort | Implementation assessment |
|---|---|---|---|---|
| C01 | Import audio, waveform display, sample properties and forward/ping-pong loops | Present | Yes / S polish | Expand format qualification and large-file behavior; retain current fast waveform summaries. |
| C02 | Sample clipboard, mix paste, copy to new sample, channel operations and drawing | Partial | Yes / M | Add frame-accurate selection operations and chunked sample undo. Drawing should update only touched waveform summaries. |
| C03 | Normalize, reverse, fade, invert, silence, trim, DC removal and smoothing | Partial | Yes / S–M | Most basic operations exist. Add DC removal, smoothing and curved fades on the document worker, with exact undo. |
| C04 | Zero-crossing/grid snap, crossfade loops, reverse loops and loop-finish behavior | Partial | Yes / M | Reuse existing loop metadata where possible; reverse-loop and release semantics need explicit native playback validation. |
| C05 | Manual/transient slicing; nondestructive slice aliases; slice-to-pattern conversion | New | Yes / M for copied slices; L for aliases | Ship detection, markers and generated notes first. Shared source buffers and independently editable slice properties require asset references. |
| C06 | Beat sync by repitching | New UI | Yes / M | Calculate playback rate from sample length and musical duration. Define what happens during tempo changes and sample-rate conversion. |
| C07 | Pitch-preserving beat sync/time stretch, with percussion and texture approaches | New | Prototype / L | Evaluate a suitable DSP implementation against transients, sustained sounds, pitch extremes and CPU load. Offline processing can precede live stretching. |
| C08 | One-shot, auto fade, sample seek/chase and configurable interpolation | Partial/core machinery | Yes / M–L | Expose existing controls where semantics match. Reconstructing an already-playing sample during seeking needs more than moving the song cursor. |
| C09 | Record microphone/line input with monitor, channel choice and repeated takes | New | Yes / L | Add Core Audio input, bounded disk capture and take insertion. Initially stop/rebuild the song when committing a take. |
| C10 | Pattern-synchronized recording, dry capture with processed monitoring and latency correction | New | Yes / L | Depends on musical time, input capture and the routing graph. Calibrate input/output offsets with loopback tests. |
| C11 | Apply an effect chain to sample audio | New | Yes / M | Reuse offline plugin processing; add cancellation, tail-length choices, replacement/new-sample modes and reversible commits. |
| C12 | High-resolution sample storage and multisampling | Current native samples are 8/16-bit | Yes / XL | Preserve high-resolution assets in the native project and extend the voice path. Float32 WAV export does not mean samples retain float32 precision internally. |

Evidence: [asset editors](/Users/rewbs/code/openmpt/mac/App/AssetEditors.swift), [sample operations](/Users/rewbs/code/openmpt/editor/TrackerDocument.cpp), [sample storage width](/Users/rewbs/code/openmpt/soundlib/ModSample.h:109), [audio device](/Users/rewbs/code/openmpt/mac/Audio/AudioDevice.hpp).

## D. Instruments, modulation and phrases

Renoise reference: [instruments](https://tutorials.renoise.com/wiki/Instruments), [keyzones](https://tutorials.renoise.com/wiki/Sampler_Keyzones), [sample modulation](https://tutorials.renoise.com/wiki/Sampler_Modulation), [sample effects](https://tutorials.renoise.com/wiki/Sampler_Effects), [phrases](https://tutorials.renoise.com/wiki/Phrase_Editor), [instrument management](https://tutorials.renoise.com/wiki/Instrument_Selector).

| ID | Capability | Resonance now | Feasibility / remaining effort | Implementation assessment |
|---|---|---|---|---|
| D01 | Multi-sample keyboard mapping and graphical keyzone editing | Partial | Yes / M | Existing note maps support a useful piano-based editor and automatic drum/key distribution. |
| D02 | Velocity layers, overlapping zones, release samples and round-robin/random selection | New | Yes / XL | Replace the single-sample-per-key assumption with zones and a bounded voice allocator. This enables a much stronger native drum sampler. |
| D03 | Mute/choke groups, one-shot drum behavior and note-stealing policies | Partial note-action support | Yes / M–L | OpenMPT has new-note/duplicate-note actions. Explicit cross-sample choke groups and layered voice release need additional state. |
| D04 | Volume/pan/pitch/filter envelopes with sustain and loops | Present subset | Yes / M | Improve graphical editing, curve options, presets and clipboard. Existing tracker envelopes remain valuable for compatibility instruments. |
| D05 | Per-voice modulation sets: AHDSR, envelope, fader, LFO, stepper, key/velocity tracking and arithmetic combinations | Partial envelopes only | Yes / L–XL | Compile bounded modulation programs per voice; reuse curve editing UI. Avoid evaluating arbitrary scripting code in the audio callback. |
| D06 | Per-sample effect-chain assignment, shared chains and output routing | New | Yes / L–XL | Depends on layered instruments and the audio graph. Distinguish per-voice modulation from shared audio effects. |
| D07 | Instrument macros controlling multiple destinations with ranges/curves | New | Yes / M after stable parameter identities | Build a shared mapping system for UI, MIDI, API and automation. It can initially target existing AU/VST3 parameters. |
| D08 | Scale/key restriction, mono/legato/glide and common instrument controls | Partial/core machinery | Yes / M–L | Input scale filtering is straightforward; consistent legato and retrigger behavior across samples and plugins requires explicit policy. |
| D09 | Scala tuning and MTS-ESP client tuning for sample instruments | Core tuning support only | Yes / M for saved tuning; Prototype / L for live integration | OpenMPT has per-instrument tuning objects. Add import/UI/persistence first, then validate live tuning updates without disturbing legacy songs. |
| D10 | Instruments combining samples, a plugin and external MIDI | New | Yes / L–XL | Current plugin assignment replaces the playback sample map and forces MIDI channel 1. Introduce explicit layers and routes instead. |
| D11 | Reusable phrases with key/program triggers, independent resolution, shuffle, looping and transpose | New | Yes / L–XL | Reuse pattern editing controls; add an instrument-local event scheduler with independent phrase voices and seek behavior. Stored pattern snippets are an earlier, smaller step. |
| D12 | Generate phrases from selections; phrase presets and baking to patterns | New | Yes / M after D11 | Copies and references need explicit semantics; retain reproducible source notes when baking. |
| D13 | Scripted generative phrases and live coding | External static generation already possible | Yes / M for offline generators; Prototype / XL for live phrases | The existing API can write generated patterns now. A bounded scheduler and script worker are needed for live script changes. Renoise describes its newer phrase scripting as experimental. |
| D14 | Instrument duplicate/reorder/remap, remove unused assets and reusable presets | Partial | Yes / M | Use reference-aware operations, including plugin assignments and future phrases. Show unused-asset candidates before an undoable cleanup. |

The current one-sample mapping and tuning hook are in [ModInstrument](/Users/rewbs/code/openmpt/soundlib/ModInstrument.h:111). The plugin assignment constraint is explicit in [attachInstruments](/Users/rewbs/code/openmpt/mac/Audio/NativeInstrument.cpp:104). New phrase scripting is documented in the [3.5 announcement](https://forum.renoise.com/t/renoise-3-5-and-redux-1-4-released/76590).

## E. Mixer, routing and plugin hosting

Renoise reference: [mixer](https://tutorials.renoise.com/wiki/Mixer), [routing devices](https://tutorials.renoise.com/wiki/Routing_Devices), [plugin instruments](https://tutorials.renoise.com/wiki/Plugin), [plugin effects](https://tutorials.renoise.com/wiki/Plugin_Effects), [preferences, including plugin sandboxing](https://tutorials.renoise.com/wiki/Preferences).

| ID | Capability | Resonance now | Feasibility / remaining effort | Implementation assessment |
|---|---|---|---|---|
| E01 | Mixer strips, meters, mute/solo, pre/post gain/pan, width and selected effect controls | Partial channel controls/master meters | Yes / M for UI; L with full routing | A useful basic mixer can precede the graph. Full pre/post controls need well-defined positions in the signal path. |
| E02 | Independent effect inserts on each track | Master effects only | Yes / L–XL | Introduce track buses. Existing OpenMPT channel-to-plugin hooks reduce the starting cost, but the current native chain is not a track graph. |
| E03 | Nested group buses and shared processing | New | Yes / L after E02 | Persist hierarchy, compile processing order and preserve correct mute/solo behavior through nested groups. |
| E04 | Send/return tracks, pre/post sends and multiband sends | New | Yes / L after E02 | Route explicit bus edges; handle duplicate paths and latency. Start with acyclic routing and reject unsupported feedback. |
| E05 | Sidechain audio inputs to compatible effects | New | Yes / L after E02 | Negotiate auxiliary input buses and supply their audio. An envelope follower is useful but does not replace true plugin sidechain inputs. |
| E06 | Plugin auxiliary outputs and separate hardware output pairs | Main stereo only | Yes / L–XL | Battery can now load with many declared buses, but its auxiliaries remain inactive. Enable and route outputs individually; add multichannel Core Audio output. |
| E07 | AU/VST3 effects/instruments, native custom editors, state and generic parameters | Present | Yes / M ongoing qualification | Expand real-plugin coverage, focus handling, presets and failure recovery. AUv3 view-controller support is a separate Resonance hosting extension. |
| E08 | Plugin aliases, multitimbral MIDI channels and MIDI-controlled effects | New | Yes / M–L | Separate plugin instance identity from tracker instrument identity; several instruments may address one instance with distinct channels. |
| E09 | MIDI output from plugins routed to other instruments | New | Yes / L | Add bounded event-output routing, cycle prevention, timing and export behavior. Input MIDI support alone does not supply this. |
| E10 | Cached plugin browser, rescan, favorites, categories, hidden entries and preset browsing | Partial | Yes / S–M | Cache/rescan already work. Add metadata and user organization; vendor-specific preset databases may remain outside the generic host. |
| E11 | Plugin auto-suspend and dynamic parameter/bus/latency changes | Partial; graph currently stops on incompatible changes | Yes / L | Detect meaningful changes and rebuild safely away from audio. Sleeping instruments must wake before notes and retain tails correctly. |
| E12 | Run plugins in separate processes so their crashes do not terminate the song editor | Scan isolation only | Prototype / XL | Prototype shared-memory audio, bounded IPC, process restart and remote custom-editor ownership. Measure the latency/CPU tradeoff before choosing a default policy. |
| E13 | Graph-wide delay compensation and per-track timing offsets | Partial serial-chain/instrument alignment | Yes / L–XL | Compute longest-path latency across groups/sends/sidechains. Negative track offsets require scheduling lookahead, not moving already-rendered audio backward. |

Evidence: [host structures](/Users/rewbs/code/openmpt/mac/Audio/AudioUnitHost.hpp), [VST3 implementation](/Users/rewbs/code/openmpt/mac/Audio/VST3Host.mm), [core mixing/routing hooks](/Users/rewbs/code/openmpt/soundlib/Fastmix.cpp), [documented plugin limits](/Users/rewbs/code/openmpt/mac/COMPATIBILITY.md). Scanner isolation and runtime isolation are distinct capabilities.

## F. Built-in effects and control devices

These are feasible as **functional equivalents**; no promise is made to reproduce Renoise's proprietary DSP algorithms or preset sound. Existing macOS AU effects can cover some needs early, while Resonance-native effects would give predictable UI, project portability and automation behavior. Renoise references: [audio effects](https://tutorials.renoise.com/wiki/Audio_Effects), [meta devices](https://tutorials.renoise.com/wiki/Meta_Devices), [Doofer, Splitter and Notepad](https://tutorials.renoise.com/wiki/Doofer,_Splitter_%26_Notepad).

| ID | Renoise capability family | Resonance now | Feasibility / remaining effort | Implementation assessment |
|---|---|---|---|---|
| F01 | Gainer, DC Offset, Stereo Expander | No native device suite | Yes / S–M | Good first built-in devices; add parameter smoothing, stereo/mono checks and bypass tests. |
| F02 | Analog/Digital/Comb filters; EQ5, EQ10, Mixer EQ; Exciter | Plugins/core filtering cover subsets | Yes / M–L | Straightforward filters/EQ first. Nonlinear coloration and oversampling require additional listening and spectral tests. Include channel/mid/side processing. |
| F03 | Compressor, Bus Compressor, Gate, Maximizer | Available through plugins | Yes / M–L | Add native dynamics, with verified gain envelopes, lookahead and reported latency. Sidechain support depends on the graph. |
| F04 | Delay, Multitap, Repeater | Available through plugins | Yes / M | Tempo-synced delay and beat repeat fit the musical-time work; smooth timing changes and bound feedback. |
| F05 | Reverb, mpReverb, Convolver | Available through plugins | Prototype / L for quality suite | Algorithmic and convolution reverb are achievable. Evaluate sound, CPU and impulse-response loading; partition convolution away from UI work. |
| F06 | Chorus, Flanger, Phaser, Ringmod | Available through plugins | Yes / M | Share oscillators and delay/filter primitives while testing reset, stereo phase and automation behavior. |
| F07 | Distortion, Cabinet Simulator, LofiMat | Available through plugins | Yes / M–L | Implement waveshaping, cabinet filtering/IRs and bit/sample-rate reduction. Exact cabinet models and Renoise coloration are outside the target. |
| F08 | Instrument Automation, Instrument Macros, Instrument MIDI Control | Partial direct plugin automation | Yes / M–L | Provide reusable control panels using the same parameter registry as lanes, API and MIDI. MIDI output requires H02. |
| F09 | Hydra, Meta Mixer, XY Pad | New | Yes / M | Map one value to many, combine control sources and record XY gestures; make scaling and automation ownership explicit. |
| F10 | LFO, Key Tracker, Velocity Tracker, Signal Follower | New at track-device level | Yes / M–L | Use bounded control generators. Audio followers need graph taps and latency rules; live randomness should be reproducible when requested. |
| F11 | Formula device | New | Yes / M for bounded expressions; Prototype / L for scripting | Compile a restricted expression graph first. General Lua compatibility and live editing add complexity and execution limits. |
| F12 | Doofer-style macro racks; Splitter parallel/mid-side/frequency branches; Notepad | New | Yes / M–L after graph; S for notes | Reuse graph containers, macro mappings and presets. Preserve parameter identities when a rack is moved or nested. |

## G. Automation

Renoise reference: [graphical automation](https://tutorials.renoise.com/wiki/Graphical_Automation), [effect commands](https://tutorials.renoise.com/wiki/Effect_Commands), [automation following](https://tutorials.renoise.com/wiki/Song_Options).

| ID | Capability | Resonance now | Feasibility / remaining effort | Implementation assessment |
|---|---|---|---|---|
| G01 | Record parameter gestures and replay/export them | Present for hosted plugins | Yes / M extension | Extend the same contract to native devices, mixer parameters and macros. |
| G02 | Graphical lanes, parameter search, drawing, point/linear/curved segments and snapping | Partial data/API only | Yes / M for current timeline; L for full musical behavior | A first editor can expose existing points. Do not imply that absolute-frame lanes already follow moved/repeated patterns. |
| G03 | Pattern-relative automation that repeats/moves with musical material | New | Yes / L–XL | Add musical coordinates and distinguish a shared pattern's automation from an individual order occurrence. Define tempo, repeat and seek behavior. |
| G04 | Envelope clipboard, repeat/insert paste, shift/flip, ramps, sine and humanization | New UI | Yes / M | Share curve operations with instrument envelopes. Persist curves rather than indefinitely expanding them into stored points. |
| G05 | Automation chase when starting/seeking mid-song | Partial host positioning | Yes / M–L | Resolve the correct preceding value or curve segment before rendering, including loops, muted clips and tempo changes. |
| G06 | Pattern commands controlling effect parameters and automation target learn | Partial custom-editor gesture capture | Yes / M–L | Use stable parameter IDs; implement last-touched/learn where plugins report edits. Legacy tracker commands require explicit mapping. |

Evidence: [API timeline and lane operations](/Users/rewbs/code/openmpt/mac/AUTOMATION.md), [ParameterChange](/Users/rewbs/code/openmpt/mac/Audio/AudioUnitHost.hpp:34), [plugin editor](/Users/rewbs/code/openmpt/mac/App/PluginEditor.swift).

## H. MIDI, synchronization and extensibility

Renoise reference: [MIDI instruments](https://tutorials.renoise.com/wiki/MIDI), [MIDI mapping](https://tutorials.renoise.com/wiki/MIDI_Mapping), [clock synchronization](https://tutorials.renoise.com/wiki/MIDI_Clock), [Link transport](https://tutorials.renoise.com/wiki/Transport_Panel), [OSC](https://tutorials.renoise.com/wiki/Open_Sound_Control), [Lua tools](https://tutorials.renoise.com/wiki/Tools).

| ID | Capability | Resonance now | Feasibility / remaining effort | Implementation assessment |
|---|---|---|---|---|
| H01 | MIDI input routing/filtering, sustain, pitch bend, pressure and controller recording | Partial note input | Yes / M–L | Extend the existing bounded CoreMIDI path beyond notes; preserve event timestamps and route by device/channel/instrument. |
| H02 | External MIDI instruments, channels, banks/programs, CC and hardware audio return | New native workflow | Yes / L | Add CoreMIDI output scheduling and line-input returns, with offset calibration. External hardware requires real-time export. |
| H03 | MIDI learn for controls, relative encoders, ranges and saved mappings | New | Yes / M | Reuse the parameter/command registry, with absolute/relative modes, pickup behavior and project/template persistence. |
| H04 | MIDI Clock input/output, start/stop/continue and song position | New | Yes / L | Test drift, jitter, tempo changes, clock loss and reattachment. Avoid driving audio timing from UI timers. |
| H05 | Ableton Link tempo/phase and optional start/stop sync | New | Yes / M–L after musical time | Integrate the Link SDK and map its beat clock to the audio transport; verify multiple peers and device latency. |
| H06 | OSC network control and custom messages | Local JSON API instead | Yes / M | An OSC adapter can call existing musical commands. Add bounded scheduled messages; remote exposure should be a deliberate opt-in. |
| H07 | Script tools, menu/shortcut extensions, editor/console and tool packages | External API and Python client | Yes / M for external tools; L for embedded tool UI | Expand commands, subscriptions and transactions first. Embedded Lua is feasible; Renoise's complete Lua API and `.xrnx` compatibility are separate work. |
| H08 | Controller-specific performance setups such as Duplex | New | Yes / L, selectively | Build on MIDI mappings and matrix/mixer commands. Support chosen controllers; do not promise compatibility with the whole third-party ecosystem. |

The API already fulfills the user's “generate a rising drum roll” example without UI automation. A stronger version should add cross-domain transactions, event subscriptions and stable musical identities. This is Resonance's own product opportunity, not a claim that Renoise includes an AI agent. Evidence: [API contract and working example](/Users/rewbs/code/openmpt/mac/AUTOMATION.md).

## I. Rendering, audio infrastructure and performance

Renoise reference: [audio rendering](https://tutorials.renoise.com/wiki/Render_Song_to_Audio_File), [plugin instrument sampling](https://tutorials.renoise.com/wiki/Render_or_Freeze_Plugin_Instruments_to_Samples), [audio configuration and multicore processing](https://tutorials.renoise.com/wiki/Preferences).

| ID | Capability | Resonance now | Feasibility / remaining effort | Implementation assessment |
|---|---|---|---|---|
| I01 | Render song, sequence range or selection; rates/bit depths, markers and quality choices | Whole-song stereo float WAV subset | Yes / M | Parameterize the exporter; preserve latency/tails and atomic output replacement. Add integer-output dithering and duration validation. |
| I02 | Render separate tracks/stems and pattern occurrences | New | Yes / L | Needs routing taps and an explicit policy for shared returns/master effects. Stems through nonlinear shared effects do not necessarily sum to the master render. |
| I03 | Render a selection into a new sample/instrument | New workflow | Yes / M | Reuse offline rendering plus sample insertion and grouped undo. Initially bounce a defined range without running the hardware device. |
| I04 | Plugin grabber: sample a note/velocity range, retain release tails and freeze to an instrument | New | Yes / M–L for one velocity; L–XL for layered result | Existing offline plugin hosting does much of the rendering. Velocity zones, high-resolution assets and expressive behavior need the richer sampler. |
| I05 | Real-time export for external hardware or plugins requiring live operation | New | Yes / L | Capture the actual graph in real time and compensate return latency. Clearly report a missing or failed external source. |
| I06 | Multicore audio graph execution and CPU/overload monitoring | Callback metrics; current graph largely serial | Prototype / XL for parallel graph | Parallelize independent buses only after a correct serial graph exists. Measure small-buffer deadlines; more worker threads do not guarantee lower latency. |
| I07 | Audio device/rate/buffer configuration, multichannel I/O and reinitialization | Partial output path | Yes / M–L | Extend native settings and device-loss recovery. Multi-output routing and input capture are larger than device selection alone. |

Evidence: [export implementation](/Users/rewbs/code/openmpt/mac/Audio/AudioExport.mm), [audio device implementation](/Users/rewbs/code/openmpt/mac/Audio/AudioDevice.mm), [existing measured limits](/Users/rewbs/code/openmpt/mac/COMPATIBILITY.md).

## J. Browsing, project workflow and interface

Renoise reference: [disk browser](https://tutorials.renoise.com/wiki/Disk_Browser), [libraries/presets](https://tutorials.renoise.com/wiki/Libraries), [scopes/spectrum](https://tutorials.renoise.com/wiki/Track_Scopes), [GUI customization](https://tutorials.renoise.com/wiki/GUI_Customisation), [templates](https://tutorials.renoise.com/wiki/Template_Song), [song comments](https://tutorials.renoise.com/wiki/Song_Comments), [preferences](https://tutorials.renoise.com/wiki/Preferences).

| ID | Capability | Resonance now | Feasibility / remaining effort | Implementation assessment |
|---|---|---|---|---|
| J01 | Searchable sample/instrument browser, favorite folders, audition and batch import | File dialogs/basic lists | Yes / M | Index asynchronously and audition from a separate preview voice. Cancel obsolete preview loads promptly; avoid blocking playback while browsing. |
| J02 | Presets and shareable libraries for instruments, phrases, modulation and effect chains | Project/plugin state only | Yes / M–L | Define versioned Resonance preset bundles with dependencies and preview metadata. Add each preset type as its subsystem becomes available. |
| J03 | Per-track scopes, spectrum/spectrogram, phase/correlation display and source comparison | Master meters only | Yes / M; per-track taps need graph | Copy/downsample into bounded analysis buffers; calculate FFTs off the callback and draw only visible panels. |
| J04 | Themes, font/spacing/scaling, configurable shortcuts and continuous neighboring-pattern display | Partial keyboard/display settings | Yes / S–M | Preserve readable native controls and extend existing glyph/render caches. Continuous patterns need correct order-to-pattern mapping. |
| J05 | Resizable/detachable editors, mixer windows and saved workspace layouts | Partial windows | Yes / M | Use AppKit windows and shared view models, with a common transport and predictable keyboard focus. |
| J06 | Song templates, comments, instrument descriptions and device notes | Partial metadata | Yes / S–M | Native project metadata is sufficient. Templates should include routing, mappings and assets as those features arrive. |
| J07 | Automatic backups, recovery and consolidated projects | Present subset | Yes / M polish | Improve recovery browsing and policy settings. Preserve missing plugin states and avoid expensive plugin serialization on the audio thread. |
| J08 | Instrument/sample activity previews and contextual selection | Partial | Yes / S–M | Add lightweight playback telemetry and optional cursor-follow selection; keep frequent updates out of whole-document rebuilds. |

Evidence: [pattern renderer](/Users/rewbs/code/openmpt/mac/App/PatternView.swift), [keyboard/display settings](/Users/rewbs/code/openmpt/mac/App/KeyboardSettings.swift), [recovery](/Users/rewbs/code/openmpt/mac/App/RecoveryStore.swift), [plugin picker](/Users/rewbs/code/openmpt/mac/App/PluginPicker.swift).

## K. File interchange and compatibility boundaries

Renoise reference: [supported imports](https://tutorials.renoise.com/wiki/Disk_Browser), [instrument/library formats](https://tutorials.renoise.com/wiki/Libraries), [project containers and MIDI import options](https://tutorials.renoise.com/wiki/Preferences).

| ID | Capability | Resonance now | Feasibility / remaining effort | Implementation assessment |
|---|---|---|---|---|
| K01 | Traditional tracker module import | Present; broader engine preview support | Yes / M qualification | Preserve OpenMPT behavior and existing editable formats. Conversion to a richer native song must be explicit and tested. |
| K02 | Standard MIDI file import | New native workflow; upstream importer disabled in this build | Yes / M–L | Adapt the upstream importer or add native conversion with tempo maps, channel/instrument mapping, note lengths and CC lanes. Flag quantization losses. |
| K03 | SFZ keyzone/sample interchange | Partial engine import, not separately qualified | Yes / M for a documented subset; L for richer zones | Qualify existing sample mapping; velocity/round-robin/export coverage follows the native instrument model. |
| K04 | Renoise XRNS songs, XRNI instruments and component presets | No native loader found | Yes / L for limited import; Prototype / XL for broad fidelity | Start with sample extraction, notes/orders and simple mappings. Emit a conversion report for unsupported devices, automation, phrases and plugin state; never imply lossless import. |

The existing MIDI importer is gated by build flags in [Load_mid.cpp](/Users/rewbs/code/openmpt/soundlib/Load_mid.cpp:27); [the native build configuration](/Users/rewbs/code/openmpt/mac/CMakeLists.txt:15) does not enable that implementation. Its presence in the repository should not be mistaken for a working Resonance import feature.

### Targets to defer or exclude

| Target | Recommendation |
|---|---|
| Exact Renoise sound, preset reproduction and fully lossless XRNS playback | Treat as a separate compatibility program. Similar workflows are feasible without promising identical DSP or event semantics. |
| Drop-in support for every Renoise Lua tool/controller configuration | Build a good Resonance API and selected adapters first; matching Renoise's whole object model and UI API is a separate undertaking. |
| macOS VST2 | Potential legacy compatibility project, lower priority than robust AU/VST3. It is not needed to satisfy the user's stated plugin direction. |
| Windows plugins and Windows/Linux audio backends | Outside this Mac-native scope. |
| ReWire | Do not pursue for current Renoise parity: it was removed in Renoise 3.5. [Release announcement](https://forum.renoise.com/t/renoise-3-5-and-redux-1-4-released/76590). |
| Resonance itself as an AU/VST3 plugin | Feasible as a later product, but hosting inside another DAW changes lifecycle, transport, I/O and packaging. Renoise's plugin sibling is Redux; it is not the same scope as this standalone app. [Official downloads](https://www.renoise.com/download). |
| Renoise's factory sounds and third-party ecosystem | Create Resonance content or use appropriately available user libraries; do not count recreating an established ecosystem as a normal engineering feature. |

## Architecture that makes this achievable

These are engineering recommendations inferred from the source audit, not descriptions of Renoise's internals.

### 1. Keep compatibility playback; extend the native song model

Keep the current OpenMPT path for existing modules. Introduce versioned native structures for logical tracks, note columns, arrangement occurrences, automation curves, routing and layered instruments in `.resonance`. Use stable IDs for objects and plugin instances; array positions can remain display indices.

Simple songs should continue to use the existing path. Richer native songs can progressively use new scheduling, voices or routing where required. Validate any adapter that expands logical note columns into engine channels against channel and effect limits. This strategy avoids making every new feature contingent on replacing the whole engine, while acknowledging that the present six-field cell cannot represent full Renoise behavior.

Define conversion explicitly: native saving preserves all features; exporting a legacy module either performs a supported conversion with a loss report or refuses the unsupported operation. Existing projects need schema migration and recovery tests.

### 2. Establish one musical timeline

Represent order occurrence, pattern, row and sub-row position, plus tempo and groove changes. Convert that musical timeline into render-frame events. Decide explicitly which automation belongs to a reusable pattern and which belongs to an individual arrangement occurrence.

The existing absolute 48 kHz automation remains a valid legacy representation. Migrate it only when a reliable musical interpretation exists; retain absolute-time lanes where necessary. Seeking, repeat, tempo changes, instrument phrases, MIDI recording and plugin transport should all consume the same timing authority. Otherwise each feature will appear correct in isolation and disagree during playback.

### 3. Build and qualify a serial track graph before parallelizing it

Expose sample/plugin audio as track buses, then add inserts, groups, sends, sidechains and auxiliary outputs. Compile routing and allocate buffers outside the audio callback. Commit a prepared graph at a controlled boundary and retire the previous graph away from audio processing.

Define ownership of native plugin audio carefully: a shared multitimbral instance cannot always be split back into the tracker channels that triggered it. Route its declared outputs to explicit destination buses. Delay compensation must follow audio paths, including returns and bypass states.

Start with a serial graph to make timing and correctness tractable. Add multicore scheduling only after profiling demonstrates a benefit at the intended buffer sizes. Runtime plugin isolation deserves an early separate prototype, because process boundaries affect latency, custom editors and recovery.

### 4. Add a layered sampler without weakening imported instruments

Create native assets/zones/voice policies alongside legacy sample maps. Support shared sample references before nondestructive slices; support velocity/release zones before a full multisample grabber. Keep original high-resolution audio in native assets if we want high-resolution sampling throughout.

Compile modulation into bounded programs. Share UI curve editors and parameter descriptions across sample envelopes, automation lanes and macros, while retaining their different execution semantics: per-voice envelopes are not the same as global track automation.

### 5. Make commands and undo common to UI and agents

Promote today's validated pattern batches into typed musical commands such as interpolate, humanize, remap, slice, route and create phrase. Extend preview and revision checking to structural operations. Add one transaction/undo entry spanning document, routing, plugin and automation changes, plus change subscriptions.

This directly supports requests such as “add a snare roll, make it rise in volume, then automate a filter into the next section.” An external agent should receive structured musical objects and a preview of changes, not rely on screen coordinates or rewrite entire files.

## Recommended implementation order

| Delivery | User-visible result | Principal work and boundary |
|---|---|---|
| **1. Editing and browsing** | Interpolation, humanization, mix paste, remapping, command help, searchable sound browser, templates/comments | Mostly S–M additions using current commands. Begin stable IDs and shared transaction work alongside them. Basic automation point editing can ship with its current absolute-time semantics clearly shown. |
| **2. Musical arrangement and automation** | Matrix overview, named sections, curves, pattern-aware automation, better recording precision | Musical-time model and native event schema. Start with read-only matrix overview, then editable blocks; add independent note columns only with explicit track semantics. |
| **3. Mixer and plugin routing** | Per-track effects, groups/sends, Battery auxiliary outputs, sidechains and a proper mixer | Serial graph and graph-wide latency compensation are release prerequisites. Test plugin process isolation early, then ship it when its timing/editor recovery is qualified. |
| **4. Sampling and instrument design** | Slicing, render-to-sample, recording, keyzones/velocity layers, macros and plugin grabber | A simple copied-slice tool and single-velocity grabber can arrive earlier. Full layers, shared assets and precision improvements need the native instrument model. |
| **5. Phrases and performance** | Triggerable phrases, live pattern/section queues, MIDI learn/output, Link, generative tools | Reuse musical time and the graph. External/static generators can arrive much earlier; live scripting and broad XRNS import follow only after bounded prototypes. |

The most valuable individual additions are:

1. **Graphical plugin automation** that eventually follows patterns and arrangement edits.
2. **Per-track effects and a mixer**, then groups, sends and sidechains.
3. **Battery/multi-output plugin routing** and shared plugin instances with selectable MIDI channels.
4. **Advanced pattern transformations** with preview and a single undo action.
5. **A searchable, cached sound/preset browser** with quick audition.
6. **Pattern matrix and named song sections.**
7. **Sample slicing and render-selection-to-sample.**
8. **A plugin-to-sampler grabber**, initially one velocity layer.
9. **Velocity layers, choke groups and round robin** for native drums.
10. **Macros and reusable modulation**, shared across UI, controllers and agents.
11. **Reusable phrases and deterministic generators.**
12. **Plugin crash containment**, developed as a substantial reliability feature rather than a small hosting patch.

This ranking reflects product value. The delivery table reflects dependencies; they are intentionally not identical.

## Protecting the 60 fps UI and audio core

Feasibility depends on maintaining the original performance requirements throughout implementation. These are proposed acceptance gates, not new test results.

| Area | Acceptance evidence |
|---|---|
| UI responsiveness | Measure presented frame intervals during dense scrolling/editing with playback, automation, meters and plugin windows. Test large samples and parameter lists. A mean near 60 fps alone is insufficient; report missed frames and long stalls. |
| Audio callback | Retain allocation/free/lock detection and deadline monitoring, including first use, note bursts, graph changes and queue saturation. No file access, script evaluation or synchronous UI/IPC dependency in the callback. |
| Timing and routing | Use synthetic impulses, known-latency plugins and note/event fixtures to verify sub-row timing, tempo changes, loop/seek behavior, parallel routes, sidechains and latency compensation. Compare multiple rates and buffer sizes. |
| DSP quality | Use frequency/impulse responses, distortion/aliasing measurements and a fixed listening corpus. Test extreme parameters and automation; finite output alone does not establish good sound. |
| Persistence | Save/reopen/undo/redo projects with new structures, missing plugins and schema migrations. Keep sample, routing and parameter references stable after reorder/delete operations. |
| Plugin failures | Crash and hang a test plugin in the runtime helper; verify the editor survives, the affected route is silenced/recoverable and the remaining graph has bounded behavior. Test custom-editor closure and helper restart. |
| Agent operations | Verify previews do not mutate, stale revisions reject, partial failure rolls back and seeded generation is repeatable. Exercise the actual hidden app through its socket. |
| Audio regression | Continue offline reference renders and virtual-device loopback. Use exact comparisons for deterministic fixtures and appropriate tolerances/statistics for stochastic or vendor DSP. |

Most correctness checks can run without taking over the user's screen or speakers: isolated plugin probes, offline renders, hidden-app API tests and a virtual audio device are already part of this repository's approach. Final visual and sustained frame-presentation qualification still needs a visible test desktop; use a dedicated Mac or separate test session for that portion. A hidden window cannot prove visible 60 fps behavior.

The current repository documents useful short-run results, but **does not yet establish the final sustained 60 fps or 30-minute combined UI/audio gate for the expanded plugin build**. Close that gap before advertising the performance target as a blanket guarantee. See [qualification record and reproducible checks](/Users/rewbs/code/openmpt/mac/COMPATIBILITY.md) and [hidden-app API testing](/Users/rewbs/code/openmpt/mac/AUTOMATION.md).

## Scope of this work

This deliverable is the feasibility inventory and recommended sequence. No application code was changed, no feature implementation was started, and no fresh runtime performance result is claimed.
