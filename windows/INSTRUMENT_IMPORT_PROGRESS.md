# Native instrument import and keymap — 2026-09-21

The instrument editor now has an Import instrument action, also available from
the command palette. Its native chooser offers ITI, XI, PAT and SFZ instruments,
audio samples and an all-files fallback. The existing `instrument.import`
transaction imports into a new slot, preserving the shared sample-mode conversion
rules. The editor captures the imported instrument and selects it for musical
typing. Existing instrument or envelope-bank drafts must be resolved first.
Cancel, decode failure, a changed document/revision or a changed draft during
the chooser cannot partially import or discard the original draft.

A native keymap list now sits beside the envelope. It names every mapping index,
note and sample, including the eight high mapping entries beyond the 120 ordinary
tracker notes. Selecting a note fills the existing range controls without
mutating the song. Arrow keys navigate the list, F6 returns to the envelope,
and ordinary native Tab traversal reaches the range/sample fields. Staged keys
have an asterisk; Apply saves the range with other pending instrument settings
in the existing single document transaction. Unrelated mappings and envelopes
remain intact. Population is bounded to 128 entries and occurs when the mapping
is loaded or staged, outside painting and audio rendering.

Unfinished range fields now count as a captured draft. They survive Close and
attempted instrument/note selection, block Apply until staged, and participate
in generation guards. Escape discards the unfinished fields; Reload explicitly
restores the saved instrument. Enter in a range field stages it. These controls
reuse `instrument.patch`, Undo and native persistence; no schema or project-format
change was needed. `workspace.get.instrumentEnvelope` additionally reports
`mappingFields`, `mappingDirty` and `selectedKey` as transient UI observations.

## Qualification

The ARM64 executable SHA-256 is
`4194798EE1326B1008F53491015AD72565F51944386D72DC0EAADBC54FF99008`.
The full build passes. Initial focused testing passed six of seven new cases;
the geometry fixture incorrectly compared hidden tool fields at their initial
1×1 positions. It now selects the tool that exposes all five fields, preserving
the same bounds/overlap assertions. All 15 combined instrument cases then passed
in 101.647 seconds. All 29 primary native CTests pass in 13.18 seconds; all six
asset/import CTests also pass in 3.09 seconds against the unchanged shared import
code. The full application regression completed 243 cases in 665.527 seconds:
241 passed, two failed and none skipped. Surge's live-audition case failed at
the restored-output assertion after plugin removal and plugin-history Undo.
The saved host report has `processorFault: true`, zero callback overruns and
zero device errors. Its cause must be investigated before this build can pass
the audio gate. The routing-assignment case completed its body but failed the
unchanged-foreground-window assertion during private-desktop cleanup; it did
not fail the newly instrumented routing-window opening assertion.

Twenty isolated repetitions on the same executable then passed 19 cases and
reproduced the Surge failure once, in 115.872 seconds. Per-step transport traces
show successful initial audition, note release and Panic; after removal/Undo,
the restored renderer faulted at frame 24,000 and stopped. The other 19 traces
have audible restored meters. This establishes an intermittent processor failure,
not its host/vendor cause. The failed run and every repeated observation are
retained rather than replacing them with the passing repetitions.

New cases exercise native SFZ import with Unicode and spaced relative sample
paths, sample-mode conversion, imported sound selection, cancellation, corrupt
files, revision/document changes while the chooser is open, parent/bank drafts,
mapping indices 0–127, staged indicators, invalid and stale range fields, native
keyboard navigation, minimum-size geometry, one-step history and native reopen.
A short silent WASAPI case checks a no-op mapping leaves playback active and a
real mapping edit stops before publication. It is functional evidence rather
than a sustained capacity measurement. ITI/XI parsing, envelope/loop retention
and external-file handling also have the existing native import-suite coverage;
the new chooser tests specifically exercise SFZ.

Evidence is under `bin/windows-instrument-import-*`; the preserved development
package is `bin/windows-checkpoints/instrument-import-20260921/`, with source
commit, executable/file hashes, notices, raw logs and fixtures. Its manifest
explicitly fails the audio gate. The preceding sample-settings
package is preserved at `bin/windows-checkpoints/sample-settings-20260921/`, with
125 verified file hashes and source `8d0430eba`. Its full app result remains
235/236, followed by nine focused formula passes and 20 successful repetitions
of the bank-opening case. The cause of that intermittent failure, and an earlier
routing-window opening failure, remains unresolved; passing subsequent runs do
not prove either root cause fixed.

## Remaining parity work

A fresh fetch still finds default branch `codex/screamseq` at `bcfe0f8a7`, already
integrated; this remote has no `main`. The installed ARM64 Contourtonist, OrbitCab
and Surge XT lifecycle tests remain in the application regression gate.

First investigate the newly observed Surge live-audition processor fault on the
exact executable and preserve every failing report. The following implementation
area is the native sample library: separately guarded
library preferences and bounded background indexing/search, folder tags,
preview and the existing atomic multisample import operation. MIDI/device and
recording/recovery, persisted workspace/accessibility/configurable keys, live
structural and opaque plugin publication, x64/bridging, reciprocal Mac format-17
roundtrip and sustained loaded audio remain open. OrbitCab's partition discrepancy
and Contourtonist's audible automation qualification are unchanged. Private-desktop
geometry tests do not establish foreground aesthetics or presentation performance.
Full Mac parity is not complete.
