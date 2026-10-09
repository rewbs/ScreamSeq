#include "editor/hosted/PluginAudioLayout.hpp"
#include <iostream>
using namespace Tracker;
static void check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
int main(){try{
  const std::vector<PluginPhysicalBus> physical{{5,"Main"},{3,"Side"}};
  PluginAudioBufferPlan plan(physical,physical,{},{});
  check(plan.buses().size()==10&&plan.preparedInputs()==30&&plan.preparedOutputs()==30,"All five logical ports prepared in each direction");
  for(bool input:{true,false})for(uint32_t index=0;index<5;++index){const auto &b=*std::find_if(plan.buses().begin(),plan.buses().end(),[&](const auto &b){return b.input==input&&b.index==index;});
    check(b.physicalBus==(index==1||index==4?1u:0u),"First-pair physical IDs preserved");
    check(b.firstChannel==(index==2||index==4?2u:index==3?4u:0u),"Additional pairs mapped exactly");
    check(b.channels==(index==3||index==4?1u:2u)&&b.active==(index==0),"Odd final channel is explicit mono; membership remains requested");
  }
  std::array<std::array<float,8192>,5> signals{};std::array<const float *,64> wires{};
  for(size_t port=0;port<5;++port){for(size_t i=0;i<4096;++i){signals[port][i*2]=float(port+1);signals[port][i*2+1]=float(port+2);}if(port)wires[port]=signals[port].data();}
  plan.gather(signals[0].data(),wires.data(),19,17);
  for(size_t bus=0;bus<2;++bus)for(size_t channel=0;channel<plan.inputs()[bus].channels.size();++channel)std::copy_n(plan.inputs()[bus].channels[channel],17,plan.outputs()[bus].channels[channel]);
  check(plan.scatter(signals[0].data(),17),"Finite physical output accepted");
  for(uint32_t port=1;port<5;++port){const auto *out=plan.output(port);const float l=port>=3?port+1.5f:port+1.f,r=port>=3?l:port+2.f;check(out&&out[0]==l&&out[1]==r,"Every pair and odd mono round trips without hidden summation");}
  plan.gather(signals[0].data(),nullptr,0,17);for(const auto &b:plan.inputs())for(auto *p:b.channels)for(unsigned i=0;i<17;++i)if(p!=plan.inputs()[0].channels[0]&&p!=plan.inputs()[0].channels[1])check(p[i]==0,"Disconnected auxiliary planes clear");
  const auto signature=pluginAudioLayoutSignature(plan.buses());PluginAudioBufferPlan selected(physical,physical,std::array<uint32_t,2>{1,4},std::array<uint32_t,1>{3});
  check(signature==pluginAudioLayoutSignature(selected.buses()),"Membership cannot change physical fingerprint");validatePluginAudioLayout(signature,selected.buses());validatePluginAudioLayout({},selected.buses());
  auto changed=physical;changed[0].channels=4;PluginAudioBufferPlan other(changed,physical,{},{});bool rejected=false;try{validatePluginAudioLayout(signature,other.buses());}catch(const std::runtime_error &){rejected=true;}check(rejected,"Changed physical layout cannot retarget saved numeric pairs");
  rejected=false;try{PluginAudioBufferPlan excessive({},std::vector<PluginPhysicalBus>{{64,"A"},{64,"B"},{1,"C"}},{},{});}catch(const std::runtime_error &){rejected=true;}check(rejected,"More than 64 logical ports rejected before activation");
  check(plan.storageBytes()>8*4096*sizeof(float),"Physical plane allocations included in storage budget");
  std::cout<<"PASS physical channel-pair mapping, odd mono, disconnected silence, identity and bounds\n";return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
