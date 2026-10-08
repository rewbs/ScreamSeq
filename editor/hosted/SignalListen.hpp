#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>

namespace Tracker {
// One temporary monitor destination. The graph continues rendering normally;
// only the final device mix is replaced. Token zero means the normal mix.
// The control owner publishes one atomic token/gain pair. The audio owner
// captures at most two taps while a linear, sample-clocked crossfade is active.
// A newer request waits for that short transition, then replaces its target.
class SignalListen {
public:
  struct Target {uint32_t token=0;float gain=1;};
  static constexpr uint32_t maximumFrames=4096;
private:
  static uint64_t pack(Target t) noexcept {return uint64_t(t.token)<<32 | std::bit_cast<uint32_t>(t.gain);}
  static Target unpack(uint64_t v) noexcept {return {uint32_t(v>>32),std::bit_cast<float>(uint32_t(v))};}
  static constexpr uint64_t normal=0x3f800000;
  std::atomic<uint64_t> requested_{normal},settled_{normal};
  struct Buffers {std::array<float,maximumFrames*2> from{},to{};};
  std::unique_ptr<Buffers> samples_=std::make_unique<Buffers>();
  Target from_,to_;
  uint32_t transition_,elapsed_=0;
  uint64_t position_=0;
public:
  explicit SignalListen(double rate) {
    if(!std::isfinite(rate)||rate<8000||rate>384000)throw std::invalid_argument("Invalid monitor sample rate");
    transition_=uint32_t(std::round(rate*.005));
  }
  void select(uint32_t token,float gain=1) {
    if(!std::isfinite(gain)||gain<.001f||gain>4.f)throw std::invalid_argument("Monitor gain is outside its supported range");
    requested_.store(pack({token,token?gain:1.f}),std::memory_order_release);
  }
  Target requested() const noexcept {return unpack(requested_.load(std::memory_order_acquire));}
  Target settled() const noexcept {return unpack(settled_.load(std::memory_order_acquire));}
  bool pending() const noexcept {return requested_.load(std::memory_order_acquire)!=settled_.load(std::memory_order_acquire);}
  uint32_t transitionFrames() const noexcept {return transition_;}
  void begin(uint64_t position) noexcept {
    position_=position;
    if(pack(from_)==pack(to_)) {
      const auto next=requested_.load(std::memory_order_acquire);
      if(next!=pack(to_)){to_=unpack(next);elapsed_=0;}
    }
    if(from_.token)samples_->from.fill(0);
    if(to_.token)samples_->to.fill(0);
  }
  void capture(uint32_t token,const float *input,uint32_t frames,uint64_t position) noexcept {
    if(!token||!input||frames>maximumFrames||position<position_||position-position_>=maximumFrames)return;
    const auto offset=uint32_t(position-position_);const auto count=std::min(frames,maximumFrames-offset);
    auto copy=[&](auto &buffer){for(uint32_t i=0;i<count*2;++i)buffer[offset*2+i]=std::isfinite(input[i])?input[i]:0;};
    if(token==from_.token)copy(samples_->from);
    if(token==to_.token)copy(samples_->to);
  }
  void apply(float *output,uint32_t frames) noexcept {
    if(!output||frames>maximumFrames)return;
    if(!from_.token&&!to_.token)return; // Preserve the normal path bit for bit.
    for(uint32_t f=0;f<frames;++f) {
      const double amount=std::min(1.0,double(elapsed_)/transition_);
      for(uint32_t c=0;c<2;++c) {
        const auto i=f*2+c;
        const double a=(from_.token?samples_->from[i]:output[i])*from_.gain;
        const double b=(to_.token?samples_->to[i]:output[i])*to_.gain;
        output[i]=float(a+(b-a)*amount);
      }
      if(elapsed_<transition_)++elapsed_;
    }
    if(elapsed_==transition_){from_=to_;settled_.store(pack(to_),std::memory_order_release);}
  }
};
}
