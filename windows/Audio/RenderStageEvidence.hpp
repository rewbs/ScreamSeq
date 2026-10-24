#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace ScreamSeq {
// Opt-in diagnostic only. Configure and read on the stopped owner; one audio
// writer records into fixed storage. The clock must be bounded, allocation-free
// and nonthrowing. No CPU-time or scheduling conclusion follows from wall time.
class RenderStageEvidence {
public:
  enum class Stage : size_t { Preparation, Engine, Hosted, Timing, Count };
  using Clock = uint64_t (*)() noexcept;
  static constexpr size_t capacity=32, stages=size_t(Stage::Count);
  struct Event {
    uint64_t ordinal=0,frameOffset=0,generation=0,startTicks=0,totalTicks=0;
    std::array<uint64_t,stages> stageTicks{};
    uint32_t frames=0,rate=0;
    bool completed=false;
  };
  struct Snapshot {
    std::array<Event,capacity> events{};
    uint64_t frequency=0,callbacks=0,omitted=0,invalidClocks=0;
    size_t count=0;
  };
  // All previous evidence belongs to the old stopped playback owner.
  bool reset(Clock clock,uint64_t frequency) noexcept {
    data_={};clock_=nullptr;
    if(!clock||!frequency||frequency>UINT32_MAX)return false;
    clock_=clock;data_.frequency=frequency;return true;
  }
  Snapshot snapshot()const noexcept{return data_;}
  class Sample {
    RenderStageEvidence *owner_=nullptr;
    Event event_{};
    uint64_t last_=0;
    Stage stage_=Stage::Preparation;
    bool valid_=true;
    void mark() noexcept {
      const auto now=owner_->clock_();
      if(!now||now<last_){valid_=false;last_=now;return;}
      if(valid_)event_.stageTicks[size_t(stage_)]+=now-last_;
      last_=now;
    }
  public:
    Sample(RenderStageEvidence *owner,uint32_t frames,uint32_t rate,uint64_t frameOffset,uint64_t generation) noexcept {
      if(!owner||!owner->clock_||!frames||!rate)return;
      owner_=owner;event_.frames=frames;event_.rate=rate;event_.frameOffset=frameOffset;event_.generation=generation;
      event_.ordinal=++owner_->data_.callbacks;event_.startTicks=last_=owner_->clock_();
      valid_=last_!=0;
    }
    Sample(const Sample &)=delete;
    Sample &operator=(const Sample &)=delete;
    void enter(Stage next) noexcept {
      if(!owner_)return;
      mark();
      if(size_t(next)>=stages){valid_=false;return;}
      stage_=next;
    }
    void completed() noexcept {event_.completed=true;}
    ~Sample() noexcept {
      if(!owner_)return;
      mark();
      if(!valid_){++owner_->data_.invalidClocks;return;}
      event_.totalTicks=last_-event_.startTicks;
      const auto budget=uint64_t(event_.frames)*owner_->data_.frequency/event_.rate;
      if(event_.totalTicks<=budget)return;
      auto &data=owner_->data_;
      if(data.count<capacity)data.events[data.count++]=event_;else ++data.omitted;
    }
  };
private:
  Clock clock_=nullptr;
  Snapshot data_{};
};
}
