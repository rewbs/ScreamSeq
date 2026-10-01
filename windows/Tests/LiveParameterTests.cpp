#include "windows/Session/HostedProject.hpp"
#include "windows/Audio/RealtimeAudit.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <thread>
using namespace ScreamSeq;
using namespace Tracker;
namespace {
void check(bool value,const char *why){if(!value)throw std::runtime_error(why);}
float gain(const PluginChain &chain,size_t slot=0){for(auto &p:chain.parameters(slot))if(p.id==1)return p.value;throw std::runtime_error("Missing gain");}
std::vector<PluginState> gains(){auto d=NativePlugin::builtins().front();PluginState a{d},b{d};a.instanceID="gain-a";b.instanceID="gain-b";return {a,b};}
void process(PluginChain &chain){std::array<float,128> pcm{};bool ok;{AudioAudit::Scope scope;ok=chain.process(pcm.data(),64);}check(ok,"Queue render failed");}
void queueTests(){
  PluginChain chain(gains(),48000,true);
  std::vector<ParameterChange> full(4095,{0,1,-6,0});
  check(chain.enqueueParameters(full),"Accept 4095 changes as one transaction");
  std::array<ParameterChange,2> pair{{{0,1,-12,0},{1,1,-12,0}}};
  check(!chain.enqueueParameters(pair),"Reject a batch that cannot fit in its entirety");
  {AudioAudit::Scope scope;chain.applyPending();}
  check(gain(chain)==-6&&gain(chain,1)==0,"Whole large transaction consumed; rejected batch had no effect");
  pair[1].slot=2;check(!chain.enqueueParameters(pair),"Invalid slot rejects complete batch");pair[1].slot=1;
  pair[1].value=std::numeric_limits<float>::quiet_NaN();check(!chain.enqueueParameters(pair),"Nonfinite value rejects complete batch");pair[1].value=-12;
  pair[1].frame=1;check(!chain.enqueueParameters(pair),"Live batches only apply at the next render boundary");pair[1].frame=0;
  chain.applyPending();check(gain(chain)==-6&&gain(chain,1)==0,"Invalid batch did not publish its valid prefix");
  check(chain.enqueueParameters(pair),"Accept complete pair");
  {AudioAudit::Scope scope;chain.beginRenderBlock();}
  check(gain(chain)==-12&&gain(chain,1)==-12,"Both plugin parameters reach the same block");
  pair[0].value=pair[1].value=-24;check(chain.enqueueParameters(pair),"Publish while a render block is open");
  {AudioAudit::Scope scope;chain.applyPending();}
  check(gain(chain)==-12&&gain(chain,1)==-12,"Intermediate mixer/processor stages cannot consume a later batch");
  process(chain);check(gain(chain)==-12,"process keeps the initial block parameter snapshot");
  {AudioAudit::Scope scope;chain.beginRenderBlock();}
  check(gain(chain)==-24&&gain(chain,1)==-24,"Next block receives the deferred batch");process(chain);
  full.resize(4096,{0,1,0,0});for(unsigned i=0;i<20;++i){for(auto &v:full)v.value=-float(i);check(chain.enqueueParameters(full),"Queue wraps without losing capacity");process(chain);check(gain(chain)==-float(i),"4096-entry batch is never split at the ordinary 128-change bound");}
  std::cout<<"PASS atomic rejection, complete batches, ring wrap and render boundary\n";
}
void concurrentTests(){
  PluginChain chain(gains(),48000,true);std::atomic<bool> done=false,cancel=false;std::atomic<unsigned> sent=0;
  std::thread producer([&]{std::array<ParameterChange,130> batch{};for(unsigned n=1;n<=2048&&!cancel.load();++n){
    for(size_t i=0;i<batch.size();++i)batch[i]={uint32_t(i%2),1,-float(n%48),0};
    while(!chain.enqueueParameters(batch)){if(cancel.load())return;std::this_thread::yield();}++sent;
  }done=true;});
  try {do {
    {AudioAudit::Scope scope;chain.beginRenderBlock();}
    check(gain(chain)==gain(chain,1),"Concurrent SPSC publication exposed a partial transaction");process(chain);
  }while(!done.load());producer.join();chain.states();check(sent==2048&&gain(chain)==-float(2048%48)&&gain(chain)==gain(chain,1),"Drain all concurrent transactions");}
  catch(...){cancel=true;if(producer.joinable())producer.join();throw;}
  std::cout<<"PASS 2048 concurrent SPSC batches of 130 changes\n";
}
Project::ProjectState project(Tracker::Document &doc){
  auto state=Project::newProjectState(doc);const auto d=NativePlugin::builtins().front();
  NativePlugin plugin(PluginState{d},48000);auto bytes=plugin.state().state;std::vector<uint8_t> data(bytes.size());std::memcpy(data.data(),bytes.data(),bytes.size());
  state.preserved["plugins"].push_back({{"type",d.type},{"subtype",d.subtype},{"manufacturer",d.manufacturer},{"format",d.format},{"classID",d.classID},{"name",d.name},{"path",d.path},{"isInstrument",false},{"instanceID","gain"},{"state",Json::binary(data)},{"bypass",false},{"instrument",0},{"instrumentAssignments",Json::array()},{"auxiliaryInputs",Json::array()},{"auxiliaryOutputs",Json::array()}});
  // A song automation event after both manual edits must retain its own timing.
  state.preserved["automation"]=Json::array({Json::array({0,1,0,36000})});return state;
}
std::vector<float> render(Document &doc,const Project::ProjectState &state,unsigned rate,unsigned block,bool edit){
  HostedProjectPlayback playback(doc,state,rate,{},true);std::vector<float> out(size_t(rate)*2);
  const std::array<unsigned,3> boundaries{rate/4,rate/2,rate};
  for(unsigned at=0;at<rate;){
    if(edit&&(at==boundaries[0]||at==boundaries[1])){std::array<ParameterChange,2> batch{{{0,1,at==boundaries[0]?-18.f:-6.f,0},{0,2,0,0}}};check(playback.chain().enqueueParameters(batch),"Publish manual PCM edit");}
    auto end=*std::find_if(boundaries.begin(),boundaries.end(),[&](auto b){return b>at;});auto frames=std::min(block,end-at);bool ok;
    {AudioAudit::Scope scope;ok=playback.render(out.data()+size_t(at)*2,frames);}check(ok,"Prepared live PCM failed");at+=frames;
  }return out;
}
void pcmTests(){auto doc=Document::demo();auto state=project(*doc);auto before=state.preserved;
  for(unsigned rate:{44100u,48000u,96000u}){auto reference=render(*doc,state,rate,128,true),unchanged=render(*doc,state,rate,128,false);double energy=0,early=0,late=0,delta=0;
    for(size_t i=0;i<reference.size();++i){check(std::isfinite(reference[i]),"Finite edited PCM");auto d=std::abs(double(reference[i])-unchanged[i]);energy+=std::abs(reference[i]);if(i<size_t(rate/4)*2)early=std::max(early,d);else if(i>=size_t(rate*9/10)*2)late=std::max(late,d);else delta+=d;}
    check(energy>1&&delta>1&&early<1e-6&&late<1e-6,"Manual edits affect PCM at their boundary; later automation still wins");
    double worst=0;for(unsigned block:{17u,4096u,8193u}){auto other=render(*doc,state,rate,block,true);for(size_t i=0;i<other.size();++i)worst=std::max(worst,std::abs(double(other[i])-reference[i]));}
    check(worst<1e-6,"Manual parameter PCM is callback-partition independent");
    std::cout<<"PASS manual PCM rate="<<rate<<" blocks=17,128,4096,8193 energy="<<energy<<" changed-L1="<<delta<<" max-partition-delta="<<worst<<'\n';
  }check(state.preserved==before,"Playback/manual edits must not overwrite the saved baseline or automation");
}
}
int main(){try{
  {AudioAudit::Scope scope;auto p=::operator new(8);::operator delete(p);auto a=::operator new(64,std::align_val_t{64});::operator delete(a,std::align_val_t{64});}
  check(AudioAudit::allocations==2&&AudioAudit::deallocations==2,"C++ allocation audit positive control");AudioAudit::allocations=0;AudioAudit::deallocations=0;
  queueTests();concurrentTests();pcmTests();
  check(AudioAudit::allocations==0&&AudioAudit::deallocations==0,"Host C++ allocation/free in queued parameter/render callback");
  std::cout<<"PASS scoped host C++ new/delete audit (not malloc, locks or vendor internals)\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
