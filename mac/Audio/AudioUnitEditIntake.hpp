#pragma once
#include "editor/hosted/PluginEditQueue.hpp"
#include <atomic>
#include <limits>

namespace Tracker {
// The backend owns delivered AU notifications, independently of its listener
// and editor window. One main-run-loop producer and one serialized consumer.
// Closing a listener must not clear already delivered input or reset its cut.
class AudioUnitEditIntake {
  BoundedPluginEditQueue<1024> events_;
  std::atomic<uint64_t> dropped_{0};
public:
  static constexpr size_t capacity=1024;
  bool push(const PluginEdit &event)noexcept {
    if(events_.push(event))return true;
    const auto count=dropped_.load(std::memory_order_relaxed);
    if(count!=std::numeric_limits<uint64_t>::max())dropped_.store(count+1,std::memory_order_relaxed);
    return false;
  }
  uint64_t dropped()const noexcept{return dropped_.load(std::memory_order_relaxed);}
  // Capture on the native producer. This is a notification cut, not an input
  // barrier; the caller still owns native-window exclusion and AU delivery.
  PluginEditFence fence(uint64_t hostTime)const noexcept {
    const auto cut=events_.fence();return {cut.position,hostTime,dropped(),cut.pending};
  }
  PluginEditRead popBefore(const PluginEditFence &fence,PluginEdit &event)noexcept {
    return events_.popBefore({fence.position},event);
  }
  bool pop(PluginEdit &event)noexcept{return events_.pop(event);}
};
}
