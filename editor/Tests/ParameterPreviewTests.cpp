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
static void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
static PluginState gain(const char *id){PluginState s;s.instanceID=id;s.descriptor.format="Built-in";s.descriptor.classID="resonance.gainer.v1";return s;}
static PluginParameter parameter(const PluginChain &chain,size_t slot,uint32_t id){
  for(const auto &p:chain.parameters(slot))if(p.id==id)return p;
  throw std::runtime_error("Fixture parameter missing");
}
static void process(PluginChain &chain,float *pcm,unsigned frames){
  bool okay;
#ifdef _WIN32
  {ScreamSeq::AudioAudit::Scope audit;okay=chain.process(pcm,frames);}
  check(!ScreamSeq::AudioAudit::allocations.load()&&!ScreamSeq::AudioAudit::deallocations.load(),"Preview callback allocated or freed C++ storage");
#elif !defined(TRACKER_SANITIZER)
  uint64_t a=0,f=0,l=0;tracker_audit_begin();okay=chain.process(pcm,frames);tracker_audit_end(&a,&f,&l);
  check(!a&&!f&&!l,"Preview callback allocated, freed or locked");
#else
  okay=chain.process(pcm,frames);
#endif
  check(okay,"Preview audio processing failed");
}
static std::vector<float> render(PluginChain &chain,unsigned frames,unsigned block){
  std::vector<float> result(size_t(frames)*2,1.f);
  for(unsigned at=0;at<frames;){const auto count=std::min(block,frames-at);process(chain,result.data()+size_t(at)*2,count);at+=count;}
  return result;
}
static void workflow(unsigned rate,unsigned block){
  PluginChain chain({gain("preview-a"),gain("preview-b")},rate,true);
  const auto original=chain.states();
  const std::array<ParameterChange,2> edits{{{0,1,-12,0},{1,1,-6,0}}};
  check(chain.previewParameters(edits),"Valid preview batch was refused");
  check(parameter(chain,0,1).manualValue==0&&parameter(chain,1,1).manualValue==0,"Preview overwrote accepted manual values");
  const auto pcm=render(chain,2048,block);
  check(std::abs(pcm.back()-std::pow(10.f,-18.f/20))<1e-6f,"Preview did not affect real audio");
  check(chain.states()==original,"Captured opaque state baked in a preview");
  check(parameter(chain,0,1).value==-12&&parameter(chain,1,1).value==-6,"Saving reset the audible preview");
  check(chain.hasParameterPreview(0,1)&&chain.hasParameterPreview(1,1),"Capture consumed preview ownership");
  PluginChain reopened(chain.states(),rate,true);
  check(std::abs(render(reopened,2048,block).back()-1.f)<1e-6f,"Reopened captured state inherited transient audio");
  const std::array<uint32_t,1> id{1};
  check(chain.cancelParameterPreviews(0,id),"Cancel failed");
  render(chain,2048,block);
  check(!chain.hasParameterPreview(0,1)&&chain.hasParameterPreview(1,1)&&parameter(chain,0,1).value==0&&parameter(chain,1,1).value==-6,
    "Cancel changed another plugin's preview");
  // A newer accepted edit retires its own preview. Cancel must never replay
  // the original zero over that accepted edit, even before the queue drains.
  check(chain.parameter(1,1,-3),"Durable value after preview was refused");
  check(chain.cancelParameterPreviews(1,id),"Cancel after external acceptance failed");
  render(chain,2048,block);
  check(parameter(chain,1,1).value==-3&&parameter(chain,1,1).manualValue==-3&&!chain.hasParameterPreview(1,1),"Cancellation restored a stale baseline");
  const auto accepted=chain.states();check(accepted!=original,"Accepted edit was absent from captured state");
  const std::array<ParameterChange,1> again{{{1,1,-24,0}}};
  check(chain.previewParameters(again),"Second preview failed");render(chain,2048,block);
  check(chain.states()==accepted,"Second preview changed a previously accepted state");
  check(chain.cancelParameterPreviews(1,id),"Second cancellation failed");render(chain,2048,block);
  check(parameter(chain,1,1).value==-3&&chain.states()==accepted,"Second cancellation ignored the current manual baseline");
}
static void refusal(){
  PluginChain chain({gain("preview-a"),gain("preview-b")},48000,true);
  const auto original=chain.states();
  for(const auto &bad:std::vector<std::vector<ParameterChange>>{
      {{0,1,-6,0},{2,1,-12,0}},{{0,1,-6,0},{1,999,0,0}},{{0,1,-6,0},{1,1,25,0}},
      {{0,1,-6,0},{1,1,std::numeric_limits<float>::quiet_NaN(),0}},{{0,1,-6,0},{1,1,-12,1}}}){
    check(!chain.previewParameters(bad),"Invalid preview batch accepted");
    check(!chain.hasParameterPreview(0,1)&&chain.states()==original,"Invalid later entry published a valid prefix");
  }
  const std::array<ParameterChange,1> first{{{0,1,-12,0}}};
  check(chain.previewParameters(first),"Preview before saturation failed");render(chain,2048,128);
  // Fill another processor's queue traffic without committing the preview target.
  std::vector<ParameterChange> full(4096,{1,1,0,0});check(chain.enqueueParameters(full),"Queue fixture did not fill");
  const std::array<uint32_t,1> ids{1};
  const std::array<ParameterChange,1> next{{{0,1,-24,0}}};
  check(!chain.previewParameters(next)&&!chain.cancelParameterPreviews(0,ids),"Saturated queue accepted preview or cancel");
  check(chain.hasParameterPreview(0,1)&&parameter(chain,0,1).manualValue==0,"Failed cancellation lost accepted baseline or ownership");
  render(chain,2048,128);check(parameter(chain,0,1).value==-12,"Rejected preview reached audio");
  const std::array<uint32_t,2> invalidIDs{1,999};
  check(!chain.cancelParameterPreviews(0,invalidIDs)&&chain.hasParameterPreview(0,1),"Invalid later cancel ID partially reset a preview");
  check(chain.cancelParameterPreviews(0,ids),"Cancel after draining failed");render(chain,2048,128);
  check(chain.states()==original&&!chain.hasParameterPreview(0,1),"Cancellation did not recover exact accepted state");
}
int main(){try{
  for(unsigned rate:{44100u,48000u,96000u})for(unsigned block:{17u,128u,511u})workflow(rate,block);
  refusal();
  std::cout<<"PASS transient parameter PCM, manual/opaque state isolation, reopen, current-baseline cancel, atomic refusal and callback audit\n";
  return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
