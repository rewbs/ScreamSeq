#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

namespace Tracker {
// Prepared dry-delay storage and a continuous linear wet/dry fade. Processing
// remains clocked during bypass. Sources without dry audio fade to silence.
class ProcessorBypass {
public:
  struct Latency {
    std::vector<float> delay;
    size_t cursor=0;
    uint32_t warm=0,fade=0;
    size_t storageBytes() const noexcept {return sizeof(*this)+delay.capacity()*sizeof(float);}
  };
private:
  std::atomic<bool> requested_{false};
  double wet_=1,from_=1,to_=1;
  uint32_t transition_=240,elapsed_=240;
  bool source_=false,blockUnity_=true;
  std::vector<float> delay_;
  size_t cursor_=0;
  Latency *pendingLatency_=nullptr; // Borrowed from a retained prepared publication.
  std::atomic<bool> latencySettled_{true};
  struct Block {std::array<float,8192> dry{};std::array<float,4096> wet{};};
  std::unique_ptr<Block> block_=std::make_unique<Block>();
  static float delaySample(std::vector<float> &delay,size_t &cursor,float input) noexcept {
    if(delay.empty())return input;
    const auto result=delay[cursor];delay[cursor]=input;if(++cursor==delay.size())cursor=0;return result;
  }
  float drySample(float input,uint32_t channel) noexcept {
    auto value=delaySample(delay_,cursor_,input);
    if(auto *next=pendingLatency_){
      const auto other=delaySample(next->delay,next->cursor,input);
      if(!next->warm){const auto t=std::min(1.,double(next->fade)/transition_);const auto mix=t*t*(3-2*t);value=float(value+(other-value)*mix);}
      if(channel==1){
        if(next->warm)--next->warm;
        else if(++next->fade>transition_){delay_.swap(next->delay);std::swap(cursor_,next->cursor);pendingLatency_=nullptr;latencySettled_.store(true,std::memory_order_release);}
      }
    }
    return value;
  }
public:
  void prepare(double rate,uint32_t latency,bool source,bool bypass) {
    if(!std::isfinite(rate)||rate<8000||rate>384000||latency>rate*10)throw std::invalid_argument("Invalid bypass rate or latency");
    transition_=uint32_t(std::round(rate*.005));elapsed_=transition_;source_=source;wet_=from_=to_=bypass?0:1;requested_=bypass;
    delay_.assign(source?0:size_t(latency)*2,0);cursor_=0;
  }
  void latency(uint32_t samples) { // Control owner, with rendering quiescent.
    pendingLatency_=nullptr;latencySettled_.store(true,std::memory_order_release);
    if(!source_&&delay_.size()!=size_t(samples)*2){delay_.assign(size_t(samples)*2,0);cursor_=0;}
  }
  std::shared_ptr<Latency> prepareLatency(uint32_t samples) const {
    if(!latencySettled_.load(std::memory_order_acquire))throw std::runtime_error("A bypass latency transition is still warming");
    auto result=std::make_shared<Latency>();result->delay.resize(source_?0:size_t(samples)*2);result->warm=source_?0:samples;return result;
  }
  void adoptLatency(Latency &next) noexcept {
    // Old history keeps sounding while the prepared ring fills. No history
    // copy or ring disposal is performed by the callback, even for long PDC.
    if(next.delay.size()==delay_.size())return;
    pendingLatency_=&next;latencySettled_.store(false,std::memory_order_release);
  }
  bool latencyReady() const noexcept {return latencySettled_.load(std::memory_order_acquire);}
  void set(bool value) noexcept {requested_.store(value,std::memory_order_release);}
  bool requested() const noexcept{return requested_.load(std::memory_order_acquire);}
  size_t storageBytes() const noexcept{return sizeof(Block)+delay_.size()*sizeof(float);}
  void begin(const float *input,uint32_t frames) noexcept {
    if(frames>4096)return;
    const bool bypass=requested_.load(std::memory_order_acquire);
    const double target=bypass?0:1;
    if(to_!=target){from_=wet_;to_=target;elapsed_=0;}
    blockUnity_=wet_==1&&to_==1;
    // Keep latent dry history warm, but do not touch scratch for the common
    // zero-latency enabled path (or an instrument with no dry input).
    if(blockUnity_) {
      if(!delay_.empty()||pendingLatency_)for(uint32_t i=0;i<frames*2;++i)drySample(input[i],i%2);
      return;
    }
    for(uint32_t i=0;i<frames;++i) {
      block_->wet[i]=float(wet_);
      if(elapsed_<transition_)++elapsed_;
      wet_=from_+(to_-from_)*(double(elapsed_)/transition_);
      for(uint32_t c=0;c<2;++c) {
        float sample=source_?0:input[i*2+c];
        sample=drySample(sample,c);
        block_->dry[i*2+c]=sample;
      }
    }
  }
  void finish(float *output,uint32_t frames,bool auxiliary=false) const noexcept {
    if(frames>4096||blockUnity_)return;
    for(uint32_t i=0;i<frames;++i)for(uint32_t c=0;c<2;++c) {
      const auto sample=i*2+c;const float wet=block_->wet[i];
      if(wet==1)continue; // Normal enabled output remains bit-identical.
      const float dry=auxiliary?0:block_->dry[sample];
      output[sample]=wet==0?dry:dry+(output[sample]-dry)*wet;
    }
  }
};
}
