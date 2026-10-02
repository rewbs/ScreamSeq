#include "HostedAudio.hpp"
#include "mac/Audio/NativeSignalGraph.hpp"
#include "soundlib/Sndfile.h"
#include <algorithm>
#include <stdexcept>

namespace Tracker {
std::shared_ptr<const HostedSampleBindings> PluginChain::prepareSampleBindings(const NativeSong &native,const std::vector<std::shared_ptr<RackEntry>> &rack) const {
  if(!musicalSong_||sampleRoutes_.empty()) {
    if(!native.signal.instrumentAssignments.empty())throw std::invalid_argument("No sample graph adapter capacity is available for this channel layout");
    return {};
  }
  auto next=publishedSampleBindings_?std::make_shared<HostedSampleBindings>(*publishedSampleBindings_):std::make_shared<HostedSampleBindings>();
  const size_t stride=musicalSong_->GetNumChannels()+1;
  next->routes.resize(sampleRoutes_.size());next->groups.resize(sampleRoutes_.size()/stride);
  for(const auto &assignment:native.signal.instrumentAssignments){
    const auto found=std::find_if(native.instruments.begin(),native.instruments.end(),[&](const auto &entry){return entry.second.id==assignment.target;});
    if(found==native.instruments.end()||found->first>musicalSong_->GetNumInstruments()||!musicalSong_->Instruments[found->first])throw std::invalid_argument("Instrument graph target is missing from the active renderer");
    if(std::any_of(native.signal.noteRouting.triggerSources.begin(),native.signal.noteRouting.triggerSources.end(),[&](const auto &source){return source.instrument==assignment.target;}))throw std::invalid_argument("Sample instrument graph cannot process a plugin trigger source; route its plugin output instead");
    for(const auto &entry:rack)if(entry->plugin->isInstrument())for(const auto &alias:pluginAssignments(entry->baseline))if(alias.instrument==found->first)throw std::invalid_argument("Sample instrument graph cannot process a plugin instrument; route its output bus instead");
    const auto *instrument=musicalSong_->Instruments[found->first];
    const auto &previous=lastGraphControls_?lastGraphControls_->signal:preparedSignal_;
    for(size_t i=0;i<next->groups.size();++i)if(next->groups[i]&&next->groups[i]!=assignment.target&&next->routes[i*stride].instrument==instrument){
      const auto old=next->groups[i];
      if(std::any_of(previous.instrumentAssignments.begin(),previous.instrumentAssignments.end(),[&](const auto &a){return a.target==old;}))throw std::invalid_argument("A sample instrument identity is still rendering; remove its graph assignment before reusing that instrument slot");
      next->groups[i]=0;for(size_t route=0;route<stride;++route)next->routes[i*stride+route]={};
    }
    auto group=std::find(next->groups.begin(),next->groups.end(),assignment.target);
    if(group==next->groups.end())group=std::find(next->groups.begin(),next->groups.end(),uint64_t(0));
    if(group==next->groups.end()){
      // A previously removed graph has already completed its handoff before
      // prepareMixerRouting accepts this edit. Its raw zero-latency capture is
      // equivalent to the channel input, so its stable slot can be reclaimed.
      // Keep currently or newly assigned groups untouched throughout the fade.
      group=std::find_if(next->groups.begin(),next->groups.end(),[&](uint64_t id){
        return std::none_of(previous.instrumentAssignments.begin(),previous.instrumentAssignments.end(),[&](const auto &a){return a.target==id;})&&
          std::none_of(native.signal.instrumentAssignments.begin(),native.signal.instrumentAssignments.end(),[&](const auto &a){return a.target==id;});
      });
    }
    if(group==next->groups.end())throw std::invalid_argument("Live sample graph capture capacity is full for this channel layout; remove an assignment and let its transition finish before adding another");
    *group=assignment.target;const auto first=size_t(group-next->groups.begin())*stride;
    for(size_t i=0;i<stride;++i)next->routes[first+i]={musicalSong_->Instruments[found->first],assignment.target};
  }
  return next;
}
NativeSong PluginChain::prepareSampleSong(const NativeSong &native,const HostedSampleBindings &bindings,std::vector<SignalSampleSource> &sources) const {
  NativeSong prepared=native;prepared.mixer={};prepared.signal={};prepared.signal.library=native.signal.library;
  if(bindings.routes.size()!=sampleRoutes_.size())throw std::logic_error("Prepared sample source membership is inconsistent");
  sources.reserve(sampleRoutes_.size());
  for(size_t i=0;i<sampleRoutes_.size();++i){const auto &route=sampleRoutes_[i];const auto &binding=bindings.routes[i];const auto id=NativeSong::maximumID+1+i;
    prepared.mixer.buses.push_back({id,0,MixerBusKind::Group,"Instrument source"});
    const auto assignment=std::find_if(native.signal.instrumentAssignments.begin(),native.signal.instrumentAssignments.end(),[&](const auto &a){return a.target==binding.instrumentID;});
    if(assignment!=native.signal.instrumentAssignments.end())prepared.signal.assignments.push_back({id,assignment->graph,assignment->amount,assignment->wet});
    sources.push_back({id,binding.instrument,route.channel,musicalSong_->GetNumChannels(),binding.instrumentID});
  }
  return prepared;
}
}
