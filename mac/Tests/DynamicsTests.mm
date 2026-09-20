#include "editor/NativeEffects.hpp"
#include "editor/MixerRuntime.hpp"
#include "../Audio/AudioUnitHost.hpp"
#include "FilterReference.hpp"
#import "../Bridge/TrackerSession.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <numbers>
using namespace Tracker;
static constexpr auto Compressor="resonance.compressor.v1", Gate="resonance.gate.v1", Bus="resonance.bus-compressor.v1";
static void check(bool ok,const char *message) { if(!ok) throw std::runtime_error(message); }
#ifdef TRACKER_SANITIZER
static void tracker_audit_begin() {}
static void tracker_audit_end(uint64_t *a,uint64_t *f,uint64_t *l) { *a=*f=*l=0; }
#else
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *,uint64_t *,uint64_t *);
#endif
static void audited(bool ok) { uint64_t a,f,l;tracker_audit_end(&a,&f,&l);check(ok,"Dynamics processing succeeds");check(!(a+f+l),"Dynamics has no render allocation/free/lock"); }
static double amp(double db) { return std::pow(10.,db/20); }
static double reduction(double db,double threshold,double ratio,double knee) {
  // Output-level curve, independently converted to attenuation.
  if(db<threshold-knee/2) return 0;
  if(db>threshold+knee/2 || knee==0) return db-(threshold+(db-threshold)/ratio);
  return (1-1/ratio)*std::pow(db-threshold+knee/2,2)/(2*knee);
}
static PluginDescriptor descriptor(const char *id) { for(const auto &d:NativePlugin::builtins())if(d.classID==id)return d;throw std::runtime_error("Missing dynamics descriptor"); }
static void curvesAndTime() {
  double maximum=0;
  for(uint32_t rate:{8000u,44100u,48000u,96000u,192000u,384000u}) {
    for(float knee:{0.f,6.f,24.f}) for(float ratio:{1.f,4.f,40.f}) for(double db:{-60.,-30.,-21.,-18.,-15.,-6.,6.}) {
      NativeEffect effect(Compressor,rate);effect.parameter(2,ratio);effect.parameter(6,knee);effect.parameter(3,.1f);effect.parameter(9,1);
      const float key=float(amp(db));const double expected=.25*amp(-reduction(20*std::log10(key),-18,ratio,knee));
      float frame[2]{},side[]{key,key};const uint32_t count=uint32_t(std::ceil(rate*.01));bool ok=true;tracker_audit_begin();
      for(uint32_t n=0;n<count;++n) {frame[0]=frame[1]=.25f;ok&=effect.process(frame,1,side);}audited(ok);
      maximum=std::max(maximum,std::abs(frame[0]-expected));check(frame[0]==frame[1],"Linked compressor uses identical stereo gain");
    }
    NativeEffect effect(Compressor,rate);effect.parameter(6,0);effect.parameter(9,1);
    const uint32_t driven=rate/20;const double a=std::exp(-1/(.01*rate)),r=std::exp(-1/(.1*rate));
    const double d=13.5,previous=d*(1-std::pow(a,driven));bool ok=true;tracker_audit_begin();
    for(uint32_t n=0;n<rate/5;++n) {
      float frame[]{.25f,.25f},key[]{n<driven?1.f:0.f,n<driven?1.f:0.f};ok&=effect.process(frame,1,key);
      const double k=n<driven?n+1:n-driven+1;
      const double envelope=n<driven?d*(1-std::pow(a,k)):std::pow(a,k)*previous+d*(1-a)*r*(std::pow(r,k)-std::pow(a,k))/(r-a);
      maximum=std::max(maximum,std::abs(frame[0]-.25*amp(-envelope)));
    }audited(ok);
  }
  check(maximum<3e-8,"Static knees/ratios and complete attack/release trajectories match independent closed forms");
  std::cout<<"Compressor static/closed-form envelope maximum "<<maximum<<'\n';
}
static void rmsAndLink() {
  constexpr uint32_t rate=48000,count=1024;NativeEffect effect(Compressor,rate);
  effect.parameter(9,1);effect.parameter(7,1);effect.parameter(6,0);effect.parameter(1,-30);effect.parameter(3,.1f);
  const double q=std::exp(-1/(.01*rate)),a=std::exp(-1000/(double(.1f)*rate));double maximum=0;
  // Alternating +/-1 has independently known finite-window exponential power.
  // Evaluate the attack convolution directly, rather than duplicating its state.
  std::vector<double> desired(count);
  for(uint32_t n=0;n<count;++n) desired[n]=reduction(10*std::log10(1-std::pow(q,n+1)),-30,4,0);
  for(uint32_t n=0;n<count;++n) {
    float frame[]{.25f,.25f},key[]{n%2?1.f:-1.f,n%2?1.f:-1.f};tracker_audit_begin();bool ok=effect.process(frame,1,key);audited(ok);
    double envelope=0;for(uint32_t j=0;j<=n;++j)envelope+=(1-a)*std::pow(a,n-j)*desired[j];
    maximum=std::max(maximum,std::abs(frame[0]-.25*amp(-envelope)));
  }
  check(maximum<3e-8,"RMS power and gain smoothing match independent finite convolution");
  for(float link:{0.f,25.f,100.f}) {
    NativeEffect e(Compressor,rate);e.parameter(9,1);e.parameter(8,link);e.parameter(6,0);e.parameter(3,.1f);
    float frame[2];bool ok=true;tracker_audit_begin();for(uint32_t n=0;n<4800;++n) {frame[0]=frame[1]=.25f;float key[]{1,0};ok&=e.process(frame,1,key);}audited(ok);
    const double left=.25*amp(-13.5),right=.25+(left-.25)*link/100;
    check(std::abs(frame[0]-left)<3e-8 && std::abs(frame[1]-right)<3e-8,"Stereo link blends independent gains with max-key linked gain");
  }
  std::cout<<"Compressor RMS reference maximum "<<maximum<<'\n';
}
static void gates() {
  double maximum=0;
  for(uint32_t rate:{8000u,44100u,48000u,96000u,192000u,384000u}) {
    NativeEffect gate(Gate,rate),duck(Gate,rate);
    for(auto *e:{&gate,&duck}) {e->parameter(9,1);e->parameter(1,-20);e->parameter(3,2);e->parameter(4,10);e->parameter(16,20);e->parameter(17,-40);e->parameter(8,0);}
    duck.parameter(19,1);
    const uint32_t strong=rate/100,middle=rate/100,hold=uint32_t(std::ceil(rate*.02)),fall=strong+middle+hold;
    const double a=std::exp(-1/(.002*rate)),r=std::exp(-1/(.01*rate));bool ok=true;tracker_audit_begin();
    for(uint32_t n=0;n<rate/10;++n) {
      float g[]{.5f,.5f},d[]{.5f,.5f},key[]{n<strong?.2f:n<strong+middle?.08f:.01f,0};
      ok&=gate.process(g,1,key)&&duck.process(d,1,key);
      const double open=n<fall?1-std::pow(a,n+1):(1-std::pow(a,fall))*std::pow(r,n-fall+1);
      const double expected=.5*(.01+.99*open);
      maximum=std::max(maximum,std::abs(g[0]-expected));
      check(std::abs(g[1]-.005)<1e-9 && std::abs(d[1]-.5)<1e-9,"Independent gate and duck retain their closed-channel floors");
      check(std::abs(g[0]+d[0]-.505)<5e-8,"Gate and duck are complementary with the same envelope");
    }audited(ok);
    NativeEffect silent(Gate,rate);silent.parameter(9,1);float closed[]{1,-1};check(silent.process(closed,1)&&closed[0]==0&&closed[1]==0,"Minimum floor is exact initial silence with missing external key");
  }
  check(maximum<3e-8,"Gate hysteresis, exact hold duration and opening/closing curves match independent closed forms");
  std::cout<<"Gate envelope reference maximum "<<maximum<<'\n';
}
static void filteredAndBypass() {
  double maximum=0;
  for(const char *id:{Compressor,Gate,Bus}) for(uint32_t rate:{8000u,48000u,384000u}) {
    NativeEffect effect(id,rate);const uint32_t latency=uint32_t(std::llround(effect.latency()*rate));std::vector<std::array<double,2>> delayed;effect.parameter(9,1);effect.parameter(12,1);effect.parameter(13,1);effect.parameter(10,300);effect.parameter(11,1700);
    std::array<FilterReference::Reference,2> hp{{{FilterShape::HighPass,300,std::sqrt(.5),0,double(rate)},{FilterShape::HighPass,300,std::sqrt(.5),0,double(rate)}}};
    std::array<FilterReference::Reference,2> lp{{{FilterShape::LowPass,1700,std::sqrt(.5),0,double(rate)},{FilterShape::LowPass,1700,std::sqrt(.5),0,double(rate)}}};
    for(uint32_t n=0;n<rate/20;++n) {
      float key[]{float(.3*std::sin(n*.037)),float(.2*std::cos(n*.017))},frame[]{.75f,-.5f};
      tracker_audit_begin();bool ok=effect.process(frame,1,key);audited(ok);
      if(n<latency)check(effect.meters()->reductionDB[0]==0&&effect.meters()->reductionDB[1]==0,"Initial detector listening reports no reduction during lookahead pre-roll");
      delayed.push_back({lp[0].process(hp[0].process(key[0])),lp[1].process(hp[1].process(key[1]))});
      for(size_t c=0;c<2;++c)maximum=std::max(maximum,std::abs(frame[c]-(n<latency?0:delayed[n-latency][c])));
    }
    auto readings=effect.meters();check(readings && readings->reductionDB[0]==0 && readings->reductionDB[1]==0,"Detector listening reports no applied gain reduction");
    NativeEffect bypass(id,rate);bypass.parameter(0,0);for(uint32_t n=0;n<=latency;++n){float dry[]{.375f,-.7f};check(bypass.process(dry,1)&&dry[0]==(n<latency?0:.375f)&&dry[1]==(n<latency?0:-.7f),"Enabled off is exact dry at reported latency");}
    NativeEffect mixed(id,rate);mixed.parameter(14,0);for(uint32_t n=0;n<=latency;++n){float input[]{.375f,-.7f};check(mixed.process(input,1)&&input[0]==(n<latency?0:.375f)&&input[1]==(n<latency?0:-.7f),"Zero wet mix is exact delayed dry");}
    check(mixed.latency()==(mixed.definition().kind==EffectKind::BusCompressor?std::ceil(rate*.005)/rate:0)&&mixed.tail()==0,"Gain-only dynamics reports its specified lookahead with no additional audible tail");
    float poison[]{NAN,0},dry[]{0,0};check(!effect.process(dry,1,poison),"Invalid key input rejects");
  }
  check(maximum<3e-8,"Detector listening matches independent high/low-pass responses without filtering program audio");
  std::cout<<"Dynamics detector filter reference maximum "<<maximum<<'\n';
}
static void hostingAndMeters() {
  for(const char *id:{Compressor,Gate,Bus}) for(uint32_t rate:{44100u,48000u,96000u}) {
    NativeEffect settings(id,rate);settings.parameter(9,1);settings.parameter(13,1);
    PluginState state{descriptor(id)};state.auxiliaryInputs={1};state.state=settings.state();
    auto render=[&](uint32_t block,bool offline) {
      NativePlugin plugin(state,rate,offline);plugin.prepareMusicalAutomation();plugin.schedule(5,12,37);plugin.schedule(1,-24,113);plugin.schedule(5,0,4097);
      check(plugin.buses().size()==3 && plugin.buses().back().index==1 && plugin.buses().back().active,"Built-in detector input uses native bus1");
      std::vector<float> output(16384),keys(16384);for(size_t i=0;i<keys.size();++i)keys[i]=float(.15*std::sin(i*.137));
      for(uint32_t n=0;n<8192;n+=block) {
        const uint32_t count=std::min(block,8192-n);std::fill_n(output.data()+n*2,count*2,.5f);
        const PluginAudioInput key{1,keys.data()+n*2};tracker_audit_begin();bool ok=plugin.process(output.data()+n*2,count,n,{&key,1});audited(ok);
      }
      const auto delay=uint32_t(std::llround(plugin.latency()*rate));for(uint32_t n=0;n<8192;++n)for(size_t c=0;c<2;++c)check(output[2*n+c]==(n<delay?0:keys[2*(n-delay)+c]),"Sidechain offsets and reported lookahead remain exact through sample-timed block splits");
      auto met=plugin.meters();check(met && met->reductionDB[0]==0 && met->detectorDB[0]>-40,"Atomic native meter readings follow external detector");
      return output;
    };
    const auto baseline=render(128,false);for(uint32_t block:{17u,512u,4096u})check(baseline==render(block,true),"Host input and automation are exact across live/offline partitions");
    NativePlugin missing(state,rate);float zero[]{1,1};check(missing.process(zero,1,0)&&zero[0]==0&&zero[1]==0,"Activated but unrouted external input is silent");
    auto disabled=state;disabled.auxiliaryInputs.clear();NativePlugin inactive(disabled,rate);float buffer[]{1,1},key[]{1,1};PluginAudioInput source{1,key};check(!inactive.process(buffer,1,0,{&source,1}),"Inactive auxiliary input rejects supplied buffers");
    check(inactive.process(buffer,1,0)&&buffer[0]==0&&buffer[1]==0,"External selection with disabled bus remains silent, never silently falls back");
  }
  for(const char *id:{Compressor,"resonance.gainer.v1"}) {
    PluginState bad{descriptor(id)};bad.auxiliaryInputs={2};bool rejected=false;try{NativePlugin p(bad,48000);}catch(const std::invalid_argument &){rejected=true;}check(rejected,"Invalid builtin detector bus rejects");
  }
}
static void extremesAndTail() {
  double peak=0,residual=0;
  for(const char *id:{Compressor,Gate,Bus}) for(uint32_t rate:{8000u,48000u,384000u}) {
    NativeEffect e(id,rate);uint32_t seed=1729;bool ok=true;tracker_audit_begin();
    for(uint32_t n=0;n<rate/2;++n) {
      if(n%37==0)for(const auto &p:e.definition().parameters){seed=seed*1664525+1013904223;ok&=e.parameter(p.id,seed&256?p.minimum:p.maximum);}
      float frame[]{float(.1*std::sin(n*.071)),float(.1*std::cos(n*.033))},key[]{float(.3*std::sin(n*.013)),float(.2*std::cos(n*.029))};
      ok&=e.process(frame,1,key);peak=std::max({peak,std::abs(double(frame[0])),std::abs(double(frame[1]))});
    }audited(ok);
    NativeEffect tail(id,rate);tail.parameter(12,1);tail.parameter(13,1);tail.parameter(10,20);tail.parameter(11,20);
    const uint32_t driven=rate/10,end=driven+uint32_t(std::ceil(tail.tail()*rate))+uint32_t(std::llround(tail.latency()*rate));check(tail.tail()>0 && tail.tail()<10,"Listening prepares a bounded filter release");
    for(uint32_t n=0;n<end+128;++n) {float frame[]{n<driven?1.f:0,n<driven?-.5f:0};check(tail.process(frame,1),"Listening tail renders");if(n>=end)residual=std::max({residual,std::abs(double(frame[0])),std::abs(double(frame[1]))});}
  }
  check(peak<4,"Interrupted controls preserve bounded dynamics gain");check(residual<1e-8,"Detector listening retains its audible filter tail");
  std::cout<<"Dynamics interrupted peak "<<peak<<", detector-listen release residual "<<residual<<'\n';
}
static void apiAndExport() {
  for(NSString *identifier in @[@"resonance.compressor.v1",@"resonance.gate.v1",@"resonance.bus-compressor.v1"]) {
    TrackerSession *session=[TrackerSession new];NSError *problem=nil;
    auto call=[&](NSString *method,NSDictionary *params,bool write=false)->NSDictionary * {
      NSMutableDictionary *p=[params mutableCopy];if(write)p[@"expectedRevision"]=session.automationRevision;
      auto result=[session automationMethod:method params:p error:&problem];if(!result)throw std::runtime_error(problem.localizedDescription.UTF8String);return result;
    };
    NSDictionary *descriptor=nil;for(NSDictionary *d in session.builtInPlugins)if([d[@"classID"] isEqual:identifier])descriptor=d;
    check(descriptor!=nil,"Native dynamics is scan-free");call(@"plugin.add",@{@"descriptor":descriptor},true);
    auto before=session.automationRevision;auto meter=call(@"plugin.meters",@{@"slot":@0});
    check([meter[@"data"][@"supported"] boolValue] && ![meter[@"data"][@"active"] boolValue] && [before isEqual:session.automationRevision],"Meter read is supported, stopped and revision-neutral");
    check([meter[@"data"][@"reductionDB"] isEqual:@[@0,@0]] && [meter[@"data"][@"detectorDB"] isEqual:@[@-160,@-160]],"Stopped metering clears stale levels");
    NSArray *buses=call(@"plugin.buses.get",@{@"slot":@0})[@"data"][@"buses"];
    check(buses.count==3 && ![buses[2][@"active"] boolValue] && [buses[2][@"supported"] boolValue],"Detector bus is optional and inactive by default");
    call(@"plugin.buses.set",@{@"slot":@0,@"inputs":@[@1]},true);
    call(@"history.undo",@{@"domain":@"plugins"},true);
    check(![call(@"plugin.buses.get",@{@"slot":@0})[@"data"][@"buses"][2][@"active"] boolValue],"Detector activation has one Undo");
    call(@"history.redo",@{@"domain":@"plugins"},true);
    const bool gate=[identifier isEqual:@"resonance.gate.v1"];
    NSArray *params=call(@"plugin.parameters.get",@{@"slot":@0})[@"data"];
    check(params.count==(gate?18:16),"Dynamics exposes all controls without irrelevant gate ratio/knee");
    if([identifier isEqual:@"resonance.bus-compressor.v1"])check([params[7][@"choices"] isEqual:@[@"Adaptive",@"Feedback",@"Feedforward"]],"Bus response choices are available through the API");
    if(gate) {NSDictionary *hold=nil;for(NSDictionary *p in params)if([p[@"id"] intValue]==16)hold=p;check([hold[@"displayScale"] isEqual:@"linear"],"Zero-inclusive hold uses a valid linear display scale");}
    NSMutableArray *edits=[@[@{@"id":@1,@"value":@-24},@{@"id":@9,@"value":@1},@{@"id":@13,@"value":@1},@{@"id":@12,@"value":@1},@{@"id":@10,@"value":@300},@{@"id":@11,@"value":@1700}] mutableCopy];
    if([identifier isEqual:@"resonance.bus-compressor.v1"])[edits addObject:@{@"id":@7,@"value":@2}];
    call(@"plugin.parameters.set",@{@"slot":@0,@"values":edits},true);
    NSDictionary *saved=call(@"plugin.state.get",@{@"slot":@0})[@"data"];
    call(@"history.undo",@{@"domain":@"plugins"},true);check([call(@"plugin.parameters.get",@{@"slot":@0})[@"data"][1][@"value"] floatValue]==(gate?-36:-18),"One Undo restores entire dynamics batch");
    call(@"history.redo",@{@"domain":@"plugins"},true);
    call(@"mixer.enable",@{},true);NSDictionary *mix=call(@"mixer.get",@{})[@"data"];
    NSString *instance=mix[@"plugins"][0][@"id"];
    NSString *source=mix[@"buses"][1][@"id"],*target=mix[@"buses"][0][@"id"];
    // Buses are channel ordered, followed by Master. Route external detector to
    // one track; activation and the receiving instance must survive reopening.
    call(@"mixer.bus.set",@{@"bus":target,@"inserts":@[instance]},true);
    call(@"mixer.sidechains.set",@{@"plugin":instance,@"input":@1,@"sources":@[@{@"source":source,@"gainDB":@0,@"preFader":@YES,@"enabled":@YES}]},true);
    NSString *base=[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString],*project=[base stringByAppendingString:@".resonance"];
    check([session savePath:project error:&problem],"Dynamics and detector graph save");TrackerSession *restored=[TrackerSession new];check([restored openPath:project error:&problem],"Dynamics and detector graph reopen");
    check([[[restored automationMethod:@"plugin.state.get" params:@{@"slot":@0} error:&problem][@"data"][@"data"] description] isEqual:[saved[@"data"] description]],"Dynamics targets persist exactly");
    check([[restored automationMethod:@"plugin.buses.get" params:@{@"slot":@0} error:&problem][@"data"][@"buses"][2][@"active"] boolValue],"Native detector input activation persists");
    check([[restored automationMethod:@"mixer.get" params:@{} error:&problem][@"data"][@"sidechains"] count]==1,"Native detector routing persists");
    [[NSFileManager defaultManager]removeItemAtPath:project error:nil];
    // Independently verify saved WAV without external graph by processing the
    // default song through an internal detector-listen filter pair.
    TrackerSession *plain=[TrackerSession new];NSString *dry=[base stringByAppendingString:@".dry.wav"],*wet=[base stringByAppendingString:@".wet.wav"];
    check([TrackerSession exportData:plain.serializedData path:dry error:&problem],"Plain audio exports");
    session=plain;call(@"plugin.add",@{@"descriptor":descriptor},true);
    call(@"plugin.parameters.set",@{@"slot":@0,@"values":@[@{@"id":@13,@"value":@1},@{@"id":@12,@"value":@1},@{@"id":@10,@"value":@300},@{@"id":@11,@"value":@1700}]},true);
    check([session savePath:project error:&problem] && [restored openPath:project error:&problem],"Internal detector project reopens");
    check([TrackerSession exportData:restored.serializedData path:wet error:&problem],"Saved detector listen audio exports");
    NSData *a=[NSData dataWithContentsOfFile:dry],*b=[NSData dataWithContentsOfFile:wet];check(b.length>a.length,"Detector listen exports its audible filter release");
    double maximum=0;
    for(size_t c=0;c<2;++c) {
      FilterReference::Reference hp(FilterShape::HighPass,300,std::sqrt(.5),0,48000),lp(FilterShape::LowPass,1700,std::sqrt(.5),0,48000);
      for(size_t n=0;n<(b.length-44)/8;++n) {float original=0,actual;if(n<(a.length-44)/8)std::memcpy(&original,static_cast<const char *>(a.bytes)+44+(n*2+c)*4,4);std::memcpy(&actual,static_cast<const char *>(b.bytes)+44+(n*2+c)*4,4);maximum=std::max(maximum,std::abs(actual-lp.process(hp.process(original))));}
    }
    check(maximum<3e-8,"Entire saved WAV and tail match independent detector-filter reference");std::cout<<identifier.UTF8String<<" saved WAV reference maximum "<<maximum<<'\n';
    for(NSString *path in @[dry,wet,project])[[NSFileManager defaultManager]removeItemAtPath:path error:nil];
  }
}
static float programSample(uint32_t n) { return float(.2*std::sin(n*.031)+.04*std::cos(n*.113)); }
static float keySample(uint32_t n) { return n%1700<570?.6f:.001f; }
static void graphSidechains() {
  for(const char *id:{Compressor,Gate,Bus}) for(uint32_t rate:{44100u,48000u,96000u}) {
    NativeEffect settings(id,rate);settings.parameter(9,1);settings.parameter(3,.5f);settings.parameter(1,-20);
    PluginState state{descriptor(id)};state.state=settings.state();state.auxiliaryInputs={1};state.instanceID="target";
    MixerGraph graph;graph.buses={{1,20,MixerBusKind::Track,"Program"},{2,20,MixerBusKind::Track,"Key only"},{20,0,MixerBusKind::Master,"Master"}};
    graph.buses[1].gainDB=-96;graph.buses[0].inserts={"prefix","target"};graph.buses[1].inserts={"key-delay"};graph.sidechains={{2,"target",1,0,true,true}};
    const auto latency=uint32_t(std::llround(settings.latency()*rate));
    std::vector<MixerProcessorInfo> processors{{"prefix",32,0},{"target",latency,0},{"key-delay",128,0}};processors[1].activeInputs=2;
    const auto plan=compileMixer(graph,{1,2},processors,rate);check(plan.latency==128+latency,"Native dynamics graph aligns program with delayed detector source and lookahead");
    auto render=[&](uint32_t block,bool offline) {
      MixerRuntime runtime(graph,plan,rate);NativePlugin host(state,rate,offline);host.prepareMusicalAutomation();host.schedule(1,-30,333);host.schedule(1,-12,2077);
      struct Context {
        MixerRuntime *runtime;NativePlugin *host;std::array<float,64> prefix{};std::array<float,256> key{};size_t p=0,k=0;
        static bool process(void *raw,size_t index,float *audio,uint32_t frames,uint64_t position) noexcept {
          auto &c=*static_cast<Context *>(raw);
          if(index==1)return c.host->process(audio,frames,position,c.runtime->inputs(index));
          for(uint32_t n=0;n<frames*2;++n) {
            if(index==0){std::swap(audio[n],c.prefix[c.p]);if(++c.p==c.prefix.size())c.p=0;}
            else {std::swap(audio[n],c.key[c.k]);if(++c.k==c.key.size())c.k=0;}
          }return true;
        }
      }context{&runtime,&host};
      std::array<float,4096> input{};std::vector<float> result(16384);
      for(uint32_t pos=0;pos<8192;pos+=block) {
        const auto frames=std::min(block,8192-pos);tracker_audit_begin();runtime.begin(frames,pos);
        for(size_t bus:plan.order) {
          for(uint32_t n=0;n<frames;++n)input[n]=bus==0?programSample(pos+n):bus==1?keySample(pos+n):0;
          const auto *output=runtime.process(bus,input.data(),input.data(),Context::process,&context);
          if(bus==plan.master && output)std::copy_n(output,frames*2,result.data()+pos*2);
        }runtime.complete();audited(!runtime.failed());
      }return result;
    };
    const auto result=render(128,false);for(uint32_t block:{17u,512u,4096u})check(result==render(block,true),"Native dynamics sidechains/PDC/automation agree across every callback partition");
    NativeEffect direct(id,rate,state.state);double maximum=0;
    for(uint32_t n=0;n<8192;++n) {
      if(n==333)direct.parameter(1,-30);if(n==2077)direct.parameter(1,-12);
      const float p=n<128?0:programSample(n-128),k=n<128?0:keySample(n-128);float frame[]{p,p},key[]{k,k};check(direct.process(frame,1,key),"Qualified reference processes aligned input/key");
      maximum=std::max(maximum,std::abs(double(result[n*2])-float(frame[0]+float(amp(-96))*(n<128+latency?0:keySample(n-128-latency)))));
    }
    check(maximum<3e-8,"Actual host/graph matches separately qualified processor with independently delayed program/key streams");
  }
}
static void spectralAndWarm() {
  NativeEffect effect(Compressor,48000);effect.parameter(1,-20);effect.parameter(6,0);
  std::vector<double> out(4800);constexpr double omega=2*std::numbers::pi/48;
  for(uint32_t n=0;n<100800;++n){float f[]{float(std::sin(n*omega)),0};check(effect.process(f,1),"Steady sine compression");if(n>=96000)out[n-96000]=f[0];check(f[1]==0,"Compression cannot leak audio into a silent channel");}
  double re=0,im=0;for(size_t n=0;n<out.size();++n){re+=out[n]*std::cos(n*omega);im+=out[n]*std::sin(n*omega);}re*=2./out.size();im*=2./out.size();
  double error=0;for(size_t n=0;n<out.size();++n)error+=std::pow(out[n]-re*std::cos(n*omega)-im*std::sin(n*omega),2);
  const double distortion=std::sqrt(2*error/out.size())/std::hypot(re,im);check(distortion<.001,"Normal 1 kHz compression keeps modulation distortion below 0.1 percent in the specified steady fixture");
  for(const char *id:{Compressor,Gate,Bus}) {
    NativeEffect dry(id,48000),wet(id,48000);dry.parameter(0,0);
    for(uint32_t n=0;n<4000;++n){if(n==1000)dry.parameter(0,1);float a[]{programSample(n),programSample(n)},b[]{a[0],a[1]};check(dry.process(a,1)&&wet.process(b,1),"Warm bypass fixture renders");if(n>=1240+uint32_t(std::llround(dry.latency()*48000)))check(a[0]==b[0]&&a[1]==b[1],"Re-enabling uses continuously warm detector/envelope history");}
  }
  std::cout<<"Compressor steady 1 kHz non-fundamental ratio "<<distortion<<'\n';
}
int main() { @autoreleasepool { try {
  curvesAndTime();rmsAndLink();gates();filteredAndBypass();hostingAndMeters();extremesAndTail();apiAndExport();graphSidechains();spectralAndWarm();
  std::cout<<"PASS Compressor/Gate/Bus shared integration and Compressor/Gate independent curves, envelope timing, RMS, stereo linking, hysteresis/hold/duck, detector filters, native sidechains/offsets, meters, extremes/tails and realtime audit\n";return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;} } }
