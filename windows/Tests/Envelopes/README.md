# Envelope operations qualification

`EnvelopeOperations` implements the Mac result-data contract on the real shared
`Tracker::Document`. It owns no UI, audio device, plugin instances or application
preferences. Only this directory and `windows/Session/EnvelopeOperations.{hpp,cpp}`
are part of this change.

## Integration

```cpp
EnvelopeOperations(
  Tracker::Document &,
  std::function<void()> stopPlayback = {},
  EnvelopeHostHooks host = {},
  std::optional<std::filesystem::path> cataloguePath = {});
Json invoke(const std::string &method, const Json &params);
static std::vector<std::string> reads();
static std::vector<std::string> writes();
```

- Reads: `envelope.bank.list`, `envelope.catalogue.list`.
- Writes: `envelope.bank.save`, `envelope.bank.remove`, `envelope.bank.apply`,
  `envelope.bank.unlink`, `envelope.catalogue.publish`, `envelope.catalogue.import`.
- The caller must check/remove `expectedRevision` and construct the outer API
  response. A request still containing that field is rejected, not silently
  ignored. Draft/target capture and UI refresh guards remain the caller's job.
- Both host hooks have signature `bool(const std::string &, uint32_t)`.
  `parameterAvailable` resolves an actual persistent plugin instance/parameter;
  `parameterAutomationConflicts` reports conflicting host time automation.
  Both are required when creating a new parameter lane. Missing hooks reject;
  existing lanes and graph/instrument targets work without a live host. Native
  performance-command conflicts are also checked. Hooks run only after the
  complete musical candidate validates, including shared fitting/baking.
- Stop playback only after validation, immediately before a changed non-dry song
  commit. Callbacks must not mutate Document; the stop hook must not throw.
- Song edits use `annotate` or (for baked instruments) `transaction`. Master
  updates reach every linked use atomically; independent copies stay independent.
  Shared `EnvelopeBank` fitting, instrument baking and validation remain the
  musical authority. New song identities use `NativeSong::makeEntity`.
- Catalogue publication never changes song revision/history or stops playback.
  Import creates a fresh song identity and one document Undo, never a live link.
- Link `owner` and graph `pattern` fields contain stable native IDs; public target
  pattern indices are resolved before staging an edit. Release unset is the
  integer `4294967295`, never null. Formula compilation uses shared `CurveFormula`
  (2048 source bytes, 128 operations), not runtime eval.
- Link `EnvelopeOperations.cpp`, `Project/BinaryPlist.cpp` (or the project library),
  matching shared editor/core libraries, and Windows `bcrypt`/`ole32`. Production
  operations do not require `NativeMetadata.cpp`; the persistence test does.

## Catalogue storage and safety boundary

The constructor takes an **explicit absolute file path**. There is no implicit
per-user location or environment lookup in production. Omit it to disable all
catalogue operations in an inspection session. The parent host chooses the normal
per-user path and registers methods with its dispatcher/describe response.

`mac/Bridge/EnvelopeCatalogue.inc` is the authoritative file contract: UTF-8 JSON
in `envelope-catalogue-v1.json`, root fields `version:1`, `revision`, `entries`,
and entry fields `id`, `name`, `shape`. Encoding selection follows **bytes, never
the filename extension**:

| Existing destination | Read / changed publication |
| --- | --- |
| Missing file | Empty view; the first actual publication writes UTF-8 JSON |
| UTF-8 JSON | JSON in, JSON out |
| `bplist00` | Binary plist in, binary plist out (legacy preservation) |
| Malformed/unsupported content | Reject without replacement or migration |

No explicit back-compat switch is needed. An existing binary catalogue is not
silently converted; a missing `.plist` path still gets JSON. The parent should
use the authoritative `.json` name for new normal catalogues, not invent a
separate incompatible location. New IDs and stored generation tokens retain the
existing canonical GUID helper. Public revisions remain `catalogue:` plus the
SHA-256 of **actual file bytes**, using `mpt::crypto::hash::SHA256`/BCrypt; missing
files return `catalogue:0`. Treat tokens as opaque. The stored Mac generation
string is read intact, untouched on no-op/dry-run, and regenerated on a changed
publication; it is not the public optimistic-concurrency token.

Complete unknown root values survive replacement of known entries. JSON retains
objects, arrays, null, booleans, Unicode strings and numeric types; binary plists
retain subtype-free data and opaque date/UID bytes. Unknown entry, shape and point
fields reject before replacement rather than losing nested data. **Unknown JSON
root fields are deliberately more permissive than the current Mac strict root
reader; preservation is NOT evidence that Mac accepts these future fields.**

JSON uses signed/unsigned 64-bit integers and finite binary64 reals. An oversized
integer, non-finite number or nonzero value underflowing to zero rejects before
DOM construction. Integer/real distinctions and floating-point bits (including
negative zero) survive changed publication. Bare `-0` is represented as `-0.0`
because an integer subtype cannot retain its sign. Ordinary `0` stays integer.
Decimal reals have binary64 precision, not arbitrary precision. Whitespace,
key ordering, escaping and numeric spelling can change on a changed publication;
conditional no-ops preserve **exact bytes and mtime**.

Before any JSON DOM materialization a strict SAX pass rejects duplicate decoded
keys (including escaped-key collisions), malformed UTF-8/surrogates, invalid JSON
and budget excess. Bounds: 16 MiB input/output/list response, 256 entries, 250000
values including keys, 65536 members/elements per ordinary container, depth 128
(root = 1, keys count), and a 128 MiB cumulative allocation-work ledger. The JSON
ledger charges 512 bytes per value/key, four times decoded string bytes and twice
input bytes for parser work; it is conservative, not an exact RSS cap. The lexer
still has an individually input-bounded token buffer. Binary plists retain the
existing codec's object/depth/expansion budgets with a 128 MiB ledger. A counting
stream checks JSON output size before allocation; a second bounded serialization
and SAX preflight ensure a changed publication can be reopened by this reader.
No Python or Foundation dependency is added to the production path.

A nonblocking Windows named mutex is keyed by the SHA-256 of the canonical,
invariant-lowercased UTF-16 path, under
`Global\\org.resonance.tracker.envelope-catalogue-v1.`. This retains the legacy
application namespace, shares ownership across processes/sessions, and does not
create lock files or directories on reads, dry runs, stale requests or no-ops.
Atomic publication reuses `ProjectIO.hpp` (exclusive staged file, flush and
same-directory replacement). Busy returns API error -32002; validation/stale
catalogue errors return -32602. Filesystem failures preserve their system error.
All participating writers must use this lock protocol; it cannot prevent an
uncooperative external process from editing a file after validation.

**Qualification limit:** no Mac-produced catalogue was supplied. Tests use
schema-conformant generated JSON and an independent Python `json` oracle, plus
generated legacy binary data and `plistlib`. This establishes file-format support
against the authoritative schema, not actual Mac application reopen. No native
app/socket/UI integration, real plugin resolution, audio or hardware qualification
is claimed. The named mutex/DACL/path scope, GUID helpers, eight public method
signatures and all song-bank musical behavior are unchanged.

## Build and run (Git Bash, ARM64 MSVC)

From the checkout root, using the installed Visual Studio CMake:

```bash
export TMPDIR='C:/Users/P14/AppData/Local/hermes/cache/scratch'
export TEMP="$TMPDIR" TMP="$TMPDIR"
printf 'TMPDIR=%s\nTEMP=%s\nTMP=%s\n' "$TMPDIR" "$TEMP" "$TMP"
CMAKE='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin'
"$CMAKE/cmake.exe" -S windows -B bin/windows-envelope-json/core \
  -G 'Visual Studio 17 2022' -A ARM64 -DSCREAMSEQ_BUILD_APP=OFF \
  -DSCREAMSEQ_BUILD_RENDER_PROBE=OFF -DBUILD_TESTING=OFF
"$CMAKE/cmake.exe" --build bin/windows-envelope-json/core --config Release \
  --target TrackerEditor --parallel 2
"$CMAKE/cmake.exe" -S windows/Tests/Envelopes -B bin/windows-envelope-json \
  -G 'Visual Studio 17 2022' -A ARM64 \
  -DSCREAMSEQ_ENGINE_LIB_DIR=C:/Users/P14/code/ScreamSeq-windows/bin/windows-envelope-json/core/Release
"$CMAKE/cmake.exe" --build bin/windows-envelope-json --config Release --parallel 2
"$CMAKE/ctest.exe" --test-dir bin/windows-envelope-json -C Release --output-on-failure
```

`SCREAMSEQ_ENGINE_LIB_DIR` defaults to `bin/windows-editor/Release`; override it
only with matching rebuilt libraries. The build imports the real `TrackerEditor`,
`OpenMPTCore` and `TrackerFLAC` archives, compiles the current binary-plist and
native-metadata codecs, and gives the executable an 8 MiB stack. Documents are
heap-owned. Checks throw in Release/NDEBUG builds rather than disappearing.

The original eleven scenarios still cover:

- bank creation/readback, dry runs, no-op history and Undo/Redo preservation;
- linked parameter/graph/instrument master updates and independent copies;
- unlink preserving materialized points and linked-template removal rejection;
- strict types, IDs, UTF-8, unknown fields, formula limits and fit collisions;
- host hook absence, real callback ordering, unavailable/conflicting parameters;
- shared instrument bake equivalence, every-tick half-unit error, IT/XM point
  budgets, volume/pan/pitch, XM pitch rejection and atomic failure;
- pattern refitting, independent application, fresh clone IDs and link pruning;
- 4096-link preflight rejecting an extra instrument link before stopping playback;
- catalogue copy isolation, fresh imports, stable UUIDs, history separation,
  byte/mtime-preserving no-ops and unknown typed root preservation;
- same-size/same-mtime external changes, stale guards, malformed/oversized files,
  256-entry capacity, real second-process busy/successful writers, Unicode paths,
  forced atomic-replacement failure, original preservation and staging cleanup;
- native metadata plus matching real snapshot persisted as an isolated typed
  plist and restored into a heap Document; bank links, a long Unicode title and
  sparse pattern allocation survive reopen and instrument-update Undo/Redo.

The latter is a model/snapshot/metadata test wrapper, not a claim to qualify the
parent's complete native project container or Mac application reopen. Temporary
catalogues are unique children of `TMPDIR` and removed by their test owner.

Additional scenarios cover JSON read/import/publish, default JSON regardless of
suffix, strict parsing and all resource budgets, output re-readability, bounded
list responses, stale same-size/same-mtime external rewrites, read-only import and
missing-file safety. Both JSON and binary-plist writers exercise real child
process contention (exact, dot-alias and ASCII-case-alias paths), publication and
forced replacement failure. The independent oracle checks all nine curves,
scripted formulas, Unicode paths/names and complete typed unknown roots: integer
limits, binary64 extrema, real/integer distinctions, bare/real negative zero,
null/booleans/nested values, and legacy data/date/UID values. No-op and dry-run
checks preserve original bytes/mtime before a changed entry is independently
reopened. Python is required only to run that test.

Each executable invocation, including child writers, validates all three temp
environment variables point to the same Hermes `cache/scratch` root before
creating fixtures. No user catalogue, preferences, clipboard, network, audio
device or running app is used. Test documents remain heap-owned with an 8 MiB
executable stack reserve. Build products/logs live in `bin/windows-envelope-json`;
`verification.json` records executed results and exact source/library/executable
hashes. The standalone build compiles current codec sources; matching shared
libraries are rebuilt separately, not inferred from old binary timestamps.

Observed RED regressions before their corresponding fixes: unchanged original
catalogue code rejected JSON read/import/publish (`Binary plist: header`) and
defaulted to binary; an initial JSON reader accepted duplicate keys/deep input;
an initial writer could publish a value-budget-exceeding file that would not
reopen; bare `-0` lost its sign/type; a tiny nonzero real underflowed to zero.
The build directory retains the corresponding `red*.log` files.
