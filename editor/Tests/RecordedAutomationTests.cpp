#include "editor/hosted/HostedAudio.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#ifdef _WIN32
#include "windows/Audio/RealtimeAudit.hpp"
#elif !defined(TRACKER_SANITIZER)
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *,uint64_t *,uint64_t *);
#endif
using namespace Tracker;
static void check(bool condition,const char *message){if(!condition)throw std::runtime_error(message);}
static void process(PluginChain &chain,float *pcm,unsigned frames) {
  bool okay;
#ifdef _WIN32
  {ScreamSeq::AudioAudit::Scope audit;okay=chain.process(pcm,frames);}
  check(!ScreamSeq::AudioAudit::allocations.load()&&!ScreamSeq::AudioAudit::deallocations.load(),"Callback allocated or freed C++ storage");
#elif !defined(TRACKER_SANITIZER)
  uint64_t allocated=0,freed=0,locked=0;tracker_audit_begin();okay=chain.process(pcm,frames);tracker_audit_end(&allocated,&freed,&locked);
  check(!allocated&&!freed&&!locked,"Callback allocated, freed or locked");
#else
  okay=chain.process(pcm,frames);
#endif
  check(okay,"Recorded automation render failed");
}
static PluginState gain() {PluginState result;result.instanceID="recorded-gain";result.descriptor.format="Built-in";result.descriptor.classID="resonance.gainer.v1";return result;}
static std::vector<float> render(PluginChain &chain,unsigned count,unsigned block) {
  std::vector<float> result(size_t(count)*2,.25f);
  for(unsigned at=0;at<count;){const auto frames=std::min(block,count-at);process(chain,result.data()+2*at,frames);at+=frames;}return result;
}
static void settled(PluginChain &chain,float decibels) {
  const auto pcm=render(chain,4096,128);const double expected=.25*std::pow(10.,decibels/20.);
  check(std::abs(pcm.back()-expected)<1e-6,"Published timeline has the wrong current value");
}
int main() {
  try {
    for(unsigned rate:{44100u,48000u,96000u})for(unsigned block:{17u,128u,4096u}) {
      const std::vector<ParameterChange> points={{0,1,-12,0},{0,1,-3,4800},{0,1,-9,9600}};
      PluginChain initial({gain()},rate,true,points),published({gain()},rate,true);
      auto plan=published.prepareRecordedAutomation(points);check(published.publishRecordedAutomation(std::move(plan)),"Initial timeline publication rejected");
      const auto a=render(initial,rate/4,block),b=render(published,rate/4,block);
      check(a==b,"Recorded publication differs by rate, callback partition or constructor scheduling");
    }
    PluginChain chain({gain()},48000,true,{{0,1,-6,0},{0,1,-12,48000}});
    settled(chain,-6);
    auto replace=[&](float value){return chain.prepareRecordedAutomation({{0,1,value,0}});};
    auto stale=replace(-24),first=replace(-18);check(chain.publishRecordedAutomation(std::move(first)),"Timeline replacement rejected");
    check(!chain.publishRecordedAutomation(std::move(stale)),"Stale timeline source revision accepted");settled(chain,-18);
    for(float value:{-3.f,-9.f,-15.f})check(chain.publishRecordedAutomation(replace(value)),"Available timeline slot rejected");
    bool rejected=false;try{(void)replace(-30);}catch(const std::runtime_error &){rejected=true;}check(rejected,"Full timeline queue accepted a candidate");settled(chain,-15);
    check(chain.publishRecordedAutomation(chain.prepareRecordedAutomation({})),"Timeline removal rejected");settled(chain,0);
    check(!chain.hasAutomatedState(),"Removed recorded lane retained automated-state flag");
    // Seeking has already applied recorded values before the rack entry exists.
    // Deleting its timeline must still restore the original saved manual value.
    PluginChain seek({gain()},48000,true,{{0,1,-18,0}},96000);
    check(seek.publishRecordedAutomation(seek.prepareRecordedAutomation({})),"Sought timeline removal rejected");settled(seek,0);
    for(const auto &invalid:std::vector<std::vector<ParameterChange>>{{{1,1,0,0}},{{0,999,0,0}},{{0,1,25,0}},{{0,1,0,0},{0,1,-3,0}},{{0,1,std::numeric_limits<float>::quiet_NaN(),0}}}){
      rejected=false;try{(void)chain.prepareRecordedAutomation(invalid);}catch(const std::invalid_argument &){rejected=true;}check(rejected,"Invalid timeline accepted");
    }
    settled(chain,0);
    // Independent UI batches and timeline snapshots keep producer order when
    // both arrive before a callback, including a prefix beyond128 UI changes.
    std::vector<ParameterChange> manual(256,{0,1,-6,0});
    check(chain.enqueueParameters(manual),"Manual prefix rejected");
    check(chain.publishRecordedAutomation(replace(-18)),"Timeline after manual prefix rejected");settled(chain,-18);
    check(chain.publishRecordedAutomation(replace(-12)),"Timeline before manual suffix rejected");
    check(chain.parameter(0,1,-3),"Manual suffix rejected");settled(chain,-3);
    std::cout<<"PASS recorded timeline rate/partition parity, boundary catch-up/removal, saved baseline after seek, stale/full queue rejection and callback audit\n";
    return 0;
  }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
