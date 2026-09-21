# Pattern clipboard and native precise notes — 2026-09-21

Full Mac parity remains active. This checkpoint follows `0b140c5e4`; the fetched
upstream default `codex/screamseq` remains at `bcfe0f8a7` (there is no `main`).
The executable/source fingerprint is in the preserved package manifest under
`bin/windows-checkpoints/pattern-notes-20260921/`.

## Implemented

- `pattern.paste` uses the shared ordinary-cell paste rules plus unified native
  FX and stable binding remapping. Overwrite, merge, mix, logical field masks,
  explicit clipping, column expansion, dry run and one document Undo are exposed
  through the application. Precommit validation checks the candidate metadata,
  cells and immutable view budget. Preview is bounded to 512 changed cells.
- Native Copy/Paste uses the Mac `ScreamSeq Pattern 2` Unicode text payload.
  Relative FX coordinates and referenced binding identities survive transfer;
  existing bindings retain their names, unresolved targets remain explicit, and
  copied bindings receive a free ID when needed. The older `Resonance Pattern 1`
  hex text format is accepted. Serialization/parsing runs on background tasks
  using immutable captured inputs, with 16 MiB / 262144-cell bounds. Inspection
  retains private clipboard text without touching the desktop clipboard.
- The native Notes action opens a retained row draft. The dock provides a virtual
  hit list, pitch/instrument/velocity, row or beat offset, note-local effects,
  off/cut, duplicate/remove, retrigger count and ending velocity, and a timing/
  velocity canvas. Free or beat-aligned snap, drag cancellation, keyboard nudges,
  Shift velocity protection and Alt free movement are implemented. Dense rows
  draw 256 cached density bins plus the selected hit rather than every event.
- Check validates without committing or auditioning. Apply merges untouched
  pattern events and saves one transaction. Moving an ordinary note into the
  precise row moves its whitelisted local effect only when appropriate. Stale
  drafts remain visible; Use target explicitly captures the inspector's pinned/
  follow target. Async completions preserve newer fields and navigation focus.
- The grid displays precise-note markers and a first-hit pitch in otherwise empty
  note cells. Sparse immutable lookups use binary search. Delete on a note with
  precise events clears that row's ordinary/precise notes together and retains
  FX and unrelated events.

Dock geometry uses scalar bounds without copying workspace JSON. Canvas markers
share one geometry snapshot; up to 512 hits are drawn individually, and denser
rows use the cached 256-bin representation. No new DSP or audio-thread work is
introduced by these display changes.

The new UI tests found a lifetime error in the previous note-local catalogue:
range iteration used `.at()` on a temporary JSON owner. Its lifetime is now
explicit and tests require a nonempty whitelist including the no-effect entry.
Earlier catalogue assertions could pass vacuously on an empty array; the older
checkpoint's API report was insufficient evidence for this specific behavior.

## Qualification

Release ARM64 build: `bin/windows-parity/Release/ScreamSeq.exe`, built with
`windows/build.ps1 -Architecture ARM64 -BuildDirectory bin/windows-parity
-Target ScreamSeq,document-controller-tests -Jobs 3`.

Executable SHA-256:
`9C52FEA2342BB06E4466B2E40D8405802305A4EFF24022A64E4201D028DB930C`.

Final suite: **85 discovered, 84 passed, one opt-in silent-hardware test skipped**
in 82.738 seconds. All **29 CTests passed** in 16.96 seconds. The final build log
contains no compiler warnings or errors. The clipboard worker checks passed.

The current actual-app suite exercises native controls in separate, never-
switched desktops and uses exact-PID pipes. The plugin paths include the privately
installed Contourtonist and OrbitCab effects, Surge XT instrument, and provider
fixtures. The original desktop clipboard sequence and foreground are preserved.

New coverage includes masked/merge/mix paste, clipping in both dimensions, all
FX columns, stable/unresolved binding remap, late invalid-cell atomic rejection,
bounded large previews, no-op/dry-run, Undo/Redo and native save/reopen. Native
note tests cover local-effect transfer, retriggers, stale/pinned drafts across
rack/navigation, invalid offsets and collisions, note-off canonical values,
canvas drag cancellation and keyboard editing, 5000-hit list selection, grid
clear conservation, and beat conversion/snap with a six-row signature.

The separate worker's `--pattern-clipboard` checks exact text headers/fields,
LF/CRLF, legacy hex and special notes, invalid/oversized text, sparse-note lookup
boundaries, immutable old views and native reopen. It passed. The previous
shared FX/precise renderer evidence remains in `PATTERN_FX_PROGRESS.md`; this
change does not modify DSP or realtime publication.

Evidence in `bin/`:

- `windows-pattern-notes-qualified-build.log`
- `windows-pattern-notes-qualified-tests.log`
- `windows-pattern-notes-ctests.log`
- `windows-pattern-clipboard-worker.log`
- Earlier failing/rerun logs: `windows-precise-editor-tests*.log` and
  `windows-pattern-clipboard-tests.log`. The latter contained a test indexing
  assumption of four demo channels; the actual demo has eight.

These are functional native-control tests. Current desktop capture access has
not yielded a valid foreground image, so this checkpoint makes no claim of
visual approval, accessibility completion or sustained presentation performance.
No musician process, project, audio defaults or existing plugin bundle was
replaced. Installed vendor binary hashes remain unchanged from their recorded
qualification checkpoints.

## Remaining

Native structural/precise-pattern edits still stop playback after validation.
Broader pattern tools/completion, graph/mixer/envelope integration, recording,
workspace persistence/floating/accessibility, fresh reciprocal Mac reopen, x64
and long-session performance/realtime audits remain in `PARITY_PLAN.md`.

Plugin lifecycle findings remain open: OrbitCab fails the strict callback-
partition PCM comparison; Surge XT's first editor initialization changes opaque
zoom state and currently stops playback. The installed plugins' qualified paths
and constraints are in `UPSTREAM_PLUGIN_QUALIFICATION.md`,
`LIVE_PLUGIN_PARAMETERS.md` and `TRIGGER_INSTRUMENT_PROGRESS.md`. Those limitations
have not been waived by the new pattern tests.
