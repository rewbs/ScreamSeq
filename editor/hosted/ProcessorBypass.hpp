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
  std::atomic<bool> requested_{false};
  double wet_=1,from_=1,to_=1;
  uint32_t transition_=240,elapsed_=240;
  bool source_=false,blockUnity_=true;
  std::vector<float> delay_;
  size_t cursor_=0;
  struct Block {std::array<float,8192> dry{};std::array<float,4096> wet{};};
  std::unique_ptr<Block> block_=std::make_unique<Block>();
public:
  void prepare(double rate,uint32_t latency,bool source,bool bypass) {
    if(!std::isfinite(rate)||rate<8000||rate>384000||latency>rate*10)throw std::invalid_argument("Invalid bypass rate or latency");
    transition_=uint32_t(std::round(rate*.005));elapsed_=transition_;source_=source;wet_=from_=to_=bypass?0:1;requested_=bypass;
    delay_.assign(source?0:size_t(latency)*2,0);cursor_=0;
  }
  void latency(uint32_t samples) { // Control owner, with rendering quiescent.
    if(!source_&&delay_.size()!=size_t(samples)*2){delay_.assign(size_t(samples)*2,0);cursor_=0;}
  }
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
      if(!delay_.empty())for(uint32_t i=0;i<frames*2;++i){delay_[cursor_]=input[i];if(++cursor_==delay_.size())cursor_=0;}
      return;
    }
    for(uint32_t i=0;i<frames;++i) {
      block_->wet[i]=float(wet_);
      if(elapsed_<transition_)++elapsed_;
      wet_=from_+(to_-from_)*(double(elapsed_)/transition_);
      for(uint32_t c=0;c<2;++c) {
        float sample=source_?0:input[i*2+c];
        if(!delay_.empty()){std::swap(sample,delay_[cursor_]);if(++cursor_==delay_.size())cursor_=0;}
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
