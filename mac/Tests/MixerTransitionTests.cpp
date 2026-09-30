#include "editor/MixerRuntime.hpp"
#include "editor/hosted/RenderOnce.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace Tracker;
#ifdef TRACKER_SANITIZER
static void tracker_audit_begin() {}
static void tracker_audit_end(uint64_t *a,uint64_t *f,uint64_t *l){*a=*f=*l=0;}
#else
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *,uint64_t *,uint64_t *);
#endif
static void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
namespace {
// Stateful feedback, distinct auxiliary output and a delay make duplicated
// rendering and empty PDC histories observable in the resulting PCM.
struct Processor {
  std::vector<float> delay;
  size_t cursor=0;
  float memory[2]{};
  uint64_t rendered=0;
  std::array<float,8192> auxiliary{};
  explicit Processor(uint32_t frames):delay(frames*2) {}
  bool process(float *audio,uint32_t frames,uint64_t,std::span<const PluginAudioInput> inputs) noexcept {
    rendered+=frames;
    for(uint32_t i=0;i<frames*2;++i) {
      float value=audio[i];for(const auto &input:inputs)if(input.samples)value+=input.samples[i]*.125f;
      memory[i%2]=value*.75f+memory[i%2]*.125f;value=memory[i%2];
      if(!delay.empty()){std::swap(value,delay[cursor]);if(++cursor==delay.size())cursor=0;}
      audio[i]=value;auxiliary[i]=value*.25f;
    }
    return true;
  }
};
std::vector<MixerProcessorInfo> catalog() {
  return {{"tone",32,0,false,false,2,3},{"compressor",16,0,false,false,1,1,2},
          {"echo",64,0},{"limiter",7,0},{"synth",8,0,true},{"slow",96,0}};
}
MixerGraph song() {
  MixerGraph g;g.buses={{1,10,MixerBusKind::Track,"One"},{2,10,MixerBusKind::Track,"Two"},
    {3,10,MixerBusKind::Track,"Three"},{10,20,MixerBusKind::Group,"Group"},
    {11,20,MixerBusKind::Return,"Return"},{20,0,MixerBusKind::Master,"Master"}};
  g.buses[0].inserts={"tone"};g.buses[1].inserts={"compressor"};g.buses[2].inserts={"slow"};
  g.buses[4].inserts={"echo"};g.buses[5].inserts={"limiter"};g.buses[0].sends={{11,-12,true}};
  g.instruments={{"synth",2,0},{"tone",11,1}};
  g.sidechains={{1,"compressor",1},{3,"compressor",1,-6}};return g;
}
struct Harness {
  std::unique_ptr<MixerRuntime> mixer;
  std::vector<std::shared_ptr<RenderOnce<Processor>>> processors;
  std::array<float,4096> left{},right{};
  std::array<float,8192> instrument{},output{};
  Harness(const MixerGraph &graph,uint32_t rate) {
    const auto p=catalog();mixer=std::make_unique<MixerRuntime>(graph,compileMixer(graph,{1,2,3},p,rate),rate);
    for(const auto &entry:p)processors.push_back(std::make_shared<RenderOnce<Processor>>(std::make_shared<Processor>(entry.latency)));
  }
  static bool process(void *opaque,size_t slot,float *audio,uint32_t frames,uint64_t position) noexcept {
    auto &self=*static_cast<Harness *>(opaque);
    const auto okay=self.processors[slot]->process(audio,frames,position,self.mixer->inputs(slot));
    if(slot==0)self.mixer->instrument(slot,1,self.processors[slot]->processor().auxiliary.data());
    return okay;
  }
  void render(uint32_t frames,uint64_t position) noexcept {
    for(uint32_t i=0;i<frames;++i){instrument[i*2]=float(.1*std::sin((position+i)*.017));instrument[i*2+1]=instrument[i*2]*-.7f;}
    processors[4]->process(instrument.data(),frames,position);mixer->instrument(4,0,instrument.data());
    for(auto bus:mixer->plan().order) {
      const auto id=mixer->graph().buses[bus].id;
      for(uint32_t i=0;i<frames;++i) {
        left[i]=float(.05*std::sin((position+i)*.013*id));right[i]=left[i]*.75f;
        if((position+i)%523==0)left[i]+=.125f;
      }
      const auto *result=mixer->process(bus,left.data(),right.data(),process,this);
      if(bus==mixer->plan().master && result)std::copy_n(result,frames*2,output.data());
    }
    mixer->complete();
  }
};
void retainedHistories(uint32_t rate,uint32_t block) {
  const auto graph=song();const auto p=catalog();const auto plan=compileMixer(graph,{1,2,3},p,rate);
  const auto reuse=mixerTransitionReuse(graph,plan,p,graph,plan,p);
  check(std::any_of(plan.sidechains.begin(),plan.sidechains.end(),[](const auto &s){return s.delay!=0;}),"Fixture has compensated sidechain history");
  check(plan.instruments[0].delay>0,"Fixture has compensated held-instrument history");
  auto reference=std::make_unique<Harness>(graph,rate),outgoing=std::make_unique<Harness>(graph,rate);
  std::unique_ptr<Harness> incoming;
  for(uint64_t position=0;position<12000;) {
    uint64_t boundary=12000;
    for(auto event:{2500u,5000u,5033u,5601u})if(event>position)boundary=std::min<uint64_t>(boundary,event);
    const auto frames=uint32_t(std::min<uint64_t>(block,boundary-position));
    if(position==2500) {
      incoming=std::make_unique<Harness>(graph,rate);
      incoming->mixer->retainHistory(*outgoing->mixer,reuse);
      check(!incoming->mixer->activateHistory(),"A prepared plan cannot adopt controls outside an active source chunk");
      incoming->processors=outgoing->processors;
      check(incoming->mixer->historyStorageBytes()>32768,"Retained-history cache storage is accounted for");
    }
    if(position==5000) {
      std::vector<MixerControls> controls(graph.buses.size());
      for(size_t i=0;i<controls.size();++i){controls[i].preGainDB=-1;controls[i].prePan=.2;controls[i].gainDB=-3;controls[i].pan=-.3;controls[i].width=.7;}
      check(reference->mixer->controls(controls)&&outgoing->mixer->controls(controls),"Queue a complete smoothed control gesture before plan activation");
    }
    if(position==5601)outgoing.reset(); // Retire only on this control thread.
    uint64_t allocations,frees,locks;tracker_audit_begin();
    reference->mixer->begin(frames,position);
    if(outgoing)outgoing->mixer->begin(frames,position);
    const bool activated=position!=5033 || incoming->mixer->activateHistory();
    if(position>=5033)incoming->mixer->begin(frames,position);
    reference->render(frames,position);
    // Both evaluation orders must work: a shared dependency is clocked by the
    // first plan that reaches it, never assumed to be the outgoing plan.
    if(position>=5033 && position%2)incoming->render(frames,position);
    if(outgoing)outgoing->render(frames,position);
    if(position>=5033 && !(position%2))incoming->render(frames,position);
    tracker_audit_end(&allocations,&frees,&locks);
    check(activated && allocations+frees+locks==0,"History adoption/rendering allocate, free and lock nothing");
    check(!reference->mixer->failed() && (!outgoing || !outgoing->mixer->failed()) && (position<5033 || !incoming->mixer->failed()),"Retained plans use matching chunk boundaries");
    if(outgoing)check(std::equal(reference->output.begin(),reference->output.begin()+frames*2,outgoing->output.begin()),"Outgoing reference remains bit-exact during a retained transition");
    if(position>=5033)check(std::equal(reference->output.begin(),reference->output.begin()+frames*2,incoming->output.begin()),"Incoming audio preserves exact PDC, sidechain, auxiliary and in-flight fader history");
    position+=frames;
  }
  for(const auto &processor:incoming->processors)check(processor->processor().rendered==12000,"Every retained DSP advances exactly once, including after outgoing retirement");
}
void concurrentPreparation() {
  const auto graph=song();const auto p=catalog();const auto plan=compileMixer(graph,{1,2,3},p,48000);
  const auto reuse=mixerTransitionReuse(graph,plan,p,graph,plan,p);
  auto source=std::make_unique<Harness>(graph,48000);
  std::atomic<bool> finished=false;
  std::thread prepare([&] {
    for(int i=0;i<100;++i) {
      auto next=std::make_unique<MixerRuntime>(graph,plan,48000);
      next->retainHistory(*source->mixer,reuse);
      // Cancelled preparation drops only its own control-thread ownership.
    }
    finished.store(true,std::memory_order_release);
  });
  for(uint64_t position=0;!finished.load(std::memory_order_acquire);position+=17) {
    source->mixer->begin(17,position);source->render(17,position);
  }
  prepare.join();check(!source->mixer->failed(),"Concurrent preparation/cancellation cannot read or reset active history");
  auto next=std::make_unique<MixerRuntime>(graph,plan,48000);
  auto invalid=reuse;invalid.direct[0]=source->mixer->plan().nodes.size();bool rejected=false;
  try{next->retainHistory(*source->mixer,invalid);}catch(const std::invalid_argument &){rejected=true;}
  check(rejected,"Malformed mappings reject before taking history ownership");
  next->retainHistory(*source->mixer,reuse);
  const auto position=source->mixer->through();source->mixer->begin(17,position);
  check(next->activateHistory(),"A failed preparation leaves the destination reusable");
  next->begin(17,position);rejected=false;
  try{next->retainHistory(*source->mixer,reuse);}catch(const std::invalid_argument &){rejected=true;}
  check(rejected,"Published runtimes cannot replace delay ownership from the control thread");
}
}
int main() {
  try {
    for(auto rate:{44100u,48000u,96000u})for(auto block:{1u,17u,512u,4096u})retainedHistories(rate,block);
    concurrentPreparation();
    std::cout<<"PASS retained mixer transitions: exact delay/fader/sidechain/auxiliary history, one DSP clock, realtime audit\n";return 0;
  } catch(const std::exception &error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
