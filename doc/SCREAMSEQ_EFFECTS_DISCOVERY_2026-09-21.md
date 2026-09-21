# Pattern effects and instrument discovery — 21 September 2026

This change makes existing musical capabilities easier to find and fixes ambiguous
instrument audition and assignment workflows. It includes the earlier exit-crash
fix; see `SCREAMSEQ_EXIT_CRASH_2026-09-21.md` for that separate investigation.

## Implemented

- Two-character display codes for ordinary/extended source-format effects and
  `PS`, `PL`, `BS`, `BL` for native parameter/pitch set and slide commands. Existing
  source-format command bytes and one-letter entry remain compatible.
- `?` opens a floating, searchable effect finder beside the cell. Search receives
  focus; Up/Down, Return and Escape support keyboard use. The catalogue includes
  the current format's effects and the four native commands.
- Channel-header **+FX** and a right-click count menu expose up to eight native
  effect columns. Creating the first native command enables its column.
- Native effect editing starts with named plugin and parameter choices, target
  percentage, row offset and duration. Target bindings retain stable plugin and
  parameter identities; changing a target does not retarget other commands.
  Double values are displayed with round-trip precision when reopening the editor.
- Right-clicking a plugin parameter offers immediate-set and slide commands at
  the pattern cursor, with that parameter already selected.
- Instrument preview explicitly uses the instrument keymap and enabled envelopes.
  The Enable envelope checkbox applies immediately. A sample-only song no longer
  silently auditions a raw sample from the instrument panel; it explains creating
  an instrument first. Sample-panel audition remains raw.
- **Assign instrument plugin…** in the instrument inspector selects an AU/VST3
  instrument and MIDI channel, or restores sample-keymap playback. Assignment
  changes preserve other instruments sharing those plugins and use one Undo.
- Pattern, inspector, graph and floating-editor context menus expose actions and
  existing shortcuts. Editable text retains its native editing menu.
- New revision-guarded `instrument.plugin.set` API with dry run, validation and
  atomic history. Command catalogue display codes and capabilities are additive;
  the public API guide/schema and socket tests are updated.

Usage: [Pattern effects and instrument sound sources](guides/PATTERN_EFFECTS_AND_INSTRUMENTS.md).

## Verification

- Native suite: 71/72 passed on the first full run. The new plugin-alias assertion
  compared a read-only `available` field against a write payload; correcting that
  test expectation and rerunning the affected test passed. All 72 tests therefore
  have passing results. Both original and follow-up logs are retained.
- Native AppKit interface suite passed, including finder keyboard handling,
  precise command values, column provisioning, immediate envelope enable and
  assignment layout/target capture.
- Full socket API suite passed, including same-owner no-op, dry run, reassignment,
  detach, stale revisions, missing plugins, preserved aliases and single Undo.
- Rendered audio regression passed at 44.1, 48 and 96 kHz with 128-frame blocks:
  an instrument's enabled volume envelope silences its sustained tail, while
  explicit raw-sample preview remains audible. The renderer already honored
  envelopes; the UI could leave Enable uncommitted or audition a sample where no
  actual instrument existed.
- In a separate app, visually checked instrument assignment and saved it; verified
  the immediate envelope toggle via API. Opened `?`, filtered `slide plugin`, used
  Return to open PL, and saved `12.34567891234567%` over half a row. API readback
  was `0.1234567891234567` with duration `32768` out of `65536` units per row.
  Added a second FX column using the context menu, checked Escape returns focus,
  and opened PS from the Gainer Gain parameter's right-click menu.
- Sustained display/Core Audio qualification was attempted twice with the final
  build, requesting 30 seconds, a VST3 fixture, graph workload and BlackHole 2ch.
  Both attempts stopped at the visibility gate: macOS reported the QA window
  inactive and occluded, including after CUA Raise/click actions. No measured
  workload began and no frames were presented. These are setup failures, not
  passing performance runs. No new 60fps or combined graph/audio performance claim
  is made. The normal isolated interaction instance did accept the edits above.

## Scope and remaining limits

Extra columns currently support native parameter and pitch commands. Additional
ordinary source-format effect columns and full Renoise command parity remain
separate engine work. Two-character aliases do not translate command semantics
between formats, and legacy byte-valued commands retain their original precision.
The native grid value is a compact rounded preview; the editor/API retain doubles.
Third-party plugins control their own response to sample-offset automation queues.

The user's existing app process, song and default audio routing were preserved.
The Mac temporarily locked during inspection; checks resumed after it was unlocked.
Only disposable QA instances and fixtures were changed.

## Build and evidence

Application: `bin/mac-checkpoints/2026-09-21-effects-discovery/ScreamSeq.app`.
Save and quit the older app before opening this checkpoint. The adjacent
`HOW_TO_USE.md` provides the workflow guide; `qualification/` contains build,
native, interface and API logs, interaction screenshots/readback, and both
performance setup failures. The packaged app's ad-hoc signature was verified.

- Build time: `2026-09-21T05:22:37.828599+00:00`.
- Base revision: `b6c62a57970e647e54158575dd69f893d63d454a` plus working changes.
- Source fingerprint: `4d0acb0235b1501ef443243b17d87b2d19fb22134be1a704e13dbf6fab34aece`.
- Packaged executable SHA-256: `4a363e7079cec67678f9a00965b0329c36209d647fa22d911993d43af723a32e`.
- All manifest source hashes matched the workspace after packaging.
