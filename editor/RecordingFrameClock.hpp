#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Tracker {
// One audio writer and any number of control readers. Each successful render
// publishes a presentation interval, absolute device-frame origin and stream
// generation. A timestamp outside retained coverage is unknown, never "now".
// Payload atomics make ring overwrite race-free; version checks reject torn
// reads. Construct/retire this object off the callback.
class RecordingFrameClock {
  struct Slot {
    std::atomic<uint64_t> version{0},generation{0},start{0},end{0},frame{0};
    std::atomic<uint32_t> frames{0};
    std::atomic<double> rate{0};
  };
public:
  static constexpr size_t capacity=4096;
  static constexpr uint64_t maximumCanonicalFrame=uint64_t(48000)*604800;
private:
  std::array<Slot,capacity> slots_{};
  std::atomic<uint64_t> head_{0};
public:
  // Interpolate between the actual presentation boundaries. Native adapters
  // own clock correlation, lifecycle bounds and discontinuity detection.
  bool publish(uint64_t start,uint64_t end,uint64_t frame,uint32_t frames,
               double rate,uint64_t generation) noexcept {
    if(!start||end<=start||!frames||!std::isfinite(rate)||rate<=0||rate>UINT32_MAX||!generation||
       static_cast<long double>(frame)>static_cast<long double>(rate)*604800||
       frame>UINT64_MAX-frames)return false;
    const auto index=head_.load(std::memory_order_relaxed);
    if(index>=UINT64_MAX/2-1)return false;
    auto &slot=slots_[index%capacity];
    slot.version.store(index*2+1,std::memory_order_seq_cst);
    slot.generation.store(generation,std::memory_order_seq_cst);
    slot.start.store(start,std::memory_order_seq_cst);slot.end.store(end,std::memory_order_seq_cst);
    slot.frame.store(frame,std::memory_order_seq_cst);slot.frames.store(frames,std::memory_order_seq_cst);
    slot.rate.store(rate,std::memory_order_seq_cst);
    slot.version.store(index*2+2,std::memory_order_seq_cst);
    head_.store(index+1,std::memory_order_release);return true;
  }
  std::optional<uint64_t> locate(uint64_t timestamp,uint64_t generation) const noexcept {
    if(!timestamp||!generation)return {};
    const auto head=head_.load(std::memory_order_acquire),begin=head>capacity?head-capacity:0;
    for(auto cursor=head;cursor>begin;) {
      const auto index=--cursor;const auto &slot=slots_[index%capacity];
      if(slot.version.load(std::memory_order_seq_cst)!=index*2+2)continue;
      const auto storedGeneration=slot.generation.load(std::memory_order_seq_cst);
      const auto start=slot.start.load(std::memory_order_seq_cst),end=slot.end.load(std::memory_order_seq_cst);
      const auto frame=slot.frame.load(std::memory_order_seq_cst);
      const auto frames=slot.frames.load(std::memory_order_seq_cst);
      const auto rate=slot.rate.load(std::memory_order_seq_cst);
      if(slot.version.load(std::memory_order_seq_cst)!=index*2+2)continue;
      if(storedGeneration!=generation||timestamp<start||timestamp>=end)continue;
      const long double position=(static_cast<long double>(frame)+
        static_cast<long double>(timestamp-start)*frames/(end-start))*48000/rate;
      if(!std::isfinite(position)||position<0||position>maximumCanonicalFrame)return {};
      return uint64_t(position);
    }
    return {};
  }
};
// Retain independently of the device/renderer while a control owner drains its
// event queue. Generations are caller-assigned and never reused for a new stream.
// publish is audio-writer-only; close/invalidate may run after that writer has
// quiesced. Missing presentation intervals remain holes in the ring.
class RecordingFrameTimeline {
  RecordingFrameClock clock_;
  std::atomic<uint64_t> generation_{0},first_{0},end_{0};
  std::atomic<bool> stopped_{false};
  // Single-writer lifecycle state, including before the first valid interval.
  // invalidate is terminal for this retained timeline; a replacement playback
  // owns a new object. A generation change cannot resurrect a failed renderer.
  uint64_t newestGeneration_=0,lastStart_=0,lastFrameEnd_=0;
  double rate_=0;
  bool invalidated_=false;
public:
  bool publish(uint64_t start,uint64_t end,uint64_t frame,uint32_t frames,double rate,uint64_t generation) noexcept {
    if(invalidated_||!generation||generation<newestGeneration_||
       (generation==newestGeneration_&&stopped_.load(std::memory_order_relaxed)))return false;
    const auto previous=generation_.load(std::memory_order_relaxed);
    // Correlated device boundaries can overlap slightly as their estimate
    // settles. Reject regression of the boundaries or the musical frame origin;
    // do not turn a forward unmapped interval into fabricated coverage.
    if(previous==generation&&(start<lastStart_||end<=end_.load(std::memory_order_relaxed)||
       frame<lastFrameEnd_||rate!=rate_)) {invalidate();return false;}
    if(!clock_.publish(start,end,frame,frames,rate,generation))return false;
    if(previous!=generation) {
      generation_.store(0,std::memory_order_release);first_.store(0);end_.store(0);stopped_.store(false);
    }
    if(!first_.load(std::memory_order_relaxed))first_.store(start,std::memory_order_release);
    newestGeneration_=generation;lastStart_=start;lastFrameEnd_=frame+frames;rate_=rate;
    end_.store(end,std::memory_order_release);generation_.store(generation,std::memory_order_release);return true;
  }
  void close(uint64_t generation,uint64_t time) noexcept {
    if(invalidated_||!generation||generation<newestGeneration_)return;
    // Stop can precede the first mapped callback. Remember it so a delayed
    // publication cannot start a stream which has already stopped.
    if(!newestGeneration_) {
      newestGeneration_=generation;stopped_.store(true,std::memory_order_release);return;
    }
    if(generation_.load(std::memory_order_acquire)!=generation)return;
    const auto end=end_.load(std::memory_order_relaxed);
    if(time&&time<end)end_.store(time,std::memory_order_release);
    stopped_.store(true,std::memory_order_release);
  }
  void invalidate() noexcept {invalidated_=true;generation_.store(0,std::memory_order_release);}
  uint64_t generation() const noexcept {return generation_.load(std::memory_order_acquire);}
  bool stopped() const noexcept {return stopped_.load(std::memory_order_acquire);}
  std::optional<uint64_t> stoppedEndTime(uint64_t expectedGeneration) const noexcept {
    if(!expectedGeneration||!stopped()||generation()!=expectedGeneration)return {};
    const auto end=end_.load(std::memory_order_acquire);
    return generation()==expectedGeneration?std::optional<uint64_t>(end):std::nullopt;
  }
  std::optional<uint64_t> locate(uint64_t time,uint64_t expectedGeneration) const noexcept {
    if(!expectedGeneration||generation()!=expectedGeneration)return {};
    const auto first=first_.load(std::memory_order_acquire),end=end_.load(std::memory_order_acquire);
    if(!first||time<first||time>=end)return {};
    const auto result=clock_.locate(time,expectedGeneration);
    return generation()==expectedGeneration&&time<end_.load(std::memory_order_acquire)?result:std::nullopt;
  }
};
static_assert(std::atomic<uint64_t>::is_always_lock_free);
static_assert(std::atomic<uint32_t>::is_always_lock_free);
static_assert(std::atomic<double>::is_always_lock_free);
}
