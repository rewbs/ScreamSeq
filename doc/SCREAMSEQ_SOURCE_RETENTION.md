# ScreamSeq source dependency boundary

This records the dependency audit for the independent repository cleanup on
2026-10-01. The cleanup removes unused upstream applications and infrastructure
from the current tree while retaining their commits in Git history. It does not
rewrite the shared playback engine or change native project data.

## Retained sources

| Paths | Reason |
| --- | --- |
| `editor/`, `mac/`, `windows/`, `assets/branding/` | Shared ScreamSeq editing/hosting, native frontends, native tests and application assets |
| `common/`, `soundlib/`, `sounddsp/` | Module loading, playback, saving, sample formats and plugin compatibility |
| `src/mpt/` and remaining `src/openmpt/` | Portable support, platform/architecture branches, audio conversion and WAV writing |
| `tracklib/SampleEdit.cpp`, `tracklib/SampleEdit.h`, `tracklib/Types.h` | Shared sample editing used by `TrackerDocument` |
| `include/flac/` | FLAC sample import/export; Windows also builds its UTF-8 file I/O helper |
| `include/miniz/`, `include/minimp3/`, `include/stb_vorbis/` | Compressed module/sample data, MP3 and Vorbis decoding |
| `include/r8brain/` | Sample resampling in `SampleEdit` and the native sample clipboard |
| `include/nlohmann-json/` | Windows API/project data and portable Windows tests compiled on macOS |
| `include/pugixml/` | Windows plugin preset parsing |
| `mac/ThirdParty/vst3/` | Pinned VST3 interfaces shared by both native plugin hosts and scanners |
| `build/svn_version/svn_version.h` | Fallback included by `common/version.cpp`; retained at its upstream path |
| `test/test.flac`, `test/test.mod`, `test/test.mptm`, `test/test.s3m`, `test/test.xm` | Native import/round-trip/sample tests and reference-render fixtures |
| `LICENSE`, retained vendor license files and source notices | OpenMPT and third-party attribution |

The engine/support trees are retained conservatively. A header absent from one
macOS dependency file may still be needed by Windows, another architecture or a
module format. OpenMPT namespaces, version constants and historical Resonance
storage identifiers remain compatibility contracts.

## Removed upstream-only groups

- Applications and integrations: `mptrack/`, `openmpt123/`, `pluginBridge/`,
  `libopenmpt/`, `examples/`, `contrib/`.
- Legacy packaging/build infrastructure: root `Makefile`, `.appveyor.yml`,
  `installer/`, `packageTemplate/`, upstream GitHub workflows and `build/` except
  the version-header fallback above. ScreamSeq's two native workflows remain.
- Unused platform services: `misc/`, `unarchiver/`,
  `src/openmpt/sounddevice/`, `src/openmpt/streamencoder/`.
- Unused sample stretch implementation: `tracklib/TimeStretchPitchShift.cpp`
  and `tracklib/TimeStretchPitchShift.h`.
- Upstream test drivers in `test/`; the five fixtures above remain.
- Unused bundled dependency directories under `include/`:
  `SignalsmithStretch`, `ancient`, `asiomodern`, `cryptopp`, `lame`, `lhasa`,
  `mpg123`, `ogg`, `opus`, `opusenc`, `opusfile`, `portaudio`, `premake`,
  `pthread-win32`, `rtaudio`, `rtkit`, `rtmidi`, `unrar`, `vorbis`, `zlib`.

Both native CMake builds compile the engine with `LIBOPENMPT_BUILD` and
`OPENMPT_EDITOR_CORE`, plus `MPT_WITH_MINIZ`, `MPT_WITH_MINIMP3`,
`MPT_WITH_STBVORBIS` and `MPT_WITH_FLAC`. References to the removed OpenMPT UI,
legacy VST bridge and utility code inside retained engine files are guarded by
inactive `MODPLUG_TRACKER`/`MPT_WITH_VST` branches. Archive support is disabled by
the native engine configuration. The native hosts use their own Core Audio,
WASAPI, AU and VST3 implementations. Keep the engine guards intact; enabling an
old upstream application configuration is outside this repository's build scope.

## Independent playback reference

`mac/build-reference.sh` extracts original OpenMPT commit
`f83cedb0cd5446e4dfaa83ac97e3087107e26767` from the preserved local Git history.
Its full source, fixtures and licenses live under the selected build directory's
`reference-source/<commit>/`, outside the tracked application sources. The helper
uses that archive's Makefile and headers, builds its stock library and test
executable, runs `libopenmpt_test` from the archive, and links the separate
reference renderer. It does not download sources or restore removed directories
to the working tree. A shallow clone lacking the commit receives an explicit
upstream fetch command.

`mac/test.sh` invokes the helper and preserves the native versus stock audio
comparisons. `SCREAMSEQ_BUILD_DIR`/`SCREAMSEQ_BUILD_JOBS` select the disposable
output directory and parallelism; the `RESONANCE_*` aliases remain supported.
Reference revision, library/renderer hashes and stock-test status are recorded
in `reference-source.json` alongside the build/test logs. The helper recreates its
disposable archive directory on each run, excluding both modified files and
additional sources/configuration from earlier reference builds.

## Audit evidence and qualification boundary

The audit inspected both native CMake builds, the Windows VST3 provider and
portable tests, Mac build/packaging scripts, engine compile guards and fixture
references. It also resolved 1,012 existing macOS CMake dependency files: none
depended on `mptrack/`, `pluginBridge/`, `misc/`, `unarchiver/`, `libopenmpt/`,
the upstream sound-device layer or stream encoders. The version-header fallback
was their only dependency under `build/`.

Those dependency files corroborate the source audit; they are historical build
artifacts, not proof that the cleaned tree currently passes. A clean native build,
native regressions and the separately compiled stock comparison are the cleanup's
verification steps. macOS results do not qualify the Windows executable; the
Windows workflow remains the platform-specific build/test gate.

The local cleanup check rebuilt the pinned stock library and passed its
`libopenmpt_test` suite after removing the current-tree upstream build sources.
Fresh `bin/mac-repo-cleanup` core tests passed the native editing/round-trip checks
and all four module fixtures. All 30 reference renders at 44.1, 48 and 96 kHz,
including later-order and two-sequence cases, matched every finite sample and
frame count exactly. The render audit reported zero intercepted allocations,
deallocations and locks. Logs, result JSON, pinned-reference metadata and executable
hashes are saved locally under `artifacts/repo-cleanup/`. These were offline
correctness checks during a concurrent build; callback durations do not establish
a performance or presentation pass.

## Cleanup verification (2026-10-01)

The fresh macOS build in `bin/mac-repo-cleanup` compiled all C++ targets after
removal. Its full CTest suite passed **89/89** tests. The pinned upstream test
suite also passed. All **30** stock-versus-native PCM comparisons at 44.1, 48 and
96 kHz matched exactly, including later-order and multi-sequence fixtures, with
zero intercepted allocation, deallocation or lock calls. These offline checks
ran alongside compilation and do not establish display or callback performance.

The cleanup removes **6,868 tracked files (about 174 MiB)** from the working
tree. Git history is preserved, so this is not a claim of a smaller historical
Git database. Literal source references in both native CMake trees were checked
after deletion; none was missing. The root BSD license is unchanged.

The preceding checkpoint passed Windows native CI. Its macOS CI exposed a Swift
compiler type-checking limit in the source-scope picker; replacing a chained
collection expression with a typed array and simple loops preserves the picker
contents and ordering. The cleaned tree's native platform workflows run on
`main`; their results remain separate from local macOS/portable-test evidence.

The final macOS application also built and passed strict bundle signature
verification, the actual-app socket/API suite, recovery tests, plugin-picker
tests and sample-library tests. The API suite ran without windows or audio
output. Build/test logs and oracle provenance are in the ignored local
`artifacts/repo-cleanup/` directory; fresh CI results are published by the two
native GitHub workflows. Sustained UI/audio performance was not requalified by
this repository-only cleanup.
