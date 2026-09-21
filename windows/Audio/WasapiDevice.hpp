#pragma once

#include <cstdint>
#include <memory>

namespace ScreamSeq {

// Lifecycle methods are serialized by the owner, never called from the callback.
// stats()/running() may be polled concurrently. No COM setup is needed by the caller.
class WasapiDevice final {
public:
  using RenderCallback = void (*)(void*, float*, std::uint32_t) noexcept;
  enum class Mode : std::uint32_t { Closed, LowLatencyShared, StandardShared };
  struct Stats {
    std::uint64_t callbackCount;
    std::uint64_t framesRendered;
    std::uint64_t maxCallbackNanoseconds;
    std::uint64_t maxServiceNanoseconds;
    std::uint64_t maxWakeGapNanoseconds;
    std::uint64_t deadlineOverruns;
    std::uint64_t starvationIndicators;
    std::uint64_t waitTimeouts;
    std::uint64_t deviceErrors;
    std::int32_t lastError;       // HRESULT; zero means no error since open.
    std::int32_t fallbackError;   // Why low-period negotiation fell back.
    std::uint32_t mmcssError;     // Win32 error, zero on successful registration.
    std::uint32_t bufferFrames;
    std::uint32_t endpointChannels;
    Mode mode;
  };

  WasapiDevice();
  ~WasapiDevice();
  WasapiDevice(const WasapiDevice&) = delete;
  WasapiDevice& operator=(const WasapiDevice&) = delete;
  WasapiDevice(WasapiDevice&&) = delete;
  WasapiDevice& operator=(WasapiDevice&&) = delete;

  // Selects the existing default eRender/eMultimedia endpoint, stereo float only.
  // Other channel layouts are explicitly rejected, not silently remapped.
  // No callback runs until start(). Prepare the renderer at sampleRate() first.
  bool open(RenderCallback callback, void* context);
  std::uint32_t sampleRate() const noexcept;
  std::uint32_t periodFrames() const noexcept;
  bool start();
  // Wakes either start/event wait and joins. A driver call or user callback which
  // never returns can still block the join: threads are never unsafely detached.
  void stop() noexcept;
  void close() noexcept;
  bool running() const noexcept;
  // Atomic fields are sampled independently, not a transactional snapshot.
  Stats stats() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace ScreamSeq
