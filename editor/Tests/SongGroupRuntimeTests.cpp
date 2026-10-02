#include "editor/SongGroupRuntime.hpp"
#include "editor/SongModulation.hpp"
#include <cmath>
#include <iostream>
#ifdef TRACKER_SANITIZER
static void tracker_audit_begin(){}static void tracker_audit_end(uint64_t *a,uint64_t *f,uint64_t *l){*a=*f=*l=0;}
#else
extern "C" void tracker_audit_begin();extern "C" void tracker_audit_end(uint64_t *,uint64_t *,uint64_t *);
#endif
using namespace Tracker;
static void check(bool okay,const char *why){if(!okay)throw std::runtime_error(why);}
static std::vector<float> render(uint32_t rate,uint32_t block,bool inner,bool outer) {
  MixerGraph graph;graph.buses={{1,10,MixerBusKind::Track,"Track"},{2,10,MixerBusKind::Track,"Other"},{10,0,MixerBusKind::Master,"Main"}};graph.buses[0].inserts={"a","b","c"};
  const std::vector<MixerProcessorInfo> catalog{{"a",2,0},{"b",3,0},{"c",5,0}};const auto plan=compileMixer(graph,{1,2},catalog,rate);
  SignalGraph signal;SignalSongGroup first;first.id=100;first.parent=101;first.name="Inner";first.nodes={"plugin:a","plugin:b"};first.bypass=inner;
  SignalSongGroup second;second.id=101;second.name="Outer";second.nodes={"plugin:c"};second.bypass=outer;signal.groups={first,second};
  auto groups=std::make_unique<SongGroupRuntime>(signal,graph,plan,catalog,rate);auto runtime=std::make_unique<MixerRuntime>(graph,plan,rate);
  groups->runtime(runtime.get());runtime->routeTransform([](void *p,MixerRuntime::RouteKind k,size_t i,float *audio,uint32_t n,uint64_t at)noexcept{static_cast<SongGroupRuntime *>(p)->route(k,i,audio,n,at);},groups.get());
  struct State {std::array<std::vector<float>,3> rings;std::array<size_t,3> cursors{};std::array<uint64_t,3> through{};bool failed=false;} state;
  for(size_t i=0;i<3;++i)state.rings[i].resize(catalog[i].latency*2);
  const auto process=[](void *p,size_t index,float *audio,uint32_t count,uint64_t frame)noexcept{auto &s=*static_cast<State *>(p);if(s.through[index]!=frame)s.failed=true;s.through[index]=frame+count;
    for(uint32_t i=0;i<count*2;++i){auto &position=s.cursors[index];std::swap(audio[i],s.rings[index][position]);if(++position==s.rings[index].size())position=0;audio[i]*=index==0?2.f:index==1?3.f:5.f;}return true;};
  std::array<float,4096> left{},right{};std::vector<float> output(6000*2);
  for(uint32_t at=0;at<6000;){const auto count=std::min(block,6000-at);uint64_t a,f,l;tracker_audit_begin();groups->begin(count,at);runtime->begin(count,at);
    for(auto bus:plan.order){for(uint32_t i=0;i<count;++i)left[i]=right[i]=bus==0?float(.1+.02*std::sin((at+i)*.003)):bus==1?.03f:0.f;
      const auto *audio=runtime->process(bus,left.data(),right.data(),process,&state);if(bus==plan.master)std::copy_n(audio,count*2,output.data()+at*2);}
    runtime->complete();tracker_audit_end(&a,&f,&l);check(!runtime->failed()&&!groups->failed()&&!state.failed&&a+f+l==0,"Song group wrapper violated realtime safety or route order");at+=count;
  }
  for(uint32_t frame=0;frame<6000;++frame){const float source=frame<10?0.f:float(.1+.02*std::sin((frame-10)*.003));const float expected=frame<10?0:source*(outer?1.f:inner?5.f:30.f)+.03f;
    check(std::abs(output[frame*2]-expected)<4e-7,"Nested group dry boundary is not independently latency aligned");}
  check(state.through==std::array<uint64_t,3>{6000,6000,6000},"A bypassed group stopped its members");return output;
}
static void controlBoundary() {
  MixerGraph mixer;mixer.buses={{1,10,MixerBusKind::Track,"Track"},{10,0,MixerBusKind::Master,"Main"}};mixer.buses[0].inserts={"a"};
  SignalGraph graph;SignalSongSource source;source.node.id=50;source.node.kind=SignalNodeKind::Amount;source.node.name="Amount";graph.songSources={source};
  graph.songModulation={{50,"a",7,.2,.4,true,false},{50,"b",7,.2,.4,true,false}};
  SignalSongGroup group;group.id=100;group.name="Group";group.nodes={"source:n50","plugin:a"};group.bypass=true;graph.groups={group};
  const std::vector<MixerProcessorInfo> catalog{{"a",0,0},{"b",0,0}};auto groups=std::make_unique<SongGroupRuntime>(graph,mixer,compileMixer(mixer,{1},catalog,48000),catalog,48000);
  const std::array<SongModulationParameter,2> parameters{{{"a",7},{"b",7}}};SongModulationRuntime modulation(graph,parameters,48000);
  modulation.contributionGain([](void *p,uint64_t source,const std::string &target,uint64_t frame)noexcept{return static_cast<SongGroupRuntime *>(p)->modulation(source,target,frame);},groups.get());
  modulation.amount(50,.5);uint64_t a,f,l;tracker_audit_begin();groups->begin(128,0);const bool rendered=modulation.renderSource(0,128,0,{});double inside=0,outside=0;const bool okay=modulation.overlay(0,127,.1,inside)&&modulation.overlay(1,127,.1,outside);tracker_audit_end(&a,&f,&l);
  check(rendered&&okay&&a+f+l==0&&std::abs(inside-.4)<1e-12&&outside==.1,"Group bypass must suppress the complete external contribution while internal modulation stays active");
}
int main(){try{for(auto rate:{44100u,48000u,96000u})for(bool inner:{false,true})for(bool outer:{false,true}){const auto expected=render(rate,17,inner,outer);check(expected==render(rate,128,inner,outer)&&expected==render(rate,4096,inner,outer),"Song group boundaries depend on callback partition");}controlBoundary();std::cout<<"PASS song group boundary bypass, nested dry alignment, internal clocks and modulation isolation, partition and realtime audit\n";}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
