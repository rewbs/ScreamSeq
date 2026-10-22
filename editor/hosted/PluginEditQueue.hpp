#pragma once
#include "PluginTypes.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace Tracker {
// Original-time notifications, separate from latest audible values. Exactly one
// native UI producer and one serialized control consumer. No allocation, locks,
// validation, timestamp substitution or overflow policy belongs in this queue.
// Capture a fence on the actual native input owner for an admission boundary;
// a fence alone is NOT a native input barrier or a provider identity.
template<size_t Capacity> class BoundedPluginEditQueue {
public:
  static_assert(Capacity>0);
  static constexpr size_t capacity=Capacity;
  struct Fence {uint64_t position=0,pending=0;};
  using Read=PluginEditRead;
private:
  static_assert(std::atomic<uint64_t>::is_always_lock_free);
  std::array<PluginEdit,capacity> events_{};
  std::atomic<uint64_t> write_{0},read_{0};
public:
  // Producer only. The consumer can only make more space after this check.
  bool full()const noexcept {
    return write_.load(std::memory_order_relaxed)-read_.load(std::memory_order_acquire)>=capacity;
  }
  bool push(const PluginEdit &event)noexcept {
    const auto write=write_.load(std::memory_order_relaxed);
    if(write-read_.load(std::memory_order_acquire)>=capacity)return false;
    events_[write%capacity]=event;
    write_.store(write+1,std::memory_order_release);return true;
  }
  // Call on the producer or consumer, not an unrelated observer. The caller's
  // own cursor cannot move during this read; the other owner only makes progress
  // toward it. Native arming holds the consumer while the producer captures it.
  Fence fence()const noexcept {
    const auto write=write_.load(std::memory_order_acquire);
    return {write,write-read_.load(std::memory_order_acquire)};
  }
  // Consumer only. Invalid/stale bounds never consume later input. Keeping the
  // same fence across bounded polling turns prevents a busy editor from moving
  // the finish line. Adapter must separately guard the provider incarnation.
  Read popBefore(Fence end,PluginEdit &event)noexcept {
    const auto read=read_.load(std::memory_order_relaxed);
    const auto available=write_.load(std::memory_order_acquire)-read;
    const auto remaining=end.position-read;
    if(remaining>available||remaining>capacity)return Read::InvalidFence;
    if(!remaining)return Read::Complete;
    event=events_[read%capacity];
    read_.store(read+1,std::memory_order_release);return Read::Event;
  }
  bool pop(PluginEdit &event)noexcept{return popBefore(fence(),event)==Read::Event;}
};
// Preserve the VST3 notification budget and all existing users. Native adapters
// with a different existing budget share the same cursor/fence implementation.
using PluginEditQueue=BoundedPluginEditQueue<8192>;
}
