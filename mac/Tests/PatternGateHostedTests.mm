#include "../Audio/AudioUnitHost.hpp"
#include "FixtureTrust.hpp"
#include "editor/TrackerDocument.hpp"
#include "soundlib/ModInstrument.h"
#include <iostream>
#include <iomanip>
#include <dlfcn.h>
using namespace Tracker;
using namespace OpenMPT;
std::vector<PluginDescriptor> registerFixtureAUs();
void setFixtureAUPitchMode(bool);
void setFixtureAUStepped(bool);
#ifdef TRACKER_SANITIZER
static void tracker_audit_begin(){}
static void tracker_audit_end(uint64_t *a,uint64_t *f,uint64_t *l){*a=*f=*l=0;}
#else
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *,uint64_t *,uint64_t *);
#endif
static void check(bool v,const char *why){if(!v)throw std::runtime_error(why);}
static void near(double a,double b,double epsilon,const char *why){if(std::abs(a-b)>epsilon){std::cerr<<a<<" != "<<b<<'\n';throw std::runtime_error(why);}}
static PatternCommand start(const NativeSong &n,GateTarget target,uint8_t column=0){PatternCommand c;c.pattern=n.patterns.at(0).id;c.track=n.tracks.at(0).id;c.kind=PatternCommandKind::Native;c.native=NativePatternOp::Gate;c.column=column;c.arguments=nativePatternDefaults(c.native);c.arguments[0]=int(target);c.arguments[3]=.375;c.arguments[4]=.0625;c.arguments[5]=.3;c.arguments[6]=c.arguments[7]=.08;c.arguments[8]=c.arguments[9]=int(GateShape::EaseInOut);return c;}
static PatternCommand stop(PatternCommand c,uint32_t row){const auto target=c.arguments[0],binding=c.arguments[11];c.native=NativePatternOp::GateStop;c.arguments=nativePatternDefaults(c.native);c.arguments[0]=target;c.arguments[1]=binding;c.position=row*65536;return c;}
static std::unique_ptr<Document> fixture(bool instrument){auto d=std::make_unique<Document>();d->transaction([&](CSoundFile &s){s.Patterns[0].Resize(8);s.Order().assign(2,0);s.Order().SetDefaultTempoInt(125);s.Order().SetDefaultSpeed(6);
  if(instrument){s.m_nInstruments=1;s.Instruments[1]=new ModInstrument(0);}else{s.m_nSamples=1;auto &sample=s.GetSample(1);sample.Initialize();sample.nLength=131072;sample.nC5Speed=8192;sample.uFlags.set(CHN_16BIT);check(sample.AllocateSample()!=0,"Sample allocation");for(size_t i=0;i<sample.nLength;++i)sample.sample16()[i]=1200;sample.PrecomputeLoops(s,false);}
  s.ChnSettings[0].nPan=128;auto &note=*s.Patterns[0].GetpModCommand(0,0);note.note=61;note.instr=1;
});return d;}
static std::vector<float> render(Document &d,const std::vector<PluginState> &rack,uint32_t rate,uint32_t block){
  Renderer renderer(d.snapshotData(),rate);PluginChain chain(rack,rate,true);chain.attachInstruments(renderer,&d.native());chain.attachMusicalAutomation(renderer,d.native());
  std::vector<float> out(size_t(rate)*2);for(uint32_t at=0;at<rate;at+=block){const auto count=std::min(block,rate-at);uint64_t a,f,l;tracker_audit_begin();chain.beginRenderBlock();chain.syncTransport(renderer);renderer.render(out.data()+2*at,count);const bool okay=chain.process(out.data()+2*at,count);tracker_audit_end(&a,&f,&l);check(okay&&!renderer.faulted()&&a+f+l==0,"Hosted gates render without callback allocations, frees, locks or faults");}return out;
}
static void hosted(const PluginDescriptor &descriptor,uint32_t rate,double step=0){
  PluginState state{descriptor};state.instanceID="gate-plugin";state.instrument=descriptor.instrument?1:0;
  auto d=fixture(descriptor.instrument);const auto dry=render(*d,{state},rate,128);
  d->annotate([&](NativeSong &n){auto c=start(n,GateTarget::Parameter);c.arguments[11]=1;n.performance.bindings[1]={state.instanceID,7,"Gain"};n.performance.commands={c,stop(c,3)};
    n.automation={{n.makeEntity().id,n.patterns.at(0).id,state.instanceID,7,true,{{0,.2,AutomationCurve::Step},{512,.8,AutomationCurve::Step}}}};
  });
  const auto reference=render(*d,{state},rate,1);GateSignal s;s.period=.375;s.phase=.0625;s.width=.3;s.rise=s.fall=.08;s.riseShape=s.fallShape=GateShape::EaseInOut;
  for(uint32_t frame=100;frame<uint32_t(rate*.95);++frame){double value=frame<rate*.36?s.value(double(frame)/rate/.48):.8;if(step>0&&frame<rate*.36)value=std::round(value/step)*step;near(reference[2*frame],dry[2*frame]*2*value,3e-6,"Plugin gate overrides an evolving envelope and stop reveals its current value");}
  for(auto block:{17u,128u,4096u}){const auto pcm=render(*d,{state},rate,block);for(size_t i=0;i<pcm.size();++i)near(pcm[i],reference[i],3e-6,"AU/VST3 gate PCM is independent of callback partition");}
}
static void channelMix(uint32_t rate){
  auto d=fixture(false);auto dry=render(*d,{},rate,128);
  d->annotate([](NativeSong &n){auto gain=start(n,GateTarget::Volume),pan=start(n,GateTarget::Pan,1);pan.arguments[1]=pan.arguments[2]=.5;n.performance.columns[gain.track]=2;n.performance.commands={gain,pan,stop(gain,3)};});
  const auto pcm=render(*d,{},rate,128);GateSignal s;s.period=.375;s.phase=.0625;s.width=.3;s.rise=s.fall=.08;s.riseShape=s.fallShape=GateShape::EaseInOut;
  for(uint32_t f=100;f<uint32_t(rate*.95);++f){const auto g=f<rate*.36?s.value(double(f)/rate/.48):1;near(pcm[2*f],dry[2*f]*g*.5,3e-6,"Projected channel mixer applies volume and absolute pan once");near(pcm[2*f+1],dry[2*f+1]*g,3e-6,"Projected channel mixer retains right channel level");}
}
int main(int argc,char **argv){trustFixtureArguments(argc,argv);@autoreleasepool{try{
  check(argc==2,"Fixture bundle required");const auto vst=NativePlugin::discoverVST3(argv[1]),au=registerFixtureAUs();
  for(auto rate:{44100u,48000u,96000u}){channelMix(rate);for(const auto &descriptor:{vst[0],vst[1],au[0],au[1]})hosted(descriptor,rate);}
  setFixtureAUStepped(true);hosted(au[0],48000,true);setFixtureAUStepped(false);
  void *bundle=dlopen((std::string(argv[1])+"/Contents/MacOS/ResonanceFixture").c_str(),RTLD_NOW);void *symbol=bundle?dlsym(bundle,"ResonanceFixturePitchMode"):nullptr;check(symbol,"Pitch fixture hook");auto pitchMode=reinterpret_cast<void(*)(bool)>(symbol);pitchMode(true);setFixtureAUPitchMode(true);
  for(const auto &descriptor:{vst[1],au[1]}){auto d=fixture(true);PluginState state{descriptor};state.instanceID="pitch";state.instrument=1;
    d->annotate([](NativeSong &n){auto c=start(n,GateTarget::Pitch);c.arguments[1]=-1;c.arguments[2]=1;n.performance.commands={c,stop(c,3)};});
    auto pcm=render(*d,{state},48000,128);GateSignal s;s.floor=-1;s.ceiling=1;s.period=.375;s.phase=.0625;s.width=.3;s.rise=s.fall=.08;s.riseShape=s.fallShape=GateShape::EaseInOut;
    for(auto block:{1u,17u,4096u}){const auto other=render(*d,{state},48000,block);for(size_t i=0;i<pcm.size();++i)near(other[i],pcm[i],3e-7,"Pitch gate rounding is partition independent");}
    for(unsigned f=0;f<40000;++f){const auto semitones=f<17280?s.value(double(f)/48000/.48):0;const int wheel=std::clamp(int(std::lround(std::round(semitones*4096*1e9)/1e9))+8192,0,16383);if(std::abs(pcm[f*2]-.1*wheel/8192)>3e-7)std::cerr<<descriptor.format<<" frame "<<f<<" semitones "<<std::setprecision(17)<<semitones<<" wheel "<<wheel<<"\n";near(pcm[f*2],.1*wheel/8192,3e-7,"Pitch gate drives sample-aligned MIDI wheel and stops at current baseline");}
  }
  pitchMode(false);setFixtureAUPitchMode(false);
  auto stepped=reinterpret_cast<void(*)(bool)>(dlsym(bundle,"ResonanceFixtureStepped"));auto large=reinterpret_cast<void(*)(bool)>(dlsym(bundle,"ResonanceFixtureLargeCatalog"));auto single=reinterpret_cast<uint64_t(*)()>(dlsym(bundle,"ResonanceFixtureSingleSampleCalls"));check(stepped&&large&&single,"Gate queue fixture hooks");
  stepped(true);hosted(vst[0],48000,.25);large(true);
  auto dense=fixture(false);PluginState state{vst[0]};state.instanceID="dense";
  dense->annotate([&](NativeSong &n){for(unsigned i=0;i<64;++i){auto c=start(n,GateTarget::Parameter,uint8_t(i%8));c.track=n.tracks.at(i/8).id;c.arguments[11]=i+1;n.performance.columns[c.track]=8;n.performance.bindings[uint16_t(i+1)]={state.instanceID,i?20000+i:7,"Dense gate"};n.performance.commands.push_back(c);}});
  stepped(true);const auto densePCM=render(*dense,{state},48000,128);check(single()<1000,"64 stepped VST gate targets use bounded offset queues, not one-sample audio calls");
  stepped(true);const auto other=render(*dense,{state},48000,4096);check(single()<1000,"Large callback preserves bounded VST processing");for(size_t i=0;i<other.size();++i)near(other[i],densePCM[i],3e-6,"Maximum gate target queues preserve PCM across partitions");large(false);stepped(false);dlclose(bundle);
  std::cout<<"PASS native mixer, AU/VST3 effect/instrument gate overlays, evolving baseline restore, discrete targets, MIDI pitch, rates/partitions and realtime audit\n";return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}}
