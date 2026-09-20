#include "../Audio/NativeSignalGraph.hpp"
#include "editor/NativeEffects.hpp"
#include <iostream>
#include <cmath>
#include <dlfcn.h>
using namespace Tracker;
struct FixturePlayState : OpenMPT::PlayState { using PlayState::m_nBufferCount; };
#include "GraphRealtimeAudit.hpp"
static void check(bool b,const char *message){if(!b)throw std::runtime_error(message);}
static SignalDefinition definition(uint64_t id,const PluginDescriptor &p,bool amount){
  SignalDefinition d;d.id=id;d.number=uint16_t(id);d.name="Fixture";
  d.nodes={{id+1,SignalNodeKind::Input,"Input"},{id+2,SignalNodeKind::Plugin,"Effect"},{id+3,SignalNodeKind::Output,"Output"}};
  d.nodes[1].plugin={p.format,p.name,p.path,p.classID,p.type,p.subtype,p.manufacturer};d.audio={{id+1,id+2},{id+2,id+3}};
  if(amount){d.nodes.push_back({id+4,SignalNodeKind::Amount,"Amount"});d.modulation={{id+4,id+2,7}};}
  return d;
}
int main(int argc,char **argv){@autoreleasepool{try{
  check(argc==2,"Pass VST3 fixture");auto plugins=NativePlugin::discoverVST3(argv[1]);
  auto gain=definition(100,plugins[0],true);
  auto builtin=NativePlugin::builtins();auto dc=std::find_if(builtin.begin(),builtin.end(),[](const auto &p){return p.classID=="resonance.dc-offset.v1";});check(dc!=builtin.end(),"DC fixture available");
  auto add=definition(200,*dc,false);NativeEffect effect(dc->classID,48000);effect.parameter(2,0);effect.parameter(1,25);add.nodes[1].plugin.state=effect.state();
  NativeSong native;native.patterns[0].id=3;native.tracks[0].id=1;native.mixer.buses={{1,2,MixerBusKind::Track,"Track"},{2,0,MixerBusKind::Master,"Master"}};
  native.signal.library={gain,add};native.signal.assignments={{1,100,.5,1}};
  native.signal.commands={
    {3,1,200,0,0,SignalCommandKind::Start,1,1},
    {3,1,100,32768,1,SignalCommandKind::Start,.5,1},
    {3,1,100,65536,0,SignalCommandKind::Row,.25,1},
    {3,1,100,131072,0,SignalCommandKind::Start,.8,1},
    {3,1,200,196608,0,SignalCommandKind::Stop},
    {3,1,0,262144,0,SignalCommandKind::Clear}};
  std::vector<float> reference;
  for(uint32_t block:{1u,17u,128u,257u}){
    NativeSignalGraph graph(native,48000,true);auto mixer=native.mixer;std::vector<MixerProcessorInfo> info;graph.compile(mixer,info);check(info.size()==1&&mixer.buses[0].inserts.size()==1,"Prepared graph inserted before normal bus effects");
    FixturePlayState state;state.m_nMusicSpeed=1;state.m_nSamplesPerTick=256;state.m_nTickCount=0;state.m_nPattern=0;state.m_nCurrentOrder=0;
    std::vector<float> output(256*5*2,.2f);
    for(uint32_t pos=0;pos<1280;){state.m_nRow=pos/256;state.m_nBufferCount=256-pos%256;auto count=std::min({block,256-pos%256,1280-pos});
      tracker_audit_begin();graph.begin(state,count,pos,{120,double(pos)/24000,0,4,true});bool okay=graph.process(0,output.data()+pos*2,count,pos,{});uint64_t a,f,l;tracker_audit_end(&a,&f,&l);check(okay&&a+f+l==0,"Graph playback allocates/frees/locks or fails");pos+=count;
      if(pos==256||pos==512||pos==1280){const auto activity=graph.activity();check(activity.size()==(pos==256?3:pos==512?4:1),"Graph activity includes inactive processors");
        auto ordinary=std::find_if(activity.begin(),activity.end(),[](const auto &v){return v.role==2;});check(ordinary!=activity.end()&&ordinary->order==activity.size(),"Ordinary copy must follow all live pattern processors");
        if(pos==512){auto row=std::find_if(activity.begin(),activity.end(),[](const auto &v){return v.role==0;});check(row!=activity.end()&&row->order==1,"Row processor must appear first in live activity");}}
    }
    for(uint32_t i=0;i<1280;++i){const double expected=i<128?.225:i<256?.1125:i<512?.075:i<768?.18:i<1024?.08:.1;
      if(std::abs(output[i*2]-expected)>1e-6){std::cerr<<"frame "<<i<<" value "<<output[i*2]<<" expected "<<expected<<'\n';throw std::runtime_error("Subgraph order / precise command boundary / repeat-update / row expiry / stop / clear mismatch");}}
    if(reference.empty())reference=output;else check(output==reference,"Graph commands differ across callback partitions");
  }
  // Same definition, two targets: Amount and histories must not bleed between them.
  native.mixer.buses.insert(native.mixer.buses.begin()+1,{4,2,MixerBusKind::Track,"Second"});native.signal.assignments.push_back({4,100,.9,1});NativeSignalGraph independent(native,48000,true);
  FixturePlayState state;state.m_nMusicSpeed=1;state.m_nSamplesPerTick=256;state.m_nBufferCount=256;state.m_nPattern=0;state.m_nRow=0;state.m_nTickCount=0;state.m_nCurrentOrder=0;
  independent.begin(state,128,0,{120,0,0,4,true});std::array<float,256> second;second.fill(.2f);check(independent.process(1,second.data(),128,0,{}),"Second target processing failed");check(std::abs(second[255]-.18)<1e-6,"Subgraph Amount leaked between targets");
  // A latency-bearing processor must reveal the continuously delayed dry
  // stream immediately on stop, rather than replaying stale wet compensation.
  void *bundle=dlopen((std::string(argv[1])+"/Contents/MacOS/ResonanceFixture").c_str(),RTLD_NOW|RTLD_LOCAL);
  auto delayed=reinterpret_cast<void(*)(bool)>(dlsym(bundle,"ResonanceFixtureEffectDelay"));
  auto observe=reinterpret_cast<void(*)(bool)>(dlsym(bundle,"ResonanceFixtureObserve"));
  auto observed=reinterpret_cast<uint64_t(*)()>(dlsym(bundle,"ResonanceFixtureObservedFrames"));
  auto clockErrors=reinterpret_cast<uint64_t(*)()>(dlsym(bundle,"ResonanceFixtureClockErrors"));
  check(delayed&&observe&&observed&&clockErrors,"Fixture delay and transport hooks unavailable");delayed(true);
  NativeSong timed;timed.patterns[0].id=3;timed.tracks[0].id=1;timed.mixer.buses={{1,2,MixerBusKind::Track,"Track"},{2,0,MixerBusKind::Master,"Master"}};timed.signal.library={gain};
  for(bool tails:{false,true})for(uint32_t block:{1u,17u,128u,4096u}){
    timed.signal.commands={{3,1,100,65536,0,SignalCommandKind::Start,.5,1},{3,1,100,131072,0,SignalCommandKind::Stop,1,1,tails}};
    NativeSignalGraph graph(timed,48000,true);auto mixer=timed.mixer;std::vector<MixerProcessorInfo> info;graph.compile(mixer,info);check(info[0].latency==32,"Prepared graph did not reserve constant latency");observe(true);
    FixturePlayState clock;clock.m_nMusicSpeed=1;clock.m_nSamplesPerTick=256;clock.m_nTickCount=0;clock.m_nPattern=0;clock.m_nCurrentOrder=0;
    std::array<float,2048> output{};for(uint32_t i=0;i<1024;++i)output[i*2]=output[i*2+1]=float(1+i*.001);
    for(uint32_t pos=0;pos<1024;){clock.m_nRow=pos/256;clock.m_nBufferCount=256-pos%256;const auto count=std::min({block,256-pos%256,1024-pos});
      tracker_audit_begin();graph.begin(clock,count,pos,{120,double(pos)/24000,0,4,true});const bool okay=graph.process(0,output.data()+pos*2,count,pos,{});uint64_t a,f,l;tracker_audit_end(&a,&f,&l);check(okay&&a+f+l==0,"Latent graph switching failed realtime audit");pos+=count;}
    for(uint32_t i=0;i<1024;++i){const double dry=i>=32?float(1+(i-32)*.001):0;const double expected=i<256?dry:i<288?0:i<512?dry*.5:tails&&i<544?dry*1.5:dry;
      if(std::abs(output[i*2]-expected)>2e-7){std::cerr<<"Latency switch frame "<<i<<" got "<<output[i*2]<<" expected "<<expected<<" tails "<<tails<<'\n';throw std::runtime_error("Graph stop replays wet history or loses bypass compensation");}}
    check(observed()==1024&&clockErrors()==0,"Inactive plugin did not receive continuous silent processing and accurate transport");observe(false);
  }
  // Stopping A in a delayed A→B stack must not move B ahead of A's bypass
  // delay and substitute an unrelated dry history for B's still-active sound.
  auto secondGain=definition(200,plugins[0],true);timed.signal.library={gain,secondGain};
  timed.signal.commands={{3,1,100,0,0,SignalCommandKind::Start,.5,1},{3,1,200,0,1,SignalCommandKind::Start,.25,1},{3,1,100,131072,0,SignalCommandKind::Stop},{3,1,200,196608,0,SignalCommandKind::Stop}};
  for(uint32_t block:{1u,17u,128u,4096u}){
    NativeSignalGraph graph(timed,48000,true);FixturePlayState clock;clock.m_nMusicSpeed=1;clock.m_nSamplesPerTick=256;clock.m_nTickCount=0;clock.m_nPattern=0;clock.m_nCurrentOrder=0;
    std::array<float,2048> output{};for(uint32_t i=0;i<1024;++i)output[i*2]=output[i*2+1]=float(1+i*.001);
    for(uint32_t pos=0;pos<1024;){clock.m_nRow=pos/256;clock.m_nBufferCount=256-pos%256;const auto count=std::min({block,256-pos%256,1024-pos});graph.begin(clock,count,pos,{120,double(pos)/24000,0,4,true});check(graph.process(0,output.data()+pos*2,count,pos,{}),"Delayed stack render failed");pos+=count;}
    for(uint32_t i=0;i<1024;++i){const double raw=i>=64?float(1+(i-64)*.001):0,expected=raw*(i<544?.125:i<768?.25:1);
      check(std::abs(output[i*2]-expected)<2e-7,"Stopping a delayed graph moved the remaining processor or replayed its bypass history");}
  }
  delayed(false);dlclose(bundle);
  std::cout<<"PASS hosted graph audio: row→persistent→ordinary ordering, half-row switching, update-in-place, expiry/stop/clear, independent target copies, bit-exact block partitions, delayed wet/dry cut and tails, inactive transport continuity, and zero realtime allocations/frees/locks\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}}
