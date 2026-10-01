# ScreamSeq editing workflow — 22 September 2026

This checkpoint implements the requested macOS UI and editing changes. The
musician's existing app process and bundle were preserved; live checks used
separate, uniquely identified QA bundles and disposable demo songs.

- Primary actions use a subdued teal fill and light text. Selected inspector
  tabs use a slate/blue treatment and underline. Inspector shortcut labels
  moved into hover tooltips; accessibility descriptions retain them.
- Return on a pattern instrument cell makes that instrument active for new
  notes. Option–Up/Down selects the previous/next populated instrument or sample.
  Keypad divide/multiply follows OpenMPT's octave convention; Option–Left/Right
  provides a laptop alternative. The pattern footer shows active instrument,
  name and octave. Menus and context menus expose the actions.
- Header BPM supports direct fractional editing, Return/focus-loss commit,
  Escape cancellation, validation, revision checks and document Undo.
- The bottom status bar shows the full effect description or contextual
  instrument help independently of transport/status messages.
- Command-X works through the responder chain. Cut writes the clipboard first,
  then checks the captured song revision before clearing. Precise events,
  extra FX and stable parameter bindings travel with copied cells. Clear and
  paste preserve unrelated channels, use one Undo transaction and support the
  same data through the macOS and Windows API adapters.
- Precise-note edits save automatically after a 180 ms typing pause or a
  completed timeline gesture. Newer edits survive an in-flight reply; rejected
  edits remain visible. The hit table directly edits beat/row fractions, notes,
  instruments, velocities, supported per-hit FX codes and hexadecimal values.
  Single-click selects a cell for typing; note cells use musical keys. Return
  or double-click starts text editing. Tab/arrows navigate. Additional controls
  remain in the collapsed Selected hit details section. There is no Apply button.
- Beat and row representations update together. Explicit Undo/Redo refreshes a
  clean precise-note inspector at the same target and retains keyboard focus.

| Action in the pattern editor | Shortcut |
| --- | --- |
| Use instrument under cursor | Return on instrument cell |
| Previous / next instrument | Option–Up / Down |
| Lower / raise octave | Keypad ÷ / ×, or Option–Left / Right |
| Cut selection | Command-X |
| Open precise notes | Command-Shift-N, or Return on note |

The agent API gained guarded `workspace.input` for active instrument/octave.
`pattern.paste` accepts relative precise `notes`; the schema and public guide
are updated. Musical changes continue to use existing guarded APIs, Undo and
native persistence. Input context itself is UI state and does not change song
history.

Design research: the [macOS design skill](https://skills.sh/ehmo/platform-design-skills/macos-design-guidelines)
and Apple's [button](https://developer.apple.com/design/human-interface-guidelines/buttons)
and [colour](https://developer.apple.com/design/human-interface-guidelines/color)
guidance informed the distinction between actions and selected state.
OpenMPT keyboard conventions were checked against `mptrack/DefaultKeyBindings.h`.

Verification:

- All 75 CTest checks passed, including existing offline audio/plugin tests and
  the new precise clipboard preparation/Undo regression.
- AppKit interface suite passed: automatic-save races, invalid offsets,
  new keyboard shortcuts, clipboard contents, tab tooltips and existing editors.
- Actual app/socket tests passed for guarded input, malformed/stale rejection,
  precise cut/paste, no-op/dry-run behavior, Undo/Redo, native save/reopen,
  fractional BPM and existing workspace behavior.
- Live native UI inspection verified the button treatments, BPM entry and
  cancellation, precise timing and musical typing, companion-value updates,
  instrument/octave shortcuts, and Cut/Undo. Undo focus was checked on the final
  build.
- Timing-sensitive interface tests now wait for their expected asynchronous
  requests with a bounded timeout, rather than assuming a fixed delay.

This is a macOS UI change. The shared clipboard semantics and Windows API adapter
were updated, but the Windows application was not built on this Mac. Existing
precise-note writes retain their transport-stopping behavior; this work does not
introduce live audio-thread mutation. No new sustained frame-presentation or
physical audio-device benchmark is claimed by these UI checks.

Build fingerprint: `a7f3c00ba0babff4cc467d2d4fab1ba10c25bf3220d4e9ea60029cde0e616756`.
Verification logs and executable identity: [qualification evidence](mac-native-qualification/2026-09-22-editing-workflow/artifacts.json).
