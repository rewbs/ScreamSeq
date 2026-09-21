#include "../Audio/AudioUnitHost.hpp"
#include "editor/TrackerDocument.hpp"
#include "soundlib/ModInstrument.h"
#include <iostream>
#include <cmath>
using namespace Tracker;
using namespace OpenMPT;
#include "GraphRealtimeAudit.hpp"
static void check(bool okay,const char *why){if(!okay)throw std::runtime_error(why);}
static std::vector<float> render(Document &doc,uint32_t rate,uint32_t block){
  Renderer renderer(doc.serialize(),rate);PluginChain chain({},rate,true);chain.attachInstruments(renderer,&doc.native());chain.attachMusicalAutomation(renderer,doc.native());
  std::vector<float> audio(rate*2);bool ended=false;
  for(uint32_t pos=0;pos<rate;pos+=block){auto count=std::min(block,rate-pos);tracker_audit_begin();chain.syncTransport(renderer);
    if(!ended&&renderer.render(audio.data()+pos*2,count)<count){chain.endNotes();ended=true;}
    bool okay=chain.process(audio.data()+pos*2,count);uint64_t a,f,l;tracker_audit_end(&a,&f,&l);
    check(okay&&!renderer.faulted(),"Song graph render failed");check(a+f+l==0,"Song graph allocated/freed/locked in callback");
  }return audio;
}
static double difference(const std::vector<float>&a,const std::vector<float>&b,double scale=1){double worst=0;for(size_t i=0;i<a.size();++i)worst=std::max(worst,std::abs(double(a[i])-b[i]*scale));return worst;}
int main(){@autoreleasepool{try{
  auto notes=Document::demo();notes->transaction([](CSoundFile &song){song.Order().SetDefaultSpeed(1);song.Order().SetDefaultTempoInt(125);for(auto &p:song.Patterns)if(p.IsValid())for(ROWINDEX row=0;row<p.GetNumRows();++row){auto &cell=*p.GetpModCommand(row,0);cell={};cell.note=61;cell.instr=1;}});
  Renderer noteRenderer(notes->serialize(),48000);std::array<float,1000> noteAudio{};noteRenderer.render(noteAudio.data(),500);const auto firstOnset=noteRenderer.song().m_PlayState.Chn[0].nativeNoteGeneration;noteRenderer.render(noteAudio.data(),500);
  check(firstOnset==1&&noteRenderer.song().m_PlayState.Chn[0].nativeNoteGeneration==2,"Repeated identical tracker notes must emit distinct native envelope onsets");
  for(uint32_t rate:{44100u,48000u,96000u}){
    auto doc=Document::demo();doc->transaction([](CSoundFile &s){s.Order().assign(2,0);s.Patterns[0].Resize(4);for(auto &p:s.Patterns)if(p.IsValid())for(ROWINDEX row=0;row<p.GetNumRows();++row)for(CHANNELINDEX ch=1;ch<s.GetNumChannels();++ch)*p.GetpModCommand(row,ch)={};});
    uint64_t graph=0,source=0,target=0,aux=0,master=0;
    doc->annotate([&](NativeSong &n){master=n.makeEntity().id;aux=n.makeEntity().id;source=n.tracks.at(0).id;target=n.tracks.at(1).id;
      for(const auto &[channel,track]:n.tracks)n.mixer.buses.push_back({track.id,master,MixerBusKind::Track,"Track"});
      n.mixer.buses.push_back({aux,master,MixerBusKind::Return,"Graph return"});n.mixer.buses.push_back({master,0,MixerBusKind::Master,"Master"});});
    auto baseline=render(*doc,rate,128);double energy=0;for(auto sample:baseline)energy+=std::abs(sample);check(energy>1,"Reference song is silent");
    doc->annotate([&](NativeSong &n){graph=n.makeEntity().id;auto in=n.makeEntity().id,out=n.makeEntity().id;
      n.signal.library.push_back({graph,1,"Sidechain through",{{in,SignalNodeKind::Input,"Input"},{out,SignalNodeKind::Output,"Output"}},{{in,out,0,0,1},{in,out,1,1,1}}, {}});
      n.signal.assignments={{target,graph,1,1}};n.signal.inputs={{source,target,1,0,false}};n.signal.outputs={{target,aux,1}};});
    auto doubled=render(*doc,rate,128);check(difference(doubled,baseline,2)<2e-6,"Cross-subgraph input and auxiliary return do not sum to the expected audio");
    for(uint32_t block:{17u,4096u})check(difference(doubled,render(*doc,rate,block))<2e-7,"External graph routes depend on callback partition");
    // A group-level row command processes the complete summed signal. Starting
    // the same unity graph twice must not duplicate a processor or its output.
    doc->annotate([&](NativeSong &n){n.signal.assignments.clear();n.signal.inputs.clear();n.signal.outputs.clear();n.signal.library[0].audio.resize(1);
      n.signal.lanes[master]=2;auto pattern=n.patterns.at(0).id;
      n.signal.commands={{pattern,master,graph,0,0,SignalCommandKind::Start},{pattern,master,graph,32768,1,SignalCommandKind::Start},{pattern,master,graph,65536,0,SignalCommandKind::Row},{pattern,master,0,131072,0,SignalCommandKind::Clear}};});
    check(difference(baseline,render(*doc,rate,17))<2e-7,"Group row/persistent commands alter a unity graph");
    auto before=doc->native();bool rejected=false;try{doc->annotate([&](NativeSong &n){n.signal.assignments={{target,graph}};auto &d=n.signal.library[0];d.audio.push_back({d.nodes[0].id,d.nodes[1].id,1,1,1});n.signal.inputs={{master,target,1}};n.signal.outputs={{target,aux,1}};});}catch(const std::invalid_argument &){rejected=true;}
    check(rejected&&doc->native()==before,"Cross-subgraph feedback cycle partially committed");
  }
  for(uint32_t rate:{44100u,48000u,96000u}){
    auto doc=Document::demo();doc->transaction([](CSoundFile &song){
      song.m_nInstruments=2;song.Instruments[1]=new ModInstrument(1);song.Instruments[2]=new ModInstrument(2);song.Instruments[1]->nNNA=NewNoteAction::Continue;
      for(auto &p:song.Patterns)if(p.IsValid())for(ROWINDEX r=0;r<p.GetNumRows();++r)for(CHANNELINDEX ch=0;ch<song.GetNumChannels();++ch)*p.GetpModCommand(r,ch)={};
      song.Order().SetDefaultSpeed(1);song.Order().SetDefaultTempoInt(125);
      for(auto [r,ch,inst]:{std::tuple{0,0,1},std::tuple{0,1,1},std::tuple{1,0,2},std::tuple{2,1,1},std::tuple{3,1,2}}){auto &c=*song.Patterns[0].GetpModCommand(r,ch);c.note=61;c.instr=uint8_t(inst);}
      song.ChnSettings[0].nPan=0;song.ChnSettings[1].nPan=256;
    });
    uint64_t instrumentGraph=0;
    doc->annotate([&](NativeSong &n){auto master=n.makeEntity().id;for(const auto &[channel,track]:n.tracks)n.mixer.buses.push_back({track.id,master,MixerBusKind::Track,"Track"});n.mixer.buses.push_back({master,0,MixerBusKind::Master,"Master"});
      instrumentGraph=n.makeEntity().id;auto in=n.makeEntity().id,out=n.makeEntity().id;n.signal.library.push_back({instrumentGraph,1,"Half level",{{in,SignalNodeKind::Input,"Input"},{out,SignalNodeKind::Output,"Output"}},{{in,out,0,0,.5}}, {}});
      auto channelGraph=n.makeEntity().id;in=n.makeEntity().id;out=n.makeEntity().id;n.signal.library.push_back({channelGraph,2,"Channel double",{{in,SignalNodeKind::Input,"Input"},{out,SignalNodeKind::Output,"Output"}},{{in,out,0,0,2}}, {}});n.signal.assignments={{n.tracks.at(0).id,channelGraph,1,1}};
    });
    const auto sampleOneVolume=doc->song().GetSample(1).nGlobalVol,sampleTwoVolume=doc->song().GetSample(2).nGlobalVol;
    doc->transaction([](CSoundFile &song){song.GetSample(2).nGlobalVol=0;});auto onlyOne=render(*doc,rate,128);
    doc->transaction([&](CSoundFile &song){song.GetSample(2).nGlobalVol=sampleTwoVolume;song.GetSample(1).nGlobalVol=0;});auto onlyTwo=render(*doc,rate,128);
    doc->transaction([&](CSoundFile &song){song.GetSample(1).nGlobalVol=sampleOneVolume;});
    doc->annotate([&](NativeSong &n){n.signal.instrumentAssignments={{n.instruments.at(1).id,instrumentGraph,1,1}};});
    for(uint32_t block:{17u,128u,4096u}){auto result=render(*doc,rate,block);for(size_t i=0;i<result.size();++i)check(std::abs(result[i]-(onlyOne[i]*.5+onlyTwo[i]))<3e-6,"Instrument graph loses per-channel routing, NNA ownership, or dry neighboring instruments");}
  }
  std::cout<<"PASS song graph integration: sample voices, external sidechains, auxiliary returns, group command stacks, three sample rates, callback partitions, realtime safety, atomic feedback rejection\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}}
