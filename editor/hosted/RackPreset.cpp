#include "HostedAudio.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Tracker {
size_t NativePlugin::Preset::storageBytes() const noexcept {
  return sizeof(*this)+parameters.capacity()*sizeof(PluginParameter)+manualValues.capacity()*sizeof(double)+vendor->preparedStorageBytes()+(previous?previous->preparedStorageBytes():0);
}
std::shared_ptr<NativePlugin::Preset> NativePlugin::preparePreset(const PluginState &state,bool offline,const std::shared_ptr<Preset> &previous) const {
  const auto *current=publishedVendor();
  if(!presetSettled_.load(std::memory_order_acquire)||(previous&&previous->vendor.get()!=current))
    throw std::runtime_error("A preset replacement is still preparing; retry the edit");
  if(isInstrument())throw std::runtime_error("Instrument preset replacement needs stopped playback to reset held voices");
  if(state.descriptor!=descriptor_||state.instanceID!=instanceID_)
    throw std::invalid_argument("A live preset must retain the plugin identity");
  auto next=std::make_shared<Preset>();next->expected=const_cast<NativePlugin *>(current);
  next->previous=previous?previous->vendor:nullptr;
  next->vendor=std::make_shared<NativePlugin>(state,rate_,offline);next->parameters=next->vendor->parameters();
  const auto &vendor=*next->vendor;const auto old=parameters();
  if(vendor.audioLayout()!=audioLayout_||vendor.preparedInputs_!=preparedInputs_||vendor.preparedOutputs_!=preparedOutputs_||
     std::llround(vendor.latency()*rate_)!=std::llround(latency()*rate_)||old.size()!=next->parameters.size())
    throw std::invalid_argument("This preset changes ports, latency or parameter identity; stop playback before loading it");
  for(size_t i=0;i<old.size();++i){const auto &a=old[i],&b=next->parameters[i];
    if(a.id!=b.id||a.min!=b.min||a.max!=b.max||a.step!=b.step||a.writable!=b.writable||a.continuous!=b.continuous)
      throw std::invalid_argument("This preset changes the parameter catalogue; stop playback before loading it");
    if(b.writable)next->vendor->includeParameterRange(b.id,b.min,b.max);
  }
  for(const auto &base:baselines_){const auto found=std::find_if(next->parameters.begin(),next->parameters.end(),[&](const auto &p){return p.id==base.id;});
    if(found==next->parameters.end())throw std::invalid_argument("Preset baseline identity changed");
    next->manualValues.push_back(found->value);
  }
  next->warmup=uint32_t(std::llround(latency()*rate_));next->fadeFrames=std::max(1u,uint32_t(std::ceil(rate_*.01)));
  return next;
}
bool NativePlugin::presetReady(const Preset &preset) const noexcept {
  return presetSettled_.load(std::memory_order_acquire)&&publishedVendor()==preset.expected;
}
void NativePlugin::adoptPreset(Preset &preset) noexcept {
  // No allocation, reference-count mutation or UI work at adoption.
  // The stable facade keeps every queued event, active ramp and recorded cursor.
  if(renderVendor_==preset.vendor.get())return;
  presetTailEpoch_.fetch_add(1+(renderVendor_->builtin_?renderVendor_->builtin_->tailRevision():0),std::memory_order_release);
  activePreset_=&preset;preset.elapsed=0;renderVendor_=preset.vendor.get();
  for(size_t i=0;i<baselines_.size();++i){auto &base=baselines_[i];
    const bool laterManual=base.queuedManual&&int32_t(base.manualSerial-preset.parameterFence)>=0;
    if(!laterManual&&(base.source.kind==ParameterOrigin::Baseline||base.source.kind==ParameterOrigin::Manual||base.source.kind==ParameterOrigin::PluginEditor||base.source.kind==ParameterOrigin::Reset)){base.value=preset.manualValues[i];base.source={ParameterOrigin::Baseline};}
    else {
      // A recorded/step value can remain held for many blocks. Restore it at
      // this boundary instead of waiting for a future automation point.
      const bool accepted=renderVendor_->builtin_?renderVendor_->builtin_->parameter(base.id,float(base.value)):renderVendor_->backend_->parameter(base.id,base.value,0);
      if(!accepted)presetActivationFailed_=true;
    }
  }
  tail_.store(renderVendor_->tail(),std::memory_order_release);
  presetSettled_.store(false,std::memory_order_relaxed);publishedVendor_.store(renderVendor_,std::memory_order_release);
  transport(transport_);
}
void NativePlugin::transport(const PluginTransport &value) noexcept {
  transport_=value;
  if(auto *backend=renderBackend())backend->transport(value);
  if(activePreset_&&activePreset_->expected->backend_)activePreset_->expected->backend_->transport(value);
}
bool NativePlugin::processBlock(float *buffer,uint32_t frames,uint64_t position,uint32_t offset) noexcept {
  if(presetActivationFailed_)return false;
  auto *preset=activePreset_;
  if(preset)std::copy_n(buffer,frames*2,preset->oldAudio.data());
  if(preset&&!preset->expected->processVendorBlock(preset->oldAudio.data(),frames,position,offset,*this))return false;
  if(!renderVendor_->processVendorBlock(buffer,frames,position,offset,*this))return false;
  auto mixAt=[&](uint32_t frame)noexcept{
    const auto elapsed=preset->elapsed+frame;
    return elapsed<preset->warmup?0.f:std::min(1.f,float(elapsed-preset->warmup+1)/preset->fadeFrames);
  };
  if(preset)for(uint32_t f=0;f<frames;++f){const auto mix=mixAt(f);for(unsigned c=0;c<2;++c)buffer[f*2+c]=preset->oldAudio[f*2+c]*(1-mix)+buffer[f*2+c]*mix;}
  if(renderVendor_!=this)for(uint32_t port=1;port<64;++port)if(auxiliaryOutputBuffers_[port]){
    auto *output=auxiliaryOutputBuffers_[port]->interleaved.data()+offset*2;
    const auto *current=renderVendor_->auxiliaryOutput(port);if(!current)return false;current+=offset*2;
    if(!preset){std::copy_n(current,frames*2,output);continue;}
    const auto *before=preset->expected->auxiliaryOutput(port);if(!before)return false;before+=offset*2;
    for(uint32_t f=0;f<frames;++f){const auto mix=mixAt(f);for(unsigned c=0;c<2;++c)output[f*2+c]=before[f*2+c]*(1-mix)+current[f*2+c]*mix;}
  }
  if(preset){preset->elapsed+=frames;if(preset->elapsed>=uint64_t(preset->warmup)+preset->fadeFrames){activePreset_=nullptr;presetSettled_.store(true,std::memory_order_release);}}
  return true;
}
}
