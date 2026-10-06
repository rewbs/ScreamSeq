# Scratch phrases

ScreamSeq's scratch arrangement has two layers: compact **SK** events in the pattern, and a song-local library of paired **Motion** and **Fader** envelopes. **NF/NR** remain useful for a quick push; SK gives repeatable, deliberately composed strokes and cuts.

## Design basis

The closest precedent is [Image-Line Wave Traveller](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/Wave%20Traveller.htm): a sample playback path and a separate volume curve. A position path makes the distance travelled independent of how quickly the phrase is performed. [Gross Beat](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/Gross%20Beat.htm) demonstrates sequencing paired motion/volume shapes and the need for click reduction and DC handling. Its rolling audio buffer is a different model from manipulating an already available sample.

[Cableguys ShaperBox](https://downloads.cableguys.com/Cableguys-ShaperBox-3-Manual.pdf) contributes reusable shapes, synchronized timing and direct curve editing as useful interaction precedents. [Turntablist Transcription Method](https://www.ttm-dj.com/ttm-chp-3-advance-scratches/) makes the distinction between record movement and fader clicks explicit. These inspired the representation; ScreamSeq's starter curves are original constructions, not copies of commercial preset data or claims to capture every performance nuance of a named technique.

The ScreamSeq interpretation keeps the arrangement visible in the tracker. Numbers in a cell answer **which phrase, how long, how far, how many cycles**. The non-modal phrase editor explains the resulting motion and cuts without taking away pattern navigation. Song-local references share edits; **Make unique** creates an independent variation. Immediate reversible editing, explicit context and one chronological Undo history follow the application's existing UI philosophy.

## Write a scratch arrangement

1. Put a sample or sample-instrument note in a pattern channel.
2. Open **Scratch phrases…** from the Pattern menu, context menu or command palette. The editor captures that FX cell. Choosing SK in an empty-bank song also opens it.
3. Choose **New phrase… → Baby**, Chirp, Transform, One-click flare, Two-click flare, Crab or Scribble, then **Use in pattern**.
4. Edit the SK parameter slots directly in the pattern: phrase number, total duration (beats by default; rows are available), source travel in milliseconds, and repeat count. Reverse is in the expanded parameters.
5. Double-click SK to edit its paired curves. Motion rises to move forward, falls to move back, and stays flat to hold position. The fader controls audibility independently; motion continues through closed sections.
6. Draw or drag points, select a curve type, zoom, or enter exact point values. Scripted segments have completion, the expandable formula workbench and the shared reference. Both plots use the same cycle zoom.
7. Copy SK cells to arrange combinations. Reusing the same phrase links its curves; **Make unique at captured row** duplicates them. **SX** ends a scratch early. **Return to row** takes you back to the captured event.

For example, a Baby phrase over two beats with four repeats produces four forward/back strokes across those two beats. Replacing only its fader curve can turn the same motion into a transform rhythm. Increasing travel makes the stylus traverse more sample material in the same time; shortening duration makes that travel faster. Place SO before SK when you want to start from another cue or need room for an initially backward stroke.

## Musical semantics

- The active main **sample voice** owns the phrase. A new note cancels its ownership; other channels, plugin instruments, preview notes and NNA tails do not inherit it.
- The cue is the sample's actual position at SK. Travel is measured at the voice's captured normal pitch. The curve is relative to its initial value.
- Closed curves return to the same cue each cycle. Open curves accumulate their end-minus-start travel at each repeat, avoiding a forced jump back at cycle boundaries.
- Duration follows the musical beat clock, including tempo and row-length changes. Repeats divide the total duration. Pattern boundaries end activity; strict edits require duration to fit the remaining pattern.
- During SK, motion traverses the physical sample rather than wrapping its configured loops. It clamps and holds at sample ends. Normal sample-loop behavior resumes afterward.
- The fader multiplies existing note gain and envelopes. It is smoothed over up to 0.5 ms. At the end or SX, normal playback resumes from the final position with the fader reopening.
- A 20 Hz per-voice DC blocker with a short transition prevents a stationary sample position from producing a constant output signal.
- Editing a shared phrase during playback keeps its elapsed time and captured cue. The prepared curves take over at an audio boundary, with a short transition for a changed trajectory. Adding/changing pattern events follows the existing pattern-effect editing path.
- There are up to 255 phrases per song, 256 points per lane, and 16 scratch tracks. Coordinates use 65,536 subdivisions per cycle. Formulas are compiled before publication; the callback neither parses scripts nor reads a mutable catalogue.

In scripted curves, `t` is the segment's normalized progress; `beat` is elapsed time within the cycle; `duration` is the current segment's duration in beats. Cycle duration is total SK beats divided by repeats. The shared formula variable `row` follows the editor's synthetic 256-row cycle coordinate, not the song's pattern row.

The paired phrase library is song-local in this checkpoint. There is no automatic link to an app-wide catalogue. A copied pattern selection carries its used phrase definitions and remaps them safely if another song already uses those numbers. Native files retain the curves, formulas and references; best-effort recovery warns when damaged phrases or dependent commands are omitted.

## Agent access

See [the scratch API contract](../mac/AUTOMATION.md#scratch-phrases). An agent can list/create/edit/clone phrases, author both envelopes and their formulas, place SK/SX commands and preview curves without controlling the UI. Writes are revision-guarded, atomic, reversible and support dry runs. Removing a referenced phrase is rejected.

## Deliberate boundaries

This is sample scratching, not a rolling-buffer effect on a plugin instrument or bus. Such an effect would need explicit latency, lookback and startup behavior. Motion uses captured pitch so travel remains predictable; it is not a promise to combine arbitrary later pitch modulation with an exact recorded path. Hardware jog-wheel/fader capture, scratch-specific cross-song catalogue publishing and a separate Windows paired-curve editor are additional workflows rather than prerequisites for pattern arrangements.
