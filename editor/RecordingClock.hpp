#pragma once
#include <array>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <optional>
namespace Tracker {
struct RecordedPosition {
  uint32_t sequence=0,order=0,pattern=0,position=0;
};
// One audio-thread writer, control-thread readers. Every payload field is atomic:
// wrapping the ring while a reader samples a slot cannot create a data race.
class RecordingClock {
  struct Slot {
    std::atomic<uint64_t> version{0},start{0},end{0};
    std::atomic<uint32_t> sequence{0},order{0},pattern{0};
    std::atomic<double> position{0},unitsPerTick{0};
  };
  static constexpr size_t capacity=32768;
  std::array<Slot,capacity> slots_{};
  std::atomic<uint64_t> head_{0};
  uint64_t previousStart_=0,previousEnd_=0;
  RecordedPosition previousLocation_{};
  double previousPosition_=0,previousUnits_=0;
public:
  void publish(uint64_t start,uint64_t end,RecordedPosition location,double position,double unitsPerHostTick) noexcept {
    if(!start||end<=start||!std::isfinite(position)||!std::isfinite(unitsPerHostTick))return;
    const auto index=head_.load(std::memory_order_relaxed);
    if(index&&start==previousEnd_&&location.sequence==previousLocation_.sequence&&location.order==previousLocation_.order&&
       location.pattern==previousLocation_.pattern&&unitsPerHostTick==previousUnits_&&
       std::abs(position-(previousPosition_+double(start-previousStart_)*previousUnits_))<1e-6) {
      // Extending a linear segment changes only its atomic end; readers keep
      // seeing a complete, self-consistent clock mapping while it grows.
      slots_[(index-1)%capacity].end.store(end,std::memory_order_seq_cst);previousEnd_=end;return;
    }
    previousStart_=start;previousEnd_=end;previousLocation_=location;previousPosition_=position;previousUnits_=unitsPerHostTick;
    auto &slot=slots_[index%capacity];
    slot.version.store(index*2+1,std::memory_order_seq_cst);
    slot.start.store(start,std::memory_order_seq_cst);slot.end.store(end,std::memory_order_seq_cst);
    slot.sequence.store(location.sequence,std::memory_order_seq_cst);slot.order.store(location.order,std::memory_order_seq_cst);
    slot.pattern.store(location.pattern,std::memory_order_seq_cst);slot.position.store(position,std::memory_order_seq_cst);
    slot.unitsPerTick.store(unitsPerHostTick,std::memory_order_seq_cst);
    slot.version.store(index*2+2,std::memory_order_seq_cst);head_.store(index+1,std::memory_order_release);
  }
  std::optional<RecordedPosition> locate(uint64_t timestamp) const noexcept {
    const auto head=head_.load(std::memory_order_acquire),begin=head>capacity?head-capacity:0;
    for(auto cursor=head;cursor>begin;) {
      const auto index=--cursor;const auto &slot=slots_[index%capacity];
      if(slot.version.load(std::memory_order_seq_cst)!=index*2+2)continue;
      const auto start=slot.start.load(std::memory_order_seq_cst),end=slot.end.load(std::memory_order_seq_cst);
      if(timestamp<start||timestamp>=end)continue;
      RecordedPosition result{slot.sequence.load(std::memory_order_seq_cst),slot.order.load(std::memory_order_seq_cst),slot.pattern.load(std::memory_order_seq_cst),0};
      const auto position=slot.position.load(std::memory_order_seq_cst)+double(timestamp-start)*slot.unitsPerTick.load(std::memory_order_seq_cst);
      if(slot.version.load(std::memory_order_seq_cst)!=index*2+2)continue;
      result.position=uint32_t(std::clamp(std::round(position),0.,double(UINT32_MAX)));
      return result;
    }
    return std::nullopt;
  }
};
}
