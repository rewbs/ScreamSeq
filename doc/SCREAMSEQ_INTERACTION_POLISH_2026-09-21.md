# ScreamSeq interaction and playback-display update — 21 September 2026

The updated application is at `bin/mac-checkpoints/2026-09-21-interaction-polish/ScreamSeq.app`.
Save and quit the existing ScreamSeq before opening this checkpoint. The musician's
running process, project, recovery copies and system audio output were preserved.
All interactive edits used disposable QA instances and test songs.

## Delivered behavior

| Request | Implementation |
| --- | --- |
| Double-click recovery copies | Double-clicking a real table row invokes the protected restore workflow. Empty table space does not restore the previously selected entry. |
| Double-click graph plugins | AU/VST3 nodes open their custom interface. Built-ins expose parameter controls. Library recipes retain their explicit **Apply plugin settings** workflow. |
| More colour | Primary actions including **Add plugin…**, **Add built-in…**, **Add effect…**, connection creation, recovery and instrument creation/import use the mint accent. Selected inspector tabs are highlighted. |
| Inspector navigation | The drop-down is replaced by a scrollable tab strip. Control-Option 1–9 select Notes, Samples, Instruments, Plugins, Mixer, Graph, FX library, Graph lanes and Automation. Captions follow remapped shortcuts. The duplicate switcher above the pattern has been removed. Existing panels and drafts remain mounted. |
| Position ruler | Click **ROW** to cycle rows → beats → pattern time → song time. Time uses the engine's tempo/speed/flow calculation and the selected order occurrence, including repeated patterns. Unreachable/unarranged rows display a placeholder. |
| Delete graph wires | Clicking an editable audio/modulation connection and pressing Delete removes that route. This covers subgraph wires, main bus outputs, sends, sidechains, auxiliary routes and plugin instrument outputs. Undo restores the route. Deleting a main output preserves independent sends. Explicitly disconnecting a plugin main output no longer falls back to Master. |
| Playback cursors | Waveform and instrument-envelope editors show a separate cursor for each active native sample voice. Positions follow real sample loops and envelope ticks, including overlapping notes, NNA voices and inspector previews while song transport is stopped. Stopping playback clears the markers. |
| Two-character effects | Direct PS/PL/BS/BL entry opens the matching target editor; ordinary two-character aliases also work. The pending first character is visible, Escape cancels it, and moving away commits its legacy one-letter meaning. Native commands provision an FX column on Apply without overwriting the ordinary effect. Converting a set command to a slide supplies a positive default duration. |

## Agent interface and persistence

- `pattern.timeline.get {pattern, order?}` reports each row's beat, pattern seconds
  and absolute song seconds. These are first-visit times within that occurrence;
  skipped or unreachable positions are null.
- `workspace.ruler {mode}` changes the UI ruler; `workspace.get.positionMode`
  reports its state. Modes are `rows`, `beats`, `patternTime`, `songTime`.
- `transport.get` includes `audioActive` and `voicePositions`: channel, sample,
  instrument, note generation, actual sample frame and volume/pan/pitch envelope ticks.
- `mixer.bus.set` accepts `output:null` for an explicitly disconnected main output.
- `mixer.plugin.route` and its instrument alias accept `disconnected:true` with
  `target:null`. Without that flag, null retains its earlier reset-to-default meaning.
- Disconnected routes require native metadata 15. Older readers reject these
  songs explicitly. Projects without this new state retain their previous metadata
  version. Musical routing edits retain revision checks, dry runs, Undo and persistence.

The voice snapshot is bounded to 512 native voices, uses atomic storage and never
allocates, frees, locks or waits in the audio callback. The UI reads it separately.
Schema, API capability descriptions, architecture notes and the user guide were updated.

## Verification

- **74/74 native tests passed** after the final routing changes (58.53 seconds).
- **AppKit interface suite passed**, including prefix entry/cancellation, default
  slide duration, ruler modes, retained inspector selection, graph opening,
  exact modulation-wire deletion and instrument output disconnection.
- **Full application socket suite passed**, including the new API fields,
  strict inputs, stale revisions, preview/no-op behavior, Undo/Redo, route persistence
  and compatibility of the older null-target reset behavior.
- **30 stock OpenMPT audio comparisons passed** at 44.1, 48 and 96 kHz with zero
  PCM differences and zero intercepted host allocations, frees or locks.
- Targeted rendered tests at all three rates verify simultaneous voice positions,
  loops, advancing envelopes, marker retirement, silence from disconnected main
  routes, audible independent sends and suppression of plugin Master fallback.
- Native save/reopen preserves disconnected bus and plugin routes, including an
  unavailable plugin identity. Timeline tests cover in-pattern tempo changes and
  repeated order occurrences.
- **Live UI checks:** recovery double-click closed the browser and restored the
  disposable song; graph double-click opened the fixture VST3's own interface;
  clicked wire/Delete and Undo changed/restored routing; PS was typed, applied at
  37.5%, and read back through the API; PL selected the slide editor with a usable
  duration; ruler clicks cycled all four modes; tab shortcuts and accent styling
  were visible. Waveform and envelope screenshots show multiple live cursors, and
  preview telemetry shows three voices with the transport stopped.

The sustained display/audio result is recorded alongside this report in the
checkpoint's `evidence/ui-vst3-graph-60s.json`. It runs the eight-channel song,
sample-instrument/channel graphs, VST3 fixture, Apple AU, automation, edits,
Undo/Redo and saves through BlackHole at 48 kHz / 512 frames. Strict 60 fps
qualification remains open: the final, exact-source 60-second run averaged
47.94 presented fps, with 697 missed presentations and a 1.30-second maximum
presentation interval. Audio completed 5,625 callbacks with zero overruns; the
99.9th-percentile callback took 2.24 ms and the maximum took 2.294 ms, against
a 10.67 ms buffer budget. CPU/GPU frame preparation remained low (99th-percentile
0.149/2.469 ms), which does not excuse the visible presentation stalls. An earlier
clean run averaged 58.70 presented fps, with 75 missed presentations and a 50 ms
maximum interval. Results vary with the desktop conditions; the cause remains
unresolved, and thresholds were not relaxed.

## Boundaries and remaining work

- **Sustained presentation:** investigate the remaining missed display deadlines;
  this delivery does not claim a strict 60 fps pass. The measured result is limited
  to the test workload and this machine's desktop conditions.
- **Graph overview:** serial insert-order links and instrument/assigned-subgraph
  summary links are structural, rather than independent editable routes. Change
  those through insert/assignment controls or inside their subgraph. Their Delete
  operation still explains the constraint; arbitrary breaks inside an implicit
  insert stack are not implemented here.
- **Plugin-internal playback displays:** AU/VST3 instruments do not expose their
  private sample/envelope positions through the generic hosting contract. Native
  instrument/sample cursors do not attempt to guess those positions.
- **Time ruler:** shows first visits, not a continuously changing estimate for each
  iteration of a pattern loop. Unreachable rows remain visibly unspecified.
- **Hardware qualification:** offline comparisons and BlackHole tests do not
  establish speaker sound quality or physical DAC latency, nor commercial-plugin capacity.

## Build identity and evidence

- Base commit: `c486a0642c753d564df2d6b9efc4c66f57add441`.
- Source fingerprint: `4bbf4a30d28cd4be632db56ace4e5d2aa81525386cba461bf820019829ff9a69`.
- Packaged executable SHA-256: `67fb793eaba8fe342462342e3f6a585270f7fe1d07fa904ae48dc3e90850d6f2`.
- All 448 recorded source hashes matched the workspace when packaged.
- The production-identity checkpoint passed strict deep signature verification.
- `BUILD.json`, test logs, audio comparison results, live screenshots and telemetry
  are retained in `bin/mac-checkpoints/2026-09-21-interaction-polish/`.

This update is left in the working tree; no new commit or push was requested for it.
