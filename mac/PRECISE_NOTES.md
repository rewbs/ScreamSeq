# Precise notes: a small editor inside a row

Open the Note inspector with Return on a precise-note cell, double-click the cell, or use Pattern → Precise Notes. The inspector follows the pattern cursor unless pinned; drafts stay on their original row while you work elsewhere.

The upper strip shows the containing beat and highlights the selected row. The larger timeline underneath expands that one row. Each dot is a note occurrence; its horizontal position is timing, its height is volume/velocity. The table shows beat and row offsets side by side.

## Timing and dragging

**Offset** accepts decimal numbers or fractions such as `1/8`. Choose Beats or Rows beside it. Both measure from the start of the selected row. With four rows per beat, a row is 1/4 beat long, so `1/8 beat` means halfway through the row. Pattern-specific beat signatures are respected. Tempo and groove affect elapsed seconds, not the displayed musical coordinates.

- Drag a hit horizontally to move it and vertically to change its volume. Hold Shift to preserve volume.
- Dragging stops at the row boundaries. The exact end belongs to the next row, so the last available position is just before it.
- Choose Free or a beat snap interval. Hold Option while dragging to bypass snap.
- Left/Right nudges time (the snap interval, or 1/256 row in Free mode). Option–Left/Right uses the smallest stored unit, 1/65536 row. Up/Down changes volume by one.
- Click a hit or table entry to edit its note, instrument, volume or effect. Volume uses 1–127 velocity units; a release/cut is an explicit event.
- Double-click empty timeline space to add a copy of the selected hit there. **Add hit** / Cmd–D duplicates it later in the row. Delete removes the selected event.

Changes stay in a draft until **Apply**. Typing offset/volume updates the timeline immediately when valid. Apply includes all visible fields; a separate Update click is unnecessary. Check edit validates without changing the song or playing audio. Applying stops playback to safely prepare the changed event schedule and creates one document Undo step.

## Retriggers

Select a note onset, enter **Retriggers** (2–64), choose an **End volume**, and click **Fill to row end**. The count includes the starting hit. Occurrences are spaced evenly through the remaining part of the row; their volumes interpolate linearly to the end volume. Set the end volume equal to the starting volume for an even burst.

Every resulting hit is independent: move it freely, change its pitch/instrument/volume, attach an effect, add a release, or remove it. Existing unrelated hits remain; overlapping onsets are rejected instead of silently overwritten. Different note pitches on the same raw channel use that instrument's normal new-note action, just like normal tracker notes.

## Effects on individual hits

Each onset has one ordinary tracker effect command and a hexadecimal parameter, in addition to its volume. The picker lists supported commands for the current song format, including sample offset, panning, volume/channel slides, portamento up/down, vibrato/tremolo/arpeggio, note-local extended commands and MIDI macros.

Instant effects execute at the hit's precise onset. Continuing tracker effects use the remaining ordinary ticks of that row; their clock is not restarted or stretched into a new row. The next hit replaces the previous hit's continuing effect, and choosing None ends it. Values such as pan, channel volume and effect memory retain normal tracker persistence. Ordinary row effects remain active until a hit supplies an overriding effect. Normal next-row processing is unchanged.

Tempo, song navigation, tone-portamento, implicit note delay/retrigger and destructive sample commands are kept in the main pattern. Use explicit hits/releases here for precise retrigger and gate timing. Native parameter/pitch-slide and graph-command lanes remain in their existing editors.

When an ordinary note is first converted, a supported ordinary effect moves with it. Apply clears the original onset/effect location to prevent a duplicate row-start trigger. Other row effects are retained at their original location. The replacement checkbox lets you keep the ordinary onset deliberately.

## Agent API

`pattern.notes.get` returns canonical integer positions, `rowsPerBeat`, and an `effects` catalog with exact `allowedParameters` for every command. `pattern.notes.set` replaces this pattern's precise events as one revision-guarded edit, supports `dryRun`, and accepts either:

- `position`: absolute pattern position in 1/65536 row units; or
- `row` and exactly one of `offsetRows` / `offsetBeats`.

Example event at half of row 8 when there are four rows per beat:

```json
{"channel":0,"row":8,"offsetBeats":0.125,"note":61,"instrument":2,"velocity":93,"effect":9,"parameter":255}
```

Effect numbers are native tracker command IDs, not the displayed format-specific letters; inspect the catalog. Position forms cannot be mixed. Offsets must be inside the specified row. Integer positions are returned after rounding to native precision. Releases use note 255 (off) or 254 (cut), instrument 0, velocity 127 and no effect.

`clearRows` or `clearLegacy` explicitly clears ordinary note/instrument/volume data at those cells. `clearRowEffects:true` also clears their ordinary effect/parameter when moving effects onto hits. Other channels/rows are untouched. Save/reopen and Undo/Redo preserve all hit data. Per-hit effects require native metadata version 12; earlier builds cannot open projects using them. Existing precise notes without per-hit effects retain their previous representation.
