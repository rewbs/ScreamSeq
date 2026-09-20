#include "editor/MixerRuntime.hpp"
#include <iostream>
#include <cmath>
using namespace Tracker;
#include "GraphRealtimeAudit.hpp"
static void check(bool b,const char *message){if(!b)throw std::runtime_error(message);}
struct Fixture {
  MixerRuntime *mixer=nullptr;
  std::array<float,6> a{};std::array<float,10> b{};std::array<float,8192> auxiliary{};size_t pa=0,pb=0;
  static bool process(void *ctx,size_t index,float *buffer,uint32_t count,uint64_t)noexcept{
    auto &f=*static_cast<Fixture *>(ctx);
    for(uint32_t i=0;i<count*2;++i){if(index==0){std::swap(buffer[i],f.a[f.pa]);if(++f.pa==f.a.size())f.pa=0;f.auxiliary[i]=buffer[i]*2;}else{std::swap(buffer[i],f.b[f.pb]);if(++f.pb==f.b.size())f.pb=0;}}
    if(index==0)f.mixer->instrument(0,1,f.auxiliary.data());return true;
  }
};
int main(){try{
  MixerGraph g;g.buses={{1,3,MixerBusKind::Track,"Source"},{2,3,MixerBusKind::Return,"Aux return"},{3,0,MixerBusKind::Master,"Master"}};g.buses[0].inserts={"multi"};g.buses[1].inserts={"return"};g.instruments={{"multi",2,1}};
  std::vector<MixerProcessorInfo> info={{"multi",3,0,false,false,2,3},{"return",5,0}};
  auto plan=compileMixer(g,{1},info,48000);check(plan.latency==8&&plan.instruments[0].owner==0&&plan.instruments[0].delay==0,"Aux effect topology and compensation wrong");
  for(bool muted:{false,true})for(uint32_t block:{1u,17u,128u,4096u}){
    g.buses[0].mute=muted;auto mixer=std::make_unique<MixerRuntime>(g,compileMixer(g,{1},info,48000),48000);Fixture f;f.mixer=mixer.get();std::array<float,4096> l{},r{};std::array<float,256> output{};
    for(uint32_t at=0;at<128;){const auto count=std::min(block,128-at);l.fill(0);r.fill(0);if(!at)l[0]=r[0]=1;
      tracker_audit_begin();mixer->begin(count,at);mixer->process(0,l.data(),r.data(),Fixture::process,&f);mixer->process(1,nullptr,nullptr,Fixture::process,&f);const auto *result=mixer->process(2,nullptr,nullptr,Fixture::process,&f);std::copy_n(result,count*2,output.data()+at*2);mixer->complete();uint64_t a,b,c;tracker_audit_end(&a,&b,&c);check(!mixer->failed()&&a+b+c==0,"Aux output callback must be allocation/lock-free");at+=count;}
    for(uint32_t frame=0;frame<128;++frame)check(output[frame*2]==(!muted&&frame==8?3.f:0.f)&&output[frame*2+1]==output[frame*2],"Aux and main impulse alignment or owner mute differs from reference");
  }
  auto rejects=[&](MixerGraph graph){try{compileMixer(graph,{1},info,48000);}catch(const std::invalid_argument &){return;}throw std::runtime_error("Unsafe auxiliary topology accepted");};
  auto bad=g;bad.instruments[0].target=1;rejects(bad);bad=g;bad.instruments[0].output=0;rejects(bad);bad=g;bad.buses[0].inserts.clear();rejects(bad);bad=g;bad.sidechains={{2,"multi",1,0,false,false}};rejects(bad);
  std::cout<<"PASS effect auxiliary outputs: separate return processing, exact parallel PDC, source mute, callback partitions, zero realtime allocations/locks, ownership and cycle rejection\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
