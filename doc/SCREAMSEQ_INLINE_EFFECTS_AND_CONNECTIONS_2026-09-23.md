# In-pattern scratch values and graph connections — 23 September 2026

## Changes

- NF and NR open two numeric fields on the pattern row. Strength is percent;
  duration is rows, including recovery. Tab / Shift-Tab switches fields, Return
  saves both and advances, Escape cancels. Existing values can be edited with
  Return, double-click, or by typing in the value field. The grid displays both
  values (`62.5/0.375`). All eight FX columns use the same wider layout.
- Entries retain their captured cell/revision and preserve sub-row onset. Invalid
  numbers and out-of-pattern durations do not dispatch. Rejected edits retain
  the draft. The existing single-cell API provides atomic changes, Undo and
  project persistence; no format or protocol change is required.
- Selecting a graph wire or its label shows a dedicated connection inspector
  above node controls. Double-click / Return focuses settings; right-click offers
  Edit connection and Remove connection; Delete removes the selected route.
  Update connection saves; New… creates a separate connection.
- Fixed chain-order wires explain their ownership and offer a direct link to the
  subgraph or mixer. Locked owning endpoints are disabled. Changing the source of
  an existing main-output route can no longer silently reroute a different bus.
- Connection help wraps, gain units are explicit, and selection survives a reload
  only when its endpoints and ports still exist. Graph refresh also works when
  the panel has keyboard focus, while text and curve drafts remain protected.

## Verification

- 76/76 CTest tests passed (69.27 seconds).
- AppKit interface suite passed: exact fractional values and onset, keyboard
  switching, cancellation, stale and invalid edits, eight columns, accessibility,
  selected connection layout, Return/double-click/label targeting, graph ownership,
  unrelated audio-edge preservation and source ownership validation.
- The first live QA build saved NR strength 62.5%, duration 0.375 rows through
  actual keyboard input. Socket readback confirmed value 0.625 and duration 24576.
  Double-clicking a modulation wire focused its minimum; editing it to 0.25
  preserved the two existing audio routes.

The audio renderer and shared musical model are unchanged by this UI work.
No new physical-audio, Windows UI, or sustained display-performance claim is made.
The musician's running app and system audio route were left alone.

Final package qualification repeated inline entry/reopen/cancel with accessible
fields, opened a fixed wire's subgraph, edited the modulation minimum to 0.3,
deleted it with Backspace, and restored it with Cmd-Z. Socket readback confirmed
0.3 and both untouched audio connections. The real-app scratch API suite passed
as well (discovery, exact cell writes, dry-run/no-op, Undo/Redo, eight columns,
clipboard and unrelated-cell preservation). The packaged source fingerprint
matches the current sources. See `mac-native-qualification/2026-09-23-inline-effects/`
for logs, exact readback, build fingerprint and executable hash.

Build: `bin/mac-2026-09-23/ScreamSeq.app`. It has a separate development bundle
identity so it can be launched without replacing the musician's existing process.
For a cramped graph dock, use Panel → Float, then Fit.
