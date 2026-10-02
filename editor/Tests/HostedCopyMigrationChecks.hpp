#pragma once
#include "editor/TrackerDocument.hpp"
#include "soundlib/ModInstrument.h"
static SignalDefinition hostedCopyDefinition(uint64_t id,const PluginDescriptor &p) {
  SignalDefinition d;d.id=id;d.number=uint16_t(id);d.name="Copy migration fixture";
  d.nodes={{id+1,SignalNodeKind::Input,"Input"},{id+2,SignalNodeKind::Plugin,"Effect"},{id+3,SignalNodeKind::Output,"Output"},{id+4,SignalNodeKind::Amount,"Amount"}};
  d.nodes[1].plugin={p.format,p.name,p.path,p.classID,p.type,p.subtype,p.manufacturer};d.audio={{id+1,id+2},{id+2,id+3}};d.modulation={{id+4,id+2,7}};return d;
}
// Actual shared host qualification: the renderer and sample voices continue
// while ordinary copy ownership changes at the prepared dry handoff.
static void hostedCopyMigration(const PluginDescriptor &descriptor,uint32_t rate,uint32_t block,bool sample=false) {
  auto doc=Document::demo();if(sample)doc->transaction([](OpenMPT::CSoundFile &song){song.m_nInstruments=1;song.Instruments[1]=new OpenMPT::ModInstrument(1);for(auto &pattern:song.Patterns)if(pattern.IsValid())for(auto &cell:pattern)cell={};auto &pattern=song.Patterns[0];for(auto row:{0u,4u,8u,12u}){auto &cell=*pattern.GetpModCommand(row,0);cell.note=61;cell.instr=1;}});auto native=doc->native();native.ensureMixer();
  auto recipe=hostedCopyDefinition(900,descriptor);native.signal.library={recipe};
  Renderer reference(doc->snapshotData(),rate),renderer(doc->snapshotData(),rate);
  auto referenceChain=std::make_unique<PluginChain>(std::vector<PluginState>{},rate,true);
  auto chain=std::make_unique<PluginChain>(std::vector<PluginState>{},rate,true);
  referenceChain->attachInstruments(reference,&native);chain->attachInstruments(renderer,&native);
  std::array<float,8192> original{},actual{};float expected=1;uint32_t edit=0;
  for(uint32_t position=0;position<22000;){
    if(position==4096||position==8192||position==12288||position==16384){
      expected=position==4096?.2f:position==8192?.8f:position==12288?1.f:.2f;
      auto &assignments=sample?native.signal.instrumentAssignments:native.signal.assignments;assignments.clear();if(expected!=1)assignments={{sample?native.instruments.at(1).id:native.masterID,900,expected,1}};
      const auto ports=chain->signalObservation().ports.size();std::unique_ptr<MixerTransition::Plan> prepared;
      try{prepared=chain->prepareMixerRouting(native);}catch(const std::exception &error){std::cerr<<descriptor.format<<" sample="<<sample<<" rate="<<rate<<" block="<<block<<" edit="<<position<<" ";throw;}
      check(prepared&&chain->signalObservation().ports.size()==ports,"Preparing live copy membership publishes telemetry prematurely");
      check(chain->publishMixerRouting(prepared),"Shared host rejected prepared ordinary graph copy membership");edit=position;
    }
    if(position==20480){auto invalid=native;invalid.signal.assignments={{native.masterID,999,1,1}};
      const auto ports=chain->signalObservation().ports.size();bool rejected=false;try{chain->prepareMixerRouting(invalid);}catch(const std::exception &){rejected=true;}
      check(rejected&&chain->signalObservation().ports.size()==ports&&!chain->failed(),"Rejected live copy candidate changed the accepted plan or catalogue");
    }
    auto count=std::min(block,22000-position);for(auto boundary:{4096u,8192u,12288u,16384u,20480u})if(position<boundary)count=std::min(count,boundary-position);
    tracker_audit_begin();referenceChain->beginRenderBlock();chain->beginRenderBlock();reference.render(original.data(),count);renderer.render(actual.data(),count);
    const bool okay=referenceChain->process(original.data(),count)&&chain->process(actual.data(),count);uint64_t a,f,l;tracker_audit_end(&a,&f,&l);
    if(!okay||a+f+l)std::cerr<<descriptor.format<<" sample="<<sample<<" rate="<<rate<<" block="<<block<<" frame="<<position<<" okay="<<okay<<" allocations="<<a<<" frees="<<f<<" locks="<<l<<'\n';
    check(okay&&a+f+l==0,"Hosted copy migration touched realtime memory/locks or failed rendering");
    for(uint32_t i=0;i<count;++i)if(!edit||position+i>=edit+uint32_t(std::ceil(rate*.035)))for(size_t channel=0;channel<2;++channel)
      if(std::abs(actual[i*2+channel]-original[i*2+channel]*expected)>=2e-6){std::cerr<<descriptor.format<<" sample="<<sample<<" rate="<<rate<<" block="<<block<<" frame="<<position+i<<" got="<<actual[i*2+channel]<<" expected="<<original[i*2+channel]*expected<<'\n';check(false,"Settled live graph copy PCM differs from continuing source reference");}
    check(renderer.telemetry().frames==reference.telemetry().frames,"Live copy migration reset the render clock");position+=count;
  }
}
