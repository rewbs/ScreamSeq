# Crash fix and autosave recovery — 20 September 2026

The supplied crash report points to `PreciseNotesEditor.capture()` at the
ordinary-note-to-precise-note conversion. The tracker cell uses `UInt8` fields;
Swift evaluated `cell[3] * 127 / 64` in that type and trapped on multiplication.
Even an ordinary volume of 64 overflows. Conversion now widens every cell field
to `Int` before constructing the draft, also preserving the numeric types the
editor reads back from its API-shaped dictionaries.

The new offscreen regression reproduced SIGTRAP against the old expression and
passes with the correction. It covers full volume, all 256 stored byte values,
note/instrument preservation, note-off and cut.

## Existing work recovered

The older build already wrote basic recovery copies every 30 seconds. A copy
from 19:00:11.938 was present, approximately 1.92 seconds before the reported
19:00:13.858 crash. That file has been preserved separately, without modifying
the original recovery directory:

`bin/mac-checkpoints/2026-09-20-crash-recovery/Recovered song before 19-00 crash.resonance`

This is a full native project (50,583 bytes, project version 4, no native plugins).
It is a recovery snapshot, not a reconstruction of any edits after its save time.

## Autosave changes

- Automatic recovery snapshots every 10 seconds when the document is available.
- Ten complete generations per document session; unchanged songs do not churn
  the history.
- A footer control shows the most recent autosave or a persistent error and
  opens the recovery browser. File → Recover a Song… provides the same browser.
- Normal startup discovers existing recovery copies and opens the browser.
- The browser identifies copies by date, song title, original file and whether
  an unfinished recording is included. Older copies remain selectable.
- Snapshot capture is serialized with document edits; immutable snapshot data
  is written on a separate utility queue. Disk writes do not keep editing busy.
- Files are staged, synchronized and renamed before they become discoverable.
  Failed or interrupted writes do not replace prior generations. Incomplete
  staging files and symbolic links are excluded from recovery discovery.
- An unfinished recording is copied and stopped within the snapshot, including
  releases for held notes. Live recording continues. Recovery reopens the take
  stopped for review/commit; a take from a mismatched song remains inspectable
  but cannot be silently committed against another version.
- Restoring protects the current unsaved song first and opens the selected
  recovery as an unsaved document. A corrupt copy does not replace the current
  document. Recovery never writes over the original project file.
- Manual Save cleans only this session's copies. Copies from other sessions,
  including the source of a restored song, remain available.

## Agent interface

The native application advertises `recovery.status`, `recovery.list`,
`recovery.save` and `recovery.restore`. Save/restore require `expectedRevision`;
restore accepts an opaque ID returned by list. Request-ID retry protection
applies. `context.get` includes autosave status. The request schema and
`mac/AUTOMATION.md` document these application-specific methods.

## Qualification

Final validation results and packaged build identity are recorded below.

UI qualification uses actual AppKit controls and offscreen layouts. The bitmap
captures establish layout but do not fully render macOS's native rounded-button
surfaces; visible-window interaction and sustained display frame rate were not
retested. All application launches for this work are isolated, windowless test
instances. No hardware audio, physical MIDI or commercial plugins are used.
The user's running instance and earlier packaged builds are left unchanged.

### Final results

- All 59 selected CTest suites passed, including sample/AU/VST3 timing and audio-reference tests, persistence, and recovery-session tests. Hardware MIDI and device stress tests were excluded.
- Plugin-picker and recovery-store checks passed. Both session-host and native-app API suites passed, including the application-specific recovery method schema.
- The real ten-second timer, unchanged-document suppression, two SIGKILL/relaunch cycles, exact pattern recall, corrupt-copy handling, current-song protection, filesystem failure/retry, request idempotency, unfinished takes and scoped cleanup passed.
- Windowless native startup and the AppKit editor interaction/layout suite passed. Recovery-browser and precise-note layouts were rendered and inspected offscreen.
- The preserved pre-crash song opened successfully in the corrected native app: Untitled, 8 channels, 1 pattern, 2 samples. Its source bytes were unchanged.

### Delivered build

`bin/mac-checkpoints/2026-09-20-crash-recovery/Resonance.app`

The development copy is also in `bin/mac-background/Resonance.app`. Earlier checkpoints are unchanged. Open recording recovery copies using this corrected build.

Executable SHA-256: `a1093b0ecb34fe968bae22930426dfe58d888618919c68b6ec8c7f85c5d2804b`

Source fingerprint (392 files): `1289827a2915f33e181ca92950d8aee786013cde099e05ef8f1bfc09fae2ccf6`

Strict code-signature verification passed. Logs and offscreen images are in `doc/mac-native-qualification/2026-09-20-crash-recovery/`.
