# Native ARM64 Windows VST3 provider

See [current installed-plugin qualification](../UPSTREAM_PLUGIN_QUALIFICATION.md)
for Contourtonist/OrbitCab evidence, dynamic latency support, initial JUCE mapping
notification handling and the retained OrbitCab offline failure. Provider checks
do not establish the pending application rack/editor history workflow.

This is a platform provider for the existing `editor/hosted/PluginBackend.hpp`.
It does not replace the shared scheduler, tracker DSP, graphs, project records,
or the Windows application's document/state bridge. No app integration files
are owned by this directory.

## Integration (parent-owned application)

Define `TrackerEditor` first, then:

```cmake
include("${ROOT}/windows/Plugins/Provider.cmake")
# Sets TRACKER_HOSTED_BACKEND_SOURCES. Do not also compile UnavailableBackend.
add_subdirectory("${ROOT}/editor/hosted" hosted)
screamseq_windows_vst3_target(TrackerHosted)
target_link_libraries(YourApp PRIVATE TrackerHosted)
add_dependencies(YourApp ScreamSeqVST3Scanner)
```

`Provider.cmake` compiles the private sources plus the unchanged vendored SDK
identifier sources and existing `windows/Project/BinaryPlist.cpp`. It supplies
private SDK/JSON include paths and user32/ole32/bcrypt linkage. Build ARM64 with
MSVC C++20. Package `ScreamSeqVST3Scanner.exe` beside the app and package
`VST3-NOTICES/` with its license and revision pins. Never package/install the test
fixture modules into user plugin folders. There is exactly one
`Tracker::platformPluginBackendFactory()`; both rack and independent graph
instances reach it through the same shared `NativePlugin` facade.

### Discovery and identity

`WindowsVST3.hpp` is the application-facing private-platform API:

- `configure(scannerExecutable, cacheFile)`: optional absolute UTF-8 paths,
  before using the factory, on the single stopped/control owner. Default scanner
  is beside the executable; default cache is
  `%LOCALAPPDATA%/ScreamSeq/vst3-arm64-cache.json`.
- `factory.discover()` reads **only the persistent cache**. An absent cache is
  empty. A malformed cache is an error, not a reason to execute installed code.
  Do not invoke `discoverVST3` on startup to populate a menu.
- `defaultSearchRoots()` and `candidates(roots)` only enumerate the filesystem.
  Enumeration is bounded and does not follow symlink directories. Present
  candidate paths/scan failures to the user; do not imply unscanned compatibility.
- `rescan(absoluteUTF8Path, timeoutMs=5000)` explicitly executes one isolated
  scanner child and atomically replaces that module's cached record on success.
  `factory.discoverVST3(path)` is this **explicit rescan** operation too. Failure
  leaves previous cache records intact. A parent bulk-rescan UI should retain
  individual errors and honor cancellation between modules.

A scan record binds canonical Windows binary path, PE ARM64 machine, SHA-256,
canonical SDK FUID string, name and instrument classification. Creation checks
all identity fields against the cache and current binary. Exact 32-character
hexadecimal validation happens **before** `FUID::fromString`. FUID strings are
not raw Windows GUID byte hex. No name fallback, foreign Mac path retargeting,
implicit rescan or silent substitute occurs. A Mac recipe requires an explicit
user-authorized installation resolution in the parent; this backend never
changes the source recipe. AU creation throws and the document must retain its
entire recipe and opaque state.

Accepts an absolute DLL-style `.vst3` file, or a bundle with exactly one binary
under `Contents/arm64-win`. AMD64/x64 and ARM64EC binaries are rejected; there is
**no x64 bridge**. The loaded binary is pinned against writes/deletion. Loading
uses `LoadLibraryExW(LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
LOAD_LIBRARY_SEARCH_SYSTEM32)`, never the current-directory DLL search.
UTF-8 paths are validated and converted to UTF-16, including file/hash reads.
Live instances share a weakly cached module on the private STA: InitDll/factory
acquisition once, distinct components/controllers per instance, then final
factory release, ExitDll and FreeLibrary. Dependency DLL hashes are not included
in the scan fingerprint.

The scanner uses a kill-on-close job, one-process limit, restricted inherited
handles, hidden/no-console startup, 1 MiB output bound and 50..60000 ms timeout.
Crashes, malformed responses, output flooding and changed identities fail closed.
This is crash containment, **not a security sandbox for hostile native code**.
The scanner does not instantiate processors or open editors merely to enumerate
classes. Cache writes are control-owner serialized, not a multi-app merge service.

## Ownership, rendering and state

All module/component/controller initialization, teardown, state, program and HWND
operations run on a private COM STA with its own Win32 message loop. Callers may
be document/control workers; do not add their own COM/UI access to this backend.
Host application, handler/frame, streams, event lists and parameter queues honor
SDK queryInterface/addRef/release ownership. Separate controllers connect in both
directions; partial failures unwind initialized/active/processing objects.

**The caller must stop and join rendering before state/program changes or
instance destruction.** Do not call the synchronous UI-owner bridge on an audio
callback. State export flushes pending offset-zero control edits. Rendering,
parameter queues, MIDI, transport and `popEdit` do not marshal to the STA.
Editor gestures have independent fixed-capacity audio and commit-value SPSC
queues. The editor's UI timer synchronizes atomic values back to its controller.
A handler invoked from the wrong thread fails/faults rather than calling UI from
the audio thread. Editor creation uses SW_SHOWNOACTIVATE but remains clickable;
view attach/remove/frame resize and native WM_CLOSE own the view lifetime.

The shared README remains the authoritative provider contract. This adapter:

- prepares all bus/audio/event/parameter storage before activation;
- advertises sample-offset parameters, retaining **double** normalized 0..1
  queue points (32 points per ID, 512 IDs/block), replacing same-offset points;
- consumes/resets input and output queues each slice, accepts frames <=4096,
  copies sidechain inputs at the whole-block input offset, returns auxiliary
  outputs starting at zero, and uses the Mac mono/stereo conversion rules;
- requires stereo main buses, supports mono/stereo enabled auxiliaries 1..63,
  retains inactive bus metadata and rejects nonexistent/duplicate activations;
- publishes current transport before MIDI, supplies current VST process context,
  emits note on/off/poly pressure and mapped CC/bend/aftertouch, tracks channels
  for all-notes/all-sound-off, and does not invent another musical clock;
- consumes bounded output parameter changes into value shadows and validates/
  consumes output events. **The shared facade has no MIDI-output routing API**:
  output MIDI is deliberately not routed or fed back to the same processor;
- reports prepared latency/tail in seconds and stable Mac-style unit/program IDs;
- faults/silences on queue overflow, invalid output and processing failure.
  Structural restartComponent notifications (latency/buses/reload and other
  metadata changes) latch a fault. Stop and recreate on the control owner;
  continuing with stale prepared buffers is not supported.

There is no additional backend stop/resume API in the shared interface: stopping
means the caller stops processing; resumed calls reuse the prepared instance.
For a reset or structural change, destroy/recreate off the callback. Vendor DSP
state intentionally survives a pause. Destruction calls setProcessing(false),
setActive(false), disconnect, controller/component terminate/release, then module
shutdown. Runtime vendor DSP/UI hangs and access violations remain **in-process**
and can hang/crash the app; UI calls are not hard-time-bounded.

Saved state is the Mac binary-plist dictionary with `component` and `controller`
ordinary DATA streams, not a new wrapper. Unknown inner dictionary values are
retained when those two streams are updated. Opaque typed plist binaries are
rejected as streams. Stream seek/arithmetic and reads/writes are bounded; outer
state is capped at 16 MiB with explicit parser depth/object/allocation budgets.
This uses the existing C++ binary-plist codec; **no Python runtime** is required.

## Qualification boundaries

See `../Tests/Plugins/README.md` for actual DLL/PCM/HWND tests and commands.
These tests do not qualify commercial plugins, full AU/Mac parity, integrated
application UI/API/persistence, WASAPI/system audio, sanitizer coverage, or a
complete malloc/free/lock realtime audit. No zero-allocation claim is inferred
from the deterministic fixture. The parent must finish app integration and its
own native UI qualification without replacing a musician's running session.

## Provenance and licensing

The backend is adapted from `mac/Audio/VST3Host.mm`; the fixture DSP is adapted
from `mac/Tests/FixtureVST3.mm`. Original repository/OpenMPT/ScreamSeq attribution
and licensing remain applicable. Shared scheduling/DSP sources and vendored SDK
sources were not edited by this provider work.

The existing MIT Steinberg interfaces are pinned by
`mac/ThirdParty/vst3/REVISIONS.json`: pluginterfaces
`4f547e8e102b47de4a8b8aaf343c73b700786372`, public.sdk
`586dc5e6c8012c3e4b01c79389375cbe96bdb1da`.
Retain the included MIT copyright/license text in every binary deliverable.
