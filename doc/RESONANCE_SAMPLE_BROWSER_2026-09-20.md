# Resonance sample library

The new sample browser searches nested sample packs, treats their folder names
as inherited tags, auditions files without changing the song, and loads multiple
samples in a single Undo step. It is available in this separate checkpoint:

[Resonance.app](../bin/mac-checkpoints/2026-09-20-sample-browser/Resonance.app)

Open **File → Browse Samples… (⌘⇧L)** or **Samples → Browse…** in that build.
The first visit indexes `~/samples`. Other folders can be added, removed from
the library, and explicitly rescanned. Subsequent launches reuse a persistent
index rather than rediscovering the files.

## Using the browser

- Search combines words across filenames and every ancestor folder. Try
  `808 "bass drum"` or `junos chords`. Quoted phrases stay together;
  `-maschine` excludes matching paths. Matching ignores case and accents.
- The left pane lists folder tags with matching sample counts. Select a pack
  or category to see all of its nested samples; ⌘-click combines tags. A separate
  search field finds folder tags, and Clear filters resets the search.
- ↑/↓ moves through results. Auto-preview auditions each selection; Space
  replays it, and Escape or Stop stops it. The waveform, duration, sample rate,
  channels, preview-volume control and source path help compare candidates.
- ⌘-click or Shift-click selects a batch. Load selected, Return or double-click
  appends sample slots. The optional instrument checkbox creates a corresponding
  instrument per sample. Choose files also supports a multiple-file selection.
- An invalid file rejects the entire batch. A successful batch is one document
  Undo step, with exact Redo and native project persistence. Existing samples
  receive matching instruments when entering instrument mode, preserving their
  existing pattern assignments.

Preview uses a separate native player, independent of the song transport,
mixing and plugin rack. Rapid selection changes retire older decoding requests,
so late results cannot start playing after a newer selection or Stop. Decoded
previews have a bounded eight-file/32 MiB cache. The selected preview gain is
independent of song volume, and playback releases its renderer after completion.

[Browser screenshot](mac-native-qualification/2026-09-20-sample-browser/SampleBrowser.png)

## Performance and limits

A read-only scan of this machine's `~/samples` found **22,387 audio candidates**
in **1.785 seconds**. Hidden resource forks account for much of the larger raw
file count; hidden files, `__MACOSX`, symlinks and non-audio extensions are
excluded. Actual supported decoding is checked on inspection/import.

| Query | Matches | In-memory search |
| --- | ---: | ---: |
| Empty | 22,387 | 13.34 ms |
| `808 "bass drum"` | 331 | 68.50 ms |
| `Junos chords` | 82 | 64.56 ms |
| `snare -maschine` | 1,255 | 69.60 ms |
| `"From Mars" WAV` | 21,856 | 58.67 ms |

The running application's complete initial scan **and cache write** took
**4.48 seconds**. The same queries took **67–79 ms** including their API round
trips. Eight actual pack files decoded to waveform/preview data in **1.5–10.8
ms** each, covering mono/stereo audio at 44.1 and 48 kHz. This check used a private
index and silent previews, and verified that the song remained unchanged.

Scanning, cache reads, searching and preview decoding run off the UI thread.
Search is debounced by 120 ms; result tables reuse visible rows and show 500
items per page. These measurements demonstrate background search cost, not a
physical-display 60 fps qualification. No foreground display or physical audio
test was performed during this work.

The current limits are:

- 32 library roots and 250,000 indexed files. Rescan explicitly refreshes changes;
  filesystem watching, custom tags and automatic BPM/key detection are not part
  of this delivery.
- Preview plays the first 30 seconds, capped at 2,097,152 frames at high rates.
  The full duration and any preview truncation are shown. The existing sample
  decoder reads the file before preparing this bounded preview.
- Preview uses the tracker engine's internal 8/16-bit sample representation and
  follows the macOS default output. Source files are never rewritten or normalized.
- Bulk import accepts 1–128 distinct files and at most 256 MiB of encoded and
  decoded audio per batch, subject to the song format's sample/instrument limits.
  Applying an import stops song playback; browsing and auditioning do not.

## Agent access

The browser's capabilities are exposed through the same local API:

| Capability | Method |
| --- | --- |
| Status, roots, index revision | `sample.library.get` |
| Search, inherited tags, pagination | `sample.library.search` |
| Configure roots | `sample.library.roots.set` |
| Explicit cache refresh | `sample.library.rescan` |
| Read file metadata and waveform | `sample.library.inspect` |
| Audition / Stop | `sample.library.preview` / `sample.library.preview.stop` |
| Atomic batch import, optional instruments, dry run | `sample.importMany` |

Library configuration/search uses `libraryRevision`, independently of song
revisions. Imports require the song's `expectedRevision`. The schema and
`api.describe` advertise the new methods. Library methods belong to the native
app; bulk import also works in the standalone API host.

See [API details and example](../mac/AUTOMATION.md#sample-library-and-bulk-import)
and [usage instructions](../mac/README.md#sample-library).

## Qualification

The complete background checks cover 60 core tests, both socket API hosts,
native application startup, crash recovery, preview audio, and offscreen editor
layouts. The new targeted checks verify:

- Nested folder search, intersecting tags, quotes/exclusions, accent folding,
  pagination, overlapping roots and hidden-file filtering.
- Fresh indexing, persistent cache reuse after restart, explicit rescan,
  removal of roots without deleting files, and stale library revisions.
- macOS path aliases: the filesystem can return `/private/var` for a root
  configured through `/var`. Consistent canonical paths now keep relative
  folder tags and root filters correct.
- Native sample decoding, source PCM/rate/channel preservation, waveform bounds,
  malformed-file failure, all-or-nothing import, dry runs, retry deduplication,
  stale song revisions, exact core Undo/Redo and project recall.
- The actual preview player rendered offline at its selected gain. Its output
  matched the source signal; switching from 48 kHz mono to 44.1 kHz stereo
  remained finite and audible after resampling. Stop shut down the renderer.
  Cancelled requests completed without hanging an API connection.
- Native browser keyboard actions, stale asynchronous search rejection,
  multi-selection, inherited tag filters, prevention of duplicate import clicks,
  and layout at a 980 × 730 point content size. The captured interface was
  visually inspected.

Tests use disposable, windowless application instances and private caches. They
do not alter the user's sample packs, control the desktop, load commercial
plugins or play hardware audio. Earlier delivered apps and the recovered user
song retain their previous hashes.

[Evidence directory](mac-native-qualification/2026-09-20-sample-browser/)

- Binary SHA-256: `e05ed4c7b67bb513a4ab6840bd70a8539a16769c0a7e28d8bf5157f356bc9c88`
- Source fingerprint: `5f58085be5e2aa817477dfb887dc35acfa4892b5660f2c30270dc8d0c140edd5`
- All 401 source hashes match the bundled manifest.

## Prior art

The design draws on folder-aware all-term search and preview controls described
in [Ableton's browser manual](https://www.ableton.com/en/live-manual/12/working-with-the-browser/),
and selection-based sample audition described in
[Renoise's Disk Browser documentation](https://tutorials.renoise.com/wiki/Disk_Browser).
Resonance implements these interactions with native AppKit controls, a local
index and its existing tracker sample decoder.
