# Resonance: numeric safety and note-offset fixes

The reported note-offset problem was reproduced in an offscreen UI regression:
typing an offset and clicking Apply submitted the previously stored draft. The
typed value was only copied into that draft by **Update selected**. Converting an
ordinary note therefore created a precise event at offset zero, which explains
the `~` marker without an audible delay.

## Changes

- **Precise notes:** Apply and Check edit now validate and save the displayed
  fields directly. Selection changes retain valid edits; invalid input remains
  visible for correction. Sorting after changing an offset keeps the same event
  selected. Reloading the same row explicitly refreshes its fields. Removing the
  last event still leaves an empty row and does not re-add the old fields.
- **Timing explanation:** the dialog gives fractional-row examples: `0` is row
  start, `0.5` halfway, and `0.75` three quarters. This delays the note rather than
  skipping into the sample. The validation-only Preview button is now **Check
  edit**. `~` identifies precise events, including zero-offset events.
- **Plugin parameter controls:** removed an unconditional floating-point-to-Int
  conversion that could trap on NaN, infinity, or a finite value outside Int's
  range. Invalid vendor values/ranges disable editing instead of feeding bad
  numbers to AppKit or inventing a parameter change. Wide logarithmic ranges no
  longer divide maximum by minimum, and sub-precision steps cannot overflow an
  edit into infinity.
- **Waveform navigation:** pan distances are bounded before integer addition;
  zoom anchors and scroll distances are bounded before integer conversion.
  Non-finite zoom anchors are ignored.

The audit focused on the class of the previous crash: narrow arithmetic and
floating-point/integer conversions before validation. It covered native Swift
editor conversion sites, bridge numeric validators and call-site bounds, and
precise-note/audio scheduling. No additional unbounded UInt8 volume scaling was
found. The existing volume conversion fix and autosave remain included.

## Timing verification

The audio engine already honored the submitted offset. No scheduler or audio
callback change was necessary. A new end-to-end test converts an ordinary note
through `pattern.notes.set`, clears just its ordinary onset, serializes a native
project, exports WAV, and measures the actual waveform onset. It covers row zero
and a later row, retains effect commands and neighboring notes, and checks Undo,
Redo and save/reopen.

At 125 BPM, six ticks per row, classic timing, and 48 kHz:

| Row fraction | Measured delay | Time |
| --- | ---: | ---: |
| 0.25 | 1,440 samples | 30 ms |
| 0.5 | 2,880 samples | 60 ms |
| 0.75 | 4,320 samples | 90 ms |

Additional tested offsets are 0, 1/65536, 1234/65536 and 65535/65536. Fractional
sample times round up to the next audio sample; at the last offset this can reach
the next row boundary. The first 128 frames of each delayed waveform are
identical to the zero-offset waveform. Reopening the saved half-row delay gives
byte-identical exported audio.

The existing precise-note suite also checks sample voices and local AU/VST3
instrument fixtures at 44.1, 48 and 96 kHz, callback sizes 1/17/128/4096, repeats,
tempo/groove, timestamped recording and zero callback allocations/frees/locks.
The extended suite passed.

Offscreen UI checks passed direct Apply, selected-event identity after sorting,
invalid fields, event deletion, ordinary volume bytes 0–255, plugin numeric
boundaries and waveform navigation boundaries. The revised dialog image was
inspected. Some system AppKit button surfaces appear blank in never-presented
window captures; their titles, actions and geometry are checked separately.

## Agent interface

The dialog continues to use `pattern.notes.get` / `pattern.notes.set`, with
revision checks, dry-run validation and one document Undo step. No API or file
format migration is needed. Positions are absolute within the pattern in 65,536
units per row. Halfway through row 3 is position 229376; use
`clearRows: [{"row": 3, "channel": 0}]` to replace the ordinary onset while
preserving its effects, and retain the other returned precise events.

## Delivery and qualification

The complete background suite passed: **59/59 CTests**, plugin-picker and recovery
store checks, the full socket API suite against both hosts, real native app
startup with a 127-channel song, the actual autosave timer followed by forced
termination/relaunch/recovery, and offscreen interface/Metal checks. Tests did
not take desktop control, play hardware audio, or load commercial plugins.

- App: [Resonance.app](../bin/mac-checkpoints/2026-09-20-note-offset-audit/Resonance.app)
- Evidence: [qualification directory](mac-native-qualification/2026-09-20-note-offset-audit/)
- Binary SHA-256: `04eec0b21e11ff3eb015e781aca7c2cbe8a4cdafa8728c77d932ca62f056271e`
- Source fingerprint: `994883c777c0271fb18290f06bdedbe97778609931aa2ee207b905be3b206858`

All 392 source hashes match the packaged manifest. Strict recursive code-signing
verification passed. Earlier delivered native/stable apps, the crash-recovery
checkpoint, and the recovered user song retain their previous hashes.

Production changes are in `mac/App/PreciseNotes.swift`, `PluginEditor.swift` and
`WaveformView.swift`. Regression coverage is in
`mac/Tests/PreciseNotesInterfaceTests.swift`, `InterfaceTests.swift` and
`PreciseNoteTests.mm`; usage and agent timing units are documented in
`mac/README.md`.
