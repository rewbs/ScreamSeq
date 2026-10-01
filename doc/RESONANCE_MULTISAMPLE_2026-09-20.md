# Filename-based multi-sample instruments

The sample browser now detects families of recordings at different notes and
offers to import them as one mapped instrument.

Use the [new Resonance checkpoint](../bin/mac-checkpoints/2026-09-20-multisample/Resonance.app).
Open **File → Browse Samples…**, select any Clav note, then click
**Import as one instrument…**. The review lists the entire matching family,
including notes outside the current search/page. Check the instrument name,
octave offset, root notes and playable ranges, optionally **Check import**, then
**Import one instrument**. The resulting instrument becomes the selected
instrument for note entry.

## Detection and mapping

- Matching uses the same parent folder, filename family and file extension.
  Leading sequence numbers are removed from the family name; other variation
  text remains. Files named simply `C4.wav`, `D4.wav` use their containing folder
  as the instrument name.
- Note tokens support sharps, flats, Unicode accidentals and negative octaves.
  Filenames containing several note tokens are ambiguous and are not grouped.
  Velocity/round-robin suffixes remain separate families; duplicate/enharmonic
  roots block import rather than selecting an arbitrary file.
- Consistent numeric prefixes across at least three files can suggest an octave
  alignment. This is a visible, adjustable filename inference, not audio pitch
  detection. Tracker roots in the review omit the tracker grid's padding dash,
  so positive `C1` cannot be confused with a filename's negative-octave `C-1`.
- Each recorded root plays its own sample at imported tuning. Gaps use the
  nearest recorded root with semitone transposition; ties use the lower root.
  Keys outside the lowest/highest supplied root remain unmapped, as stated in
  the review. No destructive resampling is performed.
- The complete import appends the samples and one multi-sample instrument in
  one document Undo step. Entering instrument mode also creates matching
  instruments for existing sample-only assignments so earlier notes keep their
  meaning. Redo and native project saves retain sample PCM, loops and both
  sample/note mappings.

The limits remain 128 files and 256 MiB each of encoded/decoded audio per batch,
within the song format's sample/instrument/note capacities. Roots must be
distinct and within tracker C0…B9 and the current format's narrower range, if any.
Per-file exclusions, velocity layers, round robin and audio pitch estimation
are outside this delivery. Such ambiguous families require explicit roots or
a suitable subset through the API.

## The actual Clav pack

The selected `077 Clav Junos F4.wav` belongs to a **109-file** family. Its
numbered keys run 0…108, while the filenames call the range C−2…C7. All numeric
prefixes agree with a **+2 octave** alignment to tracker C0…C9. The review shows
this conversion explicitly; for example, filename F4 maps to tracker F6.

The complete real family was imported into a private, silent disposable song:

- Dry-run validation of all 109 files: **0.206 seconds**.
- Import of all 109 samples as one mapped instrument: **0.173 seconds**.
- Every recorded root selected its own sample with untransposed source playback.
- One Undo removed the complete batch; Redo restored it.
- A native project was saved and reopened in a fresh application process,
  retaining both instrument maps exactly.

These are local diagnostic timings, not guarantees for other disks or packs.
The source sample files were only read. No hardware audio or desktop control
was used.

## Agent interface

- `sample.library.multisample.get {path, expectedLibraryRevision?}` returns the
  full family, source note tokens, semitone values and suggested octave shift.
  It is an application-owned read and returns `group: null` if none is found.
- `instrument.importMultisample {name, samples: [{path, rootNote}],
  expectedRevision, dryRun?}` accepts explicit reviewed roots and returns the
  instrument number and inclusive sample zones. Roots are one-based tracker
  notes: C0 = 1, C4 = 49, C5 = 61. Both API hosts support import.
- `instrument.get` now returns the 128-entry `noteMapping` alongside its existing
  sample `mapping`. Native saves preserve both.

Dry run fully validates the files without changing song, playback or history.
Apply stops playback and uses the existing revision checks and request-ID retry
protection. Failed batches cannot leave partial samples/instruments/history.

The schema, `api.describe`, [API guide](../mac/AUTOMATION.md#filename-based-multi-sample-instruments)
and [browser guide](../mac/README.md#multi-sample-instruments-from-filenames) are updated.

## Verification

Core tests verify exact PCM and mapping Undo/Redo, project recall, invalid and
duplicate roots, failed-file atomicity, nearest-note boundaries and unmapped
keys. Offline audio tests measure the actual rendered frequency at roots and
between roots in MPT, IT and XM songs, including different source sample rates
and channel layouts. Roots preserve source pitch; gap notes transpose correctly;
unmapped keys render silence.

Native UI checks cover the full family, octave adjustments and bounds, the
visible name field, dry-run/apply, stale-song rejection, duplicate-click
protection and late detection results. The review was captured and visually
inspected offscreen. Running-app socket checks cover family detection, strict
types, retries, stale revisions, exact mappings, Undo/Redo and native relaunch.
All **60 core regression tests passed**. The background application checks also
cover the existing editing, audio, plugins, API, startup and crash recovery
behavior; hardware MIDI and live-device stress tests are excluded.

[Review screenshot](mac-native-qualification/2026-09-20-multisample/MultisampleImportView.png)
· [Qualification evidence](mac-native-qualification/2026-09-20-multisample/)

Earlier checkpoints remain separate. Build fingerprints and signature
verification are recorded in the evidence directory.
