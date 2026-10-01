# Native VST3 qualification (no audio hardware)

## Reproduce

From the Windows worktree, using Visual Studio 2022 Build Tools' bundled CMake
and CTest (the installed CMake directory is
`C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin`):

```text
cmake -S windows/Tests/Plugins -B bin/windows-vst3 -G "Visual Studio 17 2022" -A ARM64
cmake --build bin/windows-vst3 --config Release -j 2
ctest --test-dir bin/windows-vst3 -C Release --output-on-failure
```

The standalone target imports the freshly built shared core libraries from
`bin/windows-snapshot-fix/Release` by default. Override
`SCREAMSEQ_EDITOR_BUILD` if that build moved. It builds the actual shared host
once, with `windows/Plugins/Provider.cmake` injecting the real provider. All test
executables use an 8 MiB stack reserve; large graph/rack objects are heap-owned.
No Python runtime, downloaded SDK, plugin installation, project write, service,
or system audio device is used. Generated caches and disposable DLL copies stay
under the dedicated build directory. The HWND test runs in its own CTest-bounded
process and uses SW_SHOWNOACTIVATE, not synthetic global input.

## Actual native result

MSVC 19.44, Windows SDK 10.0.26100, ARM64 Release: **5/5 CTest tests pass**.
`IsWow64Process2` inside the test verifies ARM64 hardware and a non-emulated
process. The passing executables/modules are in `bin/windows-vst3/Release`:

| CTest | Actual exercised path |
|---|---|
| `vst3-native` | Real DLL factory, canonical FUID, invalid IDs/class/path/AU rejection; effect and instrument PCM; double parameter endpoints at offsets 0 and count-1; same-offset replacement/reset; shared precise MIDI/ramp; 44.1/48/96 kHz with 17/128/4096-frame blocks; shared ramp partition checks; current transport; sidechain inputOffset and mono/stereo auxiliary outputs including index 31; MIDI note-off/channel handling and bend mapping; programs/read-only steps; latency; binary-plist state/unknown values; input/output queue overflows and silence; output parameter values. |
| `vst3-Lifecycle` | Real rack plus two independent graph target processors and their PCM; shared-module InitDll lifetime; partial initialization/setup/activation/processing failures; retained stream refs and extreme/invalid seeks; restartComponent latency fault/silence; all objects terminated/released and final ExitDll balanced. |
| `vst3-Editor` | Real plugin-owned HWND attach, IPlugFrame resize to a 520x220 client, editor callback/commit-value readback and .625 gain PCM, remove/close/reopen three times, one scoped WM_CLOSE, foreground window unchanged. |
| `vst3-Scanner` | Empty/cache-only startup without module execution; scanner-in-another-process assertion; persistent-cache read even with scanner unavailable; Unicode loading/DSP; stale hash, missing binary, wrong PE machine and ambiguous bundle rejection; actual crashing, hanging and output-flooding DLLs in controlled scanner children. Failed scans preserve cache. |
| `vst3-Separate` | Separate component/controller factory objects, two-way IConnectionPoint connection/disconnection, distinct component (4-byte) and controller (8-byte) DATA streams, controller marker plus unknown inner state preservation, explicit connection-failure cleanup. |

PCM comparisons use a 1e-6 float bound; parameter queue endpoint values are
checked for exact double equality before fixture DSP conversion. This is not a
claim of bit-identical commercial-plugin rendering. The wrong-architecture test
changes the PE machine of a disposable fixture copy to AMD64 to exercise the
pre-load gate; it is **not** x64 execution or bridge qualification.

`qualification-tests.log` and `qualification-build.log` in `bin/windows-vst3`
retain the final execution output; `qualification-manifest.json` records source
and artifact SHA-256 values. Steinberg MIT notices/revision pins are copied into
`Release/VST3-NOTICES`. Do not distribute the crash/hang/flood fixtures to users.

## Test-first evidence and fixture origin

The initial compiled discovery test failed on an empty provider, then passed
through the isolated scanner and actual DLL. The next real PCM test failed on
the absent DSP backend. Further observed red/green cases exposed invalid sample
offsets, missing output event sink, malformed MIDI acceptance, stream ownership/
seek validation, ignored controller connection errors, repeated InitDll while
instances were live, SDK queue ownership and ignored output overflow. The
shared SampleRamp endpoint convention was read and the test's duration-vs-last-
sample expectation corrected without changing shared scheduling.

`FixtureVST3.cpp` adapts the platform-neutral DSP and class IDs from
`mac/Tests/FixtureVST3.mm` (source SHA-256
`f51851732354722e9ba7df6217b3741a233ec3385add0fea639e9c4cc43d256f`, Git blob
`055549e389b6d5651a8dc6b7bbb0b178f569f8fb`). Windows additions provide actual
InitDll/ExitDll/GetPluginFactory exports, a plugin-owned HWND, ref/lifecycle and
queue observations, a separate-controller build mode and controlled fault modes.
These are actual compiled `.vst3` DLLs, not `PluginBackend` mocks. The host port's
Mac authority had Git blob `38420203a420796eacedcb323e13533da615fb13` when read.
No Mac or vendored SDK source was edited by this work. Existing upstream and
repository attribution/licenses remain applicable.

## Not qualified

No commercial plugin was tested. No main-application native UI/API/state bridge,
real Mac application reopen, full graph musical regression suite with vendor
DSP, WASAPI output/loopback, physical latency, runtime vendor crash isolation,
sanitizer or comprehensive malloc/free/lock audit is claimed. Scanner containment
is not a hostile-code sandbox. Runtime UI/DSP vendor code remains in-process and
can hang/crash. MIDI output events are bounded/consumed but not routed because
the current shared provider interface has no output-MIDI API. Structural vendor
restart requests require stopped recreation, not seamless live reconfiguration.
See `../../Plugins/README.md` for the exact parent integration and ownership rules.
