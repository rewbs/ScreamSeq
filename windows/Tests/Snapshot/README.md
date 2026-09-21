# Shared snapshot conservation regression

The root cause was treating the module serializer/loader as an exact copy of the
editable `CSoundFile`. Module title fields truncate, and module pattern/order
formats can fill holes, discard unreferenced or trailing patterns, change cells
or settings, and trim/translate order entries. Fixing a Windows validation copy
cannot fix history, native persistence or playback preparation.

`editor/SampleArchive.cpp` now writes an optional **RSCORE1\0** correction at the
end of the existing sample archive, after any RSLOOP1/RSENVS1 extensions. The
shared load path already restores this archive before timing, native metadata
validation and publication. There is no Windows song model or title limit.
The Windows structural-validation topology-copy workaround has been removed.

## Wire contract

No change to `SongSnapshotParts`, `packSongSnapshot` or `splitSongSnapshot`.
RSONGS1 still has the 16-byte header and module/sample sections; RSONGS2 still
has the 20-byte header and module/sample/nonempty-timing sections. The writer
selects 1 when no timing correction is required, 2 otherwise. Legacy archives
without a correction stay readable. Old readers reject RSCORE1 as an unknown
sample extension instead of silently accepting a lossy song.

RSCORE1 is present only if title, pattern state/container size or order state
changes in the module roundtrip. All integers are unsigned little-endian;
text is `u32 byteLength` followed by exactly that many bytes, without a NUL.

- Magic: eight bytes `RSCORE1\0`.
- `u8 flags`: bit 0 title, bit 1 patterns, bit 2 orders; nonzero, no other bits.
- If title: text containing exact `m_songName` bytes in the charset already
  recorded by the sample archive. No trimming or charset conversion.
- If patterns: `u16 channels`, `u16 slots`, `u16 allocatedCount`. Channels must
  match the loaded module, slots <= 4000, count <= slots. The section is an
  authoritative allocation list: omitted slots are unallocated, not blank
  allocated patterns. For each allocated pattern, strictly increasing index:
  `u16 index`, `u32 rows`, `u32 rowsPerBeat`, `u32 rowsPerMeasure`, `u32 color`,
  text name, `u32 grooveCount`, grooveCount `u32` factors, then rows*channels
  cells. Each cell is exactly six bytes: note, instrument, volumeCommand,
  volume, effectCommand, parameter. No ABI/padding/native-endian structures.
- If orders: `u16 sequenceCount` matching the module; for each sequence in
  index order: `u32 orderCount`, `u16 restart`, UTF-8 text name, then orderCount
  `u16` pattern indices. Includes repeated, skip, stop, unallocated and trailing
  entries exactly. Existing timing archives retain tempo/speed and selected
  sequence remains in the existing sample header.
- This extension must be last and unique. Unknown flags/version, bad inventory,
  duplicate/out-of-order indices, invalid signatures/grooves, oversized counts,
  truncated cell/text payloads and trailing data reject. Rows <= 4096, channels
  <= 192, grooves <= 65536, orderCount <= MAX_ORDERS. The existing 512 MiB encoded
  limit remains; decoded sample PCM plus restored pattern cells is bounded by
  the same limit. Complete cell bytes are checked before allocation.

Patterns are constructed in the destination container, keeping correct core
back-references. Native pattern grooves restore already-normalized values,
without renormalizing them: OpenMPT's user-weight Normalize is not idempotent
for extreme valid grooves. The scoped const_cast addresses the non-const
unpublished pattern behind its const-only groove accessor; no active renderer
or validation-only copy is patched.

Outer native plist **container 4/5**, **metadata 14**, and inner **RSONGS1/2**
are independent version axes. None is bumped. Container-only Python parsers
need no framing change; the sample section is still opaque to them. A future
Python semantic parser would need the RSCORE1 grammar above.

API addition: `Tracker::validateSongStructureExport(const CSoundFile &source,
const CSoundFile &converted)` in SampleArchive.hpp. Existing
`Document::validateModuleSampleExport()` now includes this check. `save()`
always rejects title/pattern/order loss before opening the destination, even
when preserveSamples is false. `serialize()` remains the unchecked module-base
primitive needed by snapshotData; it is not a safe standalone export API.

## Reproduce on Windows ARM64

Run from the Windows sibling, with TMPDIR/TEMP/TMP set to the Hermes scratch
folder. Use the full installed VS CMake/CTest paths if absent from PATH.

```sh
powershell.exe -NoProfile -ExecutionPolicy Bypass -File windows/build.ps1 \
  -BuildDirectory C:/Users/P14/code/ScreamSeq-windows/bin/windows-snapshot-fix \
  -Target portable-tests -NoApp
"$CTEST" --test-dir bin/windows-snapshot-fix -C Release --output-on-failure -L portable
"$CMAKE" -S windows/Tests/Snapshot -B bin/windows-snapshot-tests \
  -G 'Visual Studio 17 2022' -A ARM64 \
  -DSCREAMSEQ_ENGINE_LIB_DIR=C:/Users/P14/code/ScreamSeq-windows/bin/windows-snapshot-fix/Release
"$CMAKE" --build bin/windows-snapshot-tests --config Release --parallel 3
"$CTEST" --test-dir bin/windows-snapshot-tests -C Release --output-on-failure -V
```

The standalone targets always compile TrackerDocument.cpp and SampleArchive.cpp
fresh, overriding those units in the imported library. Tests throw under NDEBUG;
renderers are heap-owned and executables reserve an 8 MiB stack.

Set SCREAMSEQ_REFERENCE_PROJECT to the supplied unchanged reference.screamseq
for the actual Mac fixture case (otherwise that case explicitly prints SKIP).
Expected source SHA-256:
`96ec9f809ef95a113d16613c29e73841e1f4a64ecc129672e5a48e548e587cd7`.
No user binary is copied into the repository.

## Evidence and limits

Before implementation, ARM64 Release snapshot-tests failed both exact-title and
unallocated-hole assertions. Expanded conservation then failed on MOD order
translation/trimming; export refusal also went red before its implementation.
An extreme normalized pattern groove went red before exact restoration.

Coverage includes all five editable module types, 100-byte ASCII and 100-byte
UTF-8 titles, whitespace/combining characters, charset retention, sparse/empty/
nonempty/unreferenced/trailing patterns, names/colors/signatures/grooves, all six
cell fields, fractional per-sequence timing, repeated/skip/stop/trailing orders,
selected sequence, repeated Undo/Redo, failed-transaction rollback, native
4/5 save/reopen with metadata 14 and stable identities, renderer preparation,
legacy snapshot 1/2, every byte truncation of a structure correction and targeted
malformed framing. The original --snapshot-probe is now a strict regression.

The actual supplied Mac fixture is restored, edited only in memory/disposable
output, saved/reopened, and checked for exact PCM, metadata and plugin retention.
Mac compilation/reopen, live UI/audio, sanitizers and realtime allocation auditing
are NOT claimed. MOD base conversion emits upstream warnings about unreferenced
patterns; the native correction retains those patterns (tests assert this).
Snapshot sequence/channel inventory changes that the module cannot represent
still reject explicitly; no alternative sequence/channel model is introduced.

## Independent-review conservation fixes

The snapshot/export comparison now explicitly compares all six cell fields,
including volume with `VOLCMD_NONE` and parameter with `CMD_NONE`. Upstream
`ModCommand` and `CPattern` semantic equality are unchanged. Pattern rows,
channels, signature, groove, color and raw name bytes are also compared.
Module export additionally compares every allocated pattern name decoded using
**each song's own `GetCharsetInternal()`**. This check is export-only: native
snapshots continue restoring the archived source charset. RSCORE1's grammar,
optional-last placement, validation limits and exact groove restore are unchanged.

The isolated fixtures start from freshly serialized/reopened modules with the
ASCII title `Conservation`. They assert a stable baseline and no unrelated
changes to title, pattern inventory, cells, settings or orders. The dormant-cell
fixture edits only p0/r0/ch0 to `[0,0,0,31,0,55]`; the charset fixture changes only
the source charset to UTF-8 and p0's name to `日本語`, confirming identical reopened
raw core data but different decoded text. Both exercise structural Undo/Redo.
Rejected saves test both `preserveSamples` values against an existing disposable
destination and verify its exact bytes and absence of staging files.

Review-fix verification used the separate **`bin/windows-snapshot-review-fix`**
standalone build, linked against `bin/windows-snapshot-fix/Release`. The initial
build compiled both `TrackerDocument.cpp` and `SampleArchive.cpp` from source;
each production change recompiled `SampleArchive.cpp`. The final clean rebuild
compiles all standalone source units again. Both executable PE headers identify
ARM64 (`0xaa64`) and reserve 8 MiB stacks; Documents remain heap-owned.

- `red-cell.log`: **25 cases, 25 failures** before the exact-field fix (snapshot,
  structural history, validation and both save modes for MOD/XM/S3M/IT/MPTM).
- `green-cell.log`: **25 cases, 0 failures** after the fix.
- `red-name.log`: **15 cases, 9 failures** before decoded-name validation
  (XM/IT/MPTM export validation and both save modes failed; six native snapshot/
  history controls already passed).
- `green-name.log`: **15 cases, 0 failures** after the fix.
- `full-ctest.log`: **3/3 CTest entries passed**: snapshot-tests (**48 cases**,
  including the actual supplied Mac fixture), snapshot-editing-tests, and the
  strict snapshot-probe. No portable-suite rerun is claimed by this narrow fix.
- Build logs, `ReviewBuildInfo.json` (source/library/executable hashes and PE
  checks), `fixture-hash.log` and copied license notices accompany the artifacts
  in `bin/windows-snapshot-review-fix`. The actual Mac fixture hash remained the
  expected SHA-256 above before/after testing; only disposable output was saved.

To reproduce without touching other snapshot builds, use the configure/build/
CTest commands above with `bin/windows-snapshot-review-fix` instead of
`bin/windows-snapshot-tests`. Run `Release/snapshot-tests.exe idleCell` or
`Release/snapshot-tests.exe patternName` inside that build for a focused subset.
The optional argument is a case-name substring; an unmatched filter fails.
Use the unchanged fixture environment setting for the full suite. Existing
upstream MOD/XM lossy-base warnings remain expected in the complex fixture;
no Mac executable, GUI/audio, sanitizer or realtime audit is qualified here.
