# Native project container coordination

## Status and scope

`native_project.py` is a **portable Python 3.10+ / standard-library `plistlib`
qualification and repacking foundation**, tested here with Python 3.11. It is
**not integrated C++ application persistence**. No portable C++ plist dependency
was found in the inspected CMake definitions. This work does not add a parallel
song model, a module exporter, a plugin host, or any Mac/shared-model changes.

Both `.screamseq` and legacy `.resonance` name the same container format; the
extension is not a codec switch. New native songs must not be saved as a renamed
IT/MPTM module. Existing identifiers and `RSONGS*` magic remain unchanged.

The latest inspected envelope report and current decoder require **native
metadata 14**. The earlier delivery report's metadata 13 statement is historical,
not the current ceiling. Container version, metadata version, and snapshot magic
are three independent version domains.

## Source contract inspected

Repository-relative authorities (source, not inferred file-extension behavior):

- `mac/Bridge/TrackerSession.mm`: `decodeProject`, `decodePlugins`,
  `decodeAutomation`, `projectDataForRecovery`, `serializedData`.
- `mac/Bridge/PluginAssignments.inc`: `nativeProjectVersion`.
- `mac/Bridge/NativeSongMetadata.inc`: `encodeNativeSong`, `decodeNativeSong`.
- `mac/Bridge/SignalGraphMetadata.inc`: graph recipes and their state encoding.
- `mac/Bridge/EnvelopeBankMetadata.inc`: song-local templates and links.
- `editor/SampleArchive.cpp`: `isSongSnapshot`, `splitSongSnapshot`,
  `packSongSnapshot`; `editor/TrackerDocument.cpp`: nested-snapshot rejection.
- `doc/SCREAMSEQ_ARCHITECTURE.md`, `assets/branding/BRANDING.md`,
  `doc/SCREAMSEQ_DELIVERY_REPORT_2026-09-21.md`, and the newer
  `doc/SCREAMSEQ_ENVELOPES_2026-09-21.md`.

### Outer property list

The writer uses `NSPropertyListBinaryFormat_v1_0` (`bplist00`), with a dictionary
root, not a ZIP archive or JSON document. Current editable saves contain:

| Key | Stored value |
| --- | --- |
| `version` | Container integer: supported Mac range 1–5 |
| `module` | NSData / plist data: exact native song snapshot for versions 4–5 |
| `native` | NativeSong metadata dictionary, required from container version 3 |
| `sequence` | Current sequence index; optional on old files, range 0–255 |
| `plugins` | Array of rack device dictionaries |
| `automation` | Arrays `[slot, parameterID, value, frame]`; frames use canonical 48 kHz |
| `recoveryTake` | Optional recovery dictionary with compatibility flag, precise-note events, and recording diagnostics |

Versions 1–3 embed a legacy module rather than an exact snapshot. Version 3 adds
native metadata. Version 4 carries exact snapshots. Version 5 adds plugin
`instrumentAssignments` arrays of `{instrument, channel}`. Current saves choose
4 unless a plugin has a non-default MIDI channel or aliases, then choose 5.
Repacking does **not** promote legacy modules to exact snapshots or change any
version. Native metadata, if present even on an old outer version, is version
checked rather than ignored by this tool.

Mac limits: whole project 600 * 1024 * 1024 bytes, embedded module/snapshot
nonempty and at most 512 * 1024 * 1024 bytes. The Python layer enforces these and
rejects boolean/float/string versions rather than coercing them. It requires the
exact `bplist00` header; XML input is deliberately not a project import path.

### Exact snapshot framing

All sizes below are **unsigned little-endian 32-bit integers**:

```text
RSONGS1\0 (8 bytes) | module_size | samples_size
                     module bytes | sample archive bytes

RSONGS2\0 (8 bytes) | module_size | samples_size | timing_size
                     module bytes | sample archive bytes | timing archive bytes
```

Header sizes are 16 and 20 bytes respectively. Every declared section must be
nonempty (timing exists only in version 2); the size sum must equal the complete
snapshot size, with no trailing bytes. Nested known song snapshots are rejected.
Unknown snapshot magic/version is rejected for modern containers; reserved
`RSONGS` payloads cannot masquerade as legacy modules.

`split_snapshot` returns read-only memoryviews of the three sections (empty
third view for version 1), without copying sample bytes. The sample archive
contains exact sample metadata/PCM and instrument restoration data; the timing
archive restores native timing. These sections **must not be discarded in favor
of the inner module**. Their internal serialization is not implemented here;
the entire `module` plist data value is retained byte-for-byte.

### Native metadata through 14

The current encoder selects the lowest version required by its feature set,
not unconditionally 14. The decoder accepts 1–14. Principal milestones:

| Version | Native metadata addition |
| --- | --- |
| 1 | Stable entities / indexed patterns, tracks, samples, instruments, sequences; allocator `nextID` |
| 2 | Musical automation |
| 3 | Mixer graph |
| 4 | Mixer sidechains |
| 5 | Note tracks and column mutes |
| 6 | Mixer input balance (`prePan`) |
| 7 | Native pattern performance commands |
| 8 | Pitch commands |
| 9 | Precise notes |
| 10 | Signal graphs |
| 11 | Scripted automation curves |
| 12 | Per-note effects |
| 13 | Graph automation sources/per-pattern envelopes; sample-instrument graph assignments |
| 14 | Song-local envelope templates and stable target links (`envelopeBank`) |

Native IDs are strings such as `n30`, not array positions. Preserve all indexed
entity maps and references without reallocation. `envelopeBank` stores `entries`
(`id`, `name`, `shape`) and `links` (`target`, `template`, `span`). A shape has
`span`, `rowsPerBeat`, `points`, `instrument`, `flags`, and five `markers`.
Unset release markers use **4294967295 / UINT32_MAX**, not JSON null. Link targets
carry `kind`, `owner`, and `pattern` (empty except graph targets).

### Plugin portability and unknown fields

Rack device records retain `format`, `type`, `subtype`, `manufacturer`, `name`,
`path`, `classID`, `isInstrument`, `bypass`, `instrument`, `instanceID`, `state`,
`auxiliaryInputs`, `auxiliaryOutputs`, and version-5 assignments. Rack `state` is
**plist data**. Signal graph node recipes use **base64 text** for `state`, plus
`inputs`/`outputs`, not rack auxiliary-bus field names.

The complete plist tree is decoded and re-encoded, never rebuilt from a list of
known keys. Unknown fields at any depth, opaque data, native IDs, rack instance
IDs, class IDs, AU type/subtype/manufacturer identity, path hints, envelope links,
recovery data, and graph state text are carried through without rewriting.
No plugin is instantiated, resolved, dropped, substituted, or assigned a new ID.
AU is macOS-only: retaining an AU recipe/state does **not** make it playable on
Windows. Installed-plugin availability reporting belongs to the future host.

Unknown-field retention is a tool guarantee for `plistlib`-representable values,
not a claim that Mac's strict inner metadata decoders accept hypothetical new
keys. This tool preserves opaque data even where it cannot interpret it. Plist
object-table order, integer widths, date representation precision, sharing, and
other physical encoding details are not a byte-identical container guarantee;
keep the original file if exact container bytes are required. Sample/plugin data
payload bytes remain unchanged. No JSON conversion is used for the project tree.

## Usage and qualification

From repository root:

```sh
python -B -m unittest discover -s windows/Tests/Project -p 'test_*.py' -v
python -B windows/Project/native_project.py path/to/song.screamseq
python -B windows/Project/native_project.py path/to/old.resonance --repack path/to/new.screamseq
```

Inspection emits a small JSON report, explicitly including
`song_semantics_validated: false`. Repacking validates before creating its
output and uses exclusive creation: an existing destination, including the
source itself, is never overwritten. It is a qualification copy, **not** an
atomic, crash-safe application save implementation; an interrupted new write
can leave an incomplete destination. The caller must remove/retry that file.

Library API: `loads(bytes) -> dict`, `dumps(dict) -> bytes`,
`split_snapshot(bytes) -> (module_view, samples_view, timing_view)`. Structural
and version rejections use `ValueError`; invalid caller-supplied plist values
can also raise standard `plistlib` serialization errors.

Tests were written and observed failing before each implementation slice, then
run green. They use actual `plistlib` binary serialization of generated trees
based on the inspected schema: metadata 14, envelope sentinel/links, unavailable
AU rack and graph recipes, opaque extension values, Unicode, dates, large
integers, and all-byte sample payloads. Tests cover both snapshot headers,
legacy preservation, unknown/newer versions, wrong primitive types, malformed
headers, truncation, length overflow/trailing bytes, nested snapshots, section
boundaries, size-limit boundaries, and CLI no-overwrite/rejection behavior.
Reduced configurable constants exercise size boundaries without huge fixtures.

**The inner module/sample/timing sections in the generated tests remain
intentionally opaque qualification payloads, not playable song archives.**
The supplied Mac-produced UI reference is now separately qualified below. It is
external to Git; generated cases must never stand in for this opt-in fixture.

### Actual Mac reference: bounded container qualification

The user's `ScreamSeq-UI-Reference-2026-09-21` pack describes
`reference.screamseq` as a disposable Midnight Circuit demo with synthetic
samples and built-in effects. Its unchanged input is **63,474 bytes**, SHA-256:

```text
96ec9f809ef95a113d16613c29e73841e1f4a64ecc129672e5a48e548e587cd7
```

`qualify_project.py` uses the existing `native_project` codec to read the input,
repack into a new disposable directory, reopen the repacked file from disk, and
compare the **complete decoded plist tree**, including scalar types and float
bits. It compares the full opaque snapshot and each section byte-for-byte, then
rereads the source to check it is unchanged. Temporary files are cleaned up;
there is no destination-song argument and no source write. `TMPDIR` selects the
scratch parent. It emits JSON evidence containing source/repacked/section hashes,
codec/utility source hashes, runtime, preserved field names and explicit limits.
JSON is only a report; the project tree never passes through JSON.

From repository root (Git Bash; set the three scratch variables to an existing
approved directory before running):

```sh
export TMPDIR='C:/path/to/scratch' TEMP='C:/path/to/scratch' TMP='C:/path/to/scratch'
export SCREAMSEQ_REFERENCE_PROJECT='C:/path/to/ScreamSeq-UI-Reference-2026-09-21/reference.screamseq'
python -B windows/Project/qualify_project.py "$SCREAMSEQ_REFERENCE_PROJECT"
python -B -m unittest discover -s windows/Tests/Project -p 'test_*.py' -v
```

PowerShell equivalent for the opt-in path is
`$env:SCREAMSEQ_REFERENCE_PROJECT = 'C:\path\to\reference.screamseq'`; pass
`$env:SCREAMSEQ_REFERENCE_PROJECT` to the utility. Set `$env:TMPDIR`, `$env:TEMP`
and `$env:TMP` to the existing scratch directory there as well.

`test_reference_project.py` skips only if `SCREAMSEQ_REFERENCE_PROJECT` is unset
or empty. An explicitly configured missing, changed or substitute file **fails**;
the test pins the hash above. The general utility accepts other codec-supported
containers but does not authenticate their provenance. Standard generated tests
run without the external pack. Tests for the utility were observed red before
implementation, including type-loss and CLI failure/reporting regressions.

Observed and asserted for this specific Mac file:

- Outer version **4**, native metadata **14**, sequence **0**, `RSONGS1\0`.
- Inner module **53,937 bytes**, sample archive **3,453 bytes**, and **no timing
  archive** (version-1 snapshot). Their payload bytes are preserved, not decoded.
- Native metadata has 1 pattern, 8 track, 4 sample, 7 instrument and 1 sequence
  records, allocator `nextID = 38`, 4 precise notes and 1 parameter automation
  record. Tests assert the stored precise-note positions/velocities and scripted
  formula text; they do not restore notes or evaluate formulas.
- Two graph definitions (`n20` Motion filter with 5 nodes; `n26` Crunch accent
  with 3), row/start/wet/stop commands, a track assignment and per-pattern graph
  envelope survive. Full mixer, routing, IDs, graph layouts and other fields are
  included in tree equality, **not** checked for musical cross-reference validity.
- Three envelope-bank entries (`n16`, `n18`, `n37`), the parameter link
  `n17 -> n16` and volume link `n30 -> n37`, span values, shape/formula data and
  `4294967295` release-marker sentinels survive unchanged.
- One Built-in Gainer rack record (`resonance.gainer.v1`) retains instance ID
  `097DAD08-D422-4700-B432-41E4586A9EA0` and its **empty data state**. Graph
  Digital Filter/Distortion recipes retain their Built-in class IDs and **empty
  string states**. No AU, VST3 or nonempty plugin state is exercised by this file.
- Outer automation is empty; recovery data and sample-instrument graph
  assignments are absent/empty. Do not infer their compatibility from this pass.

On Python 3.11.16 the repacked container is **62,931 bytes**, SHA-256
`a9125ffc21428732dd4a5591be727cd809ba632e6e432f1ea19afdc101845f1e`.
It has a different physical plist encoding from the source; **the complete
container is not byte-identical**. The source itself remains exactly unchanged,
and decoded values/types plus opaque snapshot sections compare equal.

This establishes **one actual Mac-produced file's Python container roundtrip**,
not full interchange. No Mac reopen, Windows application open/restore, native
metadata semantic validation, plugin instantiation or audio comparison was done.
The report keeps all four corresponding validation flags false. No partial
musical document is constructed or loaded, and no application or audio device is
started. Parser hardening/resource limits remain those of the existing codec.

## Exact blockers before application persistence can be claimed

1. Select and qualify a portable C++ plist reader/writer, or extract a shared
   persistence layer; preserve unknown fields across typed document edits.
   Python is not linked into the Windows application or its realtime path.
2. Connect snapshot sections to shared `Document`/`SampleArchive`/timing restore
   code, native metadata to `NativeSong`, and enforce full cross-reference and
   musical invariants. This layer only validates container shape/version and
   snapshot framing; it does not validate plugin fields/count/state budgets,
   automation, graph topology, envelope targets, or inner archive semantics.
3. Implement explicit unavailable-device placeholders preserving stable identity,
   state and routing, including AU-only projects; resolve VST3 by stable class
   identity, not Mac path hints. Do not silently replace unsupported devices.
4. Extend the single supplied outer-4 / snapshot-1 / metadata-14 Built-in demo
   qualification with licensed, disposable Mac-produced outer-5, snapshot-2,
   timing, unavailable AU, VST3, nonempty plugin-state and legacy fixtures.
   Perform Mac → tool/Windows → Mac reopen and compare semantic state, exact
   payloads and rendered audio; the existing demo pass is container-only.
5. Integrate Undo-aware document save/reopen, atomic destination replacement,
   recovery and error UI, then qualify actual Windows app paths. Existing Python
   tests do not establish any of those application behaviors.
6. Harden and resource-qualify the parser for hostile files. The input cap is not
   a decoded-memory/depth/object-count budget; `plistlib` materializes the tree,
   and output size is checked after serialization. No fuzzing, huge-file stress,
   or arbitrary binary-plist graph/duplicate-key preservation claim is made.

Keep these blockers explicit when coordinating the frontend/build work. Do not
label this foundation as native project Save/Open support in the C++ app.
