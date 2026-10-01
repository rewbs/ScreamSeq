# Windows asset operations qualification

`ScreamSeq::AssetOperations` uses the existing `Tracker::Document` and shared sample/envelope helpers. There is no Windows decoder, name-based retargeting, or alternate song model.

## Status — all four import methods enabled

`sample.import`, `sample.importMany`, `instrument.import` and `instrument.importMultisample` are advertised. The missing-external-SFZ gate is resolved by shared decoder fixes, not an alternate Windows importer. Full `--imports` is now registered in CTest rather than the formerly passing `--imports-safe` subset.

Editor-core SFZ imports reject missing, empty or decoder-rejected referenced audio (including the tested truncated Vorbis inputs), absent sample assignments, unknown generators, reversed key ranges, missing includes and exhausted sample capacity. Rejection propagates through the existing Document transaction/candidate before host validation or transport stop. Upstream OpenMPT's best-effort SFZ behavior remains outside `OPENMPT_EDITOR_CORE`.

## Integration contract

```cpp
AssetOperations(Tracker::Document &,
    std::function<void()> stopPlayback = {},
    std::function<void(const Tracker::Document &)> validateImport = {});
Json invoke(const std::string &method, const Json &params);
static std::vector<std::string> reads();
static std::vector<std::string> writes();
```

Construct/invoke on the document control worker. Cross-thread calls reject with -32003. The caller checks/removes `expectedRevision` and wraps the Mac `result.data` object with revision/history information. Recreate AssetOperations after replacing Document. Its clipboard/UUID belong to that document session.

The stop hook must not throw or mutate Document. The import-validation hook receives the fully imported candidate, including on dry runs, before stop. An empty validator explicitly represents **no external plugin state**. A host with plugin assignments must check real reserved instrument slots/collision/capacity and resource budgets; when unavailable, reject with -32601, not invented API parameters or assumed empty plugin state. Tests exercise this seam with fixture validation, not a real plugin host.

Imports stage through the existing shared Document importers on an exact snapshot plus restored native metadata. Complete decoding, format/identity/native checks and response construction happen before stop. Commit transfers the already-decoded asset allocations and instrument objects through one existing Document transaction. Files are never reopened after stop; tests remove sources in the stop callback. Unrelated patterns, orders and settings are not replaced. Effective identical single-slot replacements do not stop, revise, clear Redo or add Undo. Batch/multisample dry runs fully materialize and validate a candidate without touching the live document.

Import parameter/result shapes match TrackerSessionAPI.inc: single `path, slot?` returns `index` (no single-import dryRun); batch `paths, createInstruments?, dryRun?` returns `samples,count,dryRun`; multisample `name,samples:[{path,rootNote}],dryRun?` returns `instrument,zones,count,dryRun`. Batch order is retained; multisample roots sort through the shared implementation. Whole batches reject atomically.

## Advertised catalog (25 methods)

Reads: `sample.snap.get`, `sample.pcm.get`, `sample.clipboard.get`, `instrument.get`, `instrument.envelope.get`, `instrument.envelope.copy`.

Writes: `sample.patch`, `sample.process`, `sample.loops.set`, `sample.draw`, `sample.crossfade`, `sample.copyToNew`, `sample.pcm.set`, `sample.clipboard.copy`, `sample.clipboard.set`, `sample.cut`, `sample.delete`, `sample.paste`, `instrument.create`, `instrument.patch`, `instrument.envelope.transform`, `sample.import`, `sample.importMany`, `instrument.import`, `instrument.importMultisample`.

`sample.get` and `sample.waveform.get` remain owned by DocumentOperations. Instrument get/patch use numeric slots; envelope operations use canonical stable `n...` IDs. Existing DSP, envelope, PCM bounds and clipboard behavior are unchanged.

## Shared filesystem fixes

- `Document::importSample/importInstrument`: `std::ifstream(std::filesystem::u8path(path), ...)` rather than Windows code-page narrow filenames.
- Single-sample basename: existing `mpt::PathString::FromUTF8(...).GetFilenameBase().ToUTF8()`.
- Batch/multisample canonicalization: convert public UTF-8 to a filesystem path before `weakly_canonical`; serialize canonical names/error basenames through UTF-8.
- `ReadSampleFromSong` copies raw name bytes. Batch/multisample now transcode decoded UTF-8 document names to the destination's actual internal charset. CP437 legacy documents deliberately cannot retain Chinese names; tests assert the established conversion, not nonexistent Unicode support in legacy charsets.
- Single instrument FileReader already had the correct `PathString::FromUTF8(path)` filename. It is preserved and exercised with a real relative Unicode SFZ sample.

No SampleArchive, snapshot implementation, root CMake, Mac, app/controller, host or UI edits were made for this task.

## Reproduction

Use installed VS CMake/CTest on PATH, or their full paths under `C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/`. From Git Bash in the checkout:

```sh
export TMPDIR='C:/Users/P14/AppData/Local/hermes/cache/scratch'
export TEMP="$TMPDIR" TMP="$TMPDIR"
python -c 'import os; print({k:os.environ[k] for k in ("TMPDIR","TEMP","TMP")})'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File windows/build.ps1 \
  -Architecture ARM64 -BuildDirectory bin/windows-external-import-fix \
  -Target portable-tests -NoApp -Jobs 2 -Test
cmake -S windows/Tests/Assets -B bin/windows-external-import-assets -A ARM64 \
  -DCORE_LIBRARY_DIR=C:/Users/P14/code/ScreamSeq-windows/bin/windows-external-import-fix/Release
cmake --build bin/windows-external-import-assets --config Release --parallel 2
# Reassert/check these if any toolchain initialization changed the environment.
export TEMP="$TMPDIR" TMP="$TMPDIR"
ctest --test-dir bin/windows-external-import-assets -C Release --output-on-failure
bin/windows-external-import-assets/Release/asset-tests.exe --probe-import "$TMPDIR"
```

The asset harness compiles TrackerDocument.cpp and SampleImport.cpp directly, not stale imported objects. Core libraries were rebuilt in the separate `windows-external-import-fix` root build. Test Documents are heap-owned; executables reserve 8 MiB stack. Real ARM64 PE machine type is `0xaa64`. No Python runtime is used by the app or C++ tests; Python is used for synthetic fixture regeneration and process exit/hash evidence.

### Earlier passing evidence (before the decoder fix)

- `bin/windows-import-fix/portable-tests.log`: **24/24 portable core tests passed**. Functional-only; not a realtime allocation/lock audit or sanitizer claim.
- `bin/windows-import-assets/assets.log`: original **17 groups passed** (process, loops, draw, snap, crossfade, copyToNew, clipboard, pcm, patch, instrument, instrumentPatch, envelope, modelRoundtrip, processOptions, envelopeOptions, guards, mixSaturation).
- `bin/windows-import-assets/imports-safe.log`: **7 advertised-subset import groups passed**. CTest reports **2/2** executables/entries passed. Instrument-import API cases are explicitly excluded while its gate is closed; positive shared instrument-path tests still run.
- `bin/windows-import-assets/unicode-probe.log`: all **8 ASCII/UTF-8 shared entry-point combinations passed**.
- `bin/windows-import-assets/pre-gate-imports.log`: **9 groups passed before removing instrument.import from the catalog**, including all four real API import paths, ASCII/Unicode exact mono/stereo PCM, sample rates, basename/charset conversion, Unicode error basename, batch failure/dry run, keymaps, one Undo/Redo, no-op/Redo preservation, native ID/format/encoded file limits, host-validation ordering, source disappearance at commit, snapshot/nativeMetadata roundtrip, and actual ITI/XI/FLAC/SFZ fixtures.
- `bin/windows-import-assets/pre-gate/asset-tests.exe` and `manifest.json` preserve that ARM64 valid-input checkpoint with source/binary hashes. This binary is **not qualified for exposure**: the separate missing-external-file case fails.

Fixtures use independently written real WAV bytes. ITI/XI/FLAC files are generated by actual shared exporters from that PCM, with asserted names, loops and envelopes; SFZ is real text resolving a real Unicode WAV. These are generated regression fixtures, not claims of arbitrary third-party format fidelity or Mac-app interchange. Exact snapshot/nativeMetadata tests are not application project-container open/save, pipe/revision integration or audio-device qualification.

## Historical external-instrument blocker (resolved)

```sh
bin/windows-import-assets/Release/asset-tests.exe --probe-external-import
```

This creates a fresh disposable SFZ containing `<region> sample=absent.wav key=60` and imports through the shared Document API. With freshly rebuilt ARM64 core libraries, the real process exits **3221226505 / 0xc0000409**, not a C++ rejection. Evidence: `bin/windows-import-assets/external-import-failure.json`.

A temporary diagnostic CRT invalid-parameter handler plus linker map isolated the stack:

```text
stb_vorbis start_page -> start_decoder -> stb_vorbis_open_pushdata
CSoundFile::ReadVorbisSample -> ReadSampleFromFile -> ReadSFZInstrument
Document::importInstrument
```

The missing external file supplies an empty reader to the Vorbis probe. When the diagnostic handler temporarily allowed execution to continue, SFZ logged `Unable to load sample` but still returned success, accepting a partial/empty instrument. Thus merely preventing the CRT abort is insufficient: **the shared external instrument decoder must reject incomplete imports atomically as well**. Diagnostic handler/assert overrides have been removed; no suppression/workaround ships.

The follow-up decoder fix below resolves this blocker. The earlier binary/logs above remain historical red evidence, not the current qualified artifact.

## Shared decoder fix and current qualification

- `soundlib/SampleFormatVorbis.cpp`: reject empty/short/non-Ogg probes before opening the decoder; validate consumption before advancing pointers; reject no-progress truncated input; use RAII to close stb on every return; validate channel bounds before copying PCM. The safety changes are not editor-only.
- `soundlib/Load_mo3.cpp`: its two direct stb entry points had the same no-progress loop. Narrow prefix/consumption/progress guards cover separate/shared headers and retain best-effort module loading. No replacement MO3 importer was added.
- `soundlib/SampleFormatSFZ.cpp`: return failure instead of silently skipping incomplete regions/capacity in `OPENMPT_EDITOR_CORE` only. The shared Document already owns rollback; no duplicate transaction or DSP was introduced.
- Upstream author/license headers and vendored `include/stb_vorbis/stb_vorbis.c` are unchanged. No CRT/assert suppression, SEH catch or fabricated sanitizer result.

Evidence lives in `bin/windows-external-import-assets/`:

| Evidence | Actual result |
| --- | --- |
| `red-external.json`, `red-vorbis.json` | Original missing SFZ and direct empty Vorbis each exited 3221226505 / `0xc0000409` |
| `green-empty-red-sfz.json` | Empty-reader fix passed; missing SFZ then failed normally because it was wrongly accepted |
| `red-sfz-matrix.log` | 33/48 initial incomplete-SFZ assertions failed before strict rejection |
| `red-truncated.json`, `red-mo3.json` | Actual truncated Vorbis/MO3 children each timed out at 15 seconds before progress guards |
| `red-capacity.log` | All six over-capacity SFZ cases were incorrectly accepted before the capacity guard |
| `red-api-gate.log` | Full import suite failed while `instrument.import` remained unadvertised |
| `portable-tests.log` | Fresh ARM64 build: **24/24 portable tests passed** |
| `ctest.log` | **6/6 asset/decoder CTest entries passed**, including full imports |
| `assets.log`, `imports.log`, `unicode-probe.log` | **17 existing groups**, **9 full import groups**, **8 ASCII/UTF-8 probes** passed |
| `external-shared.log`, `external-api.log` | **66 shared + 66 API cases passed**: invalid-only, valid-first/invalid-later, invalid-first/valid-later; append and replace; exact snapshot/native IDs/PCM, revision, Undo/Redo and history-byte preservation; API rejection before validation/stop |
| `vorbis.log` | Empty/fallback/non-Ogg and **18 truncated probes** rejected without changing existing PCM; real generated Vorbis decoded 4800 mono frames at 24000 Hz |
| `mo3-vorbis.log` | **8 generated MO3 cases** passed, including actual valid PCM and empty/truncated data through direct/shared-header paths |
| `qualification.json`, `manifest.json` | Captured exits/counts, source/library/executable SHA-256, native/PE ARM64 `0xaa64`, 8 MiB stack and unchanged vendored source |

The actual linked libraries are `bin/windows-external-import-fix/Release/{OpenMPTCore,TrackerEditor,TrackerFLAC}.lib`; the qualified test executable is `bin/windows-external-import-assets/Release/asset-tests.exe`. The harness compiles current TrackerDocument/SampleImport directly and links the freshly rebuilt decoder library. Documents/decoder fixtures are heap-owned. CTest imposes a 60-second child timeout. Failed-fixture directory names include a time/collision suffix because Windows can reuse a crashed probe's PID.

The positive SFZ regression resolves two relative Unicode WAVs with different channel counts/rates, removes the SFZ and both WAVs in the stop callback, then verifies imported PCM and native snapshot restoration. Existing valid WAV/FLAC/ITI/XI, Undo/Redo, no-op, basename/charset and host-validation tests remain green.

### Limits and fixture provenance

`Fixtures/mono-24000.ogg` is independently generated test audio: 4800 samples of a 440 Hz sine at 24000 Hz, encoded with ffmpeg/libvorbis by `GenerateVorbisFixture.py`. No third-party song or sample pack is used. The C++ test writes minimal literal-coded MO3 containers around those bytes. Python/ffmpeg are regeneration/evidence tools, not runtime dependencies. Expected warning logs in the negative SFZ/MO3 cases are retained.

This is supported-opcode/keymap conversion, **not arbitrary SFZ fidelity**. Unsupported SFZ opcodes, velocity layers, round-robin behavior and independent per-region instrument envelopes are not newly implemented or qualified; overlapping keyboard assignments retain the existing conversion semantics. The generated fixtures do not establish arbitrary third-party format fidelity, full hostile-input validation or Mac application interchange. The libvorbisfile backend and complete upstream OpenMPT UI build were not run; non-editor SFZ preservation is a guarded source change, not an upstream GUI qualification. No audio device, global desktop state, sanitizer or realtime allocation/lock audit was exercised. Parent review and host/pipe integration remain separate.
