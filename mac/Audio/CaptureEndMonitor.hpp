#pragma once
#include <atomic>
#include <chrono>
#include <thread>
#include <utility>

namespace Tracker {
// Off-callback recorder owner. The supported Apple libc++ does not expose
// jthread/stop_token. Explicitly join before the owner disposes its AudioUnit.
// start/stop are serialized by the control owner; callbacks must not throw or
// dispose their owner. The worker never borrows PCM or calls UI code.
class CaptureEndMonitor {
  std::atomic<bool> stopping_{false};
  std::thread worker_;
public:
  CaptureEndMonitor() = default;
  CaptureEndMonitor(const CaptureEndMonitor &) = delete;
  CaptureEndMonitor &operator=(const CaptureEndMonitor &) = delete;
  ~CaptureEndMonitor() { stop(); }

  void stop() noexcept {
    stopping_.store(true, std::memory_order_release);
    if(worker_.joinable()) worker_.join();
  }

  template<class Capturing, class Ended>
  void start(Capturing capturing, Ended ended) {
    stop();
    stopping_.store(false, std::memory_order_release);
    worker_ = std::thread([this, capturing = std::move(capturing), ended = std::move(ended)] {
      while(!stopping_.load(std::memory_order_acquire) && capturing())
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      if(!stopping_.load(std::memory_order_acquire)) ended();
    });
  }
};
} // namespace Tracker
