#include "HostedAudio.hpp"
#include "editor/TrackerDocument.hpp"
#include "editor/SongGroupRuntime.hpp"
#include "soundlib/Sndfile.h"
#include "mac/Audio/PatternCommandRuntime.hpp"
#include <algorithm>
#include <stdexcept>

namespace Tracker {
struct PluginChain::HostedMixerPlan::SongControls {
  std::unique_ptr<SongModulationRuntime> runtime;
  std::vector<PluginSongModulation> processors;
  struct Tap {size_t source,bus=SIZE_MAX,processor=SIZE_MAX;uint32_t output=0;};
  std::vector<Tap> taps;
  struct Notes {
    size_t source;
    const OpenMPT::ModInstrument *instrument=nullptr;
    std::vector<bool> members;
    struct Watch {uint16_t index;uint64_t generation=0;};
    std::vector<Watch> voices;
  };
  std::vector<Notes> notes;
  std::array<uint8_t,128> controllers{};
  SignalClock clock;
  std::array<float,8192> groupFollower{};
};

std::shared_ptr<PluginChain::HostedMixerPlan::SongControls> PluginChain::prepareSongControls(
    const NativeSong &native,MixerTransition::Plan &plan,const HostedMixerPlan &hosted) {
  auto result=std::make_shared<HostedMixerPlan::SongControls>();
  auto graph=native.signal;
  std::vector<SongModulationParameter> parameters;
  std::vector<size_t> owners(plan.catalog.size(),SIZE_MAX);
  for(size_t bus=0;bus<plan.runtime->plan().nodes.size();++bus)
    for(auto processor:plan.runtime->plan().nodes[bus].processors)owners[processor]=bus;
  auto scheduled=[&](size_t slot){return slot!=SIZE_MAX && (owners[slot]!=SIZE_MAX || plan.catalog[slot].instrument ||
    std::find(plan.runtime->plan().detached.begin(),plan.runtime->plan().detached.end(),slot)!=plan.runtime->plan().detached.end());};
  auto processor=[&](const std::string &id){const auto i=std::find_if(plan.catalog.begin(),plan.catalog.end(),[&](const auto &p){return p.instance==id;});return i==plan.catalog.end()?SIZE_MAX:size_t(i-plan.catalog.begin());};
  for(const auto &entry:hosted.rack){const auto slot=processor(entry->baseline.instanceID);
    if(!scheduled(slot))continue;
    for(const auto &p:entry->parameters)if(p.max>p.min && p.writable){
      double step=p.step/(p.max-p.min);
      if(!p.continuous && step<=0){if(p.choices.size()>1)step=1./(p.choices.size()-1);else if(p.unit==2)step=1;}
      parameters.push_back({entry->baseline.instanceID,p.id,step,p.writable,p.continuous});
    }
  }
  // Saved unresolved links survive removals and become active again on Undo.
  // They do not prevent unrelated audio from playing or change document data.
  std::erase_if(graph.songModulation,[&](const auto &edge){return !edge.enabled || std::none_of(parameters.begin(),parameters.end(),[&](const auto &p){return p.plugin==edge.plugin && p.parameter==edge.parameter;});});
  for(size_t i=0;i<graph.songSources.size();++i){auto &source=graph.songSources[i];
    if(source.node.kind==SignalNodeKind::Follower){
      // The saved endpoint is typed as an aggregate stage; only the prepared
      // evaluator resolves it to the synthetic processor that owns that PCM.
      if(source.audioStage){source.audioPlugin=signalBusIdentity(source.audioStage);source.audioStage=0;}
      if(source.audioBus){auto bus=std::find_if(plan.runtime->graph().buses.begin(),plan.runtime->graph().buses.end(),[&](const auto &b){return b.id==source.audioBus;});
        if(bus==plan.runtime->graph().buses.end())source.audioBus=0;
        else result->taps.push_back({i,size_t(bus-plan.runtime->graph().buses.begin()),SIZE_MAX,source.preFader?0u:1u});
      } else if(!source.audioPlugin.empty()){
        const auto slot=processor(source.audioPlugin);
        if(!scheduled(slot) || source.output>=64 || !(plan.catalog[slot].activeOutputs&(uint64_t(1)<<source.output)))source.audioPlugin.clear();
        else result->taps.push_back({i,SIZE_MAX,slot,source.output});
      }
    }
    if(source.node.kind==SignalNodeKind::NoteEnvelope){
      HostedMixerPlan::SongControls::Notes notes;notes.source=i;notes.members.resize(native.tracks.size());
      if(source.noteInstrument && musicalSong_)for(const auto &[index,instrument]:native.instruments)if(instrument.id==source.noteInstrument && index<std::size(musicalSong_->Instruments))notes.instrument=musicalSong_->Instruments[index];
      const auto &buses=plan.runtime->graph().buses;
      for(const auto &[channel,track]:native.tracks){bool included=!source.noteTarget || source.noteTarget==track.id;auto id=track.id;
        for(size_t depth=0;!included && id && depth<buses.size();++depth){if(id==source.noteTarget){included=true;break;}auto bus=std::find_if(buses.begin(),buses.end(),[&](const auto &b){return b.id==id;});id=bus==buses.end()?0:bus->output;}
        if(channel<notes.members.size())notes.members[channel]=included;
      }
      for(uint16_t channel=0;channel<OpenMPT::MAX_CHANNELS;++channel)if(channel>=notes.members.size() || notes.members[channel])notes.voices.push_back({channel});
      result->notes.push_back(std::move(notes));
    }
  }
  result->runtime=std::make_unique<SongModulationRuntime>(graph,parameters,sampleRate_);
  result->processors.resize(plan.catalog.size());
  for(size_t index=0;index<result->runtime->targets().size();++index){const auto &target=result->runtime->targets()[index];const auto slot=processor(target.plugin);
    const auto entry=std::find_if(hosted.rack.begin(),hosted.rack.end(),[&](const auto &p){return p->baseline.instanceID==target.plugin;});
    const auto p=std::find_if((*entry)->parameters.begin(),(*entry)->parameters.end(),[&](const auto &p){return p.id==target.parameter;});
    auto &binding=result->processors[slot];binding.runtime=result->runtime.get();binding.targets.push_back({target.parameter,index,p->min,p->max});
    if(binding.targets.size()>128)throw std::invalid_argument("Song modulation supports at most 128 parameters per processor");
    for(const auto &contribution:target.contributions)for(const auto &tap:result->taps)if(tap.source==contribution.source){
      if(plan.catalog[slot].instrument&&!plan.catalog[slot].scheduledSource)throw std::invalid_argument("Follower target has no prepared instrument stage");
      if(tap.processor!=SIZE_MAX && plan.catalog[tap.processor].instrument&&!plan.catalog[tap.processor].scheduledSource)continue; // Sources render before the mixer.
      plan.dependencies.push_back({tap.bus,tap.processor,slot});
    }
  }
  plan.processorStorage+=sizeof(*result)+result->runtime->storageBytes()+result->taps.capacity()*sizeof(result->taps[0]);
  for(const auto &p:result->processors)plan.processorStorage+=sizeof(p)+p.targets.capacity()*sizeof(PluginSongModulation::Target);
  for(const auto &n:result->notes)plan.processorStorage+=sizeof(n)+n.members.capacity()/8+n.voices.capacity()*sizeof(n.voices[0]);
  return result;
}

void PluginChain::prepareSongGroups(const NativeSong &native,MixerTransition::Plan &plan,HostedMixerPlan &hosted) {
  hosted.groups.reset();if(native.signal.groups.empty())return;
  hosted.groups=std::make_shared<SongGroupRuntime>(native.signal,plan.runtime->graph(),plan.runtime->plan(),plan.catalog,sampleRate_);
  hosted.groups->runtime(plan.runtime.get());
  // The compiled timing vertices already order each exact ingress capture
  // before its egress; treating a processor input as its output would invent
  // feedback in valid crossed dry maps.
  if(plan.runtime->plan().timing.empty())for(const auto &dependency:hosted.groups->dependencies())plan.dependencies.push_back(dependency);
  plan.processorStorage+=hosted.groups->storageBytes();plan.runtime->routeTransform(HostedMixerPlan::transformRoute,&hosted);
  if(hosted.song)hosted.song->runtime->contributionGain([](void *p,uint64_t source,const std::string &target,uint64_t frame)noexcept {
    return static_cast<SongGroupRuntime *>(p)->modulation(source,target,frame);
  },hosted.groups.get());
}
void PluginChain::HostedMixerPlan::transformRoute(void *opaque,MixerRuntime::RouteKind kind,size_t index,float *samples,uint32_t frames,uint64_t position) noexcept {
  auto &hosted=*static_cast<HostedMixerPlan *>(opaque);if(!hosted.groups)return;
  hosted.groups->route(kind,index,samples,frames,position);if(hosted.groups->failed())hosted.owner->failed_=true;
}
void PluginChain::HostedMixerPlan::adopt(void *opaque,void *previous) noexcept {
  auto &next=*static_cast<HostedMixerPlan *>(opaque);
  if(next.sampleBindings)next.owner->adoptSampleBindings(*next.sampleBindings);
  if(next.instrumentBindings)next.owner->adoptInstrumentBindings(*next.instrumentBindings);
  if(next.noteRouting)next.owner->adoptNoteRouting(*next.noteRouting);
  if(previous && next.song){const auto &old=*static_cast<HostedMixerPlan *>(previous);if(old.song){next.song->runtime->inheritState(*old.song->runtime);next.song->controllers=old.song->controllers;
    for(auto &scope:next.song->notes)for(const auto &prior:old.song->notes)if(next.song->runtime->source(scope.source).node.id==old.song->runtime->source(prior.source).node.id)
      {size_t index=0;for(auto &watch:scope.voices){while(index<prior.voices.size() && prior.voices[index].index<watch.index)++index;if(index<prior.voices.size() && prior.voices[index].index==watch.index)watch.generation=prior.voices[index].generation;}}
  }}
  next.owner->observation_->activate(next.observationPlan);
  next.owner->activeHostedMixer_=&next;next.owner->activeSongControls_=next.song.get();
  if(next.musical)next.owner->activateMusicalPlan(*next.musical);
  if(next.commands&&next.owner->activeCommands_!=next.commands.get()){
    if(next.owner->activeCommands_)next.commands->inheritState(*next.owner->activeCommands_);
    next.owner->activeCommands_=next.commands.get();
  }
}
void PluginChain::HostedMixerPlan::begin(void *opaque,uint32_t frames,uint64_t position,bool current) noexcept {
  auto &plan=*static_cast<HostedMixerPlan *>(opaque);
  if(current&&plan.noteRouting)plan.owner->adoptNoteRouting(*plan.noteRouting);
  if(plan.groups)plan.groups->begin(frames,position);
  plan.owner->beginSongControls(plan,frames,position,current);
}
void PluginChain::beginSongControls(HostedMixerPlan &hosted,uint32_t frames,uint64_t position,bool current) noexcept {
  if(current){activeHostedMixer_=&hosted;activeSongControls_=hosted.song.get();}
  if(!hosted.song)return;auto &song=*hosted.song;song.clock=songClock_;
  for(auto &processor:song.processors){processor.clock=song.clock;processor.frame=position;}
  for(uint32_t cc=0;cc<128;++cc){const auto value=songControllers_[cc].load(std::memory_order_relaxed);if(song.controllers[cc]!=value){song.controllers[cc]=value;song.runtime->controller(cc,value/127.);}}
  if(musicalSong_)for(auto &scope:song.notes){bool gate=false,retrigger=false;const auto &state=musicalSong_->m_PlayState;
    const auto &spec=song.runtime->source(scope.source);
    for(auto &watch:scope.voices)if(watch.index<state.Chn.size()){
      const auto &voice=state.Chn[watch.index];const auto parent=voice.nMasterChn?voice.nMasterChn-1:watch.index;
      if(parent>=scope.members.size() || !scope.members[parent] || (spec.noteInstrument && (!scope.instrument || voice.pModInstrument!=scope.instrument)))continue;
      const bool held=voice.nNote>=1 && voice.nNote<=120 && !voice.dwFlags[OpenMPT::CHN_KEYOFF|OpenMPT::CHN_NOTEFADE|OpenMPT::CHN_MUTE|OpenMPT::CHN_SYNCMUTE];
      const bool sounding=held && (voice.IsSamplePlaying() || voice.HasMIDIOutput());
      gate|=sounding;retrigger|=sounding && watch.generation!=voice.nativeNoteGeneration;watch.generation=voice.nativeNoteGeneration;
    }
    song.runtime->gate(spec.node.id,gate,retrigger);
  }
  for(size_t source=0;source<song.runtime->sourceCount();++source){const auto &spec=song.runtime->source(source);
    if(spec.node.kind!=SignalNodeKind::Follower || (!spec.audioBus && spec.audioPlugin.empty()))
      if(!song.runtime->renderSource(source,frames,position,song.clock))failed_=true;
  }
}
void PluginChain::songFollower(HostedMixerPlan &hosted,size_t bus,size_t processor,uint32_t output,const float *samples,uint32_t frames,uint64_t position) noexcept {
  if(!hosted.song)return;auto &song=*hosted.song;
  for(const auto &tap:song.taps)if(tap.bus==bus && tap.processor==processor && tap.output==output){
    double existing;if(song.runtime->contribution(tap.source,position,existing))continue;
    const float *input=samples;
    if(hosted.groups){if(samples)std::copy_n(samples,frames*2,song.groupFollower.data());else std::fill_n(song.groupFollower.data(),frames*2,0.f);hosted.groups->follower(tap.source,song.groupFollower.data(),frames,position);if(hosted.groups->failed())failed_=true;input=song.groupFollower.data();}
    if(!song.runtime->renderSource(tap.source,frames,position,song.clock,input))failed_=true;
  }
}
void PluginChain::HostedMixerPlan::source(void *opaque,size_t processor,uint32_t output,const float *samples,uint32_t frames,uint64_t position) noexcept {
  auto &hosted=*static_cast<HostedMixerPlan *>(opaque);hosted.owner->songFollower(hosted,SIZE_MAX,processor,output,samples,frames,position);
}
const PluginSongModulation *PluginChain::modulationFor(const HostedMixerPlan &hosted,size_t processor) const noexcept {
  return hosted.song && processor<hosted.song->processors.size()?&hosted.song->processors[processor]:nullptr;
}
const PluginSongModulation *PluginChain::instrumentModulation(size_t processor) const noexcept {
  if(!activeHostedMixer_ || processor>=instances_.size())return nullptr;
  for(size_t slot=0;slot<activeHostedMixer_->processors.size();++slot){const auto &device=activeHostedMixer_->processors[slot]->processor();if(device.plugin==plugins_[processor])return modulationFor(*activeHostedMixer_,slot);}
  return nullptr;
}
void PluginChain::prepareRoutingMusical(const NativeSong &native,HostedMixerPlan &hosted,MixerTransition::Plan &routing) {
  hosted.publishMusical=false;hosted.musicalSpec.clear();hosted.musicalTargets.clear();
  hosted.commandTargets.clear();for(const auto &entry:hosted.rack)hosted.commandTargets.emplace_back(entry->baseline.instanceID,entry->plugin.get());
  const bool changedRack=hosted.commandTargets!=commandTargets_;
  if(!hosted.commands)hosted.commands=publishedCommands_;
  if(changedRack){
    std::vector<std::shared_ptr<NativePlugin>> plugins;std::vector<std::string> identities;std::vector<bool> bypass;
    for(const auto &entry:hosted.rack){plugins.push_back(entry->plugin);identities.push_back(entry->baseline.instanceID);bypass.push_back(false);}
    std::vector<ParameterChange> recorded;
    for(auto point:automation_)if(point.slot<instances_.size()){
      const auto id=std::find(identities.begin(),identities.end(),instances_[point.slot]);if(id!=identities.end()){point.slot=uint32_t(id-identities.begin());recorded.push_back(point);}
    }
    hosted.commands=std::make_shared<PatternCommandRuntime>(native,plugins,identities,bypass,recorded);
  }
  if(native.automation!=musicalSpec_||changedRack){
    if(!musicalSong_ || musicalSerial_==UINT64_MAX)throw std::runtime_error("Musical automation cannot be prepared");
    auto plan=prepareMusicalPlan(native,hosted.rack);plan->revision=musicalSerial_+1;
    hosted.musicalSourceRevision=musicalSerial_;hosted.musicalSpec=native.automation;hosted.musicalTargets=musicalTargets_;
    for(const auto &p:plan->reset)hosted.musicalTargets.emplace_back(p.plugin.get(),p.id);
    std::sort(hosted.musicalTargets.begin(),hosted.musicalTargets.end());hosted.musicalTargets.erase(std::unique(hosted.musicalTargets.begin(),hosted.musicalTargets.end()),hosted.musicalTargets.end());
    hosted.musical=std::move(plan);hosted.publishMusical=true;
  }
  if(hosted.commands)routing.processorStorage+=hosted.commands->storageBytes();
  routing.processorStorage+=hosted.commandTargets.capacity()*sizeof(hosted.commandTargets[0]);for(const auto &p:hosted.commandTargets)routing.processorStorage+=p.first.capacity();
  if(hosted.musical){const auto &plan=*hosted.musical;
    routing.processorStorage+=sizeof(MusicalPlan)+plan.reset.capacity()*sizeof(MusicalPlan::Reset)+plan.patterns.capacity()*sizeof(plan.patterns[0]);
    for(const auto &pattern:plan.patterns){routing.processorStorage+=pattern.capacity()*sizeof(MusicalLane);for(const auto &lane:pattern){routing.processorStorage+=lane.points.capacity()*sizeof(AutomationPoint);for(const auto &point:lane.points)routing.processorStorage+=point.formula.bytes();}}
  }
  routing.processorStorage+=hosted.musicalTargets.capacity()*sizeof(hosted.musicalTargets[0])+hosted.musicalSpec.capacity()*sizeof(MusicalAutomationLane);
  for(const auto &lane:hosted.musicalSpec){routing.processorStorage+=lane.points.capacity()*sizeof(AutomationPoint)+lane.plugin.capacity();for(const auto &point:lane.points)routing.processorStorage+=point.formula.bytes();}
}
bool PluginChain::acceptsRoutingMusical(const HostedMixerPlan &hosted) const noexcept {
  return !hosted.publishMusical || (hosted.musical && hosted.musicalSourceRevision==musicalSerial_ && hosted.musical->revision==musicalSerial_+1);
}
void PluginChain::commitRoutingMusical(HostedMixerPlan &hosted) noexcept {
  if(!hosted.publishMusical)return;
  musicalSerial_=hosted.musical->revision;musicalSpec_.swap(hosted.musicalSpec);musicalTargets_.swap(hosted.musicalTargets);hasMusicalControls_.store(true,std::memory_order_relaxed);
  publishedCommands_=hosted.commands;commandTargets_.swap(hosted.commandTargets);
}
} // namespace Tracker
