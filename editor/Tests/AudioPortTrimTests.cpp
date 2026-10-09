#include "editor/SignalRuntime.hpp"
#include "editor/GraphTrims.hpp"
#include "editor/GraphEditing.hpp"
#include "editor/hosted/HostedAudio.hpp"
#include <iostream>
#include <stdexcept>
using namespace Tracker;
#ifdef _WIN32
#include "windows/Audio/RealtimeAudit.hpp"
static void tracker_audit_begin(){ScreamSeq::AudioAudit::allocations=0;ScreamSeq::AudioAudit::deallocations=0;ScreamSeq::AudioAudit::active=true;}
static void tracker_audit_end(uint64_t *a,uint64_t *f,uint64_t *l){ScreamSeq::AudioAudit::active=false;*a=ScreamSeq::AudioAudit::allocations;*f=ScreamSeq::AudioAudit::deallocations;*l=0;}
#elif defined(TRACKER_SANITIZER)
static void tracker_audit_begin(){}
static void tracker_audit_end(uint64_t *a,uint64_t *f,uint64_t *l){*a=*f=*l=0;}
#else
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *,uint64_t *,uint64_t *);
#endif
static void check(bool v,const char *m){if(!v)throw std::runtime_error(m);}
static AudioPortTrims pair(double gain){AudioPortTrims t;t.links["i:0"]="o:0";t.set("i:0",gain);return t;}
static SignalDefinition definition(){SignalDefinition d;d.id=1;d.number=1;d.name="Port drive";d.nodes={{2,SignalNodeKind::Input,"In"},{3,SignalNodeKind::Plugin,"Drive"},{4,SignalNodeKind::Output,"Out"},{5,SignalNodeKind::LFO,"Motion"}};d.nodes[1].plugin.classID="resonance.gainer.v1";d.audio={{2,3},{3,4}};return d;}
static std::vector<float> render(uint32_t block,double rate,bool grouped,bool nonlinear,bool modulation){
  auto spec=definition();if(grouped)spec.groups={{20,0,"Wrapped",0,0,{3}}};
  auto &trims=grouped?spec.groups[0].trims:spec.nodes[1].trims;
  std::string in="i:0",out="o:0";
  if(grouped){const auto b=signalGroupBoundary(spec,20);in=audioTrimKey(b.inputs[0]);out=audioTrimKey(b.outputs[0]);}
  trims.links[in]=out;trims.set(in,-12);if(modulation)trims.modulation[in]={{5,0,6}};
  SignalRuntime runtime(spec,compileSignal(spec),rate);SignalCallbacks cb;cb.context=&nonlinear;
  cb.process=[](void *p,uint64_t,float *b,uint32_t n,uint64_t,std::span<const MixerAudioInput>)noexcept{if(*static_cast<bool *>(p))for(uint32_t i=0;i<n*2;++i)b[i]=std::tanh(4*b[i]);return true;};
  std::unique_ptr<SignalControls> changed;std::vector<float> result(4800*2);std::array<float,8192> data{};
  for(uint32_t at=0;at<4800;){if(at==1200){trims.set(in,12);changed=std::make_unique<SignalControls>(spec,rate);runtime.controls(*changed);}if(at==2400&&grouped){spec.groups[0].bypass=true;changed=std::make_unique<SignalControls>(spec,rate);runtime.controls(*changed);}
    auto n=std::min(block,4800-at);for(auto b:{1200u,2400u})if(at<b)n=std::min(n,b-at);for(uint32_t i=0;i<n;++i)data[2*i]=data[2*i+1]=float(.2*std::sin((at+i)*.017));
    uint64_t a,f,l;tracker_audit_begin();bool okay=runtime.render(data.data(),n,at,SignalClock{.beat=at*2./rate},cb);tracker_audit_end(&a,&f,&l);check(okay&&a+f+l==0,"Trim audio render failed or touched the allocator/lock");
    std::copy_n(data.data(),n*2,result.data()+at*2);at+=n;
  }
  if(!nonlinear)for(uint32_t i=0;i<4800;++i)check(std::abs(result[i*2]-.2*std::sin(i*.017))<1e-7,"Inverse pair changed a linear/bypassed signal");return result;
}
static void nestedTrims(){
  auto spec=definition();spec.groups={{20,0,"Outer",0,0,{},true},{21,20,"Inner",0,0,{3}}};
  for(auto &g:spec.groups){const auto b=signalGroupBoundary(spec,g.id);auto in=audioTrimKey(b.inputs[0]),out=audioTrimKey(b.outputs[0]);g.trims.gains[in]=g.id==20?-12:6;if(g.id==20)g.trims.gains[out]=12;}
  SignalRuntime runtime(spec,compileSignal(spec),48000);SignalCallbacks cb;cb.process=[](void *,uint64_t,float *,uint32_t,uint64_t,std::span<const MixerAudioInput>)noexcept{return true;};std::array<float,512> audio;audio.fill(.1f);check(runtime.render(audio.data(),256,0,{},cb),"Nested trimmed bypass failed");for(auto v:audio)check(std::abs(v-.1f)<2e-8,"Outer bypass retained an inner group's input trim");
}
static void stageAuxiliary(){
  AudioPortTrims spec;spec.gains={{"i:1",-6},{"o:1",3}};GraphStageTrims trims("stage:n1",3,3,48000,spec);trims.controls(spec);
  std::array<float,256> main{},side;side.fill(.25f);const std::array<PluginAudioInput,1> inputs{{{1,side.data()}}};uint64_t a,f,l;
  tracker_audit_begin();const auto prepared=trims.begin(main.data(),128,0,inputs);trims.output(1,prepared[0].samples,128,0);const bool okay=trims.finish(main.data(),128,0);tracker_audit_end(&a,&f,&l);
  check(okay&&a+f+l==0,"Auxiliary stage trim failed realtime audit");for(auto value:side)check(value==.25f,"Trim modified a borrowed sidechain source");for(size_t i=0;i<side.size();++i)check(std::abs(trims.output(1)[i]-.25*std::pow(10.,-3./20))<3e-8,"Auxiliary input/output gains were not applied once");
}
static void plugin(){PluginState state;state.descriptor.format="Built-in";state.descriptor.classID="resonance.gainer.v1";state.descriptor.name="Gainer";NativePlugin plugin(state,48000,true);auto trims=pair(-18);plugin.portTrims(trims);std::array<float,512> data;data.fill(.125f);
  uint64_t a,f,l;tracker_audit_begin();const bool okay=plugin.process(data.data(),256,0,{});tracker_audit_end(&a,&f,&l);check(okay&&a+f+l==0,"Hosted plugin trims failed realtime audit");for(auto sample:data)check(std::abs(sample-.125f)<2e-8,"Hosted linked trims do not cancel");
}
int main(){try{
  auto t=pair(-12);check(t.gain("o:0")==12,"Pair does not compensate");t.set("o:0",6);check(t.gain("i:0")==-6,"Output-side edits do not compensate");const auto before=t;try{t.set("o:0",49);throw std::logic_error("accepted");}catch(const std::invalid_argument &){}check(t==before,"Invalid linked edit mutated values");
  for(auto rate:{44100.,48000.,96000.})for(bool group:{false,true})for(bool mod:{false,true}){auto a=render(17,rate,group,false,mod),b=render(4096,rate,group,false,mod);if(a!=b){double error=0;size_t frame=0;for(size_t i=0;i<a.size();++i)if(std::abs(a[i]-b[i])>error){error=std::abs(a[i]-b[i]);frame=i/2;}std::cerr<<rate<<" group="<<group<<" mod="<<mod<<" max="<<error<<" at="<<frame<<"\n";}check(a==b,"Linked trims depend on callback size");}
  const auto a=render(127,48000,false,true,false);check(std::abs(a[100]-.2*std::sin(50*.017))>.01,"Drive trims did not change nonlinear colour");check(a==render(4096,48000,false,true,false),"Nonlinear drive depends on callback size");plugin();stageAuxiliary();nestedTrims();
  auto spec=definition();spec.nodes[1].trims=pair(-6);spec.nodes[1].trims.modulation["i:0"]={{5,0,12}};NativeSong song;song.nextID=100;song.signal.library={spec};auto copy=cloneSignalGraph(song,1);check(song.signal.library.back().nodes[1].trims.modulation.at("i:0")[0].source==copy.identities.at(5),"Clone lost trim source identity");
  std::cout<<"PASS: linked trim PCM, nonlinear drive, modulation, group bypass, partitions, clone, hosted DSP and realtime audit\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
