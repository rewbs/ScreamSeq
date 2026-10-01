# Portable native metadata codec

## Integration contract

`windows/Project/NativeMetadata.hpp` exposes:

```cpp
namespace ScreamSeq::Project {
using Json = nlohmann::json;
Tracker::NativeSong decodeNativeMetadata(const Json &metadata);
Json encodeNativeMetadata(const Tracker::NativeSong &song);
}
```

Compile `windows/Project/NativeMetadata.cpp` and link `TrackerEditor` plus its
existing core dependencies. Add `include/nlohmann-json/include` to the include
path. The four private `NativeMetadata*.inc` files are included by this one TU.
No Foundation, Windows API, Python runtime, plugin hosting or DSP implementation
is introduced by this adapter.

The decoder accepts metadata versions **1 through 14**, rejects unsupported
versions, malformed known values and forbidden legacy fields, and tolerates
unknown dictionary keys at every level. It validates canonical native IDs,
identity uniqueness, reference integrity, collection capacities, string types
and UTF-16 lengths, numeric/boolean types and ranges. It invokes the existing
`compileSignal`, `SignalGraph::validate`, `MixerGraph::validate`,
`signalRoutingGraph`, `NativeNoteEffectSupported`, `CurveFormula` and
`validateEnvelopeShape`; it does not replace their musical algorithms.

The encoder **always emits version 14**, including all mandatory arrays and
objects, even for an empty/simple song. It checks that decoding its output
returns the exact model, rejecting invalid or conditionally unrepresentable
state instead of silently losing it. This is canonical known-field JSON, not a
lossless copy of the original dictionary tree.

**Mandatory loader sequence:** construct the document from its real matching
snapshot, decode metadata, and call `Document::restoreNative(decoded)`. Only
that shared operation can check actual pattern bounds, module-format support,
sample/instrument structure and materialized linked envelopes. The standalone
metadata adapter cannot prove snapshot compatibility and does not synthesize a
musical document to pretend otherwise.

The container owner must retain its original typed plist tree and merge unknown
fields separately. No unknown-preservation merge is implemented here.

## Version coverage

| Version | Introduced payload |
| --- | --- |
| 1 | Entity maps, sequences, nextID |
| 2 | Musical automation |
| 3 | Mixer buses and instrument outputs |
| 4 | Sidechains |
| 5 | Note tracks and column mutes |
| 6 | Mixer input balance |
| 7 | Parameter performance commands/bindings/columns |
| 8 | Pitch commands |
| 9 | Precise notes |
| 10 | Signal graphs and auxiliary effect outputs |
| 11 | Scripted musical automation |
| 12 | Per-note effects |
| 13 | Graph automation sources and instrument assignments |
| 14 | Envelope templates and target links |

The decoder intentionally enforces pitch/script/effect-output introduction gates
that the current Mac decoder does not consistently enforce, but that its encoder
and versioned format define. Empty `signalGraph.instrumentAssignments` remains
legal before version 13 because Mac emits that field for older graph metadata.
Unknown fields are tolerated; known forbidden fields are not treated as unknown.

## Build and test

The standalone harness imports the existing Release engine libraries; it does
not modify the parent Windows CMake file:

```text
cmake -S windows/Tests/Metadata -B bin/windows-metadata -G "Visual Studio 17 2022" -A ARM64
cmake --build bin/windows-metadata --config Release
ctest --test-dir bin/windows-metadata -C Release --output-on-failure
```

Override `SCREAMSEQ_ENGINE_LIB_DIR` if the existing libraries live elsewhere.
The test owns Documents on the heap, reserves an 8 MiB test stack and uses
throwing checks, not release-disabled `assert`.

Actual Mac qualification is explicit and hash-pinned:

```text
python windows/Tests/ProjectNative/export_metadata_fixture.py <reference.screamseq> <scratch>/native-metadata-fixture.json
cmake -S windows/Tests/Metadata -B bin/windows-metadata -A ARM64 -DSCREAMSEQ_METADATA_FIXTURE=<scratch>/native-metadata-fixture.json
ctest --test-dir bin/windows-metadata -C Release --output-on-failure
```

The Python helper exports **only** `native` metadata as the independent plistlib
oracle. It is test tooling, never part of the application. No project binary or
generated metadata fixture is added to Git.

Verified on native MSVC ARM64 Release (19.44.35229): **2/2 CTests passed**.
The opt-in fixture run reports **2111 throwing checks passed**. Evidence is in
`bin/windows-metadata/Testing/Temporary/LastTest.log`; the executable is
`bin/windows-metadata/Release/native-metadata-tests.exe`.

Coverage includes:

- Nonempty representative models for all 14 versions, canonical upgrade and
  shared Document restoration; malformed and unsupported version gates.
- All known fields populated: every graph node/command kind, all curve kinds,
  mixer kinds/routes, all performance kinds, note releases/cuts/effects, every
  envelope target kind and instrument markers including UINT32_MAX.
- Full model equality, stable canonical encoding, source-script whitespace,
  AU/VST3/Built-in identity, graph ports and arbitrary state bytes, all base64
  padding lengths, the 8 MiB state boundary and over-limit rejection.
- Null and incorrect-type mutation for every populated known field/member;
  optional defaults; nested unknown dictionaries; duplicate IDs/maps/cells,
  dangling references, numeric/string boundaries, malformed UTF-8/base64,
  invalid formula source, signed-zero floating-point preservation.
- Shared restore rejects actual row-bound and envelope-materialization mismatches
  without replacing the current native model.
- Actual Mac v14 metadata re-encodes with complete known-field JSON equality and
  exact model roundtrip. Original fixture hash remains
  `96ec9f809ef95a113d16613c29e73841e1f4a64ecc129672e5a48e548e587cd7`.

## Limits / parent integration still required

The supplied actual fixture has empty performance commands, graph state and
auxiliary-port/routing collections. Those paths are exercised by a separately
labelled generated model, not claimed as Mac provenance. No historical Mac
binary fixtures were supplied for versions 1–13.

The actual Mac snapshot is not extracted by this test oracle and is not restored
by this standalone metadata test. Real project codec/snapshot integration,
unknown-tree merge, actual Mac save/reopen, app feature exposure, plugin loading,
GUI/audio and sanitizers remain outside this adapter's verification. No app
metadata read/restore feature is exposed here.
