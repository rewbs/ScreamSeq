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
    // Optional diagnostic CPU-cycle counts, never converted to elapsed time.
    // Previous callback supplies a nearby comparison, not a scheduling verdict.
    std::uint64_t threadCycles=0,previousThreadCycles=0,previousCallbackNanoseconds=0;
    std::uint32_t previousFrames=0;
    bool threadCyclesValid=false,previousThreadCyclesValid=false;
  };
  struct Snapshot {std::array<Event,capacity> events{};std::size_t count=0;std::uint64_t omitted=0,cycleSamples=0,cycleFailures=0;};
  void observeCycles(bool valid)noexcept{
    auto &count=valid?data_.cycleSamples:data_.cycleFailures;
    if(count<std::numeric_limits<std::uint64_t>::max())++count;
  }
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
