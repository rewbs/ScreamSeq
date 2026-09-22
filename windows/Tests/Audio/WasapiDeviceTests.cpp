// Silent, opt-in hardware integration test. Never captures or changes system defaults.
#include "../../Audio/WasapiDevice.hpp"
#include "../../Audio/PeriodSelection.hpp"
#ifdef SCREAMSEQ_WASAPI_ALLOCATION_AUDIT
#include "../../Audio/RealtimeAudit.hpp"
#include <malloc.h>
#include <cstdlib>
#include <new>

void* operator new(std::size_t size) {
  ScreamSeq::AudioAudit::allocated();
  if (void* p = std::malloc(size ? size : 1)) return p;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { ScreamSeq::AudioAudit::freed(p); std::free(p); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
void* operator new(std::size_t size, std::align_val_t alignment) {
  ScreamSeq::AudioAudit::allocated();
  if (void* p = _aligned_malloc(size ? size : 1, static_cast<std::size_t>(alignment))) return p;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size, std::align_val_t alignment) { return ::operator new(size, alignment); }
void operator delete(void* p, std::align_val_t) noexcept { ScreamSeq::AudioAudit::freed(p); _aligned_free(p); }
void operator delete[](void* p, std::align_val_t alignment) noexcept { ::operator delete(p, alignment); }
void operator delete(void* p, std::size_t, std::align_val_t alignment) noexcept { ::operator delete(p, alignment); }
void operator delete[](void* p, std::size_t, std::align_val_t alignment) noexcept { ::operator delete(p, alignment); }
#endif
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <type_traits>
#include <limits>

namespace {
struct CallbackState {
  std::atomic<std::uint64_t> calls{0};
  std::atomic<std::uint64_t> frames{0};
  std::atomic<bool> badBuffer{false};
};
void silence(void* context, float* output, std::uint32_t frames) noexcept {
  auto& state = *static_cast<CallbackState*>(context);
  if (!output || !frames) { state.badBuffer.store(true); return; }
  for (std::uint32_t i = 0; i < frames * 2; ++i) output[i] = 0.0f;
  state.frames.fetch_add(frames, std::memory_order_relaxed);
  state.calls.fetch_add(1, std::memory_order_relaxed);
}
bool require(bool value, const char* message) {
  if (!value) std::fprintf(stderr, "FAIL: %s\n", message);
  return value;
}
}
int main(int argc, char** argv) {
  static_assert(std::is_trivial_v<ScreamSeq::WasapiDevice::Stats>);
  static_assert(std::is_standard_layout_v<ScreamSeq::WasapiDevice::Stats>);
  ScreamSeq::WasapiDevice device;
  bool ok = require(!device.running(), "new device is stopped");
  for(const auto &row:std::vector<std::vector<std::uint32_t>>{{0,4,48,448,48},{64,4,48,448,64},{65,4,48,448,68},{512,4,48,448,448},{64,128,256,1024,256},{257,128,256,1024,384},{0,4,49,448,52},{0,0,1,4,0},{0,4,8,4,0},{4,8,1,7,0},{UINT32_MAX,4,4,UINT32_MAX,UINT32_MAX-3}})
    ok &= require(ScreamSeq::sharedPeriod(row[0],row[1],row[2],row[3])==row[4],"legal period rounding, clamping and widened overflow");
#ifdef SCREAMSEQ_WASAPI_ALLOCATION_AUDIT
  {
    ScreamSeq::AudioAudit::Scope scope;
    void* probe = ::operator new(8);
    ::operator delete(probe);
    void* aligned = ::operator new(64, std::align_val_t{64});
    ::operator delete(aligned, std::align_val_t{64});
  }
  ok &= require(ScreamSeq::AudioAudit::allocations.load() == 2 &&
                ScreamSeq::AudioAudit::deallocations.load() == 2, "allocation audit detects deliberate probes");
  ScreamSeq::AudioAudit::allocations.store(0);
  ScreamSeq::AudioAudit::deallocations.store(0);
#endif
  device.stop(); device.close(); device.close();
  ok &= require(!device.start(), "start without open fails");
  ok &= require(!device.open(nullptr, nullptr), "null callback fails");
  CallbackState invalid;
  ok &= require(!device.open(silence,&invalid,{L"",65537}),"out-of-range requested period fails before opening hardware");
  ok &= require(!device.open(silence,&invalid,{std::wstring(L"a\0b",3),128}),"embedded null endpoint rejected");
  if (argc < 2 || std::strcmp(argv[1], "--silence") != 0) {
    std::puts("Lifecycle tests only. Pass --silence [cycles] [milliseconds] for real output tests.");
    return ok ? 0 : 1;
  }
  const int cycles = argc > 2 ? std::atoi(argv[2]) : 10;
  const int milliseconds = argc > 3 ? std::atoi(argv[3]) : 250;
  if (cycles < 1 || cycles > 1000 || milliseconds < 50 || milliseconds > 60000) return 2;
  CallbackState state;
  if (!device.open(silence, &state)) {
    const auto s = device.stats();
    std::fprintf(stderr, "OPEN FAILED HRESULT=0x%08x (no simulated success)\n", static_cast<unsigned>(s.lastError));
    return 1;
  }
  const auto negotiated = device.stats();
  const auto resolved=device.endpointId();
  auto endpoints=ScreamSeq::WasapiDevice::endpoints();
  ok &= require(!resolved.empty()&&std::any_of(endpoints.begin(),endpoints.end(),[&](const auto &e){return e.id==resolved&&e.isDefault&&!e.name.empty();}),"enumeration identifies the opened default endpoint");
  std::printf("sample_rate=%u period_frames=%u buffer_frames=%u endpoint_channels=%u mode=%u fallback_hr=0x%08x\n",
    device.sampleRate(), device.periodFrames(), negotiated.bufferFrames,
    negotiated.endpointChannels, static_cast<unsigned>(negotiated.mode), static_cast<unsigned>(negotiated.fallbackError));
  ok &= require(device.sampleRate() > 0 && device.periodFrames() > 0, "negotiated rate and period");
  ok &= require(state.calls.load() == 0, "open does not call unprepared renderer");
  // stop before the first start must wake the initialization/start wait and join.
  device.stop();
  for (int cycle = 0; cycle < cycles && ok; ++cycle) {
    auto before = state.calls.load();
    ok &= require(device.start(), "start succeeds");
    ok &= require(device.endpointId()==resolved,"restart retains the prepared output endpoint");
    ok &= require(device.start(), "start is idempotent");
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    ok &= require(device.running(), "still running before stop");
    const auto beginStop = std::chrono::steady_clock::now();
    device.stop(); device.stop();
    const auto stopMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - beginStop).count();
    const auto after = state.calls.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ok &= require(after > before, "real callback count increased");
    ok &= require(state.calls.load() == after, "stop joined callback thread");
    ok &= require(!device.running(), "stop clears running");
    std::printf("cycle=%d callbacks=%llu stop_ms=%.3f\n", cycle + 1,
      static_cast<unsigned long long>(after - before), stopMs);
  }
  const auto s = device.stats();
  std::printf("callbacks=%llu frames=%llu callback_max_ns=%llu service_max_ns=%llu wake_gap_max_ns=%llu deadline_overruns=%llu starvation_indicators=%llu wait_timeouts=%llu faults=%llu mmcss_error=%u last_hr=0x%08x\n",
    static_cast<unsigned long long>(s.callbackCount), static_cast<unsigned long long>(s.framesRendered),
    static_cast<unsigned long long>(s.maxCallbackNanoseconds), static_cast<unsigned long long>(s.maxServiceNanoseconds),
    static_cast<unsigned long long>(s.maxWakeGapNanoseconds), static_cast<unsigned long long>(s.deadlineOverruns),
    static_cast<unsigned long long>(s.starvationIndicators), static_cast<unsigned long long>(s.waitTimeouts),
    static_cast<unsigned long long>(s.deviceErrors), s.mmcssError, static_cast<unsigned>(s.lastError));
  ok &= require(!state.badBuffer.load(), "callback receives nonempty valid buffer");
  ok &= require(s.callbackCount == state.calls.load(), "callback telemetry matches actual calls");
  ok &= require(s.framesRendered == state.frames.load(), "frame telemetry matches actual calls");
  ok &= require(s.deviceErrors == 0 && s.lastError == 0, "stream completed without device error");
  device.close();
  ok &= require(device.sampleRate() == 0 && device.periodFrames() == 0, "close clears negotiated format");
  ok &= require(device.endpointId().empty(),"close clears endpoint identity");
  for(unsigned preferred:{64u,128u,256u,512u}) {
    ScreamSeq::WasapiDevice selected;
    ok &= require(selected.open(silence,&state,{resolved,preferred}),"explicit output and preferred period opens");
    ok &= require(selected.endpointId()==resolved&&selected.periodFrames()>0,"explicit endpoint retained with truthful negotiation");
    ok &= require(selected.start(),"explicit output starts");
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    ScreamSeq::WasapiDevice missing;
    ok &= require(!missing.open(silence,&state,{L"ScreamSeq-QA-missing-output",preferred}),"missing selected output must not fall back to default");
    ok &= require(selected.running(),"failed independent configuration disrupted live output");
    selected.stop();const auto measured=selected.stats();
    std::printf("explicit_preferred=%u actual_period=%u buffer=%u callbacks=%llu overruns=%llu starvation=%llu faults=%llu fallback=0x%08x\n",preferred,selected.periodFrames(),measured.bufferFrames,static_cast<unsigned long long>(measured.callbackCount),static_cast<unsigned long long>(measured.deadlineOverruns),static_cast<unsigned long long>(measured.starvationIndicators),static_cast<unsigned long long>(measured.deviceErrors),static_cast<unsigned>(measured.fallbackError));
    ok &= require(measured.callbackCount>0&&measured.deviceErrors==0&&measured.lastError==0,"selected output rendered without device faults");
  }
  // Exercise ownership cleanup without an explicit stop.
  { ScreamSeq::WasapiDevice disposable;
    ok &= require(disposable.open(silence, &state), "reopen default endpoint");
    if (ok) { ok &= require(disposable.start(), "destructor fixture starts");
      std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
  }
  const auto joined = state.calls.load();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  ok &= require(state.calls.load() == joined, "destructor joins callback");
#ifdef SCREAMSEQ_WASAPI_ALLOCATION_AUDIT
  std::printf("host_cpp_allocations=%llu host_cpp_deallocations=%llu (render service scope only; not vendor malloc/locks)\n",
    static_cast<unsigned long long>(ScreamSeq::AudioAudit::allocations.load()),
    static_cast<unsigned long long>(ScreamSeq::AudioAudit::deallocations.load()));
  ok &= require(ScreamSeq::AudioAudit::allocations.load() == 0 &&
                ScreamSeq::AudioAudit::deallocations.load() == 0, "no audited C++ allocation/free in render service");
#endif
  std::puts(ok ? "PASS (silent real-device fixture; not acoustic or glitch qualification)" : "FAIL");
  return ok ? 0 : 1;
}
