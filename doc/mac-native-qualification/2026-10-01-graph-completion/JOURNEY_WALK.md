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
