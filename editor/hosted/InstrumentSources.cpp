#include "HostedAudio.hpp"
#include "InstrumentSources.hpp"
#include "editor/NativeSong.hpp"
#include "soundlib/Sndfile.h"
#include "soundlib/ModInstrument.h"
#include <algorithm>
#include <stdexcept>

namespace Tracker {
std::shared_ptr<const InstrumentSourceBindings> PluginChain::prepareInstrumentBindings(
    const NativeSong &native,const std::vector<std::shared_ptr<RackEntry>> &rack) {
  if(instrumentGenerators_.empty())return {};
  if(instrumentPreparationSerial_==UINT64_MAX)throw std::invalid_argument("Instrument source publication sequence exhausted");
  auto result=std::make_shared<InstrumentSourceBindings>();result->revision=++instrumentPreparationSerial_;result->generators=instrumentGeneratorIDs_;
  // Reuse slots which the candidate no longer binds. Actual generator caches
  // change only at adoption, after releasing the old source's held ownership.
  for(size_t slot=0;slot<result->generators.size();++slot){
    bool retained=std::any_of(rack.begin(),rack.end(),[&](const auto &entry){return entry->baseline.instanceID==result->generators[slot]&&!pluginAssignments(entry->baseline).empty();});
    if(!retained&&publishedInstrumentBindings_)for(const auto &trigger:native.signal.noteRouting.triggerSources)
      for(const auto &binding:publishedInstrumentBindings_->instruments)
        if(binding.plugin&&binding.identity==trigger.instrument&&binding.slot==instrumentGenerators_[slot].slot)retained=true;
    if(!retained)result->generators[slot].clear();
  }
  for(const auto &[index,instrument]:native.instruments){
    if(index>=result->instruments.size() || !originalInstruments_[index].instrument)
      throw std::invalid_argument("This tracker instrument has no prepared playback source");
    const auto &original=originalInstruments_[index];
    result->instruments[index]={original.instrument,instrument.id,original.slot,original.midiChannel,false};
  }
  for(const auto &entry:rack)if(entry->plugin->isInstrument()){
    const auto assignments=pluginAssignments(entry->baseline);if(assignments.empty())continue;
    auto generator=std::find(result->generators.begin(),result->generators.end(),entry->baseline.instanceID);
    if(generator==result->generators.end()){
      generator=std::find(result->generators.begin(),result->generators.end(),std::string{});
      if(generator==result->generators.end())throw std::invalid_argument("Prepared MIDI generator capacity is exhausted");
      *generator=entry->baseline.instanceID;
    }
    const auto slot=instrumentGenerators_[size_t(generator-result->generators.begin())].slot;
    for(const auto &assignment:assignments){
      if(assignment.instrument>=result->instruments.size() || !result->instruments[assignment.instrument].instrument)
        throw std::invalid_argument("A plugin assignment requires a prepared tracker instrument");
      auto &binding=result->instruments[assignment.instrument];
      if(binding.plugin)throw std::invalid_argument("A tracker instrument has multiple native plugin assignments");
      binding.slot=slot;binding.midiChannel=uint8_t(assignment.channel);binding.plugin=true;
    }
  }
  for(const auto &trigger:native.signal.noteRouting.triggerSources){
    auto binding=std::find_if(result->instruments.begin(),result->instruments.end(),[&](const auto &value){return value.identity==trigger.instrument;});
    if(binding==result->instruments.end() || !binding->instrument)throw std::invalid_argument("Plugin trigger source has no prepared tracker instrument");
    if(binding->plugin)continue;
    const auto index=size_t(binding-result->instruments.begin());
    const auto *previous=publishedInstrumentBindings_?&publishedInstrumentBindings_->instruments[index]:nullptr;
    uint16_t slot=0;
    if(previous&&previous->plugin&&previous->identity==trigger.instrument)slot=previous->slot;
    if(!slot){
      const auto key="source:"+std::to_string(trigger.instrument);
      auto generator=std::find(result->generators.begin(),result->generators.end(),key);
      if(generator==result->generators.end()){
        generator=std::find(result->generators.begin(),result->generators.end(),std::string{});
        if(generator==result->generators.end())throw std::invalid_argument("Prepared MIDI generator capacity is exhausted");
        *generator=key;
      }
      slot=instrumentGenerators_[size_t(generator-result->generators.begin())].slot;
    }
    binding->slot=slot;binding->midiChannel=trigger.midiChannel;binding->plugin=true;
  }
  return result;
}
void PluginChain::adoptInstrumentBindings(const InstrumentSourceBindings &next) noexcept {
  if(next.revision<=instrumentRenderedSerial_)return;instrumentRenderedSerial_=next.revision;
  for(size_t index=1;index<next.instruments.size();++index){
    const auto &binding=next.instruments[index];if(!binding.instrument)continue;
    const auto *previous=activeInstrumentBindings_?&activeInstrumentBindings_->instruments[index]:nullptr;
    const bool changed=!previous || previous->identity!=binding.identity || previous->slot!=binding.slot ||
      (previous->plugin&&binding.plugin&&binding.slot&&activeInstrumentBindings_->generators[binding.slot-1]!=next.generators[binding.slot-1]) ||
      previous->midiChannel!=binding.midiChannel || previous->plugin!=binding.plugin;
    if(!changed)continue;
    if(previous && previous->plugin){
      releaseInstrumentNotesByID(previous->identity,position_);
      for(const auto &generator:instrumentGenerators_)if(generator.slot==previous->slot)
        generator.reset(generator.adapter,binding.instrument);
    }
    auto &instrument=*binding.instrument;
    instrument.nMixPlug=OpenMPT::PLUGINDEX(binding.slot);instrument.nMidiChannel=binding.midiChannel;
    std::copy(originalInstruments_[index].keyboard.begin(),originalInstruments_[index].keyboard.end(),instrument.Keyboard.begin());
  }
  for(size_t index=0;index<instrumentGenerators_.size();++index)
    if(!activeInstrumentBindings_ || activeInstrumentBindings_->generators[index]!=next.generators[index])
      instrumentGenerators_[index].reset(instrumentGenerators_[index].adapter,nullptr);
  activeInstrumentBindings_=&next;
}
void PluginChain::renderInstrumentSources(uint32_t frames,uint64_t position) noexcept {
  if(!mixerTransition_)return;
  if(!mixerTransition_->renderSources(frames,position))failed_=true;
}
bool PluginChain::HostedMixerPlan::renderSources(void *opaque,MixerTransition::Plan &routing,
    uint32_t frames,uint64_t position,bool current) noexcept {
  auto &hosted=*static_cast<HostedMixerPlan *>(opaque);auto &chain=*hosted.owner;
  for(size_t index=0;index<hosted.rack.size();++index){
    const auto &entry=*hosted.rack[index];if(!entry.plugin->isInstrument()||routing.catalog[index].scheduledSource)continue;
    auto &wrapper=*hosted.processors[index];auto &device=wrapper.processor();
    device.plugin->transport(chain.currentTransport_);
    device.modulation=chain.modulationFor(hosted,index);
    // An unconnected endpoint is still warm. Once routed, retain its release
    // tails after note cables disappear; adding a cable never replays a note.
    if(chain.activeNoteRouting_)for(const auto &route:chain.activeNoteRouting_->routes->routes)
      if(route.endpoint.context==entry.plugin.get()){device.sourceAwake=true;break;}
    std::fill_n(chain.tailBuffer_.data(),frames*2,0.f);
    if(!wrapper.process(chain.tailBuffer_.data(),frames,position))return false;
    if(!device.sourceAwake)std::fill_n(chain.tailBuffer_.data(),frames*2,0.f);
    routing.instrument(index,0,chain.tailBuffer_.data());
    chain.songFollower(hosted,SIZE_MAX,index,0,chain.tailBuffer_.data(),frames,position);
    const auto &observed=hosted.processorObservations[index];
    if(current)chain.observation_->observe(observed.output[0],chain.tailBuffer_.data(),frames,position);
    for(const auto port:device.outputs){
      const float *samples=device.sourceAwake?device.output(port):nullptr;
      routing.instrument(index,port,samples);chain.songFollower(hosted,SIZE_MAX,index,port,samples,frames,position);
      if(current && port<64)chain.observation_->observe(observed.output[port],samples,frames,position);
    }
  }
  return !routing.runtime->failed();
}
}
