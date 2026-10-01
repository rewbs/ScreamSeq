#pragma once
#include "PluginTypes.hpp"
#include <algorithm>
#include <array>
#include <memory>
#include <stdexcept>

namespace Tracker {
// Prepared routing plans share this wrapper (not just its processor) when the
// dependency compiler proves equal inputs. Both plans must use the same mix
// chunk boundaries. Output fan-out never advances DSP, MIDI or automation twice.
// Auxiliary outputs remain in the processor's prepared buffers until the next
// chunk; callers route them before advancing this wrapper again.
template<class Processor> class RenderOnce {
  static constexpr uint32_t maximumFrames=4096;
  std::shared_ptr<Processor> processor_;
  std::array<float,maximumFrames*2> output_{};
  uint64_t position_=0;
  uint32_t frames_=0;
  bool valid_=false,okay_=false;
public:
  explicit RenderOnce(std::shared_ptr<Processor> processor):processor_(std::move(processor)) {
    if(!processor_)throw std::invalid_argument("A shared render requires a prepared processor");
  }
  RenderOnce(const RenderOnce &)=delete;
  RenderOnce &operator=(const RenderOnce &)=delete;
  Processor &processor() noexcept{return *processor_;}
  static constexpr size_t storageBytes() noexcept{return sizeof(RenderOnce);}
  bool process(float *buffer,uint32_t frames,uint64_t position,std::span<const PluginAudioInput> inputs={}) noexcept {
    if(!buffer || !frames || frames>maximumFrames || position>UINT64_MAX-frames)return false;
    if(valid_ && position==position_) {
      if(frames!=frames_)return false; // Misaligned plans must not rerun DSP.
      std::copy_n(output_.data(),frames*2,buffer);return okay_;
    }
    if(valid_ && position<position_+frames_)return false;
    position_=position;frames_=frames;valid_=true;
    okay_=processor_->process(buffer,frames,position,inputs);
    if(!okay_)std::fill_n(buffer,frames*2,0.f);
    std::copy_n(buffer,frames*2,output_.data());
    return okay_;
  }
};
} // namespace Tracker
