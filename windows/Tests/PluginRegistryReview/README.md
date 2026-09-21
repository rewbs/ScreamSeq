# Isolated VST3 registry regressions

Standalone ARM64/MSVC C++20 suite; it does not edit or build the lifecycle/editor
fixture sources. Supply an existing real `VST3Fixture.vst3` from the plugin suite,
preferably copied into private scratch storage. No installed plugins, user cache,
audio hardware, global input or clipboard access is needed.

```powershell
# From the repository; use the installed VS CMake/CTest full paths if necessary.
cmake -S windows/Tests/PluginRegistryReview -B bin/windows-registry-review `
  -G "Visual Studio 17 2022" -A ARM64 `
  -DSCREAMSEQ_VST3_FIXTURE=C:/absolute/private/VST3Fixture.vst3 `
  -DSCREAMSEQ_REGISTRY_TEST_ROOT=C:/absolute/private/registry-tests
cmake --build bin/windows-registry-review --config Release -j 2
ctest --test-dir bin/windows-registry-review -C Release --output-on-failure
```

Use a dedicated build directory and explicit `TMPDIR`, `TEMP`, `TMP` pointing to
approved scratch. Each case creates a PID/tick-suffixed data directory and leaves
it as evidence. The scanner is explicitly configured by absolute path; all fresh
reader subprocesses use a deliberately nonexistent scanner, and capacity fixtures
include missing retired modules to exercise metadata-only discovery.

`SCREAMSEQ_VST3_BACKEND_DIR` optionally selects a frozen copy of `WindowsVST3.cpp`
and its sibling headers, avoiding concurrent lifecycle edits. Registry, scanner,
module and pinned SDK identifier sources always compile fresh. This provider-only
harness needs no stale `TrackerHosted` or editor/core archives.

## Cases

- `capacity`: 2048 valid retired records plus explicit real-DLL scan rejects the
  2049th entry; exact old disk bytes and complete in-memory records remain intact.
  Replacement of an existing module at capacity succeeds without dropping any
  retired records. Both outcomes are compared through fresh-process discovery.
- `identity`: lowercase-only and case-colliding cache IDs, exact duplicates and
  malformed IDs reject. The actual canonical module creates successfully.
  Lowercase source-project descriptors remain accepted without source mutation.
- `invariants`: prospective machine, digest, path, class-ID, duplicate, name,
  class-count and serialized-byte violations reject before publication.
- `write`, `short-write`, `flush`, `rename`: deterministic Win32-call failure
  injection confined to this test translation unit's included `Registry.cpp`.
  The write probes actually write partial staging bytes. There is no production
  fault-injection API, build switch or environment variable.
- `locked`, `stage`: actual sharing-denied replacement and exclusive staging
  collision; preserve old data, do not remove another owner's stage, clean owned
  temporary files, and retry successfully in the same process.
- `concurrent`: pause immediately before local rename; a distinct bounded OS
  process atomically replaces the cache. Test subsequent local success and
  failure, exact disk readback, unchanged loaded snapshots and fresh readers.
  This documents owner-snapshot/last-writer semantics, **not** multi-app merging.
- `retarget`: actual DLL at an uncached path rejects despite identical bytes;
  changed DLL bytes under the cached path reject until explicit rescan. A live
  backend pins its private module against writes/replacement. No loader changes
  or comprehensive reparse-point/TOCTOU security claim follow from this probe.

## Observed red/green evidence

Before the fixes, `capacity` published 2049 records and reload threw
`VST3 cache count exceeds limit`; `identity` accepted a lowercase-only cache ID.
Injected write/short-write/flush failures exposed a further cleanup bug: deletion
ran before the exclusive handle closed, leaving a `.tmp` file. Rename failure and
actual sharing-denied replacement already preserved data and cleaned up.

After the fixes all 11 CTests passed on native Windows ARM64 / MSVC 19.44 using
fresh registry/scanner/module units and a frozen backend/real fixture copy.
This is functional cache qualification, not ASan, realtime, commercial-plugin,
application integration or exhaustive race qualification. See
`../../Plugins/RegistryCache.md` for the persistence and external-writer contract.
