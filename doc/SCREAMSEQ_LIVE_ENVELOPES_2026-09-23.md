# Live loop and automation editing — 23 September 2026

Implemented in the macOS build at `bin/mac-live-envelopes/ScreamSeq.app`.

## Behavior

- Drag the gold handles at the top of the waveform to edit the normal loop; green handles at the bottom edit the sustain loop. Boundaries stay inside the sample and cannot cross. Click a handle and use arrows for one-frame changes, Shift-arrows for 64 frames, or Tab to cycle handles.
- Loop handles, numeric boundaries, mode switches, Use selection and Snap loop save automatically. The loop Apply/Preview buttons are removed. Changes reach active sample voices at the next audio buffer, including standalone audition. Rapid gestures coalesce at roughly 35 ms; each accepted API edit remains an Undo step.
- Switching automation plugins clears the previous catalogue and selection, then selects a valid parameter from the new plugin. Plugin and parameter identities remain stable through pending saves, queued switches and rack reordering. Controls cannot write to an obsolete selection while loading.
- Automation points, curves, formulas, enable state and numeric point fields save automatically. Newer gestures survive delayed replies; temporary busy responses retry, while revision conflicts remain visible and require an explicit reload. Formula preview success cannot hide a failed save.
- The automation canvas shows a dashed editing cursor and a solid gold playback cursor. Both follow the zoom/pan viewport; unrelated patterns and stopped playback do not show a misleading playhead.
- Lane edits, removal/disable, parameter-envelope bank application and automation-only Undo/Redo update playback without restarting transport. Disabled/removed lanes cancel pending events and ramps and restore the latest saved/manual parameter value. Plans are built on the control thread, adopted at buffer boundaries, and reclaimed off the audio thread.

The existing revision-checked sample/automation API, persistence and history remain in use. `transport.get.patternPosition` adds a fractional position in 1/256-row units. The API reference and capability description document the new live behavior.

## Verification

- All **76 CTest tests pass**; the full native AppKit interface suite and full socket/API suite pass. The socket suite also had an outdated compressor Undo assertion, now checking restoration of the current automatic-sidechain default.
- New rendered tests cover 44.1, 48 and 96 kHz, at 17-, 128- and 4096-frame buffers: new/changed/scripted/disabled/removed envelopes, several queued plans, stable destinations with reversed plugin rack order, active sample loop changes and uninterrupted transport. Audited update/render paths perform no callback allocation, deallocation or locks.
- A separate QA app (PID 74480) ran the demonstration song through the VST3 gain fixture and native EQ10 on **BlackHole 2ch, 48 kHz / 128 frames**. Live changes produced exact silence and then nonzero output, with continuing frame counts; automation Undo/Redo behaved likewise.
- On-screen checks exercised both loop handle types, keyboard nudging, plugin switching, a second plugin's distinct parameter IDs, automation point dragging, zoom, numeric edits without Set point, and both cursor overlays. Saved API data confirmed the intended plugin/parameter and loop geometry.
- Over **177 seconds / 66,396 callbacks**, the QA session reported **zero overruns**, no renderer fault and no plugin failure. Maximum callback time was 1,396.791 µs against a 2,666.667 µs buffer deadline. This is bounded fixture evidence, not qualification of every vendor plugin or physical DAC latency.
- The final packaged build was launched separately (PID 76772), reopened the saved test project, verified exact loop/envelope persistence and visibly refreshed the plugin parameter list. Strict bundle signature verification passed; all 486 source hashes match the final manifest.

Final source fingerprint: `cabdb4b2921d7015e47e2214af19c8ab92206632b29488404a65bedecc619bb6`. Executable SHA-256: `9e2d265fc4f398ab424c27daaa27ba0a90155aba03055e931f2d6186642181f5`. The live measurement build and final build manifests are recorded separately: subsequent changes strengthened stale-reply/error handling and were verified by the interface suite and final launch.

Screenshots, test logs and measurement data are in [the qualification directory](mac-native-qualification/2026-09-23-live-envelopes/). The disposable saved project is `bin/mac-live-envelopes/qualification/live-envelopes.screamseq`.

## Remaining boundaries

Loop Undo/Redo still stops playback while restoring sample history. General sample properties and destructive PCM operations retain their existing explicit application controls. Windows has the shared runtime implementation available, but its stop-on-edit bridge/UI behavior was not changed or qualified in this task. This task did not repeat sustained display-frame benchmarks; the screenshots and interaction checks do not establish a new 60 fps performance claim.

Both QA processes were closed. The musician's original PID 47040 (`bin/mac-graph-rewire/ScreamSeq.app`) was left running, with its song and audio route unchanged. Save and quit that session before opening the new build. No commit or push was requested or performed.
