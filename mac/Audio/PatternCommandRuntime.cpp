#include "PatternCommandRuntime.hpp"
#include <stdexcept>
namespace Tracker {
PatternCommandRuntime::PatternCommandRuntime(const NativeSong &native,const std::vector<std::shared_ptr<NativePlugin>> &plugins,
 const std::vector<std::string> &instances,const std::vector<bool> &bypass,const std::vector<ParameterChange> &absolute) {
  std::map<std::pair<std::string,uint32_t>,size_t> targetIndices;
  for(const auto &command:native.performance.commands) {
    if(command.kind!=PatternCommandKind::ParameterSet&&command.kind!=PatternCommandKind::ParameterSlide)
      continue; // The pitch runtime shares the musical clock but owns pitch state.
    const auto &binding=native.performance.bindings.at(command.binding);
    const auto instance=std::find(instances.begin(),instances.end(),binding.plugin);
    if(instance==instances.end())continue; // Retained unresolved bindings never retarget another rack slot.
    const auto slot=size_t(instance-instances.begin());
    if(bypass[slot])continue;
    const auto key=std::pair{binding.plugin,binding.parameter};
    auto index=targetIndices.find(key);
    if(index==targetIndices.end()) {
      const auto parameters=plugins[slot]->parameters();
      const auto parameter=std::find_if(parameters.begin(),parameters.end(),[&](const auto &p){return p.id==binding.parameter;});
      if(parameter==parameters.end()||!parameter->writable||!std::isfinite(parameter->min)||!std::isfinite(parameter->max)||parameter->max<=parameter->min)
        continue;
      if(!parameter->continuous && std::any_of(native.performance.commands.begin(),native.performance.commands.end(),[&](const auto &other){
        const auto candidate=native.performance.bindings.find(other.binding);
        return other.kind==PatternCommandKind::ParameterSlide&&candidate!=native.performance.bindings.end()&&candidate->second.plugin==binding.plugin&&candidate->second.parameter==binding.parameter;
      }))throw std::invalid_argument("Discrete parameters cannot use slide commands");
      for(const auto &lane:native.automation)if(lane.enabled&&lane.plugin==binding.plugin&&lane.parameter==binding.parameter)
        throw std::invalid_argument("A parameter cannot use both an enabled envelope and pattern commands");
      for(const auto &event:absolute)if(event.slot==slot&&event.id==binding.parameter)
        throw std::invalid_argument("A parameter cannot use both recorded absolute automation and pattern commands");
      Target target;target.plugin=plugins[slot];target.parameter=binding.parameter;target.minimum=parameter->min;target.maximum=parameter->max;
      target.current=std::clamp((parameter->value-parameter->min)/double(parameter->max-parameter->min),0.,1.);
      target.curve={target.current,target.current,0,0};target.plugin->prepareMusicalAutomation();
      index=targetIndices.emplace(key,targets_.size()).first;targets_.push_back(std::move(target));
    }
    auto &target=targets_[index->second];
    const auto pattern=std::find_if(native.patterns.begin(),native.patterns.end(),[&](const auto &p){return p.second.id==command.pattern;});
    const auto track=std::find_if(native.tracks.begin(),native.tracks.end(),[&](const auto &p){return p.second.id==command.track;});
    target.patterns[pattern->first].push_back({command.position,command.duration,track->first,command.column,command.value});
    const auto value=float(target.minimum+(target.maximum-target.minimum)*command.value);target.plugin->includeParameterRange(target.parameter,value,value);
  }
  if(targets_.size()>255)throw std::invalid_argument("Pattern commands exceed 255 parameter targets");
  std::map<NativePlugin *,size_t> counts;
  for(auto &target:targets_){if(++counts[target.plugin.get()]>64)throw std::invalid_argument("Use at most 64 pattern-command parameters per plugin");
    for(auto &[pattern,events]:target.patterns)std::sort(events.begin(),events.end(),[](const auto &a,const auto &b){return std::tie(a.position,a.channel,a.column)<std::tie(b.position,b.channel,b.column);});}
}
bool PatternCommandRuntime::render(const OpenMPT::PlayState &state,uint32_t frames,uint64_t absoluteFrame) noexcept {
  using namespace OpenMPT;
  if(!frames||state.m_flags[SONG_PAUSED]||state.m_flags[SONG_FADINGSONG]||!state.m_nSamplesPerTick||!state.TicksOnRow())return true;
  const double unitsPerSample=double(performanceUnitsPerRow)/(double(state.TicksOnRow())*state.m_nSamplesPerTick);
  const double tickPosition=double(state.m_nRow)*performanceUnitsPerRow+double(state.m_nTickCount)*performanceUnitsPerRow/state.TicksOnRow();
  const auto samplesIntoTick=state.SamplesIntoTick();
  auto musical=[&](uint32_t offset){return tickPosition+double(samplesIntoTick+offset)*unitsPerSample;};
  const auto begin=musical(0);
  const bool entering=pattern_!=state.m_nPattern||order_!=state.m_nCurrentOrder||begin<=previousPosition_;
  pattern_=state.m_nPattern;order_=state.m_nCurrentOrder;previousPosition_=begin;
  for(auto &target:targets_) {
    if(entering){const auto found=target.patterns.find(pattern_);target.events=found==target.patterns.end()?nullptr:&found->second;
      target.next=0;target.curve={target.current,target.current,begin,begin};target.activeChannel=UINT16_MAX;}
    auto muted=[&](uint16_t channel){return channel<state.Chn.size()&&state.Chn[channel].dwFlags[CHN_MUTE|CHN_SYNCMUTE];};
    if(target.activeChannel!=UINT16_MAX&&muted(target.activeChannel)){const auto held=target.curve.at(begin);target.curve={held,held,begin,begin};target.activeChannel=UINT16_MAX;}
    uint32_t offset=0;
    while(offset<frames){const double at=musical(offset);
      while(target.events&&target.next<target.events->size()&&(*target.events)[target.next].position<=at+1e-8){
        const auto &event=(*target.events)[target.next++];if(muted(event.channel))continue;
        const auto current=target.curve.at(event.position);
        target.curve={current,event.value,double(event.position),double(event.position)+event.duration};target.activeChannel=event.channel;target.used=true;
      }
      uint32_t count=frames-offset;
      auto boundary=[&](double position){if(position<=at+1e-8)return;const double sample=std::ceil((position-tickPosition)/unitsPerSample-1e-9)-samplesIntoTick;
        if(sample>offset&&sample<frames)count=std::min(count,uint32_t(sample)-offset);};
      if(target.events&&target.next<target.events->size())boundary((*target.events)[target.next].position);
      boundary(target.curve.end);
      if(target.used){const auto from=target.minimum+(target.maximum-target.minimum)*target.curve.at(at);
        const auto to=target.minimum+(target.maximum-target.minimum)*target.curve.at(musical(offset+count-1));
        if(!target.plugin->scheduleRamp(target.parameter,from,to,absoluteFrame+offset,count-1))return false;}
      offset+=count;
    }
    target.current=target.curve.at(musical(frames));
  }
  return true;
}
}
