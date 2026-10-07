#pragma once
#include "HostClock.hpp"
#include "editor/RecordingClock.hpp"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
namespace ScreamSeq {
struct RenderTime {
  std::uint64_t hostTime=0,generation=0;
  std::uint32_t sampleRate=0;
  bool valid=false,discontinuity=false,stopped=false;
  RenderTime offset(std::uint32_t frames) const noexcept {
    auto result=*this;
    if(valid&&sampleRate){const auto delta=std::uint64_t(frames)*hostTicksPerSecond/sampleRate;
      if(hostTime>UINT64_MAX-delta){result.hostTime=0;result.valid=false;}else result.hostTime+=delta;}
    return result;
  }
};
// The document owner retains this independently of the renderer. Atomic bounds
// and generation prevent a take from joining unrelated restarted streams.
struct RecordingTimeline {
  explicit RecordingTimeline(std::shared_ptr<const Tracker::RecordingClock> value):clock(std::move(value)){}
  std::shared_ptr<const Tracker::RecordingClock> clock;
  std::atomic<std::uint64_t> generation{0},firstHostTime{0},endHostTime{0},discontinuities{0};
  // True is published after Stop's final bound; a live unmapped gap must not
  // be mistaken for a stopped stream when closing held notes.
  std::atomic<bool> stopped{false};
  std::optional<Tracker::RecordedPosition> locate(std::uint64_t time,std::uint64_t expectedGeneration) const noexcept {
    const auto before=generation.load(std::memory_order_acquire);
    if(!expectedGeneration||before!=expectedGeneration)return {};
    const auto first=firstHostTime.load(std::memory_order_acquire),end=endHostTime.load(std::memory_order_acquire);
    if(!first||time<first||time>=end)return {};
    const auto result=clock->locate(time);
    return generation.load(std::memory_order_acquire)==before?result:std::nullopt;
  }
};
// Callback-owned pure correlation arithmetic. submitted includes primed silence.
// S_FALSE/startup zero produce an unmapped buffer. Regressions/confirmed missed
// device frames quarantine timing until explicit stream restart, not playback.
class PresentationClock {
  std::uint32_t rate_=0;
  std::uint64_t frequency_=0,submitted_=0,generation_=0,previousPosition_=0,previousQpc_=0;
  bool quarantined_=false,seen_=false;
public:
  void reset(std::uint32_t rate,std::uint64_t frequency,std::uint64_t primedFrames,std::uint64_t generation) noexcept {
    rate_=rate;frequency_=frequency;submitted_=primedFrames;generation_=generation;previousPosition_=previousQpc_=0;quarantined_=seen_=false;
  }
  RenderTime buffer(std::uint64_t position,std::uint64_t qpc,std::uint64_t now,bool accurate,bool missedFrames=false) noexcept {
    RenderTime result{0,generation_,rate_,false,false};
    if(quarantined_)return result;
    if(!rate_||!frequency_||!generation_||!accurate||!qpc||!now)return result;
    if(qpc>now || now-qpc>5000000)return result;
    const double deviceFrames=double(position)*rate_/double(frequency_);
    if(missedFrames || (seen_&&(position<previousPosition_||qpc<previousQpc_)) || deviceFrames>double(submitted_)+2.) {
      quarantined_=true;++generation_;result.generation=generation_;result.discontinuity=true;return result;
    }
    if(!position)return result; // Startup zero is not a presentation origin.
    seen_=true;previousPosition_=position;previousQpc_=qpc;
    const double lead=double(submitted_)/rate_-double(position)/double(frequency_);
    const double origin=double(qpc)+lead*hostTicksPerSecond;
    if(lead < -2./rate_ || lead > 2. || !std::isfinite(origin)||origin<1||origin>=double(UINT64_MAX))return result;
    result.hostTime=std::uint64_t(origin);result.valid=true;return result;
  }
  void submitted(std::uint32_t frames) noexcept {
    if(submitted_>UINT64_MAX-frames){quarantined_=true;++generation_;}else submitted_+=frames;
  }
};
}
