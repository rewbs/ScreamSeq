# Windows unified pattern FX checkpoint

This continues current Mac format-17 parity work. Upstream remains `bcfe0f8a7`
on `origin/codex/screamseq`; the remote has no `main` branch and the latest fetch
added no commits. Full parity remains in progress.

## Implemented

- `pattern.effects.get/set`, `pattern.performance.get/set` and
  `pattern.effect.set` share the Mac 1–8-column representation. FX 1 tracker
  bytes remain in ordinary cells; other tracker and precise commands use native
  metadata. Single-cell edits avoid pattern-sized temporary JSON per keypress.
- Binding validation uses actual plugin identities, writable parameter catalogs
  and absolute/native automation conflicts. Unchanged unresolved commands survive
  unrelated edits. Whole batches validate before playback stops or music changes.
- Current `pattern.notes.get/set` includes row/beat offsets, note-local effect
  catalogs, explicit ordinary-cell clearing, no-op ordering, Undo and persistence.
  The precise-note event editor UI remains pending.
- `pattern.transform` uses shared portable preparation for selection, channel,
  stable note-track, pattern and song scopes. Row transforms move all FX columns
  together. Precise notes remain independent, matching Mac. The command palette
  exposes reverse, rotate, expand, shrink, row insertion/deletion and semitone
  transposition. Insert is also a grid shortcut; lossy expand/shrink rejects.
- The virtualized grid has variable channel widths, separate two-character
  code/value fields, FX-count selection, navigation through FX 8, cursor scrolling,
  horizontal wheel/Shift-wheel input and clipped partial channels. Sparse immutable
  FX/note caches are reused across unrelated edits and charged before mutation.
- Enter on an FX cell opens a nonmodal inspector with command search, values,
  precise timing, slide duration, pitch range and parameter bindings. It captures
  its target and revision. Navigation preserves the draft; stale Apply rejects
  without discarding text. Use cursor explicitly reloads. A parameter selected in
  the plugin rack can become a new binding in the same Undo step as its command.
- Delete on an FX field preserves the note, instrument, volume and other columns.
  Selection clear uses the shared row transform.

## Qualification

ARM64 Release executable SHA-256:
`9EC5CAC1BA24ED3FCBC32FA40C8097FD6DED031F1F6F3D2377115CBFAF07F298`.

Seven new actual-app tests pass on owned, never-switched private desktops. They
cover native FX-count/input/search/binding controls, captured targets, stale
rejection, unresolved bindings, no-op/dry-run, whole-batch rejection, Undo/Redo,
precise notes, row transforms and save/reopen. These are functional native-control
tests, not screenshot or sustained presentation evidence.

A separate visible-inspection attempt on the same executable failed activation
with `GetCursorPos failed: Access is denied. (0x80070005)`. Refreshing the window
once returned the expected native accessibility tree (including 8 FX and Pattern
FX controls) but only a desktop-background image. No visual approval is inferred;
the owned inspection process was closed without editing its project.

The complete app suite discovers 74 tests: 73 pass and the opt-in live hardware
test skips (71.904 seconds). Installed effect, Surge XT and factory-program/port
fixture checks are enabled in that run. All 29 CTests pass (19.47 seconds).

The integrated worker API moves source commands from FX 1 to FX 8/4. One-second
renders at 44100, 48000 and 96000 Hz with blocks of 17, 128, 4096 and 8193 frames
are finite and audible, with exactly zero maximum PCM difference from FX 1.
The worker also passes immutable cache conservation/reuse, history/reopen and
precommit cache-budget rejection for 50,000 valid precise events. An initial
25,000-event fixture fit the configured budget; it was enlarged to exercise the
intended rejection. The original failure log is retained.

Evidence under `bin/`: `windows-pattern-final-build.log`,
`windows-pattern-final-tests.log`, `windows-pattern-worker.log`,
`windows-pattern-regressions-final.log`, `windows-pattern-core-build.log` and
`windows-pattern-core-regressions-final.log`. The initial broad UI run found four
tests using the old fixed channel width; their mouse coordinates and blank-area
expectations were updated for partial-channel rendering. The initial CTest run
found unbuilt optional executables; the final run follows their explicit build.

## Remaining

Pattern 2 clipboard support, including binding remapping, is pending. The old
cell clipboard rejects source or destination selections containing native FX,
avoiding silent partial copies/pastes. Precise-note UI, broader transform controls,
first-letter completion and visible layout/DPI/accessibility qualification remain.
Native/structural pattern edits currently stop playback after validation.
No Mac reopen, x64 or new long-session/realtime-allocation claim is made here.

Broader graph/mixer/envelope, recording, workspace and plugin limitations remain
in `PARITY_PLAN.md`. Installed-plugin qualification keeps its existing findings
and thresholds, including OrbitCab partition error and Surge first-editor stop.
