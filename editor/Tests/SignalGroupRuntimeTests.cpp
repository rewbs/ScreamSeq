#include "editor/SignalRuntime.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace Tracker;
#ifdef TRACKER_SANITIZER
static void tracker_audit_begin(){}
static void tracker_audit_end(uint64_t *a,uint64_t *f,uint64_t *l){*a=*f=*l=0;}
#else
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *,uint64_t *,uint64_t *);
#endif
static void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
static SignalDefinition definition(){SignalDefinition d;d.id=1;d.number=1;d.name="Nested boundary";
  d.nodes={{2,SignalNodeKind::Input,"Input"},{3,SignalNodeKind::Plugin,"A"},{4,SignalNodeKind::Plugin,"B"},{5,SignalNodeKind::Output,"Out"}};
  d.nodes[1].plugin.classID=d.nodes[2].plugin.classID="resonance.gainer.v1";d.audio={{2,3,0,0,.5},{3,4},{4,5}};
  d.groups={{20,0,"Outer",0,0,{4}},{21,20,"Inner",0,0,{3}}};return d;
}
struct DSP {std::array<float,14> a{};std::array<float,10> b{};size_t ai=0,bi=0;uint64_t framesA=0,framesB=0;
  static bool process(void *opaque,uint64_t node,float *samples,uint32_t frames,uint64_t,std::span<const MixerAudioInput>)noexcept{
    auto &d=*static_cast<DSP *>(opaque);float *delay=node==3?d.a.data():d.b.data();auto &cursor=node==3?d.ai:d.bi;const auto count=node==3?d.a.size():d.b.size();const float gain=node==3?2:3;
    (node==3?d.framesA:d.framesB)+=frames;
    for(uint32_t i=0;i<frames*2;++i){std::swap(samples[i],delay[cursor]);samples[i]*=gain;if(++cursor==count)cursor=0;}return true;
  }
};
static std::vector<float> nested(uint32_t block,uint32_t rate,bool replace){
  auto spec=definition();const std::vector<SignalProcessorInfo> processors{{3,7},{4,5}};
  auto runtime=std::make_unique<SignalRuntime>(spec,compileSignal(spec,processors),rate);
  DSP dsp;SignalCallbacks callbacks;callbacks.context=&dsp;callbacks.process=DSP::process;
  std::vector<std::unique_ptr<SignalControls>> controls;std::unique_ptr<SignalRuntime> old;
  std::vector<float> result(4200*2);std::array<float,8192> samples{};
  for(uint32_t at=0;at<4200;){
    if(at==1000||at==2000||at==3000){if(at==1000)spec.groups[1].bypass=true;else spec.groups[0].bypass=at==2000;
      controls.push_back(std::make_unique<SignalControls>(spec,rate));runtime->controls(*controls.back());}
    if(at==2500&&replace){auto next=std::make_unique<SignalRuntime>(spec,compileSignal(spec,processors),rate);next->inheritState(*runtime);old=std::move(runtime);runtime=std::move(next);}
    auto count=std::min(block,4200-at);for(auto boundary:{1000u,2000u,2500u,3000u})if(at<boundary)count=std::min(count,boundary-at);
    for(uint32_t i=0;i<count*2;++i)samples[i]=float(.15+.1*std::sin((at*2+i)*.007));
    uint64_t a,f,l;tracker_audit_begin();const auto okay=runtime->render(samples.data(),count,at,{},callbacks);tracker_audit_end(&a,&f,&l);
    check(okay&&a+f+l==0,"Group boundary rendering allocated, freed, locked or failed");std::copy_n(samples.data(),count*2,result.data()+at*2);at+=count;
  }
  const auto fade=std::round(rate*.005);
  auto down=[&](double frame,double start){const auto t=std::clamp((frame-start)/fade,0.,1.);return 1-t*t*(3-2*t);};
  for(uint32_t frame=12;frame<4200;++frame)for(uint32_t channel=0;channel<2;++channel){
    const auto dry=.5f*float(.15+.1*std::sin(((frame-12)*2+channel)*.007));
    const auto inner=down(double(frame)-5,1000);const auto outer=frame<3000?down(frame,2000):1-down(frame,3000);
    const auto expected=dry+(dry*3*(1+inner)-dry)*outer;
    check(std::abs(result[frame*2+channel]-expected)<2e-7,"Nested group dry boundary differs from independently delayed signal oracle");
  }
  check(dsp.framesA==4200&&dsp.framesB==4200,"Bypassed group stopped or double-rendered internal processors");return result;
}
static void modulation(){
  auto spec=definition();spec.nodes.insert(spec.nodes.end(),{6,SignalNodeKind::Amount,"Amount"});spec.groups={{20,0,"Source and A",0,0,{3,6},true}};
  spec.modulation={{6,3,7,.2,.4,0,true},{6,4,7,.2,.4,0,true}};
  SignalRuntime runtime(spec,compileSignal(spec),48000);std::array<float,128> samples{};
  struct State {double inside=-1,outside=-1;} state;
  SignalCallbacks callbacks;callbacks.context=&state;callbacks.process=[](void *,uint64_t,float *,uint32_t,uint64_t,std::span<const MixerAudioInput>)noexcept{return true;};
  callbacks.parameter=[](void *p,uint64_t node,uint32_t,double first,double last,uint64_t,uint32_t)noexcept{auto &s=*static_cast<State *>(p);(node==3?s.inside:s.outside)=last;return first==last;};
  callbacks.parameterSamples=[](void *p,uint64_t node,uint32_t,std::span<const double> values,uint64_t)noexcept{auto &s=*static_cast<State *>(p);(node==3?s.inside:s.outside)=values.back();return std::all_of(values.begin(),values.end(),[&](double v){return v==values.front();});};
  check(runtime.render(samples.data(),64,0,{},callbacks),"Grouped modulation render failed");
  check(state.inside==.4&&state.outside==0,"Group bypass must suppress the full outward contribution, including minimum, while internal modulation continues");
}
static void unconnectedSources(){
  auto spec=definition();
  spec.nodes.push_back({6,SignalNodeKind::Follower,"Unused follower"});
  spec.nodes.push_back({7,SignalNodeKind::NoteEnvelope,"Unused gate"});
  spec.nodes.push_back({8,SignalNodeKind::LFO,"Unused LFO"});
  spec.audio.push_back({2,6});spec.groups[1].nodes.insert(spec.groups[1].nodes.end(),{6,7,8});
  SignalRuntime runtime(spec,compileSignal(spec),48000);runtime.note(true);
  std::array<float,8192> samples{};std::fill(samples.begin(),samples.end(),.25f);
  SignalCallbacks callbacks;callbacks.process=[](void *,uint64_t,float *,uint32_t,uint64_t,std::span<const MixerAudioInput>)noexcept{return true;};
  uint64_t a,f,l;tracker_audit_begin();const auto okay=runtime.render(samples.data(),4096,0,{},callbacks);tracker_audit_end(&a,&f,&l);
  check(okay&&a+f+l==0,"Unconnected grouped sources failed the maximum-block realtime render");
  check(std::all_of(samples.begin(),samples.end(),[](float value){return value==.125f;}),"Unconnected group sources changed the audio path");
}
int main(){try{
  for(auto rate:{44100u,48000u,96000u}){auto expected=nested(17,rate,false);check(expected==nested(512,rate,false)&&expected==nested(4096,rate,true),"Group bypass depends on callback partition or loses adopted delay history");}
  modulation();unconnectedSources();std::cout<<"PASS group boundary DSP: nested delayed dry routes, warm vendors, source crossing suppression, exact partition history and realtime audit\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
