#include "editor/MixerTransition.hpp"
#include <cmath>
#include <iostream>
#ifdef TRACKER_SANITIZER
static void tracker_audit_begin(){}static void tracker_audit_end(uint64_t *a,uint64_t *f,uint64_t *l){*a=*f=*l=0;}
#else
extern "C" void tracker_audit_begin();extern "C" void tracker_audit_end(uint64_t *,uint64_t *,uint64_t *);
#endif
using namespace Tracker;
static void check(bool value,const char *why){if(!value)throw std::runtime_error(why);}
static MixerGraph graph(bool cut=false){MixerGraph result;result.buses={{1,10,MixerBusKind::Track,"One"},{2,10,MixerBusKind::Track,"Two"},{10,0,MixerBusKind::Master,"Main"}};result.buses[0].inserts={"a","b"};result.buses[1].inserts={"c","d"};result.pluginConnections={{"a",0,"d",0,-12.041199826559248,true},{"c",0,"b",0,-20,true}};if(cut)result.disconnectedMainInputs={"b"};return result;}
static std::vector<MixerProcessorInfo> catalog(){return {{"a",2,0},{"b",3,0},{"c",7,0},{"d",5,0}};}
struct Rack {std::array<std::vector<float>,4> rings;std::array<size_t,4> cursor{};std::array<uint64_t,4> through{};bool failed=false;
  Rack(){auto data=catalog();for(size_t i=0;i<4;++i)rings[i].resize(data[i].latency*2);}
  static bool process(void *opaque,MixerRuntime &,size_t p,float *audio,uint32_t count,uint64_t frame)noexcept{auto &self=*static_cast<Rack *>(opaque);if(self.through[p]!=frame)self.failed=true;self.through[p]=frame+count;const float gain=p==0?2:p==1?3:p==2?5:7;for(uint32_t i=0;i<count*2;++i){auto value=audio[i]*gain;std::swap(value,self.rings[p][self.cursor[p]]);if(++self.cursor[p]==self.rings[p].size())self.cursor[p]=0;audio[i]=value;}return true;}
};
static std::vector<float> render(uint32_t rate,uint32_t block,bool cut){auto g=graph(cut);auto info=catalog();auto p=std::make_unique<MixerTransition::Plan>();p->catalog=info;p->runtime=std::make_unique<MixerRuntime>(g,compileMixer(g,{1,2},info,rate),rate);check(p->runtime->plan().latency==12,"Direct fan-in PDC must align both bus outputs independently");auto rack=std::make_shared<Rack>();p->processors=rack;p->process=Rack::process;MixerTransition transition(std::move(p),{1,2,10},{1,2},rate);
  std::array<float,4096> a{},b{};std::array<MixerTransition::DirectInput,3> inputs{{{a.data(),a.data()},{b.data(),b.data()},{}}};std::vector<float> result(5000*2);
  for(uint32_t frame=0;frame<5000;){const auto count=std::min(block,5000-frame);for(uint32_t i=0;i<count;++i){a[i]=float(.03*std::sin((frame+i)*.005));b[i]=float(.02*std::cos((frame+i)*.011));}uint64_t alloc,free,locks;tracker_audit_begin();const bool begun=transition.begin(count,frame);const auto *output=begun?transition.render(inputs):nullptr;const bool okay=output&&!transition.failed();if(okay)std::copy_n(output,count*2,result.data()+frame*2);tracker_audit_end(&alloc,&free,&locks);check(okay&&alloc+free+locks==0&&!rack->failed,"Interleaved direct plugin routing broke callback safety or vendor clock");frame+=count;}
  for(uint32_t frame=0;frame<5000;++frame){const double expected=frame<12?0:float(.03*std::sin((frame-12)*.005))*(cut?3.5:9.5)+float(.02*std::cos((frame-12)*.011))*36.5;check(std::abs(result[frame*2]-expected)<4e-7,"Direct cables/main PDC must match independently derived delayed PCM");}
  check(rack->through==std::array<uint64_t,4>{5000,5000,5000,5000},"A cut upstream processor stopped clocking");return result;
}
static void scheduledSource(uint32_t rate,uint32_t block) {
  MixerGraph g;g.buses={{1,10,MixerBusKind::Track,"Input"},{10,0,MixerBusKind::Master,"Main"}};g.buses[0].inserts={"effect"};
  std::vector<MixerProcessorInfo> info{{"effect",0,0},{"instrument",0,0,true,false,1,1,1,0,true}};
  struct State{std::array<uint64_t,2> through{};bool okay=true;};auto state=std::make_shared<State>();
  auto p=std::make_unique<MixerTransition::Plan>();p->catalog=info;p->runtime=std::make_unique<MixerRuntime>(g,compileMixer(g,{1},info,rate),rate);p->processors=state;
  p->process=[](void *opaque,MixerRuntime &,size_t slot,float *samples,uint32_t frames,uint64_t at)noexcept{auto &s=*static_cast<State *>(opaque);s.okay&=s.through[slot]==at;s.through[slot]=at+frames;for(uint32_t i=0;i<frames*2;++i)samples[i]=slot? .1f+samples[i]*2:samples[i]*.5f;return s.okay;};
  MixerTransition transition(std::move(p),{1,10},{1},rate);std::array<float,4096> input{};std::array<MixerTransition::DirectInput,2> inputs{{{input.data(),input.data()},{}}};
  for(uint32_t frame=0;frame<10000;){if(frame==1000){g.pluginConnections={{"effect",0,"instrument",0,0,true}};auto next=transition.prepareRetained(g,info);next->processors=state;next->process=transition.controlPlan().process;transition.prepareDependencies(*next);check(transition.publish(next),"Instrument audio route publishes");}
    uint32_t count=std::min(block,10000-frame);if(frame<1000)count=std::min(count,1000-frame);count=transition.limitFrames(count,frame);for(uint32_t i=0;i<count;++i)input[i]=float(.02*std::sin((frame+i)*.01));
    uint64_t a,f,l;tracker_audit_begin();const bool begun=transition.begin(count,frame);const auto *out=begun?transition.render(inputs):nullptr;tracker_audit_end(&a,&f,&l);check(out&&state->okay&&!transition.failed()&&a+f+l==0,"Instrument dependency/dry handoff renders exactly once without callback ownership work");
    if(frame>=1000+uint32_t(rate*.031)||frame<1000)for(uint32_t i=0;i<count;++i)check(std::abs(out[i*2]-(.1+input[i]*(frame<1000?.5:1.5)))<1e-7,"Scheduled instrument main input PCM differs from independent arithmetic");frame+=count;
  }
  check(state->through==std::array<uint64_t,2>{10000,10000},"Scheduled instrument or upstream effect stopped advancing");
}

// Independent two-source/two-destination matrix. Every source output fans out,
// and each destination sums two main cables plus two auxiliary cables. Distinct
// vendor delays require PDC before either sum; clocks detect duplicate renders.
static std::vector<float> manyToMany(uint32_t rate,uint32_t block) {
  MixerGraph g;
  g.buses={{1,0,MixerBusKind::Track,"Source A"},{2,0,MixerBusKind::Track,"Source B"},
    {3,5,MixerBusKind::Return,"Destination C"},{4,5,MixerBusKind::Return,"Destination D"},{5,0,MixerBusKind::Master,"Main"}};
  for(size_t i=0;i<4;++i)g.buses[i].inserts={std::string(1,char('a'+i))};
  auto add=[&](const char *from,uint32_t output,const char *to,uint32_t input,double gain){g.pluginConnections.push_back({from,output,to,input,20*std::log10(gain),true});};
  add("a",0,"c",0,1);add("b",0,"c",0,.5);add("a",1,"c",1,2);add("b",1,"c",1,1);
  add("a",0,"d",0,.25);add("b",0,"d",0,1);add("a",1,"d",1,1);add("b",1,"d",1,.5);
  std::vector<MixerProcessorInfo> info{{"a",2,0,false,false,2,3,1},{"b",7,0,false,false,2,3,1},
    {"c",3,0,false,false,1,1,3},{"d",5,0,false,false,1,1,3}};
  struct MatrixRack {
    std::array<std::vector<float>,4> delay;
    std::array<size_t,4> cursor{};
    std::array<uint64_t,4> through{},calls{};
    std::array<float,8192> aux{};
    bool valid=true;
    MatrixRack(){for(size_t i=0;i<4;++i)delay[i].resize(std::array{2,7,3,5}[i]*2);}
    static bool process(void *opaque,MixerRuntime &runtime,size_t slot,float *pcm,uint32_t count,uint64_t frame)noexcept {
      auto &self=*static_cast<MatrixRack *>(opaque);self.valid&=self.through[slot]==frame;
      self.through[slot]=frame+count;++self.calls[slot];
      const auto inputs=runtime.inputs(slot);const float *side=nullptr;
      for(const auto &input:inputs)if(input.bus==1)side=input.samples;
      if(slot>=2)self.valid&=side!=nullptr;
      for(uint32_t i=0;i<count*2;++i){
        float value=slot<2?pcm[i]*float(slot+2):(pcm[i]+(side?side[i]:0)*float(slot==2?4:-2))*float(slot==2?5:7);
        std::swap(value,self.delay[slot][self.cursor[slot]]);
        if(++self.cursor[slot]==self.delay[slot].size())self.cursor[slot]=0;
        pcm[i]=value;if(slot<2)self.aux[i]=value*.25f;
      }
      if(slot<2)runtime.instrument(slot,1,self.aux.data());
      return self.valid;
    }
  };
  auto state=std::make_shared<MatrixRack>();auto plan=std::make_unique<MixerTransition::Plan>();plan->catalog=info;
  plan->runtime=std::make_unique<MixerRuntime>(g,compileMixer(g,{1,2},info,rate),rate);
  check(plan->runtime->plan().latency==12&&plan->runtime->plan().pluginConnections.size()==8,"All eight many-to-many main/aux contributions must survive compilation with aligned latency");
  plan->processors=state;plan->process=MatrixRack::process;
  MixerTransition transition(std::move(plan),{1,2,3,4,5},{1,2},rate);
  std::array<std::array<float,4096>,4> audio{};
  std::array<MixerTransition::DirectInput,5> inputs{{{audio[0].data(),audio[1].data()},{audio[2].data(),audio[3].data()},{},{},{}}};
  auto source=[](uint32_t frame,unsigned channel,unsigned source){return float((source?.02:.03)*std::sin((frame+7*channel)*(source?.011:.005)+channel*.7+source));};
  std::vector<float> result(5000*2);uint64_t chunks=0;
  for(uint32_t frame=0;frame<5000;){const auto count=std::min(block,5000-frame);
    for(uint32_t i=0;i<count;++i)for(unsigned channel=0;channel<2;++channel){audio[channel][i]=source(frame+i,channel,0);audio[channel+2][i]=source(frame+i,channel,1);}
    uint64_t allocations,frees,locks;tracker_audit_begin();const bool begun=transition.begin(count,frame);const auto *out=begun?transition.render(inputs):nullptr;
    if(out)std::copy_n(out,count*2,result.data()+frame*2);tracker_audit_end(&allocations,&frees,&locks);
    check(out&&!transition.failed()&&state->valid&&allocations+frees+locks==0,"Many-to-many matrix must allocate/free/lock nothing and advance each processor once");
    frame+=count;++chunks;
  }
  for(uint32_t frame=0;frame<5000;++frame)for(unsigned channel=0;channel<2;++channel){
    // C=5*((2A+1.5B)+4*(A+.75B)); D=7*((.5A+3B)-2*(.5A+.375B)).
    const double expected=frame<12?0:26.5*source(frame-12,channel,0)+38.25*source(frame-12,channel,1);
    check(std::abs(result[frame*2+channel]-expected)<5e-7,"Many-to-many main/aux sums differ from independent stereo PCM arithmetic");
  }
  for(size_t p=0;p<4;++p)check(state->through[p]==5000&&state->calls[p]==chunks,"Output fan-out must duplicate PCM without processing a vendor more than once per chunk");
  return result;
}
int main(){try{for(auto rate:{44100u,48000u,96000u}){const auto reference=manyToMany(rate,1);for(auto block:{17u,128u,4096u})check(reference==manyToMany(rate,block),"Many-to-many matrix changed with callback partition");}for(auto rate:{44100u,48000u,96000u})for(auto block:{17u,128u,4096u})scheduledSource(rate,block);for(auto rate:{44100u,48000u,96000u})for(bool cut:{false,true}){const auto expected=render(rate,17,cut);check(expected==render(rate,128,cut)&&expected==render(rate,4096,cut),"Processor-DAG output depends on callback partition");}auto bad=graph();bad.pluginConnections.push_back({"d",0,"a",0,0,false});bool rejected=false;try{compileMixer(bad,{1,2},catalog(),48000);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Disabled direct cable must not conceal a processor feedback cycle");std::cout<<"PASS many-to-many main/aux matrix with single vendor advancement, direct processor routing, interleaved buses, exact cuts, independent PDC, disabled-cycle rejection and realtime audit\n";}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
