# Exact native sample snapshots

Native project versions 4 and 5 store an `RSONGS1` or `RSONGS2` snapshot in its binary-plist `module` field. Versions 1–3 continue to load their original ordinary module payloads. Versions 4–5 require a snapshot; versions 1–3 reject it. Version 5 adds explicit plugin instrument/channel assignments; version 4 is still written when each instrument plugin has at most one assignment on MIDI channel 1. Older readers reject version 5. Native song metadata has its own version and remains independent of this project envelope.

The snapshot combines a module in the song's original format with a correction layer for sample headers, names, PCM and instrument keyboard/note mappings, with optional exact envelope corrections. This preserves the original playback format and compatibility behavior while retaining individual sample frames that MOD, XM and other legacy writers can quantize, reorder, duplicate or omit. It is **not** a general replacement for native pattern/instrument serialization: other legacy-format instrument properties and pattern limitations still apply to the base module.

Editable playback, sample audition, structural document history, native saving, recovery and WAV export use this snapshot. Fixed-length PCM history continues to use its smaller changed-tile patches. Save and Save As default to `.resonance`; Export Module is separate and checks sample preservation before writing. A module export rejects native annotations/plugins/automation and timing, sample PCM, header, key-map or instrument-envelope losses. These preservation checks do not certify every other legacy module field. Different out-of-range cue sentinels and the transient sample-modified flag do not count as export losses. Core `Document::serialize` and the fixture-oriented default `Document::save` remain explicit ordinary module conversions; application exports use the preservation check.

## Binary representation

All integers and signed 16-bit PCM are little endian. Sample slot zero is implicit. Bounds include at most 512 MiB for the entire snapshot, at most 512 MiB of decoded original PCM, existing core sample/instrument limits, and the existing 600 MiB project limit.

- Envelope: eight bytes `RSONGS1\0`, `uint32 moduleBytes`, `uint32 archiveBytes`, then the two nonempty payloads. Nested envelopes and trailing data are invalid.
- Archive preamble: `uint32 originalModuleType`; `uint16` sample count, instrument count, selected sequence and stable charset code. The module type must match the decoded base. Charset codes are explicitly listed in `editor/SampleArchive.cpp` rather than depending on enum numbering.
- Each sample: six `uint32` fields (frames, loop start/end, sustain start/end, C5 speed); four `uint16` fields (pan, volume, global volume, flags); seven bytes (relative tone, fine tune, vibrato type/sweep/depth/rate, root note); 22 filename bytes; nine `uint32` cue points, or 12 OPL bytes plus 24 zero padding bytes; 32 name bytes; one mode byte; `uint32` patch count; patches.
- Flags retain the low ten core sample flags, modified and no-default-volume. External storage is embedded rather than retained as a file dependency.
- Mode zero reuses the module's decoded PCM of identical length/depth/channel count. The writer emits only differing tiles, up to 256 frames each. Each patch has `uint32 firstFrame`, `uint32 frames`, and interleaved signed PCM. Ranges must be nonempty, ordered, nonoverlapping and inside the sample.
- Mode one replaces the PCM layout and requires one complete payload (or none for an empty sample). Missing positive-length PCM is an error.
- After the samples, each instrument has a presence byte followed, when present, by 128 `uint16` keyboard sample indices and 128 note bytes. Envelopes may be corrected by the optional extension below; other instrument state comes from the base module. Surplus samples or automatically generated instruments in a legacy roundtrip are removed.

An optional reverse-loop extension follows the instrument maps: eight bytes `RSLOOP1\0`, a nonzero `uint16` entry count, then strictly ascending entries of `uint16 sampleIndex` and `uint8 flags` (bit 0 normal, bit 1 sustain; values 1…3). Zero/duplicate/out-of-range indices, unknown flags, OPL entries, truncation and trailing data reject. It is omitted when no sample uses native reverse loops, preserving older snapshot bytes. Current readers accept both forms; older builds reject an extended snapshot rather than silently changing its audio. This extension alone does not change the outer project version. Module export rejects any native reverse-loop loss.

Sample names and filenames must contain a terminating NUL inside their fixed fields. Restore only targets an unpublished `CSoundFile`. Bounds and allocations are checked before use; parse failures discard that new song, retaining the active document and transport. Loop guard buffers are regenerated after PCM restoration. The core destructor frees every sample slot, including slots above the previous inventory, on a failed parse.

Tests cover all five editable formats and four PCM layouts; exact trim/mixed history and rollback; MOD padding/guard bytes/loop quantization; XM sample reordering, sharing and unused slots; empty slots, OPL, sparse million-frame corrections, truncation and malformed fields; older-project migration; public API persistence; and complete WAV comparisons against independently populated PCM/loop renderers.

## Optional musical timing payload

If an ordinary module roundtrip changes global timing mode, default beat/bar lengths, global groove or a sequence's tempo/speed, the writer selects `RSONGS2\0`. Its header is the eight-byte magic followed by three little-endian `uint32` lengths: module, sample archive and timing archive. All three payloads are nonempty. When timing already survives the module, the original `RSONGS1` envelope is retained. Nested snapshots, invalid/truncated lengths and trailing data reject. Older readers reject `RSONGS2`; the outer project and native annotation versions do not change.

The timing archive is entirely `uint32` little-endian values: version 1, mode (0 classic, 1 alternative, 2 modern), rows per beat, rows per measure, sequence count, each sequence's tempo in ten-thousandths of BPM and speed, then groove count and each fixed-point row duration (`1 << 24` = one normal row). It records all sequences, and the count must match the decoded module. Groove is empty or spans one beat with an exact mean of unity. Bounds/type/version/normalization are checked before applying to an unpublished song. The public editor uses stricter musical ranges than the archive, which retains imported settings. Pattern-specific overrides remain in the ordinary module.

Structural Undo/Redo, recovery, native save/reopen, playback and WAV export all use this common payload. Ordinary module export refuses timing loss. No callback code allocates or parses archives.

## Plugin instrument assignments

Project envelope version 5 requires an `instrumentAssignments` array on every plugin, including an empty array on effects/unassigned plugins. Entries have distinct tracker `instrument` indices 1–255 and MIDI `channel` values 1–16; ownership is unique across the rack. The first entry is the primary and must match the existing `instrument` field (zero for an empty list). Only instrument plugins can own assignments. Instrument slots absent from the current document are retained for repair/history but prevent playback until repaired. Versions 1–4 reject the new array instead of silently ignoring it. The sample/timing payload is unchanged.

All aliases share one native processor, plugin state, parameter automation and audio outputs. Their tracker note maps and velocity behavior remain independent. A saved preset contains sound settings, not these song-specific assignments. Reverting to single assignments on channel 1 returns to project envelope version 4, subject to the independent native metadata/snapshot compatibility requirements above.


## Optional instrument envelope corrections

`RSENVS1\0` follows the instrument maps, or the optional `RSLOOP1` extension when
present. It contains a nonzero `uint16` count of changed instrument/envelope
pairs, in strictly increasing `(instrument,kind)` order. Each entry contains
`uint16 instrument`, `uint8 kind` (0 volume, 1 pan, 2 pitch/filter), `uint8 count`,
`uint8 flags`, five `uint8` marker indices (loop start/end, sustain start/end,
release), then `count` nodes of `uint16 tick` and `uint8 value`.

Flags use the core's low five envelope bits; unknown flags reject. There are at
most 240 nodes, values are 0–64, the first tick is zero, subsequent ticks are
nondecreasing, and markers must address existing nodes (zero for an empty
non-release marker, 255 for an unset release). Coincident imported nodes are
preserved. Instrument presence, ordered unique pairs, count, truncation,
duplicate/unordered extensions and trailing bytes are validated before exposing
the restored song. No callback parses or allocates these records.

Only source-roundtrip differences are written. This retains native envelopes on
sampleless XM instruments, pan endpoint 64 and properties a legacy writer cannot
store. Source-compatible snapshots retain their older representation. Older
readers reject the extension; no outer project-version change is required.
Ordinary module export refuses envelope loss. Exact Undo, recovery, playback and
WAV export use the same corrections.
