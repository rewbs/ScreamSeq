#include "editor/NativeEffects.hpp"
#include "editor/TrackerDocument.hpp"
#include "../Audio/AudioUnitHost.hpp"
#include "../Audio/AudioExport.hpp"
#include "FilterReference.hpp"
#include "OversamplingReference.hpp"
#import "../Bridge/TrackerSession.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <numbers>
using namespace Tracker;
static constexpr auto ID="resonance.cabinet-simulator.v1";
static void check(bool ok,const char *message) { if(!ok) throw std::runtime_error(message); }
#ifdef TRACKER_SANITIZER
static void tracker_audit_begin() {}
static void tracker_audit_end(uint64_t *a,uint64_t *f,uint64_t *l) { *a=*f=*l=0; }
#else
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *,uint64_t *,uint64_t *);
#endif
static void audited(bool ok=true) { uint64_t a,f,l;tracker_audit_end(&a,&f,&l);check(ok,"Cabinet processing succeeds");check(!(a+f+l),"Cabinet has no render allocation/free/lock"); }
static void process(NativeEffect &effect,std::vector<float> &audio,uint32_t block=128) {
  for(uint32_t n=0;n<audio.size()/2;n+=block) { tracker_audit_begin();bool ok=effect.process(audio.data()+n*2,std::min<uint32_t>(block,audio.size()/2-n));audited(ok); }
}
static std::vector<float> signal(size_t count) {
  std::vector<float> out(count*2);for(size_t i=0;i<count;++i) { out[i*2]=float(.3*std::sin(i*.371)+.1*std::cos(i*.132));out[i*2+1]=float(.2*std::sin(i*.491)); }return out;
}
// Independent direct-form biquads followed by offline fractional-tap convolution.
// Uses only the authored model data, never production coefficients/history.
static std::vector<double> voicing(const std::vector<double> &input,const CabinetModel &m,double rate) {
  using FilterReference::Reference;
  std::array<Reference,7> filters{{
    {FilterShape::HighPass,m.lowCut,std::sqrt(.5),0,rate},
    {FilterShape::LowShelf,140,std::sqrt(.5),m.lowShelf,rate},
    {FilterShape::Bell,m.bodyFrequency,m.bodyQ,m.bodyGain,rate},
    {FilterShape::Bell,m.coneFrequency,m.coneQ,m.coneGain,rate},
    {FilterShape::Bell,m.biteFrequency,m.biteQ,m.biteGain,rate},
    {FilterShape::LowPass,m.highCut,1/(2*std::cos(std::numbers::pi/8)),0,rate},
    {FilterShape::LowPass,m.highCut,1/(2*std::cos(3*std::numbers::pi/8)),0,rate}}};
  auto filtered=input;
  for(auto &x:filtered) for(auto &f:filters) x=f.process(x);
  auto output=filtered;double norm=1;for(double g:m.reflectionGain) norm+=std::abs(g);
  for(size_t n=0;n<output.size();++n) {
    for(size_t t=0;t<3;++t) {
      const double delay=std::max(1.,m.reflectionMS[t]*rate/1000),fraction=delay-std::floor(delay);const auto whole=size_t(delay);
      if(n>=whole) output[n]+=m.reflectionGain[t]*(1-fraction)*filtered[n-whole];
      if(n>whole) output[n]+=m.reflectionGain[t]*fraction*filtered[n-whole-1];
    }
    output[n]/=norm;
  }
  return output;
}
static double magnitude(const std::vector<double> &impulse,double frequency,double rate) {
  double re=0,im=0;for(size_t n=0;n<impulse.size();++n) { const double a=2*std::numbers::pi*frequency*n/rate;re+=impulse[n]*std::cos(a);im-=impulse[n]*std::sin(a); }return std::hypot(re,im);
}
static void models() {
  check(cabinetModels().size()==18,"Eighteen named cabinet choices");double error=0,peak=0;
  for(double rate:{8000.,44100.,48000.,96000.,192000.,384000.}) for(uint32_t m=0;m<18;++m) {
    const auto count=size_t(rate*.08);std::vector<double> impulse(count);impulse[0]=1;
    auto ref=voicing(impulse,cabinetModels()[m],rate);CabinetVoicing actual(rate);actual.model(m);
    tracker_audit_begin();bool good=true;
    for(size_t n=0;n<count;++n) { auto y=actual.process({n==0?1.:0.,0});error=std::max(error,std::abs(y[0]-ref[n]));good&=y[1]==0; }
    audited(good);
  }
  check(error<2e-10,"All cabinet impulse responses match independent filter/convolution reference");
  // Verify useful bandwidth and genuinely distinct authored responses.
  std::vector<double> impulse(48000);impulse[0]=1;std::vector<std::vector<double>> responses;
  for(const auto &m:cabinetModels()) {
    auto r=voicing(impulse,m,48000);check(magnitude(r,5,48000)<.05,"Cabinets remove subsonic energy");
    check(magnitude(r,18000,48000)<.025,"Cabinets suppress ultrasonic-side content");
    check(magnitude(r,1000,48000)>.08,"Cabinets retain useful midband");
    for(const auto &other:responses) { double distance=0;for(size_t i=0;i<r.size();++i) distance+=std::abs(r[i]-other[i]);check(distance>.05,"All model impulse responses are distinct"); }
    responses.push_back(std::move(r));
  }
  CabinetVoicing changing(48000);tracker_audit_begin();bool good=true;
  for(uint32_t n=0;n<48000;++n) { if(n%37==0) changing.model((n/37)%18,240);auto y=changing.process({.1*std::sin(n*.077),.1*std::cos(n*.131)});for(double x:y) {good&=std::isfinite(x);peak=std::max(peak,std::abs(x));} }
  audited(good);check(peak<3,"Interrupted model morphs stay bounded");
  for(double invalid:{0.,-1.,7999.,384001.,double(INFINITY),double(NAN)}) { bool rejected=false;try { CabinetVoicing bad(invalid); }catch(const std::invalid_argument &) {rejected=true;}check(rejected,"Invalid voicing rate rejects before allocation"); }
  std::cout<<"Cabinet models: independent error "<<error<<", interrupted morph peak "<<peak<<'\n';
}
static std::vector<double> preampReference(std::vector<double> audio,double rate,bool automate) {
  using namespace OversamplingReference;
  const uint32_t stages=rate<=48000?4:rate<=96000?3:rate<=192000?2:1,factor=1u<<stages;
  const uint32_t latency=stages==4?90:stages==3?88:stages==2?84:72;
  std::vector<std::vector<double>> filters;
  for(uint32_t stage=0;stage<stages;++stage) {
    filters.push_back(coefficients(stage==0?145:stage==1?49:33,stage==0?11:13));
    std::vector<double> up(audio.size()*2);for(size_t n=0;n<audio.size();++n) up[n*2]=audio[n]*2;audio=convolution(up,filters.back());
  }
  const double bias=std::tanh(.2);
  for(size_t n=0;n<audio.size();++n) {
    const double progress=automate?std::clamp((double(n)-(400+latency/2)*factor+1)/(std::ceil(rate*.005)*factor),0.,1.):0;
    const double drive=automate?1+(std::pow(10.,36./20)-1)*progress:std::pow(10.,12./20);
    audio[n]=(std::tanh(audio[n]*drive+.2)-bias)/(1-bias*bias);
  }
  for(int stage=int(stages)-1;stage>=0;--stage) { const auto down=convolution(audio,filters[stage]);audio.resize(down.size()/2);for(size_t n=0;n<audio.size();++n) audio[n]=down[n*2]; }
  const double pole=std::exp(-2*std::numbers::pi*5/rate);double previous=0,memory=0;
  for(auto &x:audio) { const auto y=(x-previous)*(1+pole)*.5+pole*memory;previous=x;memory=y;x=y; }return audio;
}
static void preamps() {
  double error=0;const auto input=signal(2048);
  for(uint32_t rate:{8000u,44100u,48000u,96000u,192000u,384000u}) for(bool automate:{false,true}) {
    NativeEffect actual(ID,rate);actual.parameter(9,0);actual.parameter(7,0);actual.parameter(3,automate?0:12);
    auto out=input;if(automate) {check(actual.process(out.data(),400),"Start preamp automation");tracker_audit_begin();bool ok=actual.parameter(3,36)&&actual.process(out.data()+800,1648);audited(ok);}else process(actual,out,17);
    for(size_t c=0;c<2;++c) { std::vector<double> mono(2048);for(size_t n=0;n<mono.size();++n) mono[n]=input[n*2+c];auto ref=preampReference(mono,rate,automate);for(size_t n=0;n<ref.size();++n) error=std::max(error,std::abs(out[n*2+c]-ref[n])); }
  }
  check(error<1e-7,"Asymmetric preamp and drive automation match independent high-rate convolution at every factor");
  std::cout<<"Cabinet preamp reference error "<<error<<'\n';
}
static void routingAndTiming() {
  const auto input=signal(4096);double linearError=0;
  for(uint32_t rate:{8000u,44100u,48000u,96000u,192000u,384000u}) {
    const uint32_t delay=rate<=48000?90:rate<=96000?88:rate<=192000?84:72;
    CabinetSimulator processor(rate);check(processor.latencyFrames()==delay,"Adaptive latency agrees with resampler");
    for(uint32_t route=0;route<6;++route) {
      NativeEffect bypass(ID,rate);bypass.parameter(0,0);bypass.parameter(2,route);auto dry=input;process(bypass,dry,17);
      for(size_t i=0;i<dry.size();++i) check(dry[i]==(i<delay*2?0:input[i-delay*2]),"Disabled device is exact latency-matched dry");
      NativeEffect settings(ID,rate);settings.parameter(8,0);settings.parameter(9,0);settings.parameter(7,0);settings.parameter(2,route);
      NativeEffect identity(ID,rate,settings.state());check(identity.tail()==0,"Restored linear identity has no tail");auto flat=input;process(identity,flat,137);check(flat==dry,"Disabled stages and flat EQ form exact delayed identity");
      NativeEffect cabinet(ID,rate);cabinet.parameter(8,0);cabinet.parameter(7,0);cabinet.parameter(2,route);cabinet.parameter(1,6);cabinet.parameter(11,6);cabinet.parameter(12,1.2f);
      auto out=input;process(cabinet,out,137);
      for(size_t c=0;c<2;++c) {
        std::vector<double> mono(4096);for(size_t n=0;n<mono.size();++n) mono[n]=input[n*2+c];auto ref=voicing(mono,cabinetModels()[6],rate);
        FilterReference::Reference eq(FilterShape::Bell,80,double(1.2f),6,rate);for(auto &x:ref) x=eq.process(x);
        for(size_t n=0;n<ref.size();++n) linearError=std::max(linearError,std::abs(out[n*2+c]-(n<delay?0:ref[n-delay])));
      }
    }
    // Pure delay should commute with either independently automated linear stage
    // when all downstream parameter events receive the corresponding delay.
    for(bool eqOnly:{false,true}) {
      std::vector<float> baseline;
      for(uint32_t route=0;route<6;++route) {
        NativeEffect actual(ID,rate);actual.parameter(8,0);actual.parameter(7,0);actual.parameter(2,route);if(eqOnly) actual.parameter(9,0);
        auto out=input;check(actual.process(out.data(),173),"Start delayed-stage automation");
        tracker_audit_begin();bool ok=true;
        for(uint32_t n=0;n<3500;++n) ok&=actual.parameter(eqOnly?11:1,eqOnly?float(n%37)-18:float(n%18));
        ok&=actual.parameter(eqOnly?11:1,eqOnly?9:12)&&actual.process(out.data()+346,3923);audited(ok);
        if(route==0) baseline=out;else for(size_t i=0;i<out.size();++i) check(std::abs(out[i]-baseline[i])<2e-7,"Deferred stage automation remains input-aligned through all routes and large same-frame batches");
      }
    }
  }
  check(linearError<1e-7,"All six linear routes agree with independent cabinet/EQ convolution and explicit latency");
  // Wet mono must be mono even while old stereo histories drain; dry stays stereo.
  NativeEffect mono(ID,48000);mono.parameter(4,1);auto out=input;process(mono,out);for(size_t n=0;n<out.size()/2;++n) check(out[n*2]==out[n*2+1],"Wet mono is exact");
  NativeEffect dry(ID,48000);dry.parameter(4,1);dry.parameter(5,100);dry.parameter(6,0);dry.parameter(7,0);out=input;process(dry,out);for(size_t i=0;i<out.size();++i) check(out[i]==(i<180?0:input[i-180]),"Mono switch preserves independent stereo dry");
  std::cout<<"Cabinet six-route linear reference error "<<linearError<<'\n';
}
static void extremesAndBudget() {
  double peak=0,residual=0;
  for(uint32_t rate:{8000u,48000u,384000u}) {
    NativeEffect effect(ID,rate);uint32_t seed=1729;bool ok=true;tracker_audit_begin();
    for(uint32_t n=0;n<24000;++n) {
      if(n%37==0) for(const auto &p:effect.definition().parameters) {seed=seed*1664525+1013904223;ok&=effect.parameter(p.id,seed&256?p.minimum:p.maximum);}
      float f[]{float(.1*std::sin(n*.077)),float(.1*std::cos(n*.131))};ok&=effect.process(f,1);peak=std::max({peak,std::abs(double(f[0])),std::abs(double(f[1]))});
    }
    audited(ok);
  }
  check(peak<1000,"Rapid extreme controls stay finite and bounded without clipping output");
  // High-Q low-frequency EQ release, preamp DC and cabinet resonances may be in
  // any order. Verify the prepared conservative tail instead of truncating it.
  for(uint32_t route:{0u,2u,3u,4u}) {
    NativeEffect effect(ID,48000);effect.parameter(2,route);effect.parameter(3,36);effect.parameter(7,24);effect.parameter(1,8);
    for(uint32_t b=0;b<5;++b) {effect.parameter(10+4*b,20);effect.parameter(11+4*b,18);effect.parameter(12+4*b,12);}
    const auto tail=effect.tail();check(tail>1&&tail<=60,"Combined cabinet/EQ tail is prepared and bounded");
    const auto end=48000u+uint32_t(std::ceil(tail*48000));bool ok=true;tracker_audit_begin();
    for(uint32_t n=0;n<end+512;++n) {float f[]{n<48000?float(.001*std::sin(2*std::numbers::pi*20*n/48000)):0,n<48000?.001f:0};ok&=effect.process(f,1);if(n>=end) residual=std::max({residual,std::abs(double(f[0])),std::abs(double(f[1]))});}audited(ok);
  }
  check(residual<1e-8,"Tail preserves boosted resonant release below -160 dB");
  NativeEffect bench(ID,48000);std::array<float,256> block{};const auto start=std::chrono::steady_clock::now();
  for(uint32_t i=0;i<375;++i) {for(size_t n=0;n<block.size();++n) block[n]=.1f;check(bench.process(block.data(),128),"Benchmark renders");}
  std::cout<<"Cabinet interrupted peak "<<peak<<", release residual "<<residual<<", six warm routes CPU "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()*100<<"% of one 48 kHz core (informational)\n";
}
static PluginDescriptor descriptor() {
  for (const auto &d : NativePlugin::builtins()) if (d.classID == "resonance.cabinet-simulator.v1") return d;
  throw std::runtime_error("Missing cabinet descriptor");
}
static void compensation() {
  auto doc = Document::demo();
  doc->annotate([](NativeSong &n) {
    const auto master = n.makeEntity().id;
    for (const auto &[index, track] : n.tracks) n.mixer.buses.push_back({track.id, master, MixerBusKind::Track, "Track"});
    n.mixer.buses.push_back({master, 0, MixerBusKind::Master, "Master"});
  });
  for (uint32_t rate : {44100u, 48000u, 96000u}) {
    const uint32_t delay=rate<=48000?90:88;
    NativeEffect disabled("resonance.cabinet-simulator.v1", rate); disabled.parameter(0, 0);
    PluginState state{descriptor()}; state.instanceID = "cabinet"; state.state = disabled.state();
    auto render = [&](bool insert, uint32_t block) {
      auto native = doc->native(); if (insert) native.mixer.buses[0].inserts = {"cabinet"};
      Renderer renderer(doc->serialize(), rate); PluginChain chain(insert ? std::vector<PluginState>{state} : std::vector<PluginState>{}, rate, true);
      chain.attachInstruments(renderer, &native);
      check(chain.latency() == (insert ? double(delay) / rate : 0), "Mixer includes the exact oversampling delay");
      std::vector<float> out(16384);
      for (uint32_t n = 0; n < 8192; n += block) {
        const auto count = std::min(block, 8192 - n); tracker_audit_begin(); chain.syncTransport(renderer);
        renderer.render(out.data() + n * 2, count); bool ok = chain.process(out.data() + n * 2, count); audited(ok);
      }
      return out;
    };
    const auto baseline = render(false, 128);
    for (uint32_t block : {17u, 128u, 4096u}) {
      const auto out = render(true, block);
      for (size_t i = 0; i < out.size(); ++i) check(std::abs(out[i] - (i < delay*2 ? 0 : baseline[i - delay*2])) < 2e-7, "Parallel mixer tracks align with the delayed cabinet insert");
    }
  }
}
static void apiAndExport() {
  TrackerSession *session=[TrackerSession new];NSError *problem=nil;
  auto call=[&](NSString *method,NSDictionary *params,bool write=false)->NSDictionary * {
    NSMutableDictionary *p=[params mutableCopy];if(write)p[@"expectedRevision"]=session.automationRevision;
    NSDictionary *result=[session automationMethod:method params:p error:&problem];if(!result)throw std::runtime_error(problem.localizedDescription.UTF8String);return result;
  };
  NSDictionary *effect=nil;for(NSDictionary *d in session.builtInPlugins)if([d[@"classID"] isEqual:@"resonance.cabinet-simulator.v1"])effect=d;
  check(effect!=nil,"Cabinet is available without scanning");
  NSString *base=[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
  NSString *plain=[base stringByAppendingString:@".plain.wav"],*wet=[base stringByAppendingString:@".wet.wav"],*project=[base stringByAppendingString:@".resonance"];
  check([TrackerSession exportData:session.serializedData path:plain error:&problem],"Plain reference exports");
  call(@"plugin.add",@{@"descriptor":effect},true);
  NSArray *parameters=call(@"plugin.parameters.get",@{@"slot":@0})[@"data"];
  check(parameters.count==30 && [parameters[1][@"choices"] count]==18 && [parameters[2][@"choices"] count]==6,"API exposes all models, orders and thirty parameters");
  check([parameters[10][@"unitLabel"] isEqual:@"Hz"] && [parameters[10][@"displayScale"] isEqual:@"logarithmic"] && [parameters[29][@"choices"] count]==6,"All five EQ bands have complete metadata");
  call(@"plugin.parameters.set",@{@"slot":@0,@"values":@[@{@"id":@1,@"value":@6},@{@"id":@2,@"value":@5},@{@"id":@7,@"value":@-3},@{@"id":@8,@"value":@0},@{@"id":@11,@"value":@6},@{@"id":@12,@"value":@1.2}]},true);
  NSDictionary *saved=call(@"plugin.state.get",@{@"slot":@0})[@"data"];
  call(@"history.undo",@{@"domain":@"plugins"},true);check([call(@"plugin.parameters.get",@{@"slot":@0})[@"data"][1][@"value"] floatValue]==2,"One Undo restores entire cabinet batch");
  call(@"history.redo",@{@"domain":@"plugins"},true);
  check([session savePath:project error:&problem],"Cabinet saves");TrackerSession *restored=[TrackerSession new];check([restored openPath:project error:&problem],"Cabinet reopens");
  check([[restored automationMethod:@"plugin.state.get" params:@{@"slot":@0} error:&problem][@"data"][@"data"] isEqual:saved[@"data"]],"All cabinet targets persist exactly");
  check([TrackerSession exportData:restored.serializedData path:wet error:&problem],"Saved cabinet project exports");
  NSData *a=[NSData dataWithContentsOfFile:plain],*b=[NSData dataWithContentsOfFile:wet];check(b.length>a.length && b.length<a.length+48000*8*3,"Cabinet export retains bounded tail");
  const auto outputFrames=(b.length-44)/8;double maximum=0,energy=0;
  for(size_t c=0;c<2;++c) {
    std::vector<double> mono(outputFrames+90);for(size_t n=0;n<(a.length-44)/8;++n) {float x;std::memcpy(&x,static_cast<const char *>(a.bytes)+44+(n*2+c)*4,4);mono[n]=x;}
    FilterReference::Reference eq(FilterShape::Bell,80,double(1.2f),6,48000);for(auto &x:mono)x=eq.process(x);
    const auto reference=voicing(mono,cabinetModels()[6],48000);const double gain=std::pow(10.,-3./20);
    // Export removes the fixed delay. Independent reference has no inserted
    // resampler delay because the disabled preamp is a pure delayed identity.
    for(size_t n=0;n<outputFrames;++n) {float actual;std::memcpy(&actual,static_cast<const char *>(b.bytes)+44+(n*2+c)*4,4);maximum=std::max(maximum,std::abs(actual-reference[n]*gain));energy+=std::abs(actual);}
  }
  check(energy>1 && maximum<1e-7,"Saved WAV including release matches independent cabinet/EQ reference with latency removed");
  std::cout<<"Cabinet saved WAV independent reference error "<<maximum<<'\n';
  for(NSString *path in @[plain,wet,project])[[NSFileManager defaultManager]removeItemAtPath:path error:nil];
}
static void routeCrossfades() {
  const auto input=signal(4096);std::array<std::vector<float>,6> references;
  for(uint32_t route=0;route<6;++route) {
    NativeEffect fixed(ID,48000);fixed.parameter(2,route);fixed.parameter(3,18);fixed.parameter(11,12);fixed.parameter(15,-6);
    references[route]=input;process(fixed,references[route]);
  }
  double difference=0;for(size_t i=0;i<input.size();++i)difference+=std::abs(references[0][i]-references[3][i]);
  check(difference>1,"Preamp order has a meaningful audible consequence");
  NativeEffect actual(ID,48000);actual.parameter(3,18);actual.parameter(11,12);actual.parameter(15,-6);auto output=input;
  std::array<double,6> weights{1,0,0,0,0,0},start=weights,target=weights;uint32_t transition=0;double maximum=0;
  for(uint32_t n=0;n<input.size()/2;++n) {
    const bool event=n==400 || (n>400 && (n-400)%37==0 && n<900);
    if(event) {tracker_audit_begin();bool ok=actual.parameter(2,((n-400)/37+1)%6);audited(ok);}
    float f[]{input[n*2],input[n*2+1]};tracker_audit_begin();bool ok=actual.process(f,1);audited(ok);
    // Routing weights follow their input frame by the reported 90-frame delay.
    const uint32_t t=n>=90?n-90:UINT32_MAX;
    if(t==400 || (t>400 && t<900 && (t-400)%37==0)) {start=weights;target.fill(0);target[((t-400)/37+1)%6]=1;transition=240;}
    if(transition) {const double fraction=(241-transition)/240.;for(size_t r=0;r<6;++r)weights[r]=start[r]+(target[r]-start[r])*fraction;--transition;}
    for(size_t c=0;c<2;++c) {double expected=0;for(size_t r=0;r<6;++r)expected+=weights[r]*references[r][n*2+c];maximum=std::max(maximum,std::abs(f[c]-expected));}
  }
  check(maximum<1e-7,"Interrupted route changes mix continuously warm histories with aligned weights");
  NativeEffect bypass(ID,48000),warm(ID,48000);bypass.parameter(0,0);auto a=input,b=input;check(bypass.process(a.data(),600)&&warm.process(b.data(),600),"Warm bypass starts");bypass.parameter(0,1);
  check(bypass.process(a.data()+1200,3496)&&warm.process(b.data()+1200,3496),"Warm bypass ends");for(size_t i=(600+90+240)*2;i<a.size();++i)check(a[i]==b[i],"Enabled bypass never resets warm histories");
  std::cout<<"Cabinet interrupted routing error "<<maximum<<'\n';
}
int main() { @autoreleasepool { try {
  models();preamps();routingAndTiming();extremesAndBudget();compensation();apiAndExport();routeCrossfades();
  std::cout<<"PASS Cabinet Simulator independent models/preamp/EQ, six routing orders, adaptive latency, aligned automation, exact bypass/mono, extremes, tails, and realtime audit\n";return 0;
} catch(const std::exception &e) {std::cerr<<"FAIL "<<e.what()<<'\n';return 1;} } }
