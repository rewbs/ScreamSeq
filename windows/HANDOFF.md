# Windows native development handoff

**Paused at the user's request. Read `PAUSED_HANDOFF.md` first.** It supersedes
historical completion/status claims below. Do not resume on delayed background
notifications. Current work is uncommitted and not approved for users' songs.

**The screenshot-guided workspace continuation is documented in `WORKSPACE_CHECKPOINT.md`.
That report supersedes the paused UI state below.** The Mac reference pack has now
been supplied, including the actual disposable `reference.screamseq` fixture.
The older `native-bootstrap` executable and all pre-existing shared patches remain
preserved. No commit or push is authorized.

## Historical bootstrap checkpoint (retained evidence)

## Where the work is

- Worktree: `C:\Users\P14\code\ScreamSeq-windows`
- Branch: `codex/windows-native`, based on verified repository default
  `codex/screamseq` at `b6c62a57970e647e54158575dd69f893d63d454a`.
- Original `C:\Users\P14\code\ScreamSeq` checkout remains untouched.
- Work is **uncommitted**. `windows/` is new/untracked; do not discard it.
- No commits, pushes, history rewrites, default audio-route or master-volume changes.
- No Mac platform sources or persisted identifiers changed.

## Runnable artifact

Development executable: `bin/windows-arm64/Release/ScreamSeq.exe`.
This is a **demo-only native playback/inspection host**, not a finished tracker
or an editing release. Its current layout is scaffolding, not the final UI design.
The GUI explicitly marks unavailable features. It starts stopped.

```powershell
.\bin\windows-arm64\Release\ScreamSeq.exe
# Never opens audio hardware; enables explicit per-process local API:
.\bin\windows-arm64\Release\ScreamSeq.exe --inspection --automation
```

Space plays/stops the shared demo at an explicit -20 dB monitor attenuation;
Escape stops. Arrows/Tab navigate a separate cursor; F toggles playback-follow.
Native Play/Stop/Follow buttons exist. The sample waveform is read-only.
No native project opening/saving, pattern editing, plugin hosting, MIDI, docking,
command palette or graph/automation editor is exposed. Do not open a user's song
in this build or describe the placeholder panels as functional editors.

## Implemented and verified scope

- Native ARM64 MSVC/Win32 executable, Direct2D/DirectWrite over D3D11/DXGI flip
  rendering; actual window captured and visually inspected.
- ScreamSeq icon derived from the existing branding PNG and embedded as ICO.
- Shared `Tracker::Document` demo and `Tracker::Renderer`; no Windows-only musical
  model or DSP implementation.
- Event-driven stereo-float WASAPI; IAudioClient3 minimum-period negotiation and
  visible ordinary-shared fallback; MTA ownership, MMCSS, stop event, atomic stats.
- Private same-user named pipe, remote-client rejection, bounded framing/I/O,
  first-instance collision protection. `--automation` opts in. Exact endpoint:
  `\\.\pipe\ScreamSeq.Api.<PID>`. No TCP or discovery-by-first-process behavior.
- Seven-method API subset: api.describe, document.get, pattern.get, context.get,
  transport.get/play/stop. Reads use the real demo document. Transport uses the
  same host methods as GUI controls; requests guard revisions, validate regions,
  and deduplicate successful transport writes. WM_APP dispatch keeps document
  access on the control thread, away from audio. Full response-field parity is
  **not** claimed. Unsupported musical mutations return -32601.
- Standard-library Python explicit-pipe client, no automatic uncertain-send retry.
  Existing Mac Python client is unchanged; portable common-client integration remains.
- Python binary-plist container qualification/repacking preserves unknown fields
  and exact opaque snapshot/plugin payloads; understands metadata through 14.
  This is **not integrated application persistence** and not a tested Mac-produced
  project interchange path. Fixture inner sections are intentionally opaque.

## Reproduction and evidence

Toolchain installed with user approval: Visual Studio Build Tools 2022 17.14.41,
MSVC 19.44.35229, Windows SDK 10.0.26100, native ARM64 + x64 tools and CMake.
`windows/build.ps1` discovers the installation and uses IsWow64Process2 for native
architecture rather than Python's misleading AMD64 platform report.

From this worktree (PowerShell):

```powershell
.\windows\build.ps1 -Test
python -B windows/Tests/test_app.py
python -B windows/Tests/test_app_api.py
python -B windows/Tests/test_qualification.py
python -B -m unittest discover -s windows/Tests/Project -p 'test_*.py'
python -B windows/Tests/Api/test_client.py -v
```

Use the installed CMake/CTest full paths if not on PATH. Separate native device
and API harnesses live under `windows/Tests/Audio` and `windows/Tests/Api` with
standalone CMake files. See their READMEs. Real device test is explicitly opt-in.

Verified before pause:

- 24/24 reused portable C++ functional tests passed. They are **not** a Windows
  malloc/free/lock audit or sanitizer pass. `TRACKER_SANITIZER` only selects their
  existing functional-only path; no actual sanitizer is enabled.
- Actual app offline test passes at 44.1/48/96 kHz with 17/128/4096-frame partitions.
  PCM is finite and nonzero; maximum partition delta 8.64267e-7, within the existing
  shared SongTimingTests bound of 1e-6. **Not bit-exact partition equivalence**.
- Actual app pipe integration test passed: real demo reads, pagination, stale
  transport rejection, inspection-mode audio rejection, stop, unsupported edits.
- Standalone native API CTest passed; four Python client tests passed. Live current
  SID DACL/collision/stalled-client tests are included. Cross-account/remote-host
  tests were not performed.
- Eleven container tests and four qualification-validator tests passed.
- Windows atomic save test exercises Unicode destination, replacement, exact reopen,
  destination-directory failure preservation and temporary-file cleanup. The
  final regression also passed public `Document::open` of the Unicode path.
- Native ARM64 silent device test: ten cycles, 48000 Hz / 480-frame period /
  1056-frame buffer, 267 callbacks, no reported overruns/starvation/faults; zero
  scoped C++ new/delete calls. Full malloc/locks/driver-private audit pending.
- Loaded real-engine + UI run (before final API integration): 65.026 seconds,
  3916 drawn frames, 6503 callbacks / 3121440 frames, maximum callback 337.6 us,
  maximum service 417.9 us, zero reported overruns/starvation/device/MMCSS errors.
  `bin/windows-arm64/engine-device-loaded.json` is raw evidence. This is not
  acoustic loopback, physical latency or a comprehensive hardware glitch counter.
- Loaded render probe: 4000 frames / 66.766 s, CPU draw p99 4.143 ms. DXGI had
  four two-refresh gaps and missing statistics. **Sustained 60fps is not qualified**.
  `bin/windows-arm64/render-probe-loaded.json`, details in ARCHITECTURE.md.
- Actual app screenshot showed readable tracker data, waveform and explicit pending
  labels. Virtual-grid accessibility, complete keyboard workflow and final layout
  remain unimplemented. Current screen is 2880x1800 physical at 60 Hz, 200% DPI.

Final rebuild/test transcript is `bin/windows-arm64/final-build.log`; the preserved
checkpoint's BuildInfo.json records actual source/executable hashes. Do not treat
older `cmake/BUILD_VERIFICATION.md` installation-blocked notes as current status.

## Shared patches needing Mac review before merge

1. `editor/TrackerDocument.cpp`: `_WIN32` atomic module-save branch (CREATE_NEW,
   full writes, FlushFileBuffers, same-directory replace, failure cleanup), guarded
   POSIX include, and UTF-8-aware public module open. POSIX save behavior retained.
2. `common/mptPathString.{h,cpp}`: expose the existing Windows path converters and
   native filesystem helper to OPENMPT_EDITOR_CORE. No duplicate path algorithm.
3. `src/mpt/format/default_formatter.hpp`: qualify `using namespace ::mpt` to avoid
   MSVC ambiguous lookup when test code imports Tracker/OpenMPT namespaces.

No direct Mac-agent coordination channel was available; these isolated changes
are documented, not claimed already reviewed on Mac. Original checkout untouched.
The Mac fixtures are reused unchanged. Windows test preamble removes RPC's
`small` macro; tests get an 8 MiB stack reserve after two fixtures were confirmed
failing with Windows STATUS_STACK_OVERFLOW (0xC00000FD).

## Important next work / honest limits

Wait for the user's new UI guidance first. Keep engine/API contracts independent
of UI redesign. `PARITY.md` lists the full port requirements and current gaps.

- Native project codec/model restoration, actual Mac-produced cross-fixtures,
  editing/API/Undo/persistence parity, VST3 scanning/hosting/HWND editors and graph
  host extraction are still major work, not near-complete features.
- Unicode sample/instrument import path handling still deserves an explicit audit;
  the final change only qualifies public module save/open. Import UI is not exposed.
- Audio supports only stereo float shared mode. Exclusive/ASIO, endpoint picker,
  real device-loss/format-change qualification and MIDI are pending.
- Callback counters do not establish latency/loopback correctness. Cooperative
  shutdown can block on a stuck driver/callback; no hard boundedness guarantee.
- Main.cpp prepares playback on its control/UI thread; live preparation and richer
  editing need a proper document worker, not expansion of blocking GUI work.
- No claim of full request-field parity, general-purpose editor, 60fps pass,
  sanitizer cleanliness, runtime plugin isolation, or distribution readiness.

A live vcvars invocation reset TMPDIR/TEMP/TMP to the system temp directory once.
Before further probes explicitly point all three at the Hermes scratch directory;
no machine-wide environment setting is needed. Keep binaries/build products out
of Git. Preserve the user-requested pause.
