#pragma once
#include "PluginTypes.hpp"
#include "editor/AudioPortTrim.hpp"
#include <array>
#include <memory>
#include <span>
namespace Tracker {
// Own scratch for borrowed stage ports. Prepared once with the routing plan;
// a shared processor evaluates these gains only once even during a handoff.
class GraphStageTrims {
  using Buffer=std::array<float,8192>;
  AudioPortTrims neutral_;
  const AudioPortTrims *spec_=&neutral_;
  AudioTrimRuntime runtime_;
  std::array<std::unique_ptr<Buffer>,64> inputs_,outputs_;
  std::array<PluginAudioInput,64> supplied_{};
  bool started_=false;
public:
  std::string owner;
  uint64_t inputMask=0,outputMask=0;
  GraphStageTrims(std::string key,uint64_t in,uint64_t out,double rate,const AudioPortTrims &spec):owner(std::move(key)),inputMask(in),outputMask(out){
    std::vector<std::string> keys;for(bool output:{false,true})for(uint32_t p=0;p<64;++p)keys.push_back(audioTrimPort(output,p));runtime_=AudioTrimRuntime(std::move(keys),spec,rate);
    for(uint32_t p=1;p<64;++p){if(in&(uint64_t(1)<<p))inputs_[p]=std::make_unique<Buffer>();if(out&(uint64_t(1)<<p))outputs_[p]=std::make_unique<Buffer>();}
  }
  void controls(const AudioPortTrims &spec) noexcept {spec_=&spec;if(!started_)runtime_.initial(spec);}
  void reader(AudioTrimSourceReader read,void *context) noexcept {runtime_.sourceReader(read,context);}
  std::span<const PluginAudioInput> begin(float *main,uint32_t frames,uint64_t position,std::span<const PluginAudioInput> inputs) noexcept {
    started_=true;runtime_.begin(*spec_,position);runtime_.apply(0,main,frames,position);size_t count=0;
    for(const auto &input:inputs)if(input.bus<64&&inputs_[input.bus]){auto *buffer=inputs_[input.bus]->data();if(input.samples)std::copy_n(input.samples,frames*2,buffer);else std::fill_n(buffer,frames*2,0.f);runtime_.apply(input.bus,buffer,frames,position);supplied_[count++]={input.bus,buffer};}
    return {supplied_.data(),count};
  }
  void output(uint32_t port,const float *samples,uint32_t frames,uint64_t position) noexcept {if(port&&port<64&&outputs_[port]){auto *buffer=outputs_[port]->data();if(samples)std::copy_n(samples,frames*2,buffer);else std::fill_n(buffer,frames*2,0.f);runtime_.apply(64+port,buffer,frames,position);}}
  bool finish(float *main,uint32_t frames,uint64_t position) noexcept {runtime_.apply(64,main,frames,position);return runtime_.valid();}
  const float *output(uint32_t port) const noexcept {return port<64&&outputs_[port]?outputs_[port]->data():nullptr;}
  size_t bytes() const noexcept {size_t n=sizeof(*this)+runtime_.bytes()+owner.capacity();for(uint32_t p=1;p<64;++p)n+=(bool(inputs_[p])+bool(outputs_[p]))*sizeof(Buffer);return n;}
};
}
