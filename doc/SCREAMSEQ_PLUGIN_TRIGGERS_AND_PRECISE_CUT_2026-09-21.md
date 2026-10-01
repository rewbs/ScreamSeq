# Plugin trigger instruments and precise note cuts — 21 September 2026

This update adds a direct plugin-instrument creation workflow, fixes SC value
entry and adds the high-resolution NC command. It includes the preceding
interaction-polish changes. It does not unify the legacy and native effect columns.

## Using it

- **Plugins → select a synth → New trigger instrument…**, or **Instruments →
  New plugin instrument…**. Choose a loaded VST3/AU instrument and MIDI channel,
  name it, then choose **Create & assign instrument**. It creates an empty sample
  keymap, assigns the synth, selects its number for pattern entry and shows it in
  the inspector. Other synth assignments are retained.
- Type **SC**, then a hexadecimal tick digit in the ordinary effect column.
  Previously entering the digit shifted away the C subcommand; SC2 could become
  S32. The subcommand is now preserved.
- Type **NC**, or use **? → Precise note cut / plugin note-off**, choose the
  offset in rows or beats, then Apply. At four rows per beat, 0.125 beats is half
  a row; 0.03125 beats is one eighth of a row. NC provisions an extra effect
  column if needed and preserves the ordinary tracker effect.

NC uses 65,536 timing units per row and starts at the corresponding audio sample
boundary. Native sample voices ramp to silence, with the engine's residual
click-removal offset. Plugin instruments receive note-offs for the originating
tracker channel, rather than channel-wide All Notes Off / All Sounds Off.
Their own release envelopes and downstream audio tails remain audible. Another
tracker part sharing the plugin is preserved. Identical notes sharing a MIDI
channel still follow the plugin's own MIDI voice-matching behavior.

Cuts follow actual tempo, speed and groove, do not replay past events after a
seek, and run after a precise retrigger at the same timestamp. Muted channels
ignore them. NC does not cut separately continuing native NNA sample voices.

## Effect columns

The original column currently preserves the imported tracker command plus its
source-format byte value. Extra **Native FX** columns store precise PS/PL, BS/BL
and NC commands, including stable plugin bindings and high-resolution timing.
They cannot yet carry arbitrary legacy tracker effects.

A unified wide-column interface is feasible; the storage does not require two
user-visible column types. However, supporting legacy effects in every column
also requires shared-engine semantics for execution order, note-trigger effects,
effect memory, conflicting slides and global flow commands. Widening the displayed
value alone would not increase the resolution of a legacy tick-based SC command.
The larger unification was raised as a separate scope choice and is not claimed
as implemented here.

## API, Undo and persistence

- `instrument.create` now supports `empty:true`, an optional `name`, and
  `dryRun:true` for this mode. Conversion of a sample-only song retains its original
  sample/instrument numbers before adding the blank trigger.
- Assign through `instrument.plugin.set` with the returned slot and new revision.
  Creation and assignment use the existing separate document/plugin history
  domains. UI retry after assignment failure retains the same newly created slot.
- `pattern.performance.set` accepts `kind:"note-cut"`; `position` is in
  1/65,536-row units. Binding, value and duration are zero and can be omitted.
- NC uses native metadata **16**. Older builds reject these projects; files without
  NC retain their previous metadata requirement. Save/reopen and Undo/Redo preserve
  command timing, blank keymaps and stable plugin assignment.
- API description, schema, public API documentation and user guide were updated.

## Verification

- **74/74 native tests passed** (97.20 seconds, while the app build was running).
- **AppKit interface suite passed**, including SC2 → SC3 without losing the C,
  direct NC entry, row/beat conversion, strict row bounds, no parameter binding,
  new trigger creation, assignment retries without duplicates, and committing the
  active name/offset field before changing controls.
- **Full application socket suite passed**, including empty trigger creation,
  NC preview/no-op/stale/invalid rejection, revision guards and Undo.
- **30 stock OpenMPT PCM comparisons passed**, bit-for-bit, at 44.1/48/96 kHz with
  zero intercepted host allocations, frees or locks.
- AU and VST3 DC-output fixtures validate every sample around NC events at three
  rates and 17/128/512/4096-frame callbacks. Other tracks sharing the same plugin
  and MIDI channel remain active. Groove changes and repeated patterns retain
  callback-independent output, with zero audited allocations, frees or locks.
- Native samples retain exact cut-state boundaries and callback-independent
  output. On the test sample, the residual tail is below −80 dBFS within 10 ms
  and fully zero within 100 ms. Legacy tracker playback remains unchanged.
- Live isolated UI checks verify the new trigger form, saved empty keymap and
  plugin assignment, actual SC2 entry, and NC at 0.03125 beats reading back as
  8192 units into its row. Final follow-up checks cover committing a typed name,
  keeping the newly created instrument visible and row/beat conversion while
  the offset field has keyboard focus.

The existing sustained-presentation limitation remains open; this update does
not claim a new 60fps qualification. Offline fixture evidence does not establish
commercial-plugin capacity or physical-device latency.

The musician's existing ScreamSeq process and song were preserved. All UI/API
edits were made in private test instances. Save and quit the current app before
opening the new checkpoint. Changes remain uncommitted in the working tree.

## Build and evidence

- Application: `bin/mac-checkpoints/2026-09-21-plugin-triggers-precise-cut/ScreamSeq.app`.
- Source fingerprint: `e5855f168e13993784601e30322f833322fb429c03c3059f25b1f5356e4caf92`.
- Executable SHA-256: `9763417211f17b8fd9760ecea6d8d73a5071a729bd490ec06208d473bde4f35e`.
- All 449 recorded source files match the final bundle.
- Strict deep signature verification passed. `BUILD.json`, logs, screenshots,
  API readbacks and a generated test song are stored beside the app.
