#pragma once
#include "PluginTypes.hpp"
#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>

namespace Tracker {
// Numeric channel-pair identities are independent of names and logical cable
// selection. A saved nonempty identity must match before a processor activates.
inline std::string pluginAudioLayoutSignature(std::span<const PluginAudioBus> buses) {
  std::vector<const PluginAudioBus *> ordered;
  for(const auto &bus:buses)ordered.push_back(&bus);
  std::sort(ordered.begin(),ordered.end(),[](const auto *a,const auto *b){return a->input!=b->input?a->input>b->input:a->index<b->index;});
  std::string result="pairs-v1";
  for(const auto *bus:ordered){
    result+=(bus->input?";i":";o")+std::to_string(bus->index)+"="+
      std::to_string(bus->physicalChannels?bus->physicalBus:bus->index)+":"+
      std::to_string(bus->firstChannel)+":"+std::to_string(bus->channels)+":"+
      std::to_string(bus->physicalChannels?bus->physicalChannels:bus->channels);
  }
  return result;
}
inline void validatePluginAudioLayout(const std::string &saved,std::span<const PluginAudioBus> buses) {
  if(!saved.empty()&&saved!=pluginAudioLayoutSignature(buses))
    throw std::runtime_error("Plugin audio layout changed; review physical bus/channel mappings before reconnecting");
}
struct PluginPhysicalBus {uint32_t channels;std::string name;};
// Prepared native non-interleaved planes and logical stereo/mono slices. Every
// supported physical bus is allocated and activated before rendering starts;
// missing logical wires feed silence without changing vendor activation state.
class PluginAudioBufferPlan {
public:
  static constexpr uint32_t maximumFrames=4096,maximumPorts=64;
  struct Physical {
    std::vector<float> samples;
    std::vector<float *> channels;
    explicit Physical(uint32_t count):samples(size_t(count)*maximumFrames),channels(count) {
      for(uint32_t i=0;i<count;++i)channels[i]=samples.data()+size_t(i)*maximumFrames;
    }
  };
private:
  std::vector<PluginAudioBus> buses_;
  std::vector<Physical> inputs_,outputs_;
  std::array<std::unique_ptr<std::array<float,maximumFrames*2>>,maximumPorts> slices_;
  uint64_t inputMask_=0,outputMask_=0;
  void prepare(std::span<const PluginPhysicalBus> physical,std::span<const uint32_t> enabled,bool input) {
    if(physical.size()>maximumPorts||(!input&&physical.empty()))throw std::runtime_error("Unsupported native audio bus count");
    size_t total=0;for(const auto &bus:physical){if(!bus.channels||bus.channels>64)throw std::runtime_error("Unsupported native audio channel count");total+=(bus.channels+1)/2;}
    if(total>maximumPorts)throw std::runtime_error("Native audio layout exceeds 64 channel-pair ports");
    auto &storage=input?inputs_:outputs_;storage.reserve(physical.size());
    uint32_t extra=uint32_t(physical.size());uint64_t present=0;
    for(uint32_t i=0;i<physical.size();++i){const auto &bus=physical[i];storage.emplace_back(bus.channels);
      for(uint32_t first=0;first<bus.channels;first+=2){const uint32_t index=first?extra++:i,count=std::min(2u,bus.channels-first);
        std::string name=bus.name;if(bus.channels>2)name+=" ["+std::to_string(first+1)+(count==2?"–"+std::to_string(first+2):"")+"]";
        buses_.push_back({index,count,std::move(name),input,index==0||std::find(enabled.begin(),enabled.end(),index)!=enabled.end(),true,i,first,bus.channels});
        present|=uint64_t(1)<<index;
        if(!input&&index)slices_[index]=std::make_unique<std::array<float,maximumFrames*2>>();
      }
    }
    uint64_t selected=0;for(auto index:enabled){if(!index||index>=maximumPorts||!(present&(uint64_t(1)<<index))||(selected&(uint64_t(1)<<index)))throw std::runtime_error("Invalid native auxiliary channel-pair port");selected|=uint64_t(1)<<index;}
    (input?inputMask_:outputMask_)=present&~uint64_t(1);
  }
public:
  PluginAudioBufferPlan(std::span<const PluginPhysicalBus> inputs,std::span<const PluginPhysicalBus> outputs,
                       std::span<const uint32_t> enabledInputs,std::span<const uint32_t> enabledOutputs) {
    prepare(inputs,enabledInputs,true);prepare(outputs,enabledOutputs,false);
  }
  const auto &buses() const noexcept{return buses_;}
  auto &inputs() noexcept{return inputs_;} auto &outputs() noexcept{return outputs_;}
  uint64_t preparedInputs()const noexcept{return inputMask_;}
  uint64_t preparedOutputs()const noexcept{return outputMask_;}
  const float *output(uint32_t index)const noexcept{return index<maximumPorts&&slices_[index]?slices_[index]->data():nullptr;}
  void gather(const float *main,const float *const *aux,uint32_t offset,uint32_t frames)noexcept {
    for(const auto &bus:buses_)if(bus.input){auto &physical=inputs_[bus.physicalBus];
      const float *source=bus.index?(aux?aux[bus.index]:nullptr):main;
      if(source&&bus.index)source+=size_t(offset)*2;
      auto *left=physical.channels[bus.firstChannel],*right=bus.channels==2?physical.channels[bus.firstChannel+1]:nullptr;
      for(uint32_t i=0;i<frames;++i){const float l=source?source[i*2]:0,r=source?source[i*2+1]:0;left[i]=right?l:(l+r)*.5f;if(right)right[i]=r;}
    }
    for(auto &bus:outputs_)for(auto *channel:bus.channels)std::fill_n(channel,frames,0.f);
  }
  bool scatter(float *main,uint32_t frames,std::span<const uint64_t> silence={})noexcept {
    for(const auto &bus:buses_)if(!bus.input){auto &physical=outputs_[bus.physicalBus];const uint64_t flags=bus.physicalBus<silence.size()?silence[bus.physicalBus]:0;
      const auto left=bus.firstChannel,right=left+(bus.channels==2);auto *destination=bus.index?slices_[bus.index]->data():main;
      for(uint32_t i=0;i<frames;++i){const float l=(flags&(uint64_t(1)<<left))?0:physical.channels[left][i],r=(flags&(uint64_t(1)<<right))?0:physical.channels[right][i];
        if(!std::isfinite(l)||!std::isfinite(r))return false;destination[i*2]=l;destination[i*2+1]=r;
      }
    }return true;
  }
  void clearSlices()noexcept{for(auto &slice:slices_)if(slice)slice->fill(0);}
  size_t storageBytes()const noexcept {
    size_t bytes=sizeof(*this)+buses_.capacity()*sizeof(PluginAudioBus)+(inputs_.capacity()+outputs_.capacity())*sizeof(Physical);
    for(const auto *list:{&inputs_,&outputs_})for(const auto &bus:*list)bytes+=bus.samples.capacity()*sizeof(float)+bus.channels.capacity()*sizeof(float *);
    for(const auto &bus:buses_)bytes+=bus.name.capacity();for(const auto &slice:slices_)if(slice)bytes+=sizeof(*slice);return bytes;
  }
};
} // namespace Tracker
