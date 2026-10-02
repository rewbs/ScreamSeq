# Integrated graph completion — native journey ledger

This ledger is pending the final integrated executable. Automated AppKit checks
are recorded separately in GRAPH_ACTIONS.md. No pending row below constitutes a
native UI pass. Windows desktop/device walks are deferred at the user's request;
native Windows CI is still required. The FPS gate is excluded from this goal.

Count each click, double-click, drag, shortcut and committed numeric entry as an
action. Count panel/graph-depth switches separately, and separate redundant
confirmation buttons from Return that completes text entry. Fixture setup through
the API is documented separately and does not stand in for walking the journey.
Historical counts come from the linked paused checkpoint, not reconstructed guesses.

| Journey | Historical successful actions / switches / confirms | Final native path, count and screenshot |
|---|---|---|
| Add an effect to channel 3, tweak and compare bypass | Add only: 3 / 0 / 0; whole path not comparable | Pending |
| Insert EQ into a selected cable during playback | 4 / 0 / 0 | Pending |
| Connect channel 1 to compressor Main and channel 2 to Sidechain | Detector connection only: 1 / 0 / 0 | Pending |
| LFO to parameter, set range, mute source and return | LFO setup: 9 / 0 / 0; mute not measured | Pending |
| Create a quiet send/return and edit its gain | 5 / 0 / 0 | Pending |
| Diagnose a silent path and an overloaded path with exact scope/Listen | Mute: 3 / 0 / 0; overload: 10 / 0 / 0 | Pending |
| Inspect independent reusable processor copies and their control sources | No comparable baseline | Pending |
| Group, name and export a chain; bypass its boundary | Export: 6 / 1 / 0; bypass not measured | Pending |
| Duplicate/copy/paste a recipe selection and make one use independent | No comparable baseline | Pending |
| Save a processor preset and reuse it on another channel | No comparable baseline | Pending |
| Edit an instrument graph and return to the pattern context | 9 / 2 / 0 | Pending |
| Repatch, Undo/Redo, detach a branched group and cut selected cables | Repatch/history only: 3 / 0 / 0 | Pending |
| Patch Notes from keyboard and pointer; map MIDI channel and inspect activity | No comparable baseline | Pending |
| Add/remove a note destination while held notes sound; Undo waits for next note | No comparable baseline | Pending |
| Edit recorded automation during playback and follow its parameter provenance | Previously stopped-only | Pending |
| Route every channel-pair slice of a multichannel plugin without hidden truncation | Previously unsupported | Pending |
| Regression: write/audition melody, change instrument, use global transport and navigate back | Broader September journey report | Pending |

The final walkthrough records the exact build/source fingerprint, private fixture,
explicit device, transport state, action sequence, visible feedback, screenshot,
and any failure/retry. Capability retained in context menus or the command palette
must be exercised there at least once; a passing model/API test is insufficient.

## Native rewalk in progress — checkpoint 0e55851e1

Private bundle `ScreamSeq Graph Completion QA`, PID 33092, launched with
`--inspection --automation`. Device explicitly selected through Audio settings:
BlackHole 2ch at 48 kHz / 512 frames; system default was not changed. Fixture is
the disposable Midnight Circuit demo. This is an interim checkpoint; remaining
runtime edits are not covered by it.

- Show Graph through Cmd-K (search `graph`, Return); select Track 3 in its filter.
- Add compressor: Add, type `compressor`, Return — **3 actions / 0 switches /
  0 redundant confirms**, after selecting the channel. The inspector opens inline.
- Edit threshold from -18 to -24 in place, then toggle bypass on/off. Including
  one Space playback start, the add/tweak/compare path is **7 actions / 0 switches /
  0 redundant confirms** under this ledger's numeric-entry convention. Separate
  setup is the 2-click channel filter and initial Graph panel opening.
- Native UI and API both confirm manual -24 and bypass changes. The song ended
  naturally during the first pass; loop was then enabled for subsequent live walks.
- Found a placement regression: an automatically inserted Compressor is right of
  the implicit Master, causing a backward output cable. This is being fixed while
  preserving explicit layouts. Screenshot `channel3-inline-comparison.png` is a
  failure record, not final presentation approval.
- Inline parameter `Effective` text is a cached host read. It remained -18 after
  the manual value changed although a fresh API read returns -24; its tooltip
  explains the snapshot, but the visible label is too easy to mistake for current
  data. Further inspection is required before completing diagnostics qualification.

QA evidence currently lives under the separate ignored build directory
`bin/mac-graph-fluency/qualification/copy-notes-1e9c1630/`; final accepted images
will be copied into this report's gallery after the affected paths are rechecked.

### Additional checkpoint walks (not final acceptance)

- Keyboard Main/Sidechain patching while looped verified routing and unified
  Cmd-Z / Cmd-Shift-Z without stopping. Search ranked a hidden node ID ahead
  of the visible Track 2 title; choosing the second result added one action.
  Title-first search ranking now has a passing copied AppKit regression.
- Threshold → Inspect opened Parameter activity with the correct parameter and
  live value. Its narrow dock truncated mode/value controls and lacked a clear
  return action. The new split rows and context-preserving Back to graph passed
  copied AppKit tests; the rebuilt native presentation is still pending.
- Save plugin preset from Track 3 succeeded through More → Save → the native
  save panel. Added a second compressor on Track 4 in three actions.
- Reusing that preset during playback correctly retained the old audio but
  rejected the edit as stopped-only. This is an unfinished live workflow, not
  a successful preset journey. Compatible rack preset replacement is underway.
- An earlier preset-path error was an automation-tool targeting error: clicking
  the AX item selected a different visible file. Choosing the exact preset via
  the native Go to Folder field reached the genuine stopped-only rejection.
  No file-path application defect was established. Pointer qualification remains
  pending a fresh UI-control mapping; keyboard actions were reliable.
