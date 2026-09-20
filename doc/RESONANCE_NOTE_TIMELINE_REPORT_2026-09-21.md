# Precise-note timeline delivery — 21 September 2026

The Note inspector now acts as a small timing editor for one pattern row and channel. Open it with Return on a precise-note cell, by double-clicking the cell, or from Pattern → Precise Notes.

## Completed

- Beat or row offsets, including fraction input such as `1/8`, with the conversion displayed alongside. The beat grid respects per-pattern signatures.
- A containing-beat strip and an expanded row timeline. Horizontal dragging edits timing; vertical dragging edits volume. Timing is constrained to the row, at the existing 1/65536-row storage precision.
- Beat snapping, free positioning, keyboard nudges, Shift to preserve volume and Option to bypass snapping.
- Independent note occurrences with note, instrument, volume and one ordinary tracker effect each. Add, duplicate, remove, or double-click to insert a hit.
- Retrigger generation: 2–64 evenly spaced occurrences from the selected hit through the remaining row, with an optional linear volume ramp. Generated hits remain individually editable.
- Explicit Apply, validation without changing the song, retained selection after Apply, and one document Undo step for the edit.
- Audio scheduling of each hit and its immediate effect at the precise onset. Continuing effects hand over to the next hit without reprocessing other channels.
- Agent API support for beat-relative coordinates, per-hit effects, exact effect-parameter catalogs, revision checks, dry runs, Undo/Redo and project persistence.

## Verification

- Full native regression suite: **70/70 passed**.
- After the final effect-handoff refinement: precise-notes, precise-ramps and playback-regions tests passed again.
- Native interface suite passed after the final changes, including actual AppKit mouse handlers, row-boundary clamping, Shift-drag volume preservation, beat conversion, retrigger generation and selection retention.
- Application socket API tests passed, including beat-coordinate validation and per-hit effects.
- Offline rendered-audio checks cover multiple onsets with independent volume, panning and sample offset, buffer-size independence, ordinary-tick effect progression, vibrato handoff and the audio-callback allocation audit.
- Live UI inspection verified fraction input, retrigger generation, per-hit effect editing, Apply selection retention, timing/volume dragging and dragging beyond the row boundary. The resulting position stopped at 65535/65536 of the row.
- The live UI edit was read back through the API, undone, redone, saved, and reopened in a fresh application process with all four events and their effects intact.
- The app displayed approximately 60 fps during inspection. This was not a new sustained performance qualification or physical loopback audio test.

UI testing used a separate inspection instance and a disposable example song. The user's existing running app and unsaved song were left intact.

## Scope and compatibility

Each hit has one ordinary tracker effect slot in addition to volume. Native parameter-slide, pitch-slide and graph-command lanes remain in their existing editors. Immediate effects start at the precise onset; ongoing tracker effects retain the ordinary tracker tick clock.

Apply stops playback to prepare the changed schedule safely. Row offsets measure musical beats from the row start, not seconds. A hit cannot occupy the exact row end because that position belongs to the next row.

Projects using per-hit effects require native metadata version 12 and cannot be opened by older Resonance builds. Existing precise notes without effects keep their previous representation.

See `mac/PRECISE_NOTES.md` for controls, effect semantics and API examples. The delivery checkpoint also contains this guide, the test logs and `Beat-Retriggers.resonance`.
