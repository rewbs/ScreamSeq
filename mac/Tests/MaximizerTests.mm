#include "editor/NativeEffects.hpp"
#include "editor/MixerRuntime.hpp"
#include "../Audio/AudioUnitHost.hpp"
#import "../Bridge/TrackerSession.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <iostream>
#include <limits>
#include <numbers>
using namespace Tracker;
static constexpr auto ID="resonance.maximizer.v1";
static void check(bool ok,const char *message) { if(!ok)throw std::runtime_error(message); }
#ifdef TRACKER_SANITIZER
static void tracker_audit_begin() {}
static void tracker_audit_end(uint64_t *a,uint64_t *f,uint64_t *l) { *a=*f=*l=0; }
#else
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *,uint64_t *,uint64_t *);
#endif
static void audited(bool ok) { uint64_t a,f,l;tracker_audit_end(&a,&f,&l);check(ok,"Maximizer renders");check(!(a+f+l),"Maximizer render/control path does not allocate, free or lock"); }
static double amp(double db) { return std::pow(10.,db/20); }
using Controls=std::array<double,6>;
static constexpr Controls defaults{1,0,-.3f,20,200,-.3f};
struct Event { uint32_t frame,id;float value; };
static double convert(size_t id,double value,double rate) {
  if(id==1||id==2||id==5)return amp(value);
  if(id==3||id==4)return std::exp(-1000/(value*rate));
  return value;
}
static PluginDescriptor descriptor() { for(const auto &d:NativePlugin::builtins())if(d.classID==ID)return d;throw std::runtime_error("Missing Maximizer descriptor"); }
// Offline oracle uses a monotone deque and an explicit convolution, not the
// processor's bounded trees. In particular it has no final peak guard/clipping.
static std::vector<double> reference(const std::vector<float> &input,uint32_t rate,Controls initial=defaults,const std::vector<Event> &events={}) {
  const uint32_t L=uint32_t(std::ceil(rate*.005)),count=uint32_t(input.size()/2);
  std::vector<Controls> controls(count);Controls start{},target{};std::array<int64_t,6> begin;begin.fill(-int64_t(L));
  for(size_t j=0;j<6;++j)start[j]=target[j]=convert(j,initial[j],rate);
  for(uint32_t n=0;n<count;++n) {
    for(const auto &event:events)if(event.frame==n) {
      const auto j=event.id;const double next=convert(j,event.value,rate);
      if(next==target[j])continue; // Repeated writes retain their original deadline.
      start[j]=n?controls[n-1][j]:target[j];target[j]=next;begin[j]=n;
      if(!n)start[j]=target[j];
    }
    for(size_t j=0;j<6;++j)controls[n][j]=start[j]+(target[j]-start[j])*std::min(1.,double(int64_t(n)-begin[j]+1)/L);
  }
  std::vector<double> out(input.size()),raw(count);std::deque<std::pair<uint32_t,double>> minima;
  double fast=1,memory=0;const double attack=std::exp(-1/(rate*.05));
  for(uint32_t n=0;n<count;++n) {
    const auto &p=controls[n];const double peak=p[1]*std::max(std::abs(input[2*n]),std::abs(input[2*n+1]));
    const double desired=peak>p[2]?p[2]/peak:1;
    while(!minima.empty()&&minima.back().second>=desired)minima.pop_back();
    minima.emplace_back(n,desired);
    while(minima.front().first+L<n)minima.pop_front();
    const double bound=minima.front().second;
    fast=std::min(bound,1-(1-fast)*p[3]);
    const double targetMemory=1-bound,pole=targetMemory>memory?attack:p[4];
    memory=targetMemory*(1-pole)+memory*pole;
    if(memory<1e-15)memory=0;
    raw[n]=std::min(fast,1-memory);
    if(n<L)continue;
    double sum=0;for(uint32_t j=n-L;j<=n;++j)sum+=raw[j];const double gain=sum/(L+1);
    const auto &d=controls[n-L];const double delayedPeak=d[1]*std::max(std::abs(input[2*(n-L)]),std::abs(input[2*(n-L)+1]));
    check(gain*delayedPeak<=d[2]+1e-10*std::max(1.,d[2]),"Unguarded gain convolution independently preserves every delayed sample bound");
    for(size_t c=0;c<2;++c) {
      const double dry=input[2*(n-L)+c],wet=dry*d[1]*gain*d[5]/d[2];
      out[2*n+c]=(1-d[0])*dry+d[0]*wet;
    }
  }
  return out;
}
static std::vector<float> render(const std::vector<float> &input,uint32_t rate,Controls initial=defaults,const std::vector<Event> &events={}) {
  NativeEffect effect(ID,rate);for(size_t i=0;i<6;++i)effect.parameter(uint32_t(i),float(initial[i]));
  auto out=input;bool ok=true;tracker_audit_begin();
  for(uint32_t n=0;n<input.size()/2;++n) {
    for(const auto &event:events)if(event.frame==n)ok&=effect.parameter(event.id,event.value);
    ok&=effect.process(out.data()+2*n,1);
  }audited(ok);return out;
}
static void staticAndRelease() {
  double maximum=0;
  for(uint32_t rate:{8000u,44100u,48000u,96000u,192000u,384000u}) {
    const uint32_t L=uint32_t(std::ceil(.005*rate)),peakAt=2*L,N=10*L;
    std::vector<float> input(2*N,.001f);input[2*peakAt]=8;input[2*peakAt+1]=-2;
    Controls params{1,0,-6,20,200,-1};const auto out=render(input,rate,params);
    // Closed forms for a single peak: constant gain bound for exactly L+1
    // detector frames, followed by two exponential recovery trajectories.
    const double g=amp(-6)/8,a=std::exp(-1/(.05*rate)),f=std::exp(-1/(.02*rate)),s=std::exp(-1/(.2*rate));
    const double charged=(1-g)*(1-std::pow(a,L+1));
    auto raw=[&](uint32_t n) {
      if(n<peakAt)return 1.;if(n<=peakAt+L)return g;
      const auto k=n-peakAt-L;return std::min(1-(1-g)*std::pow(f,k),1-charged*std::pow(s,k));
    };
    for(uint32_t n=0;n<N;++n)for(size_t c=0;c<2;++c) {
      double expected=0;if(n>=L) {double sum=0;for(uint32_t k=n-L;k<=n;++k)sum+=raw(k);expected=input[2*(n-L)+c]*sum/(L+1)*amp(5);}
      maximum=std::max(maximum,std::abs(out[2*n+c]-expected));
    }
    check(std::abs(out[2*(peakAt+L)]-amp(-1))<6e-8,"Isolated peak reaches the declared ceiling without exceeding it");
    check(out[2*(peakAt+L)+1]==-.25f*out[2*(peakAt+L)],"Stereo image uses the identical linked gain");
    for(float enabled:{0.f,1.f}) {
      auto quiet=input;for(auto &x:quiet)x=.0625f;Controls p=defaults;p[0]=enabled;
      const auto dry=render(quiet,rate,p);
      for(uint32_t n=0;n<N;++n)check(dry[2*n]==(n<L?0:.0625f),"Bypass and unlimited default are exact delayed dry");
    }
    NativeEffect e(ID,rate);check(e.latency()==double(L)/rate&&e.tail()==0,"Latency is ceil(5 ms), with no extra emitted tail");
  }
  check(maximum<6e-8,"Entire isolated-peak attack and dual recovery match independent closed forms");
  std::cout<<"Maximizer closed-form peak/release maximum "<<maximum<<'\n';
}
static std::vector<float> fixture(uint32_t frames) {
  std::vector<float> input(frames*2);uint32_t seed=932457;
  for(uint32_t n=0;n<frames;++n)for(size_t c=0;c<2;++c) {seed=1664525*seed+1013904223;input[2*n+c]=float(int32_t(seed))/2147483648.f*(n%1100<30?8.f:.3f);}
  return input;
}
static void automatedReference() {
  double maximum=0;
  for(uint32_t rate:{8000u,44100u,48000u,96000u,192000u,384000u}) {
    auto input=fixture(8192);std::vector<Event> events;
    for(uint32_t n:{37u,113u,557u,1050u,1800u,3000u,4097u,5997u}) {
      const bool high=n%2;events.push_back({n,1,high?36.f:0.f});events.push_back({n,2,high?-36.f:0.f});
      events.push_back({n,3,high?1.f:200.f});events.push_back({n,4,high?5000.f:20.f});events.push_back({n,5,high?0.f:-36.f});
    }
    auto expected=reference(input,rate,defaults,events);auto out=render(input,rate,defaults,events);
    for(size_t i=0;i<out.size();++i)maximum=std::max(maximum,std::abs(out[i]-expected[i]));
    // Bypass gestures have their own source-aligned ramp and keep the limiter warm.
    events.push_back({2400,0,0});events.push_back({4200,0,1});
    expected=reference(input,rate,defaults,events);out=render(input,rate,defaults,events);
    for(size_t i=0;i<out.size();++i)maximum=std::max(maximum,std::abs(out[i]-expected[i]));
    NativeEffect enabled(ID,rate),warm(ID,rate);warm.parameter(0,0);
    for(uint32_t n=0;n<8192;++n) {
      if(n==500)warm.parameter(0,1);float a[]{input[2*n],input[2*n+1]},b[]{a[0],a[1]};
      check(enabled.process(a,1)&&warm.process(b,1),"Warm limiter processes");
      if(n>=500+2*uint32_t(std::ceil(rate*.005)))check(a[0]==b[0]&&a[1]==b[1],"Re-enabling preserves continuously warm gain history");
    }
  }
  std::cout<<"Maximizer automated independent reference maximum "<<maximum<<'\n';
  check(maximum<5e-7,"Independent unguarded offline oracle matches interrupted controls and aligned bypass at all rates");
}
static void extremesAndSpectrum() {
  for(uint32_t rate:{8000u,48000u,384000u}) {
    NativeEffect e(ID,rate);e.parameter(1,36);e.parameter(2,-36);e.parameter(5,-36);
    bool ok=true;tracker_audit_begin();
    for(uint32_t n=0;n<20000;++n) {
      float x[]{n%23?std::numeric_limits<float>::max():-std::numeric_limits<float>::max(),0};
      ok&=e.process(x,1)&&std::isfinite(x[0])&&std::abs(x[0])<=amp(-36)*(1+1e-6)&&x[1]==0;
    }audited(ok);
  }
  NativeEffect effect(ID,48000);effect.parameter(1,12);effect.parameter(2,-6);effect.parameter(5,-1);
  const double omega=2*std::numbers::pi/48;std::vector<double> out(4800);
  for(uint32_t n=0;n<100800;++n){float frame[]{float(std::sin(omega*n)),0};check(effect.process(frame,1)&&frame[1]==0,"Sine limiter preserves silent channel");if(n>=96000)out[n-96000]=frame[0];}
  double re=0,im=0;for(size_t n=0;n<out.size();++n){re+=out[n]*std::cos(n*omega);im+=out[n]*std::sin(n*omega);}re*=2./out.size();im*=2./out.size();
  double error=0;for(size_t n=0;n<out.size();++n)error+=std::pow(out[n]-re*std::cos(n*omega)-im*std::sin(n*omega),2);
  const double ratio=std::sqrt(2*error/out.size())/std::hypot(re,im);check(ratio<.0001,"Specified steady 1 kHz limited fixture remains below 0.01 percent non-fundamental energy");
  const auto meter=effect.meters();check(meter&&meter->reductionDB[0]>17&&meter->reductionDB[0]==meter->reductionDB[1]&&meter->detectorDB[1]==-160,"Meters show linked limiting and independent detector levels");
  std::cout<<"Maximizer steady 1 kHz non-fundamental ratio "<<ratio<<'\n';
}
static void graphAndHosting() {
  for(uint32_t rate:{44100u,48000u,96000u}) {
    NativeEffect settings(ID,rate);settings.parameter(1,18);settings.parameter(2,-9);settings.parameter(5,-1);
    PluginState state{descriptor()};state.state=settings.state();state.instanceID="limit";
    const uint32_t latency=uint32_t(std::ceil(rate*.005));
    MixerGraph graph;graph.buses={{1,20,MixerBusKind::Track,"Limited"},{2,20,MixerBusKind::Track,"Dry"},{20,0,MixerBusKind::Master,"Master"}};graph.buses[0].inserts={"limit"};
    const auto plan=compileMixer(graph,{1,2},{{"limit",latency,0}},rate);check(plan.latency==latency,"Mixer reports fixed lookahead for compensation");
    const auto source=fixture(8192);
    auto run=[&](uint32_t block,bool offline) {
      MixerRuntime runtime(graph,plan,rate);NativePlugin host(state,rate,offline);host.prepareMusicalAutomation();host.schedule(1,36,37);host.schedule(5,-3,113);host.schedule(1,0,4097);
      struct Context {NativePlugin *host;static bool process(void *raw,size_t,float *audio,uint32_t count,uint64_t position) noexcept {return static_cast<Context *>(raw)->host->process(audio,count,position);}} context{&host};
      std::array<float,4096> left{},right{};std::vector<float> result(source.size());
      for(uint32_t pos=0;pos<8192;pos+=block) {
        const auto frames=std::min(block,8192-pos);tracker_audit_begin();runtime.begin(frames,pos);
        for(size_t bus:plan.order) {
          for(uint32_t n=0;n<frames;++n){left[n]=bus==0?source[2*(pos+n)]:bus==1?.01f:0;right[n]=bus==0?source[2*(pos+n)+1]:bus==1?-.02f:0;}
          const auto *audio=runtime.process(bus,left.data(),right.data(),Context::process,&context);
          if(bus==plan.master&&audio)std::copy_n(audio,frames*2,result.data()+pos*2);
        }runtime.complete();audited(!runtime.failed());
      }return result;
    };
    const auto output=run(128,false);for(uint32_t block:{17u,512u,4096u})check(output==run(block,true),"Limiter graph and exact parameter offsets are callback-size/live/offline independent");
    const auto expected=reference(source,rate,{1,18,-9,20,200,-1},{{37,1,36},{113,5,-3},{4097,1,0}});double maximum=0;
    for(uint32_t n=0;n<8192;++n)for(size_t c=0;c<2;++c)maximum=std::max(maximum,std::abs(double(output[2*n+c])-(float(expected[2*n+c])+(n>=latency?(c?-.02f:.01f):0))));
    check(maximum<2e-7,"Real graph matches independent limiting plus exactly delayed dry branch");
  }
}
static void apiAndExport() {
  TrackerSession *session=[TrackerSession new];NSError *problem=nil;
  auto call=[&](NSString *method,NSDictionary *params,bool write=false)->NSDictionary * {
    NSMutableDictionary *p=[params mutableCopy];if(write)p[@"expectedRevision"]=session.automationRevision;
    auto result=[session automationMethod:method params:p error:&problem];if(!result)throw std::runtime_error(problem.localizedDescription.UTF8String);return result;
  };
  NSDictionary *d=nil;for(NSDictionary *p in session.builtInPlugins)if([p[@"classID"] isEqual:@"resonance.maximizer.v1"])d=p;
  check(d!=nil,"Maximizer discovery is scan-free");
  NSString *base=[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString],*dry=[base stringByAppendingString:@".dry.wav"],*wet=[base stringByAppendingString:@".wet.wav"],*project=[base stringByAppendingString:@".resonance"];
  check([TrackerSession exportData:session.serializedData path:dry error:&problem],"Dry audio exports");
  call(@"plugin.add",@{@"descriptor":d},true);NSArray *parameters=call(@"plugin.parameters.get",@{@"slot":@0})[@"data"];
  check(parameters.count==6&&[parameters[3][@"unitLabel"] isEqual:@"ms"]&&[parameters[3][@"displayScale"] isEqual:@"logarithmic"],"Limiter publishes all six controls with release time units/scales");
  auto rev=session.automationRevision;check([call(@"plugin.meters",@{@"slot":@0})[@"data"][@"supported"] boolValue]&&[rev isEqual:session.automationRevision],"Limiter meter is supported and read-only");
  check([call(@"plugin.buses.get",@{@"slot":@0})[@"data"][@"buses"] count]==2,"Limiter has main stereo ports only");
  call(@"plugin.parameters.set",@{@"slot":@0,@"values":@[@{@"id":@1,@"value":@24},@{@"id":@2,@"value":@-9},@{@"id":@5,@"value":@-1}]},true);
  auto state=call(@"plugin.state.get",@{@"slot":@0})[@"data"];
  call(@"history.undo",@{@"domain":@"plugins"},true);check([call(@"plugin.parameters.get",@{@"slot":@0})[@"data"][1][@"value"] floatValue]==0,"Batch limiter edit undoes as one action");call(@"history.redo",@{@"domain":@"plugins"},true);
  check([session savePath:project error:&problem],"Limiter project saves");TrackerSession *restored=[TrackerSession new];check([restored openPath:project error:&problem],"Limiter project reopens");
  check([[restored automationMethod:@"plugin.state.get" params:@{@"slot":@0} error:&problem][@"data"] isEqual:state],"Every limiter target persists exactly");
  check([TrackerSession exportData:restored.serializedData path:wet error:&problem],"Saved limiter audio exports");
  NSData *a=[NSData dataWithContentsOfFile:dry],*b=[NSData dataWithContentsOfFile:wet];check(a.length==b.length,"Export trims lookahead exactly and retains original duration");
  std::vector<float> input((a.length-44)/4+480,0);std::memcpy(input.data(),static_cast<const char *>(a.bytes)+44,a.length-44);
  const auto expected=reference(input,48000,{1,24,-9,20,200,-1});double maximum=0;
  for(size_t i=0;i<(b.length-44)/4;++i){float actual;std::memcpy(&actual,static_cast<const char *>(b.bytes)+44+i*4,4);maximum=std::max(maximum,std::abs(actual-expected[i+480]));check(std::abs(actual)<=amp(-1)*(1+1e-6),"Entire saved WAV obeys output sample ceiling");}
  check(maximum<6e-8,"Entire saved WAV matches independent unguarded limiter oracle after latency trim");std::cout<<"Maximizer independent saved WAV maximum "<<maximum<<'\n';
  for(NSString *path in @[dry,wet,project])[[NSFileManager defaultManager]removeItemAtPath:path error:nil];
}
int main(){@autoreleasepool {try {
  staticAndRelease();automatedReference();extremesAndSpectrum();graphAndHosting();apiAndExport();
  std::cout<<"PASS Maximizer: independent ceiling/dual-release/lookahead proof, interrupted controls, bypass, stereo preservation, extremes, PDC, API/history/persistence/export and realtime audit\n";return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}}
