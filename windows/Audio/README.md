# Event-driven shared WASAPI host

`ScreamSeq::WasapiDevice` is a Windows-only output host; it does not depend on the
tracker core. Link `WasapiDevice.cpp` with `ole32`, `avrt`, and `uuid`, C++17 or
later, Windows 10+ SDK. The ARM64 target is preferred on native ARM64 hardware.

## Integration contract

```cpp
ScreamSeq::WasapiDevice device;
if (!device.open(renderStereo, &renderer)) { /* inspect device.stats().lastError */ }
// Prepare the separate Tracker::Renderer at device.sampleRate() here.
// Provide capacity for variable callback lengths up to stats().bufferFrames.
if (!device.start()) { /* inspect stats */ }
// poll stats/running on the UI/control thread, never from renderStereo
// Stop/join BEFORE destroying or mutating renderer or its context.
device.stop();
device.close();
```

`renderStereo` has signature
`void(void*, float* interleavedStereo, uint32_t frames) noexcept`. The buffer is
zeroed before each invocation and preallocated before streaming. The callback
must not allocate/free, lock, log, touch UI/disk, throw, or call lifecycle methods.
Callback/context addresses must remain valid until `stop()` returns. Lifecycle
calls must be serialized by the application. Construction can allocate/throw.
`stats()` and `running()` are safe to poll concurrently; statistics are independent
atomic reads rather than a transactional snapshot.

`open()` starts a dedicated MTA owner thread and synchronously negotiates without
calling the renderer. Activation, all WASAPI calls, COM releases, and
`CoUninitialize` remain on that thread. `start()` primes silence and wakes the
owner; `stop()` wakes the explicit stop event and joins, including before the
first start. A later `start()` recreates COM/device objects off the render path,
and rejects a changed sample rate or period until an explicit `open()` and
renderer re-prepare. `start()` is idempotent while running. Error counters persist
until `open()`. `close()` clears format metadata but preserves error/counter evidence.

Shutdown is cooperative. An unreturning user callback or blocking audio-driver
COM call can delay `open`, `start`, `stop`, or destruction. The implementation does
not kill or detach threads still holding the caller's renderer context.

## Negotiation and status

- Reads the existing default **render/multimedia** endpoint; never changes defaults,
  endpoint/session/master volume, routing, or microphone/capture state.
- Rejects non-stereo endpoints with `AUDCLNT_E_UNSUPPORTED_FORMAT`; reports their
  channel count in `Stats.endpointChannels`. No surprise multichannel mapping.
- Negotiates interleaved float32 stereo at the endpoint mix rate with
  `IsFormatSupported`. Unsupported formats are explicitly rejected.
- Queries `IAudioClient3::GetSharedModeEnginePeriod`, requests the minimum period
  with `InitializeSharedAudioStream` and `AUDCLNT_STREAMFLAGS_EVENTCALLBACK`.
- On locked/unsupported negotiation, activates a fresh audio client and uses
  ordinary event-driven shared initialization (duration zero). `Mode::StandardShared`
  and `fallbackError` expose the fallback; `periodFrames()` is then the default
  device-period estimate, not a measured endpoint latency. Invalidated devices or
  stopped audio services are not retried in the render loop.
- Audio-event waits are auto-reset; the manual-reset stop event has priority.
  A two-second event timeout records a fault and exits rather than spinning.
- Registers the render thread with MMCSS `Pro Audio`. Registration failure is
  nonfatal but visible as `mmcssError`; no success/priority claim is made in that case.
- **Exclusive mode and ASIO remain pending/unimplemented.** No silent fallback to
  either, and neither is advertised as a capability.

## Telemetry boundaries

Counters report callbacks, frames, maximum callback/service/wake-gap durations,
deadline overruns, starvation indicators, event timeouts, and HRESULT faults.
A deadline overrun means callback duration exceeded its rendered-frame budget or
service duration exceeded one negotiated period. Empty padding after the first
service is a starvation *indicator*, not proof of an audible underrun. Callback
counts and rendered frames include a callback whose subsequent ReleaseBuffer fails.
No event/padding observation proves zero hardware glitches. Negotiated period and
buffer sizes are not acoustic round-trip latency. Statistics require lock-free
integer atomics at compile time. Only the worker updates timing maxima.

## Diagnostics (silence only)

From a Developer command prompt with the installed SDK/compiler:

```text
cmake -S windows/Tests/Audio -B <scratch>/wasapi-arm64 -A ARM64
cmake --build <scratch>/wasapi-arm64 --config Release
ctest --test-dir <scratch>/wasapi-arm64 -C Release --output-on-failure
<scratch>/wasapi-arm64/Release/wasapi-diagnostic.exe --silence 10 250
```

This standalone target intentionally avoids building the shared engine/frontend.
Plain invocation tests initial/closed/null-callback lifecycle, without opening an
endpoint. `--silence` is the explicit opt-in real endpoint test: repeated starts,
stops, pre-start stop, no callback before prepare/start, exact callback/frame
counter agreement, join/no-callback-after-stop, destructor cleanup, and per-cycle
stop timing. It emits zero samples only. It does not beep, record audio, select a
microphone, change routing, or demonstrate audible playback quality.

Hardware removal, service shutdown, alternate channel layouts, locked periods,
commercial plugins, and sustained loaded sessions need separate controlled tests.
The standalone diagnostic enables `SCREAMSEQ_WASAPI_ALLOCATION_AUDIT`: global
C++ new/delete (including aligned and array variants) count calls only inside the
render service scope, including the user callback. Deliberate ordinary/aligned
allocation probes first verify the hooks detect allocations and frees. This does
**not** intercept direct malloc/free, Win32 heap operations, vendor DLL private
allocations, or locks; it is not a blanket realtime-safety audit. Production builds
omit this instrumentation unless explicitly enabled. Physical loopback and a full
allocation/free/lock audit remain separate work. See
`../Tests/Audio/VERIFICATION.md` for actual executed evidence.
