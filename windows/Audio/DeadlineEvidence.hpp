#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace ScreamSeq {
// Single render-service owner; inspect/reset only after joining that owner.
// Fixed storage, no clocks, atomics, allocation, logging or overwriting of the
// earliest failures. The device enforces the stopped-only read boundary.
class DeadlineEvidence {
public:
  static constexpr std::size_t capacity=32;
  struct Event {
    std::uint64_t callback,frameOffset,generation,serviceStartTicks,qpcFrequency;
    std::uint64_t wakeGapNanoseconds,preCallbackNanoseconds,callbackNanoseconds,postCallbackNanoseconds,serviceNanoseconds;
    std::uint64_t renderBudgetNanoseconds,periodNanoseconds;
    std::uint32_t frames,padding,sampleRate;
  };
  struct Snapshot {std::array<Event,capacity> events{};std::size_t count=0;std::uint64_t omitted=0;};
  void record(const Event &event)noexcept {
    if(data_.count<capacity)data_.events[data_.count++]=event;
    else if(data_.omitted<std::numeric_limits<std::uint64_t>::max())++data_.omitted;
  }
  void reset()noexcept{data_={};}
  Snapshot snapshot()const noexcept{return data_;}
private:
  Snapshot data_;
};
}
