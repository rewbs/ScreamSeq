#pragma once
#include "editor/TrackerDocument.hpp"
#include "editor/hosted/HostedAudio.hpp"
#include "soundlib/ModInstrument.h"
#include <cmath>

// Shared by native Windows VST3 and Mac AU/VST3 suites. Compare independently
// rendered sample/plugin stems, not just nonzero output from a layered voice.
static void layeredInstrumentChecks(const Tracker::PluginDescriptor &descriptor,uint32_t rate,bool mixer,bool sampleGraph,bool preview,bool precise=false,bool nna=false) {
  using namespace Tracker;using namespace OpenMPT;
  const auto render=[&](bool sample,bool plugin,uint32_t block){
    auto doc=Document::demo();doc->transaction([&](CSoundFile &song){
      for(auto &pattern:song.Patterns)if(pattern.IsValid())for(auto &cell:pattern)cell.Clear();
      song.m_nInstruments=1;song.Instruments[1]=new ModInstrument(sample?SAMPLEINDEX(1):SAMPLEINDEX(0));
      song.Instruments[1]->nNNA=nna?NewNoteAction::Continue:NewNoteAction::NoteCut;
      for(unsigned c=0;c<2;++c){
        auto &first=*song.Patterns[0].GetpModCommand(0,c);first.note=uint8_t(49+c*7);first.instr=1;
        auto &second=*song.Patterns[0].GetpModCommand(2,c);second.note=uint8_t(52+c*7);second.instr=1;
        song.Patterns[0].GetpModCommand(4,c)->note=NOTE_KEYOFF;
        song.Patterns[0].GetpModCommand(6,c)->note=NOTE_NOTECUT;
      }
    });
    auto native=doc->native();if(mixer||sampleGraph){native.ensureMixer();
      for(auto &bus:native.mixer.buses)if(bus.kind==MixerBusKind::Track)bus.gainDB=-12;
    }
    if(sampleGraph){SignalDefinition graph;graph.id=native.makeEntity().id;graph.number=1;graph.name="Layer sample gain";
      const auto in=native.makeEntity().id,out=native.makeEntity().id;graph.nodes={{in,SignalNodeKind::Input,"Input"},{out,SignalNodeKind::Output,"Output"}};graph.audio={{in,out,0,0,.5}};
      native.signal.library={graph};native.signal.instrumentAssignments={{native.instruments.at(1).id,graph.id,1,1}};
    }
    if(precise){for(unsigned c=0;c<2;++c){const auto pattern=native.patterns.at(0).id,track=native.tracks.at(c).id;
      native.preciseNotes.push_back({pattern,track,32768,1,uint8_t(49+c*7),100});
      native.preciseNotes.push_back({pattern,track,2*65536+32768,1,uint8_t(52+c*7),100});
      native.preciseNotes.push_back({pattern,track,4*65536+32768,0,NOTE_KEYOFF,127});
      native.preciseNotes.push_back({pattern,track,6*65536+32768,0,NOTE_NOTECUT,127});
    }doc->transaction([](CSoundFile &song){for(auto &pattern:song.Patterns)if(pattern.IsValid())for(auto &cell:pattern)cell.Clear();});}
    const auto bytes=doc->snapshotData();auto renderer=std::make_unique<Renderer>(bytes,rate,0,preview,std::string{},0,PlaybackRegion{},&native);
    PluginState state{descriptor};state.instrument=1;state.instanceID="layered-synth";
    auto chain=std::make_unique<PluginChain>(plugin?std::vector<PluginState>{state}:std::vector<PluginState>{},rate,true);
    chain->attachInstruments(*renderer,&native);
    check(renderer->song().Instruments[1]->Keyboard==doc->song().Instruments[1]->Keyboard,"Plugin assignment changed playback sample keymap");
    std::vector<float> pcm(rate*2);const uint32_t release=rate/2;
    for(uint32_t at=0;at<rate;){
      if(preview&&(at==0||at==release))check(renderer->preview({49,1,100,at==0,0}),"Layered preview event refused");
      auto count=std::min(block,rate-at);if(at<release)count=std::min(count,release-at);
      uint64_t allocations,frees,locks;tracker_audit_begin();chain->beginRenderBlock();renderer->render(pcm.data()+at*2,count);const bool ok=chain->process(pcm.data()+at*2,count);tracker_audit_end(&allocations,&frees,&locks);
      check(ok&&!renderer->faulted()&&allocations+frees+locks==0,"Layered render failed or touched realtime allocation/free/locks");at+=count;
    }
    check(bytes==doc->snapshotData(),"Layered rendering altered stored samples or instrument");return pcm;
  };
  const auto sample=render(true,false,128),plugin=render(false,true,128),both=render(true,true,128);
  double sampleEnergy=0,pluginEnergy=0;for(size_t i=0;i<both.size();++i){sampleEnergy+=std::abs(sample[i]);pluginEnergy+=std::abs(plugin[i]);
    check(std::isfinite(both[i])&&std::abs(both[i]-sample[i]-plugin[i])<2e-6,"Layered PCM is not the sum of independent sample and plugin stems");}
  check(sampleEnergy>.1&&pluginEnergy>.1,"A layered fixture stem is silent");
  for(auto block:{17u,4096u}){const auto partition=render(true,true,block);for(size_t i=0;i<both.size();++i)check(std::abs(partition[i]-both[i])<2e-6,"Layered PCM depends on callback partition");}
  if(!preview&&!nna)for(size_t i=size_t(rate)*19/10;i<both.size();++i)check(std::abs(both[i])<1e-6,"Layered note cut left a sounding voice");
}
