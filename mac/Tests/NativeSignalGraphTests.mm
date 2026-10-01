#include "../Audio/NativeSignalGraph.hpp"
#include "FixtureTrust.hpp"
#include "editor/NativeEffects.hpp"
#include "soundlib/ModInstrument.h"
#include "editor/hosted/GraphPluginEndpoint.hpp"
#include <iostream>
#include <cmath>
#include <dlfcn.h>
using namespace Tracker;
std::vector<PluginDescriptor> registerFixtureAUs();
void setFixtureAUStepped(bool);
void setFixtureAUHiddenGain(float);
uint64_t fixtureAUCreatedCount();
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
static void recipeBypassEndpoint(const PluginDescriptor &descriptor,const char *path) {
  auto bundle=dlopen((std::string(path)+"/Contents/MacOS/ResonanceFixture").c_str(),RTLD_NOW|RTLD_LOCAL);
  auto latency=reinterpret_cast<int(*)(uint32_t)>(dlsym(bundle,"ResonanceFixtureLatency"));
  auto auxiliary=reinterpret_cast<void(*)(bool)>(dlsym(bundle,"ResonanceFixtureEffectAuxiliary"));
  auto observe=reinterpret_cast<void(*)(bool)>(dlsym(bundle,"ResonanceFixtureObserve"));
  auto observed=reinterpret_cast<uint64_t(*)()>(dlsym(bundle,"ResonanceFixtureObservedFrames"));
  auto clockErrors=reinterpret_cast<uint64_t(*)()>(dlsym(bundle,"ResonanceFixtureClockErrors"));
  auto hidden=reinterpret_cast<void(*)(float)>(dlsym(bundle,"ResonanceFixtureHiddenGain"));
  check(latency&&auxiliary&&observe&&observed&&clockErrors&&hidden,"Recipe bypass fixture hooks unavailable");
  auxiliary(true);latency(13);
  for(uint32_t rate:{44100u,48000u,96000u})for(uint32_t block:{17u,512u,4096u})for(bool initiallyBypassed:{false,true}) {
    auto d=definition(700,descriptor,false);d.nodes[1].plugin.outputs={1};d.nodes[1].plugin.bypass=initiallyBypassed;
    auto state=std::make_shared<GraphPluginState>(d,d.nodes[1],rate,true);GraphPluginEndpoint endpoint(state,rate);
    check(state->latency==13&&state->plugin->bypassed()==initiallyBypassed,"Recipe initializes the requested host bypass with unchanged latency");
    observe(true);double wet=initiallyBypassed?0:1,from=wet,to=wet;uint32_t elapsed=0;const auto fade=uint32_t(std::round(rate*.005));
    std::array<float,8192> audio{};
    for(uint32_t at=0;at<5000;) {
      auto count=std::min(block,5000-at);for(auto boundary:{1000u,1100u,3000u})if(boundary>at)count=std::min(count,boundary-at);
      if(at==1000||at==1100||at==3000){const bool bypass=at==1100?initiallyBypassed:!initiallyBypassed;endpoint.bypass(bypass);from=wet;to=bypass?0:1;elapsed=0;}
      for(uint32_t i=0;i<count;++i)audio[i*2]=audio[i*2+1]=float(.2+.4*std::sin((at+i)*.007));
      tracker_audit_begin();endpoint.transport({120,double(at)*2/rate,0,4,true},true);const bool okay=endpoint.process(audio.data(),count,at,{});uint64_t a,f,l;tracker_audit_end(&a,&f,&l);
      check(okay&&a+f+l==0,"Recipe host bypass, latent dry and auxiliary processing allocate/free/lock on callback");
      const auto *aux=endpoint.output(1);check(aux,"Recipe auxiliary remains available while bypassed");
      for(uint32_t i=0;i<count;++i){const auto frame=at+i;const float dry=frame<13?0:float(.2+.4*std::sin((frame-13)*.007));
        const double expected=dry*(1-.5*wet),expectedAux=dry*wet;
        check(std::abs(audio[i*2]-expected)<2e-7&&std::abs(audio[i*2+1]-expected)<2e-7&&std::abs(aux[i*2]-expectedAux)<2e-7&&std::abs(aux[i*2+1]-expectedAux)<2e-7,"Latency-aligned recipe dry and auxiliary fade differ from independent per-sample reference");
        if(elapsed<fade)++elapsed;wet=from+(to-from)*double(elapsed)/fade;
      }at+=count;
    }
    check(observed()==5000&&clockErrors()==0,"Bypassed recipe processor must keep continuous, correctly positioned vendor processing");observe(false);
  }
  // Unlike separate latency/bypass and zero-latency preset tests, this case
  // detects an audible dip from crossfading an unfilled replacement dry delay.
  for(uint32_t rate:{44100u,48000u,96000u})for(uint32_t block:{17u,512u,4096u}) {
    auto d=definition(700,descriptor,false);d.nodes[1].plugin.outputs={1};d.nodes[1].plugin.bypass=true;
    auto original=std::make_shared<GraphPluginState>(d,d.nodes[1],rate,true);hidden(.25f);
    auto replacement=std::make_shared<GraphPluginState>(d,d.nodes[1],rate,true);hidden(1);
    GraphPluginEndpoint endpoint(original,rate);std::array<float,8192> audio{};
    const uint32_t settledAt=1000+13+uint32_t(std::ceil(rate*.01));
    for(uint32_t at=0;at<2500;){auto count=std::min(block,2500-at);if(at<1000)count=std::min(count,1000-at);
      for(uint32_t i=0;i<count;++i)audio[i*2]=audio[i*2+1]=float(.2+.4*std::sin((at+i)*.007));
      tracker_audit_begin();if(at==1000){endpoint.adopt(*replacement);endpoint.bypass(true);}
      endpoint.transport({120,double(at)*2/rate,0,4,true},true);const bool okay=endpoint.process(audio.data(),count,at,{});uint64_t a,f,l;tracker_audit_end(&a,&f,&l);
      check(okay&&a+f+l==0,"Latent bypass replacement warmup allocates/frees/locks on callback");
      const auto *aux=endpoint.output(1);check(aux,"Latent replacement retains its auxiliary output identity");
      for(uint32_t i=0;i<count;++i){const auto frame=at+i;const float expected=frame<13?0:float(.2+.4*std::sin((frame-13)*.007));
        check(std::abs(audio[i*2]-expected)<2e-7&&std::abs(audio[i*2+1]-expected)<2e-7&&aux[i*2]==0&&aux[i*2+1]==0,"Bypassed preset replacement leaked cold delay samples or audible auxiliary output");}
      if(at>=1000)check(endpoint.ready(replacement.get())==(at+count>=settledAt),"Preset readiness must include bounded latency warmup and the complete fade");at+=count;
    }
  }
  latency(0);auxiliary(false);
  for(uint32_t block:{17u,512u,4096u}) {
    auto d=definition(700,descriptor,false);auto original=std::make_shared<GraphPluginState>(d,d.nodes[1],48000,true);
    hidden(.25f);auto replacement=std::make_shared<GraphPluginState>(d,d.nodes[1],48000,true);hidden(1);
    GraphPluginEndpoint endpoint(original,48000);std::array<float,8192> audio{};
    double wet=1,from=1,to=1;uint32_t elapsed=240;
    for(uint32_t at=0;at<2400;){auto count=std::min(block,2400-at);for(auto boundary:{1024u,1100u,1300u})if(boundary>at)count=std::min(count,boundary-at);
      audio.fill(1);tracker_audit_begin();if(at==1024)endpoint.adopt(*replacement);
      if(at==1100||at==1300){const bool bypass=at==1100;endpoint.bypass(bypass);from=wet;to=bypass?0:1;elapsed=0;}
      endpoint.transport({120,double(at)/24000,0,4,true},true);const bool okay=endpoint.process(audio.data(),count,at,{});uint64_t a,f,l;tracker_audit_end(&a,&f,&l);
      check(okay&&a+f+l==0,"Bypass during opaque preset replacement remains callback-safe");
      if(at==1100||at==1300)check(original->plugin->bypassed()==(at==1100)&&replacement->plugin->bypassed()==(at==1100),"Bypass request must reach both fading vendor incarnations");
      for(uint32_t i=0;i<count;++i){const auto frame=at+i;const double mix=frame<1024?0:std::min(1.,double(frame-1024+1)/480),gain=.5*(1-mix)+.125*mix;
        check(std::abs(audio[i*2]-(1+(gain-1)*wet))<2e-7,"Preset fade and interrupted bypass differ from independent combined PCM reference");
        if(elapsed<240)++elapsed;wet=from+(to-from)*double(elapsed)/240;
      }at+=count;
    }
  }
  dlclose(bundle);
}
static void scalarDiscreteHosted(const PluginDescriptor &descriptor,uint32_t parameter) {
  NativeSong native;native.patterns[0].id=3;native.tracks[0].id=1;native.mixer.buses={{1,2,MixerBusKind::Track,"Track"},{2,0,MixerBusKind::Master,"Master"}};
  auto d=definition(100,descriptor,true);d.nodes.back().kind=SignalNodeKind::Automation;
  d.nodes.back().envelopes={{3,true,{{0,.1,AutomationCurve::Linear},{64,.9,AutomationCurve::Linear},{128,.2,AutomationCurve::Linear},{256,.8}}}};
  d.modulation[0].parameter=parameter;d.modulation[0].quantized=true;native.signal.library={d};native.signal.assignments={{1,100,1,1}};
  for(uint32_t block:{17u,512u,4096u}) {
    NativeSignalGraph graph(native,48000,true);NativePlugin reference(PluginState{descriptor},48000,true);const auto catalog=reference.parameters();const auto metadata=std::find_if(catalog.begin(),catalog.end(),[&](const auto &p){return p.id==parameter;});check(metadata!=catalog.end()&&metadata->step==1&&!metadata->continuous,"AU/builtin catalogue supplies the actual discrete step");
    FixturePlayState clock;clock.m_nMusicSpeed=1;clock.m_nSamplesPerTick=2048;clock.m_nTickCount=0;clock.m_nPattern=0;clock.m_nCurrentOrder=0;clock.m_nRow=0;clock.m_nCurrentRowsPerBeat=4;
    std::array<float,4096> output,expected;for(unsigned i=0;i<2048;++i){output[i*2]=expected[i*2]=.6f;output[i*2+1]=expected[i*2+1]=.2f;}
    for(uint32_t at=0;at<2048;){auto count=std::min(block,2048-at);clock.m_nBufferCount=2048-at;
      tracker_audit_begin();graph.begin(clock,count,at,{120,double(at)/24000,0,4,true},1);const bool okay=graph.process(0,output.data()+at*2,count,at,{});uint64_t a,f,l;tracker_audit_end(&a,&f,&l);check(okay&&a+f+l==0,"AU/builtin discrete modulation fails realtime audit");at+=count;}
    for(uint32_t frame=0;frame<2048;++frame){const double value=std::round(automationValue(d.nodes.back().envelopes[0].points,frame/8.,256,4));check(reference.parameter(parameter,float(value))&&reference.process(expected.data()+frame*2,1,frame),"Independent scalar parameter reference failed");}
    for(size_t i=0;i<output.size();++i)check(std::abs(output[i]-expected[i])<2e-6,"AU/builtin stepped audio differs from independently applied sample-timed values");
  }
}
static void discreteHosted(const PluginDescriptor &descriptor,const char *path) {
  auto bundle=dlopen((std::string(path)+"/Contents/MacOS/ResonanceFixture").c_str(),RTLD_NOW|RTLD_LOCAL);
  auto stepped=reinterpret_cast<void(*)(bool)>(dlsym(bundle,"ResonanceFixtureStepped"));
  auto singles=reinterpret_cast<uint64_t(*)()>(dlsym(bundle,"ResonanceFixtureSingleSampleCalls"));
  auto hidden=reinterpret_cast<void(*)(float)>(dlsym(bundle,"ResonanceFixtureHiddenGain"));
  check(stepped&&singles&&hidden,"Discrete VST fixture hooks unavailable");stepped(true);
  PluginState quiet{descriptor};hidden(.25f);{NativePlugin plugin(quiet,48000,true);quiet=plugin.state();}hidden(1);
  NativeSong native;native.patterns[0].id=3;native.tracks[0].id=1;native.mixer.buses={{1,2,MixerBusKind::Track,"Track"},{2,0,MixerBusKind::Master,"Master"}};
  auto d=definition(100,descriptor,true);d.nodes.back().kind=SignalNodeKind::Automation;
  d.nodes.back().envelopes={{3,true,{{0,.1,AutomationCurve::Linear},{256,.9}}}};
  d.modulation[0].minimum=-.1;d.modulation[0].maximum=1;d.modulation[0].base=.05;d.modulation[0].quantized=true;
  native.signal.library={d};native.signal.assignments={{1,100,1,1}};
  auto continuous=native;continuous.signal.library[0].modulation[0].quantized=false;bool rejected=false;
  try{NativeSignalGraph unsupported(continuous,48000,true);}catch(const std::invalid_argument &){rejected=true;}
  check(rejected,"Stepped vendor parameter requires explicit discrete mode before rendering");
  std::vector<float> reference;
  for(uint32_t block:{17u,512u,4096u}) {
    NativeSignalGraph graph(native,48000,true);auto next=native;next.signal.library[0].nodes[1].plugin.state=quiet.state;GraphControlPlan replacement;graph.prepareParameters(next.signal,replacement);
    GraphControlPlan invalid;rejected=false;try{graph.prepareParameters(continuous.signal,invalid);}catch(const std::invalid_argument &){rejected=true;}check(rejected,"Live controls cannot disable the required discrete mode");
    FixturePlayState clock;clock.m_nMusicSpeed=1;clock.m_nSamplesPerTick=4096;clock.m_nTickCount=0;clock.m_nPattern=0;clock.m_nCurrentOrder=0;clock.m_nRow=0;clock.m_nCurrentRowsPerBeat=4;
    std::vector<float> output(8192,1);stepped(true);
    for(uint32_t at=0;at<4096;){auto count=std::min(block,4096-at);if(at<2048)count=std::min(count,2048-at);clock.m_nBufferCount=4096-at;
      tracker_audit_begin();if(at==2048){for(const auto &p:replacement.presets)p.endpoint->adopt(*p.state);for(const auto &[runtime,controls]:replacement.runtimes)runtime->controls(*controls);}
      graph.begin(clock,count,at,{120,double(at)/24000,0,4,true},1);const bool okay=graph.process(0,output.data()+at*2,count,at,{});uint64_t a,f,l;tracker_audit_end(&a,&f,&l);
      check(okay&&a+f+l==0,"Discrete sample points and preset fanout perform no realtime allocation/free/lock");at+=count;}
    if(block!=17)check(singles()==0,"VST discrete steps must travel inside ordinary audio blocks, never one-sample process calls");
    for(uint32_t frame=0;frame<4096;++frame){const double value=std::round(std::clamp(.05-.1+1.1*(.1+.8*frame/4096),0.,1.)*4)/4;
      const double mix=frame<2048?0:std::min(1.,double(frame-2048+1)/480);const double expected=value*(1-.75*mix);
      check(std::abs(output[frame*2]-expected)<2e-7,"Actual VST discrete values and old/new preset fade differ from independent PCM reference");}
    if(reference.empty())reference=output;else for(size_t i=0;i<output.size();++i)check(std::abs(output[i]-reference[i])<2e-7,"Hosted discrete modulation depends on caller partition");
  }
  // Validate the borrowed-span boundary before any vendor process or parameter update.
  {NativePlugin plugin(PluginState{descriptor},48000,true);std::array<float,66> audio{};std::array<double,33> values{};PluginParameterSamples excessive{7,0,1,values};
    check(!plugin.process(audio.data(),33,0,{},nullptr,{&excessive,1}),"Dense parameter preparation rejects a span beyond its fixed 32-frame bound");
    std::array<PluginParameterSamples,65> targets{};check(!plugin.process(audio.data(),1,0,{},nullptr,targets),"Dense parameter preparation rejects more than 64 targets");}
  auto large=reinterpret_cast<void(*)(bool)>(dlsym(bundle,"ResonanceFixtureLargeCatalog"));check(large,"Dense target fixture hook unavailable");large(true);
  {auto dense=native;for(uint32_t i=1;i<64;++i){auto edge=dense.signal.library[0].modulation[0];edge.parameter=20000+i;dense.signal.library[0].modulation.push_back(edge);}
    NativeSignalGraph graph(dense,48000,true);FixturePlayState clock;clock.m_nMusicSpeed=1;clock.m_nSamplesPerTick=4096;clock.m_nBufferCount=4096;clock.m_nTickCount=0;clock.m_nPattern=0;clock.m_nCurrentOrder=0;clock.m_nRow=0;clock.m_nCurrentRowsPerBeat=4;std::array<float,1024> audio;audio.fill(1);stepped(true);
    tracker_audit_begin();graph.begin(clock,512,0,{120,0,0,4,true},1);const bool okay=graph.process(0,audio.data(),512,0,{});uint64_t a,f,l;tracker_audit_end(&a,&f,&l);check(okay&&a+f+l==0&&singles()==0,"Maximum64 discrete VST targets remain bounded, realtime-safe and use multi-sample audio blocks");
    for(uint32_t i=0;i<512;++i)check(std::abs(audio[i*2]-std::round(std::clamp(.05-.1+1.1*(.1+.8*i/4096),0.,1.)*4)/4)<2e-7,"Dense target queue overflow silently lost the audible parameter");}
  large(false);stepped(false);dlclose(bundle);
}
static void sourcePreparationFailure(const PluginDescriptor &descriptor) {
  NativeSong native;native.patterns[0].id=3;native.tracks[0].id=1;native.mixer.buses={{1,2,MixerBusKind::Track,"Track"},{2,0,MixerBusKind::Master,"Master"}};
  native.signal.library={definition(100,descriptor,false)};native.signal.assignments={{1,100}};NativeSignalGraph graph(native,48000,true);
  auto next=native;next.signal.library[0].nodes.push_back({104,SignalNodeKind::Amount,"First modulation"});next.signal.library[0].modulation={{104,102,7,0,.4,.2,true}};
  for(unsigned attempt=0;attempt<3;++attempt){GraphControlPlan rejected;rejected.preparationHeadroom=sizeof(PluginParameterQueue)-1;bool refused=false;try{graph.prepareParameters(next.signal,rejected);}catch(const std::invalid_argument &){refused=true;}check(refused,"First-use scheduling preflights storage before allocating another vendor queue");}
  GraphControlPlan prepared;graph.prepareParameters(next.signal,prepared);
  check(prepared.scheduling.size()==1&&!prepared.scheduling[0].state->scheduling&&!prepared.scheduling[0].state->plugin->initiallyScheduled(),"Failed or merely prepared edits cannot attach queue storage to the live vendor");
  FixturePlayState clock;clock.m_nMusicSpeed=1;clock.m_nSamplesPerTick=4096;clock.m_nBufferCount=4096;clock.m_nTickCount=0;clock.m_nPattern=0;clock.m_nCurrentOrder=0;clock.m_nRow=0;clock.m_nCurrentRowsPerBeat=4;
  std::array<float,1024> audio;audio.fill(1);graph.begin(clock,512,0,{120,0,0,4,true},64);check(graph.process(0,audio.data(),512,0,{}),"Unpublished source cannot poison old render plan");for(float value:audio)check(std::abs(value-.5)<1e-6,"Rejected preparation leaves old audible parameter unchanged");
  tracker_audit_begin();for(const auto &queue:prepared.scheduling)queue.state->plugin->adoptScheduling(queue.queue.get());for(const auto &owner:prepared.runtimeOwners){owner.state->inheritState(**owner.target);*owner.target=owner.state.get();}for(const auto &[runtime,controls]:prepared.runtimes)runtime->controls(*controls);
  audio.fill(1);graph.begin(clock,512,512,{120,0,0,4,true},64);const bool okay=graph.process(0,audio.data(),512,512,{});uint64_t allocations,frees,locks;tracker_audit_end(&allocations,&frees,&locks);
  check(okay&&allocations+frees+locks==0,"Prepared first-use source/queue adoption requires no callback allocation/free/lock");for(float value:audio)check(std::abs(value-.6)<1e-6,"Successful retry delivers the newly prepared source to the retained vendor");
}
static void newlyWatchedNotes(const PluginDescriptor &descriptor) {
  for(uint32_t block:{17u,512u,4096u})for(unsigned scope=0;scope<4;++scope) {
    NativeSong native;native.patterns[0].id=3;native.tracks[0].id=1;native.mixer.buses={{1,2,MixerBusKind::Track,"Track"},{2,0,MixerBusKind::Master,"Master"}};
    native.signal.library={definition(100,descriptor,false)};native.signal.library[0].nodes[1].plugin.parameters[7]=.2;native.signal.assignments={{1,100}};
    OpenMPT::ModInstrument instrument;
    std::vector<SignalSampleSource> sources;if(scope>=2)sources.push_back({1,&instrument,uint16_t(scope==3?UINT16_MAX:0),1,99});
    NativeSignalGraph live(native,48000,true,sources);
    auto next=native;next.signal.library[0].nodes.push_back({104,SignalNodeKind::NoteEnvelope,"Newly watched notes"});next.signal.library[0].nodes.back().attack=.003;next.signal.library[0].nodes.back().release=.007;next.signal.library[0].modulation={{104,102,7,0,.6,.2,true}};
    GraphControlPlan prepared;live.prepareParameters(next.signal,prepared);
    NativeSignalGraph reference(next,48000,true,sources);
    FixturePlayState state;state.m_nMusicSpeed=1;state.m_nSamplesPerTick=8192;state.m_nBufferCount=8192;state.m_nTickCount=0;state.m_nPattern=0;state.m_nCurrentOrder=0;state.m_nRow=0;state.m_nCurrentRowsPerBeat=4;
    const auto voiceIndex=scope==0?0:20;auto &voice=state.Chn[voiceIndex];voice.nNote=60;voice.increment.Set(1);voice.nativeNoteGeneration=1;voice.nMasterChn=scope==3?0:1;voice.isPreviewNote=scope==3;voice.pModInstrument=scope>=2?&instrument:nullptr;
    std::array<float,8192> actual{},expected{};actual.fill(1);live.begin(state,256,0,{120,0,0,4,true},64);check(live.process(0,actual.data(),256,0,{}),"Pre-source graph keeps processing held voices");
    tracker_audit_begin();for(const auto &queue:prepared.scheduling)queue.state->plugin->adoptScheduling(queue.queue.get());for(const auto &owner:prepared.runtimeOwners){owner.state->inheritState(**owner.target);*owner.target=owner.state.get();}for(const auto &[runtime,controls]:prepared.runtimes)runtime->controls(*controls);uint64_t a,f,l;tracker_audit_end(&a,&f,&l);check(a+f+l==0,"Adding note watch must only adopt prepared state");
    bool opened=false,released=false;
    for(uint32_t at=256;at<3200;){auto count=std::min(block,3200-at);for(auto boundary:{1024u,2048u})if(boundary>at)count=std::min(count,boundary-at);
      if(at==1024)voice.dwFlags.set(OpenMPT::CHN_KEYOFF);if(at==2048){voice.dwFlags.reset(OpenMPT::CHN_KEYOFF);++voice.nativeNoteGeneration;}
      state.m_nBufferCount=8192-at;actual.fill(1);expected.fill(1);
      tracker_audit_begin();live.begin(state,count,at,{120,double(at)/24000,0,4,true},64);reference.begin(state,count,at,{120,double(at)/24000,0,4,true},64);const bool okay=live.process(0,actual.data(),count,at,{})&&reference.process(0,expected.data(),count,at,{});tracker_audit_end(&a,&f,&l);
      check(okay&&a+f+l==0,"New held-note watch processing must allocate/free/lock nothing");
      for(uint32_t i=0;i<count*2;++i){check(std::abs(actual[i]-expected[i])<2e-7,"New note watch misses a current held voice, NNA or sample-inspector scope");if(at+i/2<1024&&actual[i]>.6)opened=true;if(at+i/2>1700&&at+i/2<2048&&actual[i]<.3)released=true;}at+=count;
    }
    check(opened&&released,"New note source must both attack for a held note and release on its real note-off");
  }
}
static void adoptGraphPlan(GraphControlPlan &plan,uint64_t position) {
  for(const auto &queue:plan.scheduling)queue.state->plugin->adoptScheduling(queue.queue.get());
  for(const auto &owner:plan.runtimeOwners){if(owner.adopt)owner.adopt(owner.context,owner.state.get(),owner.processors,owner.structural);else {owner.state->inheritState(**owner.target);*owner.target=owner.state.get();}}
  for(const auto &preset:plan.presets)preset.endpoint->adopt(*preset.state);
  for(const auto &[endpoint,bypass]:plan.bypasses)endpoint->bypass(bypass);
  for(const auto &[runtime,controls]:plan.runtimes)runtime->controls(*controls);
  for(const auto &change:plan.updates){if(change.runtime)change.runtime->parameterBase(change.node,change.parameter,change.value);else if(change.endpoint){check(change.endpoint->parameter(change.parameter,change.value,position),"Prepared graph baseline rejected");if(change.appliedBaseline)*change.appliedBaseline=change.value;}}
}
static void compoundHeldNote(const PluginDescriptor &descriptor) {
  NativeSong native;native.patterns[0].id=3;native.tracks[0].id=1;native.mixer.buses={{1,2,MixerBusKind::Track,"Track"},{2,0,MixerBusKind::Master,"Master"}};native.signal.library={definition(100,descriptor,false)};native.signal.assignments={{1,100}};
  NativeSignalGraph graph(native,48000,true);auto next=native;auto &d=next.signal.library[0];
  d.nodes.push_back({104,SignalNodeKind::NoteEnvelope,"New note envelope"});d.nodes.back().attack=.001;d.modulation={{104,102,7,0,.6,.2,true}};
  d.nodes.push_back({105,SignalNodeKind::Plugin,"Unity"});d.nodes.back().plugin.classID="resonance.gainer.v1";d.audio={{101,102},{102,105},{105,103}};
  FixturePlayState state;state.m_nMusicSpeed=1;state.m_nSamplesPerTick=8192;state.m_nBufferCount=8192;state.m_nTickCount=0;state.m_nPattern=0;state.m_nCurrentOrder=0;state.m_nRow=0;state.m_nCurrentRowsPerBeat=4;
  auto &voice=state.Chn[0];voice.nNote=60;voice.increment.Set(1);voice.nativeNoteGeneration=1;
  std::array<float,8192> audio;audio.fill(1);graph.begin(state,64,0,{120,0,0,4,true},64);check(graph.process(0,audio.data(),64,0,{}),"Initial held voice processing failed");
  GraphControlPlan plan;graph.prepareParameters(next.signal,plan);check(plan.runtimeOwners[0].structural,"Compound note fixture requires a real audio topology transition");audio.fill(1);
  tracker_audit_begin();adoptGraphPlan(plan,64);graph.begin(state,4096,64,{120,64./24000,0,4,true},64);const bool okay=graph.process(0,audio.data(),4096,64,{});uint64_t a,f,l;tracker_audit_end(&a,&f,&l);
  check(okay&&a+f+l==0,"Compound source/topology adoption violates realtime ownership");
  check(audio[8190]>.79f&&audio[8190]<.81f,"A pending note envelope must capture held voices before the mid-buffer topology handoff");
}
static void structuralRecipes(const PluginDescriptor &descriptor,const char *path,bool au) {
  auto bundle=dlopen((std::string(path)+"/Contents/MacOS/ResonanceFixture").c_str(),RTLD_NOW|RTLD_LOCAL);
  auto delayed=reinterpret_cast<void(*)(bool)>(dlsym(bundle,"ResonanceFixtureEffectDelay"));
  auto hidden=reinterpret_cast<void(*)(float)>(dlsym(bundle,"ResonanceFixtureHiddenGain"));
  auto created=reinterpret_cast<uint64_t(*)()>(dlsym(bundle,"ResonanceFixtureCreated"));
  auto observe=reinterpret_cast<void(*)(bool)>(dlsym(bundle,"ResonanceFixtureObserve"));
  auto observed=reinterpret_cast<uint64_t(*)()>(dlsym(bundle,"ResonanceFixtureObservedFrames"));
  auto clockErrors=reinterpret_cast<uint64_t(*)()>(dlsym(bundle,"ResonanceFixtureClockErrors"));
  check(delayed&&hidden&&created&&observe&&observed&&clockErrors,"Structural fixture hooks unavailable");
  const auto builtins=NativePlugin::builtins();const auto dc=std::find_if(builtins.begin(),builtins.end(),[](const auto &p){return p.classID=="resonance.dc-offset.v1";});
  for(bool latent:{false,true}){if(au&&latent)continue;delayed(latent);
    for(uint32_t rate:{44100u,48000u,96000u}){
      std::vector<float> reference;
      for(uint32_t block:{17u,512u,4096u}){
        if(au)setFixtureAUHiddenGain(.73f);else hidden(.73f);
        NativeSong native;native.patterns[0].id=3;native.tracks[0].id=1;native.mixer.buses={{1,2,MixerBusKind::Track,"Track"},{2,0,MixerBusKind::Master,"Master"}};
        auto d=definition(100,descriptor,false);auto offset=definition(200,*dc,false).nodes[1];offset.id=105;offset.plugin.parameters={{1,10},{2,0}};d.nodes.push_back(offset);d.audio={{101,102},{102,105},{105,103}};native.signal.library={d};native.signal.assignments={{1,100}};
        NativeSignalGraph live(native,rate,true);const auto constructionCount=au?fixtureAUCreatedCount():created();if(au)setFixtureAUHiddenGain(.12f);else hidden(.12f);
        if(!au)observe(true);
        FixturePlayState clock;clock.m_nMusicSpeed=1;clock.m_nSamplesPerTick=8192;clock.m_nBufferCount=8192;clock.m_nTickCount=0;clock.m_nPattern=0;clock.m_nCurrentOrder=0;clock.m_nRow=0;clock.m_nCurrentRowsPerBeat=4;
        std::vector<std::unique_ptr<GraphControlPlan>> plans;std::vector<float> output(7000*2);GraphControlPlan *last=nullptr;
        const uint32_t fade=uint32_t(std::ceil(rate*.005)),latency=latent?32:0;
        auto shape=[](double t){return t*t*(3-2*t);};
        auto value=[](unsigned variant,float input){const float gain=.5f*.73f;return variant==0?float(input*gain+.1):variant==1?float((input+.1)*gain):float(input*gain);};
        for(uint32_t at=0;at<7000;){
          if(at==1000||at==3000||at==5000){
            auto next=native;
            if(at==1000)next.signal.library[0].audio={{101,105},{105,102},{102,103}};
            if(at==3000){std::erase_if(next.signal.library[0].nodes,[](const auto &n){return n.id==105;});next.signal.library[0].audio={{101,102},{102,103}};}
            if(at==5000){next.signal.library[0].nodes.push_back(offset);next.signal.library[0].audio={{101,102},{102,105},{105,103}};}
            auto prepared=std::make_unique<GraphControlPlan>();live.prepareParameters(next.signal,*prepared,last);
            check(prepared->runtimeOwners.size()==1&&prepared->runtimeOwners[0].structural,"Audio topology changes need an explicit prepared transition");
            if(last){const auto *before=last->processorOwners[0].state->find(102),*after=prepared->processorOwners[0].state->find(102);check(before&&after&&before->endpoint==after->endpoint,"Retained vendor endpoint was recreated during routing");}
            check((au?fixtureAUCreatedCount():created())==constructionCount,"Structural edit serialized/cloned the existing vendor");
            tracker_audit_begin();adoptGraphPlan(*prepared,at);uint64_t a,f,l;tracker_audit_end(&a,&f,&l);check(a+f+l==0,"Structural graph adoption allocates/frees/locks");
            last=prepared.get();plans.push_back(std::move(prepared));native=std::move(next);
            bool rejected=false;try{GraphControlPlan pending;live.prepareParameters(native.signal,pending,last);}catch(const std::runtime_error &){rejected=true;}check(rejected,"A fading topology must retain its audible predecessors until settled");
          }
          auto count=std::min(block,7000-at);for(auto boundary:{1000u,3000u,5000u})if(boundary>at)count=std::min(count,boundary-at);
          for(uint32_t i=0;i<count;++i)output[(at+i)*2]=output[(at+i)*2+1]=float(.4+.12*std::sin((at+i)*.003));
          clock.m_nBufferCount=8192-at;
          tracker_audit_begin();live.begin(clock,count,at,{120,double(at)*2/rate,0,4,true},64);const bool okay=live.process(0,output.data()+at*2,count,at,{});uint64_t a,f,l;tracker_audit_end(&a,&f,&l);check(okay&&a+f+l==0,"Structural dry bridge processing allocates/frees/locks or faults");
          for(uint32_t i=0;i<count;++i){const auto frame=at+i;if(frame<1000)continue;const auto change=frame<3000?1000u:frame<5000?3000u:5000u,elapsed=frame-change;
            const auto before=change==1000?0u:change==3000?1u:2u,after=change==1000?1u:change==3000?2u:0u;
            const float dry=float(.4+.12*std::sin((frame-latency)*.003));double expected;
            if(elapsed<fade){const auto wet=1-shape(double(elapsed)/fade);expected=value(before,dry)*wet+dry*(1-wet);}
            else if(elapsed<fade+latency)expected=dry;
            else if(elapsed<2*fade+latency){const auto wet=shape(double(elapsed-fade-latency)/fade);expected=value(after,dry)*wet+dry*(1-wet);}
            else expected=value(after,dry);
            if(std::abs(output[frame*2]-expected)>3e-7||std::abs(output[frame*2+1]-expected)>3e-7){std::cerr<<"structural frame "<<frame<<" rate "<<rate<<" block "<<block<<" latent "<<latent<<" actual "<<output[frame*2]<<" expected "<<expected<<'\n';throw std::runtime_error("Recipe dry transition differs from independent latency-aligned PCM reference");}
          }
          at+=count;
        }
        if(!au){check(observed()==7000&&clockErrors()==0,"Retained VST processes each frame once with uninterrupted transport through cyclic reorder");observe(false);}
        if(reference.empty())reference=output;else check(reference==output,"Structural transition depends on callback partition");
        // Preparation failure is audible-state neutral even after several
        // layouts replaced the initial processor lookup.
        auto invalid=native;invalid.signal.library[0].audio.push_back({105,102});bool rejected=false;
        try{GraphControlPlan candidate;live.prepareParameters(invalid.signal,candidate,last);}catch(const std::invalid_argument &){rejected=true;}check(rejected,"Cyclic recipe must reject before publication");
        std::array<float,512> tail;tail.fill(.4f);check(live.process(0,tail.data(),256,7000,{}),"Rejected recipe damaged the accepted plan");
      }
    }
  }
  delayed(false);hidden(1);setFixtureAUHiddenGain(1);
}
int main(int argc,char **argv){ trustFixtureArguments(argc, argv);@autoreleasepool{try{
  check(argc==2,"Pass VST3 fixture");auto plugins=NativePlugin::discoverVST3(argv[1]);
  recipeBypassEndpoint(plugins[0],argv[1]);
  sourcePreparationFailure(plugins[0]);
  newlyWatchedNotes(plugins[0]);compoundHeldNote(plugins[0]);
  auto fixtureAUs=registerFixtureAUs();structuralRecipes(plugins[0],argv[1],false);structuralRecipes(fixtureAUs.at(0),argv[1],true);
  discreteHosted(plugins[0],argv[1]);
  auto gain=definition(100,plugins[0],true);
  setFixtureAUStepped(true);scalarDiscreteHosted(fixtureAUs.at(0),7);setFixtureAUStepped(false);
  auto builtin=NativePlugin::builtins();auto gainer=std::find_if(builtin.begin(),builtin.end(),[](const auto &p){return p.classID=="resonance.gainer.v1";});check(gainer!=builtin.end(),"Gainer fixture available");scalarDiscreteHosted(*gainer,3);auto dc=std::find_if(builtin.begin(),builtin.end(),[](const auto &p){return p.classID=="resonance.dc-offset.v1";});check(dc!=builtin.end(),"DC fixture available");
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
  // Reactivating a stopped A after B changes the chain to B→A. The cut is
  // immediate; both processors and dry compensation keep their continuous history.
  timed.signal.commands={{3,1,100,0,0,SignalCommandKind::Start,.5,1},{3,1,200,0,1,SignalCommandKind::Start,.25,1},{3,1,100,65536,0,SignalCommandKind::Stop},{3,1,100,131072,0,SignalCommandKind::Start,.5,1}};
  std::array<float,1024> expectedReorder{};std::array<std::array<float,32>,2> wetHistory{},dryHistory{};
  for(uint32_t f=0;f<1024;++f){float value=float(1+f*.001);const std::array<int,2> order=f<512?std::array{0,1}:std::array{1,0};
    for(auto stage:order){bool active=stage==1||f<256||f>=512;const auto cursor=f%32;const auto wet=wetHistory[stage][cursor],dry=dryHistory[stage][cursor];wetHistory[stage][cursor]=active?value*float(stage==0?.5:.25):0;dryHistory[stage][cursor]=value;value=active?wet:dry;}expectedReorder[f]=value;}
  for(uint32_t block:{1u,17u,128u,4096u}){NativeSignalGraph graph(timed,48000,true);FixturePlayState clock;clock.m_nMusicSpeed=1;clock.m_nSamplesPerTick=256;clock.m_nTickCount=0;clock.m_nPattern=0;clock.m_nCurrentOrder=0;
    std::array<float,2048> output{};for(uint32_t i=0;i<1024;++i)output[i*2]=output[i*2+1]=float(1+i*.001);
    for(uint32_t pos=0;pos<1024;){clock.m_nRow=pos/256;clock.m_nBufferCount=256-pos%256;const auto count=std::min({block,256-pos%256,1024-pos});graph.begin(clock,count,pos,{120,double(pos)/24000,0,4,true});check(graph.process(0,output.data()+pos*2,count,pos,{}),"Reactivated graph render failed");pos+=count;}
    for(uint32_t i=0;i<1024;++i)check(std::abs(output[i*2]-expectedReorder[i])<2e-7,"Reactivated latent graph order/history differs from continuous reference");
    const auto active=graph.activity();check(active.size()==2&&active[0].graph==100&&active[0].order==2&&active[1].order==1,"Reactivation must append after the remaining active chain");
  }
  delayed(false);
  // Graph-owned envelopes reach the hosted VST3 at exact row-relative times.
  auto motion=gain;motion.nodes.back().kind=SignalNodeKind::Automation;
  motion.nodes.back().envelopes={{3,true,{{0,.1,AutomationCurve::Linear},{63,.8,AutomationCurve::Step},{79,.2,AutomationCurve::Scripted,CurveFormula("L")},{255,.9,AutomationCurve::Step}}}};
  timed.signal.library={motion};timed.signal.commands.clear();timed.signal.assignments={{1,100,1,1}};
  for(uint32_t block:{1u,17u,128u,256u}){
    NativeSignalGraph graph(timed,48000,true);FixturePlayState clock;clock.m_nMusicSpeed=1;clock.m_nSamplesPerTick=256;clock.m_nTickCount=0;clock.m_nPattern=0;clock.m_nCurrentOrder=0;clock.m_nRow=0;clock.m_nCurrentRowsPerBeat=4;
    std::array<float,512> output;output.fill(1);
    for(uint32_t pos=0;pos<256;){clock.m_nBufferCount=256-pos;const auto count=std::min(block,256-pos);
      tracker_audit_begin();graph.begin(clock,count,pos,{120,double(pos)/24000,0,4,true},1);const bool okay=graph.process(0,output.data()+pos*2,count,pos,{});uint64_t a,f,l;tracker_audit_end(&a,&f,&l);check(okay&&a+f+l==0,"Graph automation failed realtime audit");pos+=count;}
    for(uint32_t i=0;i<256;++i)check(std::abs(output[i*2]-automationValue(motion.nodes.back().envelopes[0].points,i,256,4))<2e-6,"Hosted graph automation onset, curve or step timing incorrect");
  }
  // Baselines are independent of opaque preset serialization and reach all
  // channel/role copies in one boundary, including currently inactive copies.
  auto baseGain=definition(300,plugins[0],false);baseGain.nodes[1].plugin.parameters[7]=.3;
  NativeSong controls;controls.patterns[0].id=3;controls.tracks[0].id=1;
  controls.mixer.buses={{1,2,MixerBusKind::Track,"A"},{4,2,MixerBusKind::Track,"B"},{2,0,MixerBusKind::Master,"Master"}};
  controls.signal.library={baseGain};controls.signal.assignments={{1,300,1,1},{4,300,1,1}};
  controls.signal.commands={{3,1,300,65536,0,SignalCommandKind::Start,1,1}};
  for(uint32_t block:{1u,17u,128u,256u}){
    NativeSignalGraph graph(controls,48000,true);auto changed=controls;
    changed.signal.library[0].nodes[1].plugin.parameters[7]=.7;
    check(sameSignalParameterLayout(controls.signal,changed.signal)&&!sameSignalProcessing(controls.signal,changed.signal),"Live control classification preserves musical-change detection");
    GraphControlPlan prepared;graph.prepareParameters(changed.signal,prepared);
    check(std::count_if(prepared.updates.begin(),prepared.updates.end(),[](const auto &v){return v.plugin&&v.node==302&&v.parameter==7;})==3,"Baseline edit covers ordinary copies and inactive persistent copy");
    FixturePlayState clock;clock.m_nMusicSpeed=1;clock.m_nSamplesPerTick=256;clock.m_nTickCount=0;clock.m_nPattern=0;clock.m_nCurrentOrder=0;
    for(uint32_t pos=0;pos<512;){const auto count=std::min({block,256-pos%256,512-pos});clock.m_nRow=pos/256;clock.m_nBufferCount=256-pos%256;
      std::array<float,512> a,b;a.fill(1);b.fill(1);
      tracker_audit_begin();
      if(pos==256)for(const auto &v:prepared.updates){if(v.runtime)v.runtime->parameterBase(v.node,v.parameter,v.value);else check(v.plugin->appliedParameter(v.parameter,v.value,pos,{}),"Live baseline was rejected");}
      graph.begin(clock,count,pos,{120,double(pos)/24000,0,4,true});const bool okay=graph.process(0,a.data(),count,pos,{})&&graph.process(1,b.data(),count,pos,{});
      uint64_t allocations,frees,locks;tracker_audit_end(&allocations,&frees,&locks);
      check(okay&&allocations+frees+locks==0,"Live baseline applies and renders with no realtime allocation/free/lock");
      for(uint32_t i=0;i<count*2;++i)check(std::abs(a[i]-(pos<256?.3:.49))<2e-6&&std::abs(b[i]-(pos<256?.3:.7))<2e-6,"Parameter edit misses a copy, leaks between roles or lands inside a block");pos+=count;
    }
    auto invalid=changed;invalid.signal.library[0].nodes[1].plugin.parameters[7]=2;bool rejected=false;try{GraphControlPlan p;graph.prepareParameters(invalid.signal,p);}catch(const std::invalid_argument &){rejected=true;}check(rejected,"Invalid live value must be rejected before publication");
  }
  // A base edit under active modulation changes the sum, not just a value
  // immediately overwritten by the next modulation quantum.
  auto modulated=gain;modulated.modulation[0].minimum=modulated.modulation[0].maximum=0;modulated.modulation[0].base=.2;
  controls.signal.library={modulated};controls.signal.assignments={{1,100,1,1}};controls.signal.commands.clear();
  NativeSignalGraph modulation(controls,48000,true);auto changed=controls;changed.signal.library[0].nodes[1].plugin.parameters[7]=.65;changed.signal.library[0].modulation[0].base=.65;
  GraphControlPlan modulationPlan;modulation.prepareParameters(changed.signal,modulationPlan);
  for(const auto &v:modulationPlan.updates){if(v.runtime)v.runtime->parameterBase(v.node,v.parameter,v.value);else check(v.plugin->appliedParameter(v.parameter,v.value,0,{}),"Modulated baseline application failed");}
  state.m_nRow=0;state.m_nBufferCount=256;std::array<float,256> modulatedAudio;modulatedAudio.fill(1);modulation.begin(state,128,0,{120,0,0,4,true});check(modulation.process(0,modulatedAudio.data(),128,0,{}),"Modulated baseline processing failed");
  for(auto v:modulatedAudio)check(std::abs(v-.65)<2e-6,"Modulation overwrote the edited baseline");
  // Replace an envelope and cable gain while retaining running processor
  // instances. Compiled scripts and their owners must survive later rendering.
  controls.signal.library={motion};controls.signal.assignments={{1,100,1,1}};
  NativeSignalGraph liveCurve(controls,48000,true);auto curveNext=controls;
  curveNext.signal.library[0].nodes.back().envelopes[0].points={{0,.6,AutomationCurve::Scripted,CurveFormula("start + t * 0.1")}};
  curveNext.signal.library[0].audio[0].gain=.5;
  check(sameSignalControlLayout(controls.signal,curveNext.signal)&&!sameSignalParameterLayout(controls.signal,curveNext.signal),"Envelope and cable controls are recognized separately from plugin baselines");
  GraphControlPlan curvePlan;liveCurve.prepareParameters(curveNext.signal,curvePlan);
  for(const auto &[runtime,control]:curvePlan.runtimes)runtime->controls(*control);
  for(const auto &v:curvePlan.updates){if(v.runtime)v.runtime->parameterBase(v.node,v.parameter,v.value);else v.plugin->appliedParameter(v.parameter,v.value,0,{});}
  state.m_nRow=0;state.m_nBufferCount=256;state.m_nCurrentRowsPerBeat=4;std::array<float,512> curveAudio;curveAudio.fill(1);
  tracker_audit_begin();liveCurve.begin(state,256,0,{120,0,0,4,true},1);const auto curveOkay=liveCurve.process(0,curveAudio.data(),256,0,{});uint64_t ca,cf,cl;tracker_audit_end(&ca,&cf,&cl);
  check(curveOkay&&ca+cf+cl==0,"Updated scripted curve renders without realtime allocation/free/lock");
  for(uint32_t f=0;f<256;++f)check(std::abs(curveAudio[f*2]-.5*automationValue(curveNext.signal.library[0].nodes.back().envelopes[0].points,f,256,4))<2e-6,"Edited scripted curve or cable gain did not take effect immediately");
  dlclose(bundle);
  std::cout<<"PASS hosted graph audio: row→persistent→ordinary ordering, half-row switching, update-in-place, expiry/stop/clear, independent target copies, bit-exact block partitions, delayed wet/dry cut and tails, inactive transport continuity, and zero realtime allocations/frees/locks\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}}
