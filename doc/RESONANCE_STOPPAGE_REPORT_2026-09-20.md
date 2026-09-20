# Resonance — stopped checkpoint, 20 September 2026

Development has stopped at your request after completing AU factory-preset and VST3 program browsing. The latest background application builds, starts without a window, and passes the automated checks described below. The broader Renoise-inspired feature list remains incomplete.

**Frozen latest build:** [Resonance Background](../bin/mac-checkpoints/2026-09-20-factory-programs/Resonance.app)  
**Working background build:** [Resonance Background](../bin/mac-background/Resonance.app)  
**Detailed feature ledger:** [all requested sections and individual feature IDs](RESONANCE_EXPANSION_PROGRESS.md)  
**Agent API:** [commands and behavior](../mac/AUTOMATION.md)

The ordinary `bin/mac-native/Resonance.app` and earlier `bin/mac-stable/Resonance.app` are unchanged. The latest features live in the separate **Resonance Background** application. The frozen latest copy and working background copy have identical executable hashes and both pass signature verification. No development or test process has intentionally been left running.

## Completed during this background run

| Priority | Delivered |
|---|---|
| A01 / A06 | Scoped insert/delete rows with field masks, exact previews, explicit data-loss guards and one-step Undo. The native menus and agents use the same command path. |
| A02 / A06 | Named polyphonic tracks using adjacent note columns; create/group/ungroup; shared mixer processing; independent saved column mutes; whole-track editing scope. |
| A09 | Searchable source-format command picker, descriptions, effect-family colors, cursor help and an agent-readable command catalog. |
| A12 / A13 | Fractional BPM, ticks per row, beat/bar settings, timing modes, saved normalized groove and two-row swing. Native previews, shared API, exact timing persistence and loss-checked module export. |
| A15 | Separate edit cursor and playhead, explicit Follow state, and revision-checked agent navigation. |
| G04 | Pattern-automation and instrument-envelope clipboard; repeated/insert paste, shift, time/value flip, scale, ramps, sine and seeded humanization. Instrument loop/sustain/release markers follow edited nodes, with rounding and reattachments reported in previews. |
| G06 | “Use last touched” for parameters changed through native controls, a plugin editor or the API. Stable identities protect against plugin reorder/removal; selecting a target preserves the current draft. |
| E01 | Virtualized mixer strips with stereo meters, pre/post gain, independent input/output balance, width, mute/solo, routing inspection and access to effect controls. |
| E08 | Multiple tracker instruments sharing one AU/VST3 instrument instance, with independent MIDI channels. One processor, state, automation and output set; guarded native/API assignment and plugin Undo. |
| E10 | Cached searchable plugin inventory; explicit rescan; favorites, categories, format/type filters and hidden entries. Native `.resonance-preset` save/inspect/load. Standard AU factory presets and VST3 unit/program lists, searchable in the native UI and accessible through `plugin.programs.get/load`. |

New musical operations are exposed to agents, with validation and the appropriate history/persistence behavior. Browser preferences have their own revision; last-touched information is session-local.

The final program-browser change validates both song and catalog revisions. Selection does not load a program. Apply prepares a disposable plugin instance, preserves routing/aliases/automation, and creates one plugin Undo step. Unsupported selectors, stale catalogs and failed preparations reject. The work also fixed VST3 state saving unnecessarily re-sending unchanged program selectors, which could reset edited sounds in some plugins.

Additional correctness fixes made during this run include deterministic mixer control ramps, a duplicate automation-knot scheduling error under small callback sizes, and exact native retention of instrument envelopes that ordinary module writers discard or alter.

## Functionality already present and retained

- **Tracker and arrangement:** native Metal pattern editor; keyboard entry, selections, clipboard and transpose; reusable patterns and order editing; sections/annotations; pattern matrix and block copying. Bulk tools include flip, expand/shrink, rotate, remap, interpolation, scaling and seeded randomization.
- **Audio and mixing:** OpenMPT playback core, offline WAV export, stereo track/group/return routing, inserts, nested groups, pre/post sends, sidechains, AU/VST3 auxiliary outputs, graph delay compensation and track offsets.
- **Plugins and automation:** macOS AU/VST3 effects and instruments, custom editors, generic parameters, state recall, gesture recording, musical pattern envelopes and export. Plugin scanning/validation is isolated; runtime plugin processing remains inside the app.
- **Sampling and instruments:** import, waveform editing/zoom/drawing, sample clipboard and structural paste, copy-to-new, normalization/reverse/fades/gain and other processing; snapping, crossfades and normal/sustain forward, ping-pong and reverse loops. Sample mapping, ITI/XI import and instrument envelopes are available.
- **Built-in effects:** 15 devices — Gainer, DC Offset, Stereo Expander, Digital Filter, EQ5, EQ10, Mixer EQ, Comb Filter, Distortion, Cabinet Simulator, LofiMat, Compressor, Bus Compressor, Gate and Maximizer. They share native/API parameters, automation, state, history and routing.

These are implemented capabilities, not claims of complete Renoise parity or full qualification of every existing workflow.

## Verification at the stopping point

- **54/54 selected core suites passed.** Physical MIDI and stress suites were deliberately excluded.
- **Both complete API hosts passed**, including the actual app running privately without a window; tests cover revisions, retries, invalid input and history.
- **Windowless application startup**, recovery and plugin-picker checks passed.
- **Offscreen native interface checks passed.** Compact program-browser and plugin-panel snapshots were inspected. The combined run initially stopped at a typo in the new Swift test fixture; the corrected final interface run passed. No production change was needed for that correction.
- **4/4 final ASan/UBSan suites passed:** factory programs, preset sessions, native plugins and auxiliary buses. Earlier slices also have targeted sanitizer evidence.
- Factory-program audio tests compare every rendered sample against independent expected gains at **44.1, 48 and 96 kHz**, using **1, 17, 128 and 4096-frame buffers**. They verify unit-specific VST3 delivery, AU preset IDs, parameter refresh, malformed/changed catalogs, failed preparation, preserved routing, Undo/Redo and native save/reopen.
- The packaged production-source hashes match the source tree. The final manifest includes the corrected test fixture, and the application was re-signed and verified. The ordinary and earlier frozen executable hashes remain unchanged.

This run used reduced-priority, two-worker builds and sequential heavy checks. It used no screen control, visible windows, physical audio/MIDI or commercial-plugin loading. Audio checks used silent offline rendering, built-in effects, Apple system AUs and private fixtures.

**Still unverified:** sustained presented 60 fps, live keyboard/MIDI/custom-editor interaction, combined hardware/audio load, and broad commercial-plugin compatibility. Offscreen timing is not counted as passing the 60 fps requirement. In particular, this run does not establish that Battery 4's previously reported failures are resolved.

[Verification logs, snapshots, build manifest and executable hashes](mac-native-qualification/2026-09-20-background/README.md) are archived alongside the earlier checkpoints.

## What remains

| Priority group | Main outstanding work |
|---|---|
| **A01–A15 — pattern editing** | Independent pan/delay and extra effect subcolumns; high-resolution note timing and recording; mono/poly/chord entry and configurable quantization; probabilistic/ghost-note behavior; dedicated tempo-envelope automation; metronome/count-in. Complete live qualification of existing entry/clipboard and source-format effect workflows remains open. |
| **G01–G06 — automation** | Mixer/macro targets and recording qualification; arbitrary-row seek/chase and muted-clip behavior; explicit pattern-command bindings to stable plugin parameters; live editing/performance checks. |
| **E01–E13 — mixing/plugins** | Multiband sends; separate hardware output pairs; MIDI-controlled effects and plugin MIDI output routing; vendor preset-file formats/proprietary browsers; auto-suspend and safe dynamic parameter/bus/latency changes; runtime plugin process isolation; wider commercial-plugin and sustained routing/PDC qualification. |
| **Other authorized sections** | Advanced arrangement/aliases/hierarchy; slicing, beat sync/time stretch, audio recording and high-resolution samples; layered instruments, modulation/macros, tuning and phrases; the remaining delay/reverb/modulation/meta-device effect families. The full ledger retains their individual scope. |

## Project compatibility and resumption

Use `.resonance` for native features. Projects using aliases/non-default plugin MIDI channels, grouped note-track metadata, input balance, or native timing/envelope corrections can require newer project payloads that older builds reject. The background app should be used to reopen those projects. Legacy module export checks for supported forms of data loss; it is not a substitute for the native project.

No commit, reset or cleanup was performed. The workspace retains the accumulated development changes and untracked native-app sources. Development should resume from this checkpoint only when requested, with the quiet-background restrictions still in force unless you change them.
