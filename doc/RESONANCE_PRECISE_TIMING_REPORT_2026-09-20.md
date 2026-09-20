# Resonance: precise timing and pattern commands

This increment addresses the requested note timing/recording, extra effect subcolumns, stable plugin parameter commands and sub-tick pitch bends. Development and qualification used quiet background mode. The original and stable applications were not replaced.

## What is implemented

### Precise notes and MIDI recording

- Independent note-on, note-off and cut events at 1/65536-row positions, with multiple events inside one row. Playback dispatches at the first audio sample reaching the position. Ordinary tracker cells retain their existing tick behavior.
- Sample voices and native AU/VST3 instruments use the same musical clock. Existing sample interpolation, loops, instruments and new-note behavior remain in the core. Plugin MIDI velocity retains seven-bit precision.
- Pattern → Precise Notes… (Command–Shift–N), Return or double-click opens the row editor. It supports adding, updating and removing events, exact fractional offsets, instrument/velocity, preview and one-step Undo. A purple `~` marks precise notes in the grid. The editor can move an ordinary row onset into precise timing while preserving effect commands.
- Live MIDI capture uses CoreMIDI timestamps mapped against the audio device's presentation clock. Delayed UI polling does not determine note placement. Takes support exact timing or optional quantization, input-time adjustment and adjacent note columns for chords. One column uses mono handoff for overlapping keys. MIDI channel plus key identifies held notes.
- A take is separate from the song while recording. Stopping closes held notes and commits the native UI take as one Undo step. Missing timestamps, polyphonic capacity exhaustion and event overflow are counted. A conflicting song edit retains the rejected take for review. Finish/discard actions are in the Pattern menu; saving or replacing/closing the document requires resolving the take first.
- Fine note events survive project save/reopen, pattern duplication, Undo/Redo and offline export. Native metadata version 9 is used only when precise notes are present.

### Extra effect subcolumns and stable plugin controls

- Up to eight extra native effect subcolumns per raw note channel. Wide tracks retain horizontal scrolling, keyboard navigation, command help, Return/double-click editing and single-cell Delete.
- `P` sets a parameter and `L` slides to a target. Numbered bindings refer to a persistent plugin instance UUID and its native parameter ID. Rack reordering cannot redirect a command. Missing plugin targets remain unresolved instead of choosing another slot.
- Commands have fractional row offsets and durations. Slides interpolate at every audio sample, including across ticks. Interruptions start from the reached value, and identical-position commands have deterministic precedence. Discrete parameters reject slides. Competing enabled automation paths are rejected.
- Parameter-only projects use metadata version 7. Their previously verified checkpoint remains available separately.

### Pitch commands

- `T` sets a semitone offset and `B` bends to a target over a fractional duration. Values can be negative, and a zero offset returns to the original note. A bend carries forward until changed.
- Native samples integrate the pitch ratio at every sample. AU/VST3 instruments receive sample-timed 14-bit MIDI pitch wheel events. The editor exposes the instrument's wheel range so command values can match its configured range.
- Pitch commands, wheel range and full value precision survive project recall and history. Pitch projects without precise notes use metadata version 8.

## Agent interface

The UI uses the same validated API as external applications:

| Methods | Purpose |
| --- | --- |
| `pattern.notes.get/set` | Read/replace precise note events, optionally clear selected ordinary note fields, preview and Undo |
| `recording.start/get/capture/stop/commit/discard` | Configure and inspect a timestamped take, capture external input, preview/commit or explicitly discard |
| `pattern.performance.get/set` | Configure extra columns, stable parameter bindings, parameter set/slide and pitch set/bend commands |
| `context.set` | Navigate all extra subcolumns |
| `plugin.parameters.get` | Inspect writable parameters and whether they support slides |

Writes use document revision guards; recording operations also identify the take. The API reference and JSON schema include all methods, value ranges, timing units and recording semantics. See [AUTOMATION.md](../mac/AUTOMATION.md).

## Qualification

The full background suite passed 58/58 CTests, plugin picker/recovery, both API hosts, windowless startup with a 127-channel song, and offscreen editor/Metal checks. The combined-control test also passed: precise notes, parameter ramps and pitch bends follow the same independent reference across all three sample rates and four callback sizes. Exported sample audio matches the playback renderer exactly. Callback audits found zero allocations, frees or locks in the tested paths. Four targeted ASan/UBSan suites passed; the precise-note suite was rerun successfully after adding export parity. Both API hosts and windowless startup were rerun after bounding long-take mutation replies.

Audio tests use independent sample-by-sample references at 44.1, 48 and 96 kHz, with 1/17/128/4096-frame callbacks. They cover fractional starts, same-row releases, interrupted slides/bends, repeats, tempo/groove changes, stable rack identity, save/reopen and history. The callback audit checks allocations, frees and locks. Additional recording tests inject host-clock timestamps, quantization, polyphonic/mono input, repeated-pattern boundaries and expired history without opening a hardware device.

Offscreen images show the precise-note editor/grid and pattern-effect editor/grid. Native control interactions and geometry are checked separately because some standard AppKit controls render blank when their window is never presented.

## Limits and remaining qualification

- These extra columns carry native parameter and pitch commands. Arbitrary extra legacy source-format effect commands and independent pan/volume grid subcolumns are not implemented.
- Traditional cell clipboard and row-transform tools operate on ordinary cells. Precise events use their dedicated editor/API; sub-row humanization and a unified clipboard remain future work.
- Instrument envelopes and ordinary legacy effects retain the core’s tick-based timing; note onsets/releases and native parameter/pitch curves have the new precise clock.
- Timestamped live recording is MIDI input. Computer-keyboard entry remains the existing step-entry workflow.
- Uncommitted takes live in memory; automatic recovery copies resume after the take is resolved. A take whose base revision changed remains available through `recording.get` for manual placement and must be explicitly resolved.
- Plugin parameter ramps currently process an affected plugin in one-frame blocks. The fixture tests establish sample-accurate host interpolation, but commercial-plugin CPU cost and behavior need qualification. Plugins control their own internal smoothing.
- Plugin pitch requires MIDI pitch support (including a VST3 MIDI-controller mapping). Match the wheel range in the plugin; values beyond it saturate. Notes on the same plugin MIDI channel share the bend. Up to 16 raw channels can carry pitch commands. Native sample bends do not have MIDI's 14-bit limit.
- No screen control, visible-window interaction, physical audio/MIDI or commercial plugins were used. Sustained visible 60 fps, hardware recording latency and vendor editor/plugin compatibility are not newly qualified by this increment.

## Frozen delivery

- App: `bin/mac-checkpoints/2026-09-20-precise-timing/Resonance.app` (display name **Resonance Background**, bundle `org.resonance.tracker.background`).
- Matching development build: `bin/mac-background/Resonance.app`.
- Executable SHA256: `04b8d4f7471fdf597b6db5023d63ab61a662996e9eb219233f30c220300dfc7b`.
- Source fingerprint: `0e94694704513176a6ab317e36c20347d6f35917915c425873719a510cc6267a` (387 source files; packaged manifest checked against the workspace).
- Both app copies passed strict signature verification.
- Evidence: [2026-09-20-precise-timing](mac-native-qualification/2026-09-20-precise-timing/).
- Original and stable executable hashes remain `c2a016b66a2ff5e9bb6b62558900dae6d47469dbd580ad8cf5c626b1953a8d8d`; the earlier parameter checkpoint remains `d6b05a0e1c6ded556ad0ebe86778c9dcb084b453ea076d4f07b2c325f0ac8378`.

Use this build when opening projects containing the new native timing metadata. Older builds reject versions they do not support. No physical output device, visible window or commercial plugin was used for this qualification.
