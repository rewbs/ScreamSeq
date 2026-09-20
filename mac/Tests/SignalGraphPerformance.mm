#include "../Audio/NativeSignalGraph.hpp"
#include <chrono>
#include <iostream>
#include <algorithm>
#include <sys/resource.h>
using namespace Tracker;
#include "GraphRealtimeAudit.hpp"
struct ClockState : OpenMPT::PlayState {using PlayState::m_nBufferCount;};
int main(int argc,char **argv){@autoreleasepool{try{
  if(argc!=2)throw std::runtime_error("Supply fixture bundle");auto vst=NativePlugin::discoverVST3(argv[1]).at(0);
  auto builtins=NativePlugin::builtins();auto gain=*std::find_if(builtins.begin(),builtins.end(),[](const auto &p){return p.classID=="resonance.gainer.v1";});
  PluginDescriptor au{kAudioUnitType_Effect,kAudioUnitSubType_LowPassFilter,kAudioUnitManufacturer_Apple,"Apple AULowpass"};
  for(auto plugin:{gain,vst,au})for(uint32_t rate:{48000u,96000u}){
    constexpr uint32_t channels=16,frames=128,blocks=1000;
    NativeSong native;native.patterns[0].id=999;native.mixer.buses.push_back({1000,0,MixerBusKind::Master,"Master"});
    SignalDefinition d;d.id=100;d.number=1;d.name="Swept parameter";d.nodes={{101,SignalNodeKind::Input,"Input"},{102,SignalNodeKind::Plugin,"Effect"},{103,SignalNodeKind::Output,"Output"},{104,SignalNodeKind::LFO,"LFO"}};d.nodes[1].plugin={plugin.format,plugin.name,plugin.path,plugin.classID,plugin.type,plugin.subtype,plugin.manufacturer};d.audio={{101,102},{102,103}};d.modulation={{104,102,plugin.format=="VST3"?7u:plugin.format=="AU"?0u:1u,.75,.85,0}};native.signal.library={d};
    for(uint16_t i=0;i<channels;++i){native.tracks[i].id=i+1;native.mixer.buses.push_back({uint64_t(i+1),1000,MixerBusKind::Track,"Track"});native.signal.assignments.push_back({uint64_t(i+1),100});}
    NativeSignalGraph graph(native,rate,true);ClockState clock;clock.m_nMusicSpeed=1;clock.m_nSamplesPerTick=256;clock.m_nPattern=0;clock.m_nCurrentOrder=0;clock.m_nTickCount=0;
    std::vector<double> times;times.reserve(blocks);std::array<float,frames*2> audio{};
    for(uint32_t b=0;b<blocks;++b){uint32_t pos=b*frames;clock.m_nRow=(pos/256)%64;clock.m_nBufferCount=256-pos%256;
      const auto begin=std::chrono::steady_clock::now();tracker_audit_begin();graph.begin(clock,frames,pos,{120,double(pos)*2/rate,0,4,true});bool okay=true;
      for(uint32_t channel=0;channel<channels;++channel){audio.fill(.01);okay&=graph.process(channel,audio.data(),frames,pos,{});}
      uint64_t a,f,l;tracker_audit_end(&a,&f,&l);const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
      if(!okay||a+f+l)throw std::runtime_error("Graph stress render failed or allocated/freed/locked");times.push_back(ms);
    }
    std::sort(times.begin(),times.end());rusage usage{};getrusage(RUSAGE_SELF,&usage);
    std::cout<<"{\"format\":\""<<plugin.format<<"\",\"sampleRate\":"<<rate<<",\"channels\":"<<channels<<",\"frames\":"<<frames<<",\"callbacks\":"<<blocks<<",\"p50MS\":"<<times[blocks/2]<<",\"p99MS\":"<<times[blocks*99/100]<<",\"maxMS\":"<<times.back()<<",\"callbackBudgetMS\":"<<1000.0*frames/rate<<",\"peakRSSBytes\":"<<usage.ru_maxrss<<"}\n";
  }return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}}
