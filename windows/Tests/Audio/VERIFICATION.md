# WASAPI diagnostic execution evidence

## Parent verification: native ARM64

After installation completed, the parent independently built and ran the native
ARM64 diagnostic using Hostarm64/arm64 MSVC 19.44.35229 and SDK 10.0.26100:

```text
cmake -S windows/Tests/Audio -B bin/windows-audio-arm64 -G "Visual Studio 17 2022" -A ARM64
cmake --build bin/windows-audio-arm64 --config Release --parallel 2
bin/windows-audio-arm64/Release/wasapi-diagnostic.exe
bin/windows-audio-arm64/Release/wasapi-diagnostic.exe --silence 10 250
```

All exited 0. Observed endpoint: 48000 Hz, period 480 frames, buffer 1056 frames,
stereo, IAudioClient3 low-period mode, no fallback. Ten cycles delivered 267
callbacks / 128160 frames. Maximum callback/service/wake gap were
11400/110000/22141100 ns. Deadline overruns, starvation indicators, timeouts,
faults and MMCSS error were all zero. Scoped host C++ new/delete counts were
zero after the deliberate detector probes. Stop times ranged 24.318–127.514 ms.
This ran concurrently with compilation; it is silent device evidence, not
acoustic latency or a full allocator/lock audit. It supersedes the older
native-ARM64 prerequisite limitation below, not the remaining fault/format gates.

## Earlier child evidence (x64)

Recorded 2026-09-21 UTC. This is a bounded silent real-device smoke test, **not**
acoustic latency, audible glitch, plugin, native ARM64 performance, or full
allocation/lock qualification.

## Build and test status

The diagnostic test source was written first. The first compile attempt returned
`cl: command not found` (exit 127), while the already-approved Build Tools install
was in progress. There was no runnable red assertion at that stage; this is not a
claim of a complete red/green TDD cycle. No duplicate installer was started.

After compiler/SDK files became available, standalone CMake/Ninja Release build
succeeded with MSVC **19.44.35229.0**, SDK **10.0.26100.0**, `/W4 /WX /permissive-`.
The selected compiler was `Hostx64/x64/cl.exe`, so this executable is **x64** on a
native ARM64 Windows machine. `IsWow64Process2` reported native machine `0xaa64`.
At verification time no ARM64-target `cl.exe` was present. Native ARM64 build/test
remains pending and no native ARM64 timing claim is made.

Standalone source: `windows/Tests/Audio/CMakeLists.txt`.
Build: `C:/Users/P14/AppData/Local/hermes/cache/scratch/screamseq-wasapi-x64`.

Commands executed after configuring with vcvarsall x64 and Ninja:

```text
ctest --test-dir C:/Users/P14/AppData/Local/hermes/cache/scratch/screamseq-wasapi-x64 --output-on-failure
C:/Users/P14/AppData/Local/hermes/cache/scratch/screamseq-wasapi-x64/wasapi-diagnostic.exe --silence 10 250
```

Both exited **0**. CTest: **1/1 wasapi-lifecycle passed**, 0.12 seconds test time.
The lifecycle test includes deliberately triggered allocation/free hook probes.

## Actual real-device output

```text
sample_rate=48000 period_frames=480 buffer_frames=1056 endpoint_channels=2 mode=1 fallback_hr=0x00000000
cycle=1 callbacks=27 stop_ms=12.632
cycle=2 callbacks=27 stop_ms=15.512
cycle=3 callbacks=27 stop_ms=15.191
cycle=4 callbacks=27 stop_ms=16.763
cycle=5 callbacks=27 stop_ms=15.576
cycle=6 callbacks=27 stop_ms=11.876
cycle=7 callbacks=27 stop_ms=14.126
cycle=8 callbacks=26 stop_ms=10.257
cycle=9 callbacks=27 stop_ms=4.085
cycle=10 callbacks=26 stop_ms=3.621
callbacks=268 frames=128640 callback_max_ns=57000 service_max_ns=245200 wake_gap_max_ns=10464000 deadline_overruns=0 starvation_indicators=0 wait_timeouts=0 faults=0 mmcss_error=0 last_hr=0x00000000
host_cpp_allocations=0 host_cpp_deallocations=0 (render service scope only; not vendor malloc/locks)
PASS (silent real-device fixture; not acoustic or glitch qualification)
```

Mode 1 is `LowLatencyShared`: IAudioClient3 accepted the minimum period it
reported. No fallback occurred on this endpoint. Repeated stop/start, stop before
first start, idempotent start/stop, no render before start, exact callback/frame
telemetry agreement, and destruction while running all passed. The callback
submitted only zeros. No default route, volume, or capture/microphone changes
were requested. The host C++ allocation audit covers new/delete in the render
service scope only, not direct malloc/free, locks, OS/driver or vendor allocations.

## Uninstrumented production-path cross-check

A separate Release build at sibling scratch directory
`screamseq-wasapi-x64-noaudit`, configured with
`-DSCREAMSEQ_WASAPI_ALLOCATION_AUDIT=OFF`, also compiled with `/W4 /WX` and passed
the lifecycle executable (exit 0). Its real silent run `--silence 2 100` exited 0:
24 callbacks, 11520 frames, maximum callback 38800 ns, maximum service 234200 ns,
maximum wake gap 10726500 ns, zero deadline/starvation/timeout/fault counters and
MMCSS error 0. Both cycles negotiated the same 48000 Hz/480-frame period/
1056-frame capacity. No audit claim applies to that uninstrumented run.

## Fingerprints of this executed source/binary

SHA-256:

| File | Hash |
|---|---|
| `windows/Audio/WasapiDevice.cpp` | `b453962a33ff9cad9565d95cbb2902621425bc68a9c795793fbdbeb7da7aeea2` |
| `windows/Audio/WasapiDevice.hpp` | `2162eb15531f95e89adc1b85e48deb348a62d3993799ba6ac48a0d59e8c51a13` |
| `windows/Audio/RealtimeAudit.hpp` | `a457addcc4e1f0409862ad098a40adecddc519325d4f174f8ec03b423da85b03` |
| `windows/Tests/Audio/WasapiDeviceTests.cpp` | `ddcb6ad1c853196423e6cc6ec3221bca6e154cb114f7b27706fe46af502434fc` |
| `wasapi-diagnostic.exe` | `7b2d129fbfa666e6114421f028415f09254169b3343bc7c9e3522272eeda35d0` |

## Remaining coverage

- Native ARM64 compiler build and execution.
- Unsupported/locked minimum-period fallback on a controlled endpoint.
- Device invalidation/service shutdown and non-stereo endpoint rejection.
- Full malloc/free/lock instrumentation, long loaded playback, physical loopback.
- The real Tracker::Renderer callback and GUI integration are separate work.
- Exclusive mode and ASIO are explicitly not implemented.

`stop()` wakes event waits and joins, but cannot bound an unreturning callback or
a blocking driver call. No forced device removal, route change, or service stop
was performed to manufacture fault-test evidence.
