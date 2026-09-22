#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "WasapiDevice.hpp"
#include "RealtimeAudit.hpp"
#include <windows.h>
#include <audioclient.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <ks.h>
#include <ksmedia.h>
#include <wrl/client.h>
#include <atomic>
#include <cstring>
#include <new>
#include <thread>
#include <vector>

namespace ScreamSeq {
namespace {
using Microsoft::WRL::ComPtr;
static_assert(std::atomic<std::uint64_t>::is_always_lock_free, "Telemetry must not lock");
static_assert(std::atomic<std::uint32_t>::is_always_lock_free, "Telemetry must not lock");
static_assert(std::atomic<std::int32_t>::is_always_lock_free, "Telemetry must not lock");

struct Event {
  HANDLE value = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  ~Event() { if (value) CloseHandle(value); }
  Event() = default;
  Event(const Event&) = delete;
  Event& operator=(const Event&) = delete;
};
struct Apartment {
  HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
};
struct Mmcss {
  HANDLE handle = nullptr;
  ~Mmcss() { if (handle) AvRevertMmThreadCharacteristics(handle); }
};
struct CoFormat {
  WAVEFORMATEX* value = nullptr;
  ~CoFormat() { CoTaskMemFree(value); }
};
std::uint64_t ticks() noexcept {
  LARGE_INTEGER value{}; QueryPerformanceCounter(&value);
  return static_cast<std::uint64_t>(value.QuadPart);
}
void maximum(std::atomic<std::uint64_t>& target, std::uint64_t value) noexcept {
  // Only the audio worker writes maxima; control readers never modify them.
  if (value > target.load(std::memory_order_relaxed)) target.store(value, std::memory_order_relaxed);
}
}

struct WasapiDevice::Impl {
  Event stopEvent, startEvent, readyEvent, startedEvent;
  std::thread worker;
  RenderCallback callback = nullptr;
  void* context = nullptr;
  std::uint32_t convertedRate = 0;
  bool opened = false;
  std::atomic<bool> active{false};
  std::atomic<std::int32_t> initResult{E_FAIL}, startResult{E_FAIL};
  std::atomic<std::uint32_t> rate{0}, period{0}, bufferFrames{0}, channels{0}, mmcssError{0};
  std::atomic<Mode> mode{Mode::Closed};
  std::atomic<std::int32_t> error{0}, fallbackError{0};
  std::atomic<std::uint64_t> callbackCount{0}, framesRendered{0}, maxCallback{0}, maxService{0}, maxGap{0};
  std::atomic<std::uint64_t> overruns{0}, starvation{0}, timeouts{0}, faults{0};

  void fail(HRESULT hr) noexcept {
    error.store(static_cast<std::int32_t>(hr));
    faults.fetch_add(1, std::memory_order_relaxed);
  }
  void resetStats() noexcept {
    callbackCount = 0; framesRendered = 0; maxCallback = 0; maxService = 0; maxGap = 0;
    overruns = 0; starvation = 0; timeouts = 0; faults = 0;
    error = 0; fallbackError = 0; mmcssError = 0;
  }
  bool eventsValid() const noexcept {
    return stopEvent.value && startEvent.value && readyEvent.value && startedEvent.value;
  }
  bool launch(bool reopening) {
    if (!eventsValid()) { fail(E_OUTOFMEMORY); return false; }
    ResetEvent(stopEvent.value); ResetEvent(startEvent.value);
    ResetEvent(readyEvent.value); ResetEvent(startedEvent.value);
    initResult = E_FAIL; startResult = E_FAIL;
    try { worker = std::thread([this, reopening] { threadMain(reopening); }); }
    catch (...) { fail(E_OUTOFMEMORY); return false; }
    if (WaitForSingleObject(readyEvent.value, INFINITE) != WAIT_OBJECT_0) {
      fail(HRESULT_FROM_WIN32(GetLastError())); stop(); return false;
    }
    if (FAILED(static_cast<HRESULT>(initResult.load()))) { stop(); return false; }
    return true;
  }
  void stop() noexcept {
    if (stopEvent.value) SetEvent(stopEvent.value);
    if (worker.joinable()) worker.join();
    active.store(false);
  }

  HRESULT initialize(IMMDevice* device, ComPtr<IAudioClient>& client,
                     ComPtr<IAudioRenderClient>& render, HANDLE audioEvent,
                     std::vector<float>& samples, bool reopening) {
    HRESULT hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                 reinterpret_cast<void**>(client.GetAddressOf()));
    if (FAILED(hr)) return hr;
    CoFormat mix;
    hr = client->GetMixFormat(&mix.value);
    if (FAILED(hr)) return hr;
    channels.store(mix.value->nChannels);
    if (mix.value->nChannels != 2 || !mix.value->nSamplesPerSec)
      return AUDCLNT_E_UNSUPPORTED_FORMAT;
    WAVEFORMATEXTENSIBLE format{};
    format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    format.Format.nChannels = 2;
    format.Format.nSamplesPerSec = convertedRate ? convertedRate : mix.value->nSamplesPerSec;
    format.Format.wBitsPerSample = 32;
    format.Format.nBlockAlign = 8;
    format.Format.nAvgBytesPerSec = format.Format.nSamplesPerSec * 8;
    format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    format.Samples.wValidBitsPerSample = 32;
    format.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    format.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    CoFormat closest;
    if(!convertedRate) {
      hr = client->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, &format.Format, &closest.value);
      if (hr != S_OK) return FAILED(hr) ? hr : AUDCLNT_E_UNSUPPORTED_FORMAT;
    }

    ComPtr<IAudioClient3> client3;
    UINT32 defaultPeriod = 0, fundamentalPeriod = 0, minPeriod = 0, maxPeriod = 0;
    UINT32 chosenPeriod = 0;
    Mode chosenMode = Mode::LowLatencyShared;
    HRESULT lowHr = convertedRate ? E_NOTIMPL : client.As(&client3);
    if (SUCCEEDED(lowHr)) {
      lowHr = client3->GetSharedModeEnginePeriod(&format.Format, &defaultPeriod,
                                                &fundamentalPeriod, &minPeriod, &maxPeriod);
      if (SUCCEEDED(lowHr)) {
        if (!minPeriod || !fundamentalPeriod || minPeriod > maxPeriod)
          lowHr = E_UNEXPECTED;
        else {
          chosenPeriod = minPeriod;
          lowHr = client3->InitializeSharedAudioStream(AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                                       chosenPeriod, &format.Format, nullptr);
        }
      }
    }
    if (FAILED(lowHr)) {
      fallbackError.store(convertedRate ? 0 : static_cast<std::int32_t>(lowHr));
      if (lowHr == AUDCLNT_E_DEVICE_INVALIDATED || lowHr == AUDCLNT_E_SERVICE_NOT_RUNNING)
        return lowHr;
      // A failed Initialize can leave a client unusable. Activate a fresh one.
      client3.Reset(); client.Reset();
      hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                            reinterpret_cast<void**>(client.GetAddressOf()));
      if (FAILED(hr)) return hr;
      const DWORD flags=AUDCLNT_STREAMFLAGS_EVENTCALLBACK | (convertedRate ?
        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM|AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY|AUDCLNT_STREAMFLAGS_NOPERSIST : 0);
      hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags,
                              0, 0, &format.Format, nullptr);
      if (FAILED(hr)) return hr;
      REFERENCE_TIME defaultDuration = 0, minimumDuration = 0;
      hr = client->GetDevicePeriod(&defaultDuration, &minimumDuration);
      if (FAILED(hr)) return hr;
      chosenPeriod = static_cast<UINT32>((static_cast<std::uint64_t>(defaultDuration) *
                      format.Format.nSamplesPerSec + 9999999) / 10000000);
      chosenMode = Mode::StandardShared;
    } else fallbackError.store(0);
    UINT32 capacity = 0;
    hr = client->GetBufferSize(&capacity);
    if (FAILED(hr)) return hr;
    if (!capacity || !chosenPeriod || capacity > 1048576) return E_UNEXPECTED;
    // Never silently change a prepared renderer's timing on stop/start.
    if (reopening && (rate.load() != format.Format.nSamplesPerSec || period.load() != chosenPeriod))
      return AUDCLNT_E_UNSUPPORTED_FORMAT;
    hr = client->SetEventHandle(audioEvent);
    if (FAILED(hr)) return hr;
    hr = client->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(render.GetAddressOf()));
    if (FAILED(hr)) return hr;
    samples.resize(static_cast<std::size_t>(capacity) * 2, 0.0f);
    rate.store(format.Format.nSamplesPerSec); period.store(chosenPeriod);
    bufferFrames.store(capacity); mode.store(chosenMode);
    return S_OK;
  }

  void stream(IAudioClient* client, IAudioRenderClient* render, HANDLE audioEvent,
              std::vector<float>& samples) noexcept {
    const auto capacity = bufferFrames.load();
    const auto hz = rate.load();
    LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
    const auto qpcHz = static_cast<std::uint64_t>(frequency.QuadPart);
    const auto nanoseconds = [qpcHz](std::uint64_t delta) noexcept { return delta * 1000000000ULL / qpcHz; };
    const auto periodNs = static_cast<std::uint64_t>(period.load()) * 1000000000ULL / hz;
    Mmcss mmcss;
    DWORD taskIndex = 0;
    mmcss.handle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
    mmcssError.store(mmcss.handle ? 0 : GetLastError());
    BYTE* output = nullptr;
    // Prime silence only: open/start handshakes cannot call the renderer early.
    HRESULT hr = render->GetBuffer(capacity, &output);
    if (SUCCEEDED(hr)) hr = render->ReleaseBuffer(capacity, AUDCLNT_BUFFERFLAGS_SILENT);
    if (SUCCEEDED(hr)) hr = client->Start();
    if (FAILED(hr)) {
      fail(hr); startResult.store(hr); SetEvent(startedEvent.value); return;
    }
    active.store(true); startResult.store(S_OK); SetEvent(startedEvent.value);
    HANDLE events[] = {stopEvent.value, audioEvent};
    std::uint64_t lastWake = 0;
    bool serviced = false;
    for (;;) {
      const DWORD wait = WaitForMultipleObjects(2, events, FALSE, 2000);
      if (wait == WAIT_OBJECT_0) break;
      if (wait == WAIT_TIMEOUT) { timeouts.fetch_add(1, std::memory_order_relaxed); fail(HRESULT_FROM_WIN32(ERROR_TIMEOUT)); break; }
      if (wait != WAIT_OBJECT_0 + 1) { fail(HRESULT_FROM_WIN32(GetLastError())); break; }
#ifdef SCREAMSEQ_WASAPI_ALLOCATION_AUDIT
      AudioAudit::Scope audit;
#endif
      const auto serviceStart = ticks();
      if (lastWake) maximum(maxGap, nanoseconds(serviceStart - lastWake));
      lastWake = serviceStart;
      UINT32 padding = 0;
      hr = client->GetCurrentPadding(&padding);
      if (FAILED(hr)) { fail(hr); break; }
      if (padding > capacity) { fail(E_UNEXPECTED); break; }
      if (serviced && padding == 0) starvation.fetch_add(1, std::memory_order_relaxed);
      const UINT32 available = capacity - padding;
      if (!available) continue;
      hr = render->GetBuffer(available, &output);
      if (FAILED(hr)) { fail(hr); break; }
      // Bounded, preallocated stereo scratch; no ownership/destruction in this path.
      std::memset(samples.data(), 0, static_cast<std::size_t>(available) * 2 * sizeof(float));
      const auto callbackStart = ticks();
      callback(context, samples.data(), available);
      const auto callbackNs = nanoseconds(ticks() - callbackStart);
      std::memcpy(output, samples.data(), static_cast<std::size_t>(available) * 2 * sizeof(float));
      hr = render->ReleaseBuffer(available, 0);
      callbackCount.fetch_add(1, std::memory_order_relaxed);
      framesRendered.fetch_add(available, std::memory_order_relaxed);
      maximum(maxCallback, callbackNs);
      const auto serviceNs = nanoseconds(ticks() - serviceStart);
      maximum(maxService, serviceNs);
      if (callbackNs > static_cast<std::uint64_t>(available) * 1000000000ULL / hz || serviceNs > periodNs)
        overruns.fetch_add(1, std::memory_order_relaxed);
      serviced = true;
      if (FAILED(hr)) { fail(hr); break; }
    }
    active.store(false);
    hr = client->Stop();
    if (FAILED(hr)) fail(hr);
  }

  void threadMain(bool reopening) noexcept {
    // Every COM interface, activation, use and release stays in this MTA thread.
    Apartment apartment;
    bool ready = false;
    try {
      HRESULT hr = apartment.result;
      // WASAPI requires an auto-reset audio event; control events are manual-reset.
      struct AudioEvent {
        HANDLE value = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        ~AudioEvent() { if (value) CloseHandle(value); }
      } audioEvent;
      // Declared after audioEvent so every early return releases COM first.
      ComPtr<IMMDeviceEnumerator> enumerator;
      ComPtr<IMMDevice> device;
      ComPtr<IAudioClient> client;
      ComPtr<IAudioRenderClient> render;
      std::vector<float> samples;
      if (SUCCEEDED(hr) && !audioEvent.value) hr = HRESULT_FROM_WIN32(GetLastError());
      if (SUCCEEDED(hr)) hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(enumerator.GetAddressOf()));
      if (SUCCEEDED(hr)) hr = enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
      if (SUCCEEDED(hr)) hr = initialize(device.Get(), client, render, audioEvent.value, samples, reopening);
      if (FAILED(hr)) fail(hr);
      initResult.store(hr); SetEvent(readyEvent.value); ready = true;
      if (FAILED(hr)) return;
      HANDLE events[] = {stopEvent.value, startEvent.value};
      const auto wait = WaitForMultipleObjects(2, events, FALSE, INFINITE);
      if (wait == WAIT_OBJECT_0 + 1) stream(client.Get(), render.Get(), audioEvent.value, samples);
      else if (wait != WAIT_OBJECT_0) fail(HRESULT_FROM_WIN32(GetLastError()));
      // release audio client before closing the event it was handed
      render.Reset(); client.Reset();
    } catch (...) {
      fail(E_OUTOFMEMORY);
      if (!ready) { initResult.store(E_OUTOFMEMORY); SetEvent(readyEvent.value); }
      else { startResult.store(E_OUTOFMEMORY); SetEvent(startedEvent.value); }
    }
    active.store(false);
  }
};

WasapiDevice::WasapiDevice() : impl_(std::make_unique<Impl>()) {}
WasapiDevice::~WasapiDevice() { close(); }
bool WasapiDevice::open(RenderCallback callback, void* context) {
  close(); impl_->resetStats();
  if (!callback) { impl_->fail(E_INVALIDARG); return false; }
  impl_->callback = callback; impl_->context = context;
  if (!impl_->launch(false)) return false;
  impl_->opened = true; return true;
}
bool WasapiDevice::openConverted(RenderCallback callback,void* context,std::uint32_t rate) {
  close();impl_->resetStats();
  if(!callback||rate<100||rate>768000){impl_->fail(E_INVALIDARG);return false;}
  impl_->convertedRate=rate;impl_->callback=callback;impl_->context=context;
  if(!impl_->launch(false))return false;
  impl_->opened=true;return true;
}
std::uint32_t WasapiDevice::sampleRate() const noexcept { return impl_->rate.load(); }
std::uint32_t WasapiDevice::periodFrames() const noexcept { return impl_->period.load(); }
bool WasapiDevice::start() {
  if (!impl_->opened) { impl_->fail(E_UNEXPECTED); return false; }
  if (impl_->active.load()) return true;
  // Completed or faulted workers must be joined before explicit reconfiguration.
  if (impl_->worker.joinable() && impl_->startResult.load() != E_FAIL) impl_->stop();
  if (!impl_->worker.joinable() && !impl_->launch(true)) return false;
  ResetEvent(impl_->startedEvent.value);
  SetEvent(impl_->startEvent.value);
  if (WaitForSingleObject(impl_->startedEvent.value, INFINITE) != WAIT_OBJECT_0) {
    impl_->fail(HRESULT_FROM_WIN32(GetLastError())); impl_->stop(); return false;
  }
  if (FAILED(static_cast<HRESULT>(impl_->startResult.load()))) { impl_->stop(); return false; }
  return true;
}
void WasapiDevice::stop() noexcept { impl_->stop(); }
void WasapiDevice::close() noexcept {
  impl_->stop(); impl_->opened = false; impl_->callback = nullptr; impl_->context = nullptr;impl_->convertedRate=0;
  impl_->rate = 0; impl_->period = 0; impl_->bufferFrames = 0; impl_->channels = 0; impl_->mode = Mode::Closed;
}
bool WasapiDevice::running() const noexcept { return impl_->active.load(); }
WasapiDevice::Stats WasapiDevice::stats() const noexcept {
  return {impl_->callbackCount.load(), impl_->framesRendered.load(), impl_->maxCallback.load(),
    impl_->maxService.load(), impl_->maxGap.load(), impl_->overruns.load(), impl_->starvation.load(),
    impl_->timeouts.load(), impl_->faults.load(), impl_->error.load(), impl_->fallbackError.load(),
    impl_->mmcssError.load(), impl_->bufferFrames.load(), impl_->channels.load(), impl_->mode.load()};
}
} // namespace ScreamSeq
