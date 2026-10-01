#include "editor/MixerRuntime.hpp"
#include "editor/hosted/SignalObservation.hpp"
#include "editor/hosted/ProcessorBypass.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <thread>
using namespace Tracker;
#ifdef TRACKER_SANITIZER
static void tracker_audit_begin() {}
static void tracker_audit_end(uint64_t *a, uint64_t *f, uint64_t *l) { *a = *f = *l = 0; }
#else
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *, uint64_t *, uint64_t *);
#endif
static void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
namespace {
struct Processor {
  std::vector<float> delay;
  size_t cursor = 0;
  uint64_t through = 0;
};
struct Effects {
  std::array<Processor, 4> processors;
  bool duplicate = false;
  Effects() { processors[0].delay.resize(64); processors[1].delay.resize(32); processors[3].delay.resize(128); }
  static bool process(void *opaque, size_t index, float *buffer, uint32_t frames, uint64_t position) noexcept {
    auto &self = *static_cast<Effects *>(opaque); auto &p = self.processors[index];
    if (p.through != position) self.duplicate = true;
    p.through = position + frames;
    for (uint32_t n = 0; n < frames * 2; ++n) {
      std::swap(buffer[n], p.delay[p.cursor]); if (++p.cursor == p.delay.size()) p.cursor = 0;
    }
    return true;
  }
};
MixerGraph graph() {
  MixerGraph g;
  g.buses = {{1, 10, MixerBusKind::Track, "Drums"}, {2, 10, MixerBusKind::Track, "Bass"},
             {10, 20, MixerBusKind::Group, "Rhythm"}, {11, 20, MixerBusKind::Return, "Space"},
             {20, 0, MixerBusKind::Master, "Master"}};
  g.buses[0].inserts = {"delay"}; g.buses[0].sends = {{11, -6, true, true}};
  g.buses[3].inserts = {"reverb"}; g.instruments = {{"synth", 2, 0}};
  return g;
}
std::vector<MixerProcessorInfo> processors() { return {{"delay", 32, 0}, {"reverb", 16, 0}, {"synth", 8, 0, true}, {"master", 64, 0}}; }
std::vector<float> render(MixerGraph graph, uint32_t block, uint32_t rate) {
  auto mixer = std::make_unique<MixerRuntime>(graph, compileMixer(graph, {1, 2}, processors(), rate), rate);
  Effects effects;
  std::array<float, 4096> left{}, right{}; std::array<float, 8192> instrument{};
  std::vector<float> output(6000 * 2);
  for (uint32_t pos = 0; pos < 6000; pos += block) {
    auto count = std::min(block, 6000 - pos);
    instrument.fill(0);
    if (pos <= 45 && pos + count > 45) instrument[(45 - pos) * 2] = instrument[(45 - pos) * 2 + 1] = 4;
    uint64_t a, f, l; tracker_audit_begin();
    mixer->begin(count, pos); mixer->instrument(2, 0, instrument.data());
    for (auto bus : mixer->plan().order) {
      left.fill(0); right.fill(0);
      if (pos <= 37 && pos + count > 37) left[37 - pos] = right[37 - pos] = bus == 0 ? 1 : bus == 1 ? 2 : bus == 4 ? 8 : 0;
      const auto *result = mixer->process(bus, left.data(), right.data(), Effects::process, &effects);
      if (bus == mixer->plan().master) std::copy_n(result, count * 2, output.data() + pos * 2);
    }
    mixer->complete(); tracker_audit_end(&a, &f, &l);
    check(a + f + l == 0 && !mixer->failed(), "Graph rendering has no realtime allocation, free, lock or fault");
  }
  check(!effects.duplicate && mixer->through() == 6000, "Each insert executes exactly once per stream sample");
  check(mixer->meters().size() == 5 && mixer->meters()[4].left > 0, "Meters are independently readable after rendering");
  return output;
}
void liveMeterRegistration() {
  auto observation=std::make_unique<SignalObservation>(48000);
  const auto first=observation->add({"first","first","First",true});
  std::atomic<bool> started{false},finished{false};std::atomic<uint32_t> latest{first};
  bool identitiesValid=true;size_t enumerations=0;
  std::thread telemetry([&]{
    do {
      size_t index=0;
      for(const auto &port:observation->ports) {
        const auto expected=index?"new/"+std::to_string(index-1):"first";
        identitiesValid&=port.key==expected && port.node==expected && port.output && port.channels==2;
        observation->read(uint32_t(++index));
      }
      ++enumerations;
    }while(!finished.load(std::memory_order_acquire));
  });
  uint64_t allocations=0,frees=0,locks=0,iterations=0;
  std::thread audio([&]{
    std::array<float,2> sample{.25f,-.5f};
    tracker_audit_begin();started.store(true,std::memory_order_release);
    do {
      observation->observe(first,sample.data(),1,iterations);
      observation->observe(latest.load(std::memory_order_acquire),sample.data(),1,iterations);
      ++iterations;
    }while(!finished.load(std::memory_order_acquire));
    tracker_audit_end(&allocations,&frees,&locks);
  });
  while(!started.load(std::memory_order_acquire))std::this_thread::yield();
  for(size_t i=0;i<1024;++i) {
    const auto key="new/"+std::to_string(i);
    latest.store(observation->add({key,key,"New return",true}),std::memory_order_release);
  }
  finished.store(true,std::memory_order_release);audio.join();telemetry.join();
  check(iterations>0 && allocations+frees+locks==0 && observation->read(first).measured && observation->ports.size()==1025,
    "Registering new return meters during observation preserves stable slots and performs no audio-thread allocation/free/lock");
  check(identitiesValid && enumerations>0,"Telemetry can enumerate immutable port identities while live bus registration publishes new meters");
}
void exactRouteTaps() {
  for(uint32_t block:{17u,512u,4096u}) {
    constexpr uint32_t total=3000;
    MixerGraph graph;graph.buses={{1,5,MixerBusKind::Track,"A"},{2,5,MixerBusKind::Track,"B"},{3,5,MixerBusKind::Return,"Pre"},{4,5,MixerBusKind::Return,"Post"},{5,0,MixerBusKind::Master,"Main"}};
    graph.buses[0].inserts={"fx"};graph.buses[0].gainDB=-6;graph.buses[0].sends={{3,-12,true},{4,-9,false}};
    graph.buses[4].inserts={"out"};graph.buses[4].gainDB=-3;
    graph.sidechains={{2,"fx",0,-10,false},{2,"fx",1,-5,true}};
    graph.instruments={{"synth",0,0},{"synth",1,1}};
    std::vector<MixerProcessorInfo> processors={{"fx",0,0,false,false,1,UINT64_MAX,2},{"out"},{"synth",0,0,true,false,2,3}};
    auto plan=compileMixer(graph,{1,2},processors,48000);
    for(auto &edge:plan.connections)if(edge.source==0)edge.delay=edge.send?(edge.preFader?5:7):3;
    for(auto &edge:plan.sidechains)edge.delay=edge.input?13:11;
    for(auto &edge:plan.instruments)edge.delay=19;
    auto measured=std::make_unique<MixerRuntime>(graph,plan,48000),plain=std::make_unique<MixerRuntime>(graph,plan,48000);
    struct Captures {std::array<std::vector<std::vector<float>>,5> values;};Captures captures;
    std::array<size_t,5> sizes{plan.connections.size(),plan.sidechains.size(),plan.instruments.size(),processors.size(),graph.buses.size()};
    for(size_t kind=0;kind<sizes.size();++kind){captures.values[kind].resize(sizes[kind]);for(auto &v:captures.values[kind])v.resize(total*2);}
    measured->routeObserver([](void *context,MixerRuntime::RouteKind kind,size_t index,const float *pcm,uint32_t count,uint64_t position)noexcept{
      auto &result=static_cast<Captures *>(context)->values[size_t(kind)][index];std::copy_n(pcm,count*2,result.data()+position*2);
    },&captures);
    auto process=[](void *,size_t processor,float *buffer,uint32_t frames,uint64_t)noexcept{if(processor==0)for(size_t i=0;i<frames*2;++i)buffer[i]*=2;return true;};
    auto a=[](int64_t p){return p<0?0.f:float(.2+.05*std::sin(p*.037));};
    auto b=[](int64_t p){return p<0?0.f:float(.1+.03*std::cos(p*.029));};
    auto synth=[](int64_t p){return p<0?0.f:float(.08-.01*std::sin(p*.053));};
    const float fader=float(std::pow(10.,-.3)),preGain=float(std::pow(10.,-.6)),postGain=float(std::pow(10.,-.45));
    const float mainGain=float(std::pow(10.,-.5));
    auto pre=[&](int64_t p){return p<0?0.f:2*(a(p)+synth(p-19)+b(p-11)*mainGain);};
    auto post=[&](int64_t p){return pre(p)*fader;};
    std::array<float,4096> left{},right{},otherLeft{},otherRight{};std::array<float,8192> instrument{};
    for(uint32_t position=0;position<total;) {
      const auto count=std::min(block,total-position);
      for(uint32_t f=0;f<count;++f){left[f]=a(position+f);right[f]=-left[f];otherLeft[f]=b(position+f);otherRight[f]=-otherLeft[f];instrument[f*2]=synth(position+f);instrument[f*2+1]=-instrument[f*2];}
      uint64_t allocations,frees,locks;tracker_audit_begin();measured->begin(count,position);plain->begin(count,position);
      measured->instrument(2,1,instrument.data());plain->instrument(2,1,instrument.data());
      for(auto bus:plan.order){measured->process(bus,bus==0?left.data():bus==1?otherLeft.data():nullptr,bus==0?right.data():bus==1?otherRight.data():nullptr,process,nullptr);plain->process(bus,bus==0?left.data():bus==1?otherLeft.data():nullptr,bus==0?right.data():bus==1?otherRight.data():nullptr,process,nullptr);}
      measured->complete();plain->complete();tracker_audit_end(&allocations,&frees,&locks);
      check(allocations+frees+locks==0 && !measured->failed() && !plain->failed(),"Exact route taps allocate/free/lock nothing in processing");
      check(std::equal(measured->busOutput(plan.master),measured->busOutput(plan.master)+count*2,plain->busOutput(plan.master)),"Enabling exact route observation preserves DSP addition order bit for bit");position+=count;
    }
    auto compare=[&](const std::vector<float> &pcm,auto expected,const char *message){for(uint32_t f=0;f<total;++f){const auto value=expected(f);check(std::abs(pcm[f*2]-value)<2e-7 && std::abs(pcm[f*2+1]+value)<2e-7,message);}};
    for(size_t i=0;i<plan.connections.size();++i){const auto &edge=plan.connections[i];if(edge.source==0)compare(captures.values[0][i],[&](int64_t p){return (edge.preFader?pre(p-edge.delay):post(p-edge.delay))*float(edge.gain);},"Main/send captures contain actual post-gain compensated contribution, including post-insert pre-fader taps");}
    for(size_t i=0;i<plan.sidechains.size();++i){const auto &edge=plan.sidechains[i];compare(captures.values[1][i],[&](int64_t p){return b(p-edge.delay)*float(edge.gain);},"Auxiliary and Main-in sidechain taps contain their own delayed gain, not aggregate destination audio");}
    compare(captures.values[2][0],[&](int64_t p){return synth(p-19);},"Instrument-output tap contains the actual compensated source contribution");
    compare(captures.values[3][0],[&](int64_t p){return a(p)+synth(p-19);},"Serial insert tap excludes separately summed Main-in routes");
    compare(captures.values[4][plan.master],[&](int64_t p){return b(p)+post(p-3)+pre(p-5)*preGain+post(p-7)*postGain;},"Master wire tap is after inserts and before final master fader");
  }
}
void currentPlanMeters() {
  auto observation=std::make_unique<SignalObservation>(48000);
  const auto one=observation->add({"one","one","One",true});
  const auto two=observation->add({"two","two","Two",true});
  const std::array<SignalPortConfiguration,2> initial{{{one,7,3},{two,2,8}}};
  const std::array<SignalPortConfiguration,1> changed{{{one,13,11}}};
  observation->activate(initial);
  check(observation->read(one).available && !observation->read(one).fresh,"A current port is available before its first measurement");
  std::array<float,1024> audio;audio.fill(.25f);
  observation->scope.watch(one);
  observation->observe(one,audio.data(),512,0);observation->observe(two,audio.data(),512,0);
  check(observation->scope.snapshot().frames==512,"Current plan scope initially captures its selected route");
  uint64_t a,f,l;tracker_audit_begin();observation->activate(changed);tracker_audit_end(&a,&f,&l);
  auto reading=observation->read(one);
  check(observation->scope.snapshot().frames==0,"Plan adoption invalidates retained scope history before new audio arrives");
  check(a+f+l==0 && reading.available && !reading.measured && !reading.fresh && reading.processorLatency==13 && reading.compensation==11,
    "Actual plan adoption invalidates old measurements and atomically updates latency without audio allocation/free/lock");
  check(!observation->read(two).available && !observation->available(two),"Retired routes cannot retain visible meters or accept a scope/listen selection");
  observation->observe(two,audio.data(),512,512);
  check(!observation->read(two).available,"Old fading plan observations cannot revive a retired port");
  observation->observe(one,audio.data(),512,512);
  check(observation->read(one).fresh && observation->read(one).through==1024,"Retargeted port measures actual new-plan PCM");
  check(observation->scope.snapshot().frames==512 && observation->scope.snapshot().waveform.front().first==512,"New route scope excludes prior route samples despite contiguous clocks");
  observation->activate(initial);
  check(observation->read(two).available && !observation->read(two).fresh,"Undo or candidate rollback restores membership but never reuses old-plan samples");
  observation->observe(two,nullptr,512,1024);observation->observe(one,audio.data(),512,6144);
  check(!observation->read(two).fresh && !observation->read(two).measured && observation->read(one).fresh,
    "An available but undriven port becomes stale as the render clock advances");
  auto prepared=observation->preparePorts({{"new","new","New",true}});
  const auto third=uint32_t(prepared.base+1);const std::array<SignalPortConfiguration,1> arriving{{{third,5,9}}};
  observation->activate(arriving);check(!observation->available(third),"Plan adoption can safely precede new identity publication");
  check(observation->scope.snapshot().frames==0,"Queued samples from a retired route cannot reappear in scope history");
  observation->publishPorts(prepared);observation->observe(third,audio.data(),512,6656);
  check(observation->read(third).fresh && observation->read(third).processorLatency==5 && !observation->read(one).available,
    "Prepared new tokens join the adopted plan without reviving historical catalogue entries");
  observation->activate(std::array<SignalPortConfiguration,1>{{{third,5,10}}});
  std::atomic<bool> finished{false};bool coherent=true;size_t reads=0;
  std::thread reader([&]{do{const auto value=observation->read(third);if(value.available)coherent&=value.compensation==value.processorLatency*2;++reads;}while(!finished.load(std::memory_order_acquire));});
  // One audio writer, many concurrent UI reads; readers must never combine the
  // latency of one route revision with the compensation of another.
  tracker_audit_begin();for(int64_t i=1;i<10000;++i){observation->activate(std::array<SignalPortConfiguration,1>{{{third,i,i*2}}});observation->observe(third,audio.data(),1,7168+i);}
  tracker_audit_end(&a,&f,&l);finished.store(true,std::memory_order_release);reader.join();
  check(coherent && reads && a+f+l==0,"Concurrent plan metadata reads are coherent and audio adoption remains realtime safe");
}
void meterDecay() {
  SignalObservation observation(48000);
  auto port=observation.add({"track/in/0","track","Track input",false});
  check(!observation.read(port).measured,"An unobserved port is distinct from measured silence");
  std::array<float,1024> stereo{};for(size_t i=0;i<512;++i){stereo[i*2]=.5f;stereo[i*2+1]=-.25f;}
  uint64_t alloc,free,locks;tracker_audit_begin();observation.observe(port,stereo.data(),512,1000);tracker_audit_end(&alloc,&free,&locks);
  const auto known=observation.read(port);
  check(alloc+free+locks==0 && known.measured && known.peakLeft==.5f && known.rmsLeft==.5f && known.peakRight==.25f && known.rmsRight==.25f && known.through==1512 && known.lastSignal==1512 && !known.clipped,"Known stereo port levels and audio clock have allocation-free observations");
  stereo[0]=1.25f;stereo[1]=std::numeric_limits<float>::quiet_NaN();observation.observe(port,stereo.data(),512,1512);
  check(observation.read(port).clipped && observation.read(port).nonFinite && std::isfinite(observation.read(port).rmsRight),"Overloads and invalid plugin output latch without contaminating telemetry");
  observation.observe(port,nullptr,512,2024);check(observation.read(port).clipped && observation.read(port).rmsLeft==0 && observation.read(port).lastSignal==2024,"Silence is measured, preserves last signal clock and does not clear an overload");
  observation.clear(port);observation.observe(port,nullptr,512,2536);check(!observation.read(port).clipped && !observation.read(port).nonFinite,"Explicit clear takes effect at the next observed block");
  MixerGraph g;g.buses={{1,2,MixerBusKind::Track,"Track"},{2,0,MixerBusKind::Master,"Master"}};
  auto mixer=std::make_unique<MixerRuntime>(g,compileMixer(g,{1},{},48000),48000);
  std::array<float,512> samples{};samples.fill(.5f);
  mixer->begin(512,0);mixer->process(0,samples.data(),samples.data(),nullptr,nullptr);mixer->process(1,nullptr,nullptr,nullptr,nullptr);mixer->complete();
  check(mixer->meters()[0].left==.5f,"Peak captures a known level");
  for(uint32_t frame=512;frame<=48000;frame+=512) {
    uint64_t allocations,frees,locks;tracker_audit_begin();
    mixer->begin(512,frame);mixer->process(0,nullptr,nullptr,nullptr,nullptr);mixer->process(1,nullptr,nullptr,nullptr,nullptr);mixer->complete();
    tracker_audit_end(&allocations,&frees,&locks);
    check(allocations+frees+locks==0,"Meter decay is bounded and realtime safe");
    const double expected=.5*std::exp(-double(frame)/(48000*.2));
    for(const auto &meter:mixer->meters())check(std::isfinite(meter.left)&&std::isfinite(meter.right)&&std::abs(meter.left-expected)<2e-6,"Meter peaks decay exponentially without unsigned-negation overflow or infinite JSON values");
  }
}
void scopeCapture() {
  auto observation=std::make_unique<SignalObservation>(48000);
  const auto first=observation->add({"one/out/0","one","First",true}),second=observation->add({"two/out/0","two","Second",true});
  std::array<float,8192> audio{};
  for(size_t i=0;i<4096;++i){audio[2*i]=float(.5*std::sin(2*std::numbers::pi*64*i/4096));audio[2*i+1]=-audio[2*i];}
  observation->scope.watch(first);
  uint64_t a,f,l;tracker_audit_begin();observation->observe(second,audio.data(),4096,0);observation->observe(first,audio.data(),4096,0);tracker_audit_end(&a,&f,&l);
  const auto known=observation->scope.snapshot(true);
  check(a+f+l==0 && known.token==first && known.frames==4096 && known.waveform.size()==256 && known.through==4096,"Scope captures only the requested host tap at full rate without realtime allocation/free/lock");
  check(known.fftFrames==4096 && std::abs(known.spectrum[64]-.5)<1e-5 && std::max_element(known.spectrum.begin(),known.spectrum.end())-known.spectrum.begin()==64,"Off-thread Hann spectrum reports known frequency/amplitude without cancelling opposite-polarity stereo");
  observation->scope.watch(second);observation->observe(first,audio.data(),4096,4096);
  check(observation->scope.snapshot().frames==0,"Changing scope tap cannot display an old source's samples");
  observation->observe(second,nullptr,128,10000);const auto silent=observation->scope.snapshot();
  check(silent.frames==128 && silent.waveform.front().first==10000 && silent.waveform.back().last==10128 && silent.waveform.front().maximum[0]==0,"Measured scope silence has explicit audio-clock coordinates");
  observation->scope.watch(first);
  for(uint64_t i=0;i<17;++i)observation->observe(first,audio.data(),4096,i*4096);
  const auto full=observation->scope.snapshot();check(full.dropped==4096 && full.through==65536,"A stalled scope reader reports bounded drops without overwriting unread data");
  observation->observe(first,audio.data(),128,17*4096);const auto gap=observation->scope.snapshot(true);
  check(gap.frames==128 && gap.waveform.front().first==17*4096 && gap.fftFrames==128,"Scope history restarts across dropped samples rather than inventing continuity");
  observation->scope.watch(0);observation->observe(first,audio.data(),512,70000);check(observation->scope.snapshot(true).frames==0,"Releasing the scope stops capture and clears the retained source");
  SignalScope concurrent;
  std::atomic<bool> done=false;
  std::thread producer([&]{std::array<float,1024> one,two;one.fill(1);two.fill(2);for(uint64_t frame=0;frame<512*2000;frame+=512){concurrent.capture(1,one.data(),512,frame);concurrent.capture(2,two.data(),512,frame);}done.store(true,std::memory_order_release);});
  bool coherent=true;uint32_t selected=1;
  while(!done.load(std::memory_order_acquire)) {
    concurrent.watch(selected);const auto state=concurrent.snapshot();
    for(const auto &bucket:state.waveform)for(size_t c=0;c<2;++c)coherent&=bucket.minimum[c]==selected&&bucket.maximum[c]==selected;
    selected=3-selected;
  }
  producer.join();check(coherent,"Concurrent scope selection and draining never attribute an old source to the new tap");
}
void listenCapture() {
  auto render=[](uint32_t block,uint32_t rate) {
    auto observation=std::make_unique<SignalObservation>(rate);
    const auto tap=observation->add({"track/out/0","track","Track output",true});
    std::array<float,8192> output{},source{};std::vector<float> result(4096*2);
    observation->listen.select(tap);
    for(uint32_t position=0;position<4096;position+=block) {
      const auto frames=std::min(block,4096-position);
      for(uint32_t i=0;i<frames;++i){source[2*i]=.25f;source[2*i+1]=-.5f;output[2*i]=.75f;output[2*i+1]=.125f;}
      uint64_t a,f,l;tracker_audit_begin();observation->listen.begin(position);
      // Render chunks may cross musical ticks inside a callback.
      const auto first=frames/2;
      observation->observe(tap,source.data(),first,position);
      observation->observe(tap,source.data()+first*2,frames-first,position+first);
      observation->listen.apply(output.data(),frames);tracker_audit_end(&a,&f,&l);
      check(a+f+l==0,"Monitor tap capture and crossfade allocate/free/lock nothing");
      std::copy_n(output.data(),frames*2,result.data()+position*2);
    }
    check(!observation->listen.pending() && observation->listen.settled().token==tap,"Monitor settles on requested source");
    const auto fade=observation->listen.transitionFrames();
    for(uint32_t i=0;i<4096;++i){const auto mix=std::min(1.,double(i)/fade);check(std::abs(result[2*i]-(.75-.5*mix))<1e-6 && std::abs(result[2*i+1]-(.125-.625*mix))<1e-6,"Listen fades linearly to the exact stereo host tap without downstream processing or gain overshoot");}
    observation->listen.select(0);observation->listen.begin(4096);source.fill(.25f);output.fill(.75f);observation->observe(tap,source.data(),4096,4096);observation->listen.apply(output.data(),4096);
    check(!observation->listen.pending() && observation->listen.settled().token==0 && output[0]==.25f && output[4095*2]==.75f,"Stop listening smoothly restores the current full mix");
    observation->listen.begin(8192);output.fill(.137f);observation->listen.apply(output.data(),4096);check(output[0]==.137f && output.back()==.137f,"Disabled monitor leaves output unchanged");
    return result;
  };
  for(auto rate:{44100u,48000u,96000u})check(render(64,rate)==render(511,rate)&&render(511,rate)==render(4096,rate),"Monitor ramp is independent of callback and tick partition");
  SignalListen listen(48000);std::array<float,8192> a{},b{},mix{};a.fill(.25f);b.fill(.5f);mix.fill(.75f);
  listen.select(1);listen.begin(0);listen.capture(1,a.data(),64,0);listen.apply(mix.data(),64);
  listen.select(2,.5f);listen.begin(64);listen.capture(1,a.data(),256,64);listen.capture(2,b.data(),256,64);mix.fill(.75f);listen.apply(mix.data(),256);
  check(listen.settled().token==1&&listen.pending(),"Rapid target changes finish the in-flight ramp without discontinuity or unbounded capture");
  listen.begin(320);listen.capture(1,a.data(),512,320);listen.capture(2,b.data(),512,320);mix.fill(.75f);listen.apply(mix.data(),512);
  check(listen.settled().token==2&&!listen.pending()&&mix[0]==.25f&&mix[1000]==.25f,"Correlated taps at matching gain produce no equal-power bump");
  listen.select(2,1);listen.begin(832);listen.capture(2,b.data(),512,832);mix.fill(.75f);listen.apply(mix.data(),512);check(mix[0]==.25f&&mix[1000]==.5f,"Monitor gain changes are smoothed independently of document faders");
  listen.begin(1344);mix.fill(.75f);listen.apply(mix.data(),512);check(mix[0]==0&&mix[1000]==0,"An unobserved/muted tap is silence, never stale samples or the normal mix");
}
void processorBypass() {
  auto render=[](uint32_t block,uint32_t rate) {
    ProcessorBypass bypass;bypass.prepare(rate,13,false,false);
    std::array<float,8192> audio{};std::array<float,26> delay{};size_t cursor=0;
    std::vector<float> result(4000*2);
    for(uint32_t position=0;position<4000;) {
      uint32_t count=std::min(block,4000-position);if(position<1000)count=std::min(count,1000-position);if(position<2500)count=std::min(count,2500-position);
      if(position==1000)bypass.set(true);if(position==2500)bypass.set(false);
      for(uint32_t i=0;i<count*2;++i)audio[i]=float(.2*std::sin((position*2+i)*.017));
      uint64_t a,f,l;tracker_audit_begin();bypass.begin(audio.data(),count);
      for(uint32_t i=0;i<count*2;++i){std::swap(audio[i],delay[cursor]);cursor=(cursor+1)%delay.size();audio[i]*=2;}
      bypass.finish(audio.data(),count);tracker_audit_end(&a,&f,&l);check(a+f+l==0,"Latency-preserving bypass is allocation/free/lock safe");
      std::copy_n(audio.data(),count*2,result.data()+position*2);position+=count;
    }
    const auto fade=std::round(rate*.005);
    for(uint32_t i=26;i<result.size();++i) {
      const double frame=i/2,wet=frame<1000?1:frame<2500?std::max(0.,1-(frame-1000)/fade):std::min(1.,(frame-2500)/fade);
      const auto dry=float(.2*std::sin((i-26)*.017));check(std::abs(result[i]-dry*(1+wet))<1e-7,"Dry and wet share latency, preserving phase while bypass ramps");
    }
    return result;
  };
  for(auto rate:{44100u,48000u,96000u})check(render(17,rate)==render(512,rate)&&render(512,rate)==render(4096,rate),"Bypass fade is callback-partition invariant");
  ProcessorBypass source;source.prepare(48000,0,true,true);std::array<float,1024> audio{};audio.fill(.5f);source.begin(audio.data(),512);source.finish(audio.data(),512);check(audio[0]==0&&audio.back()==0,"An initially bypassed instrument is silent, not dry sample pass-through");
  source.set(false);source.begin(audio.data(),512);audio.fill(.5f);source.finish(audio.data(),512);check(audio[0]==0&&audio[480]==.5f,"An instrument fades back to its continuously running processor output");
}
void impulse(const std::vector<float> &out, std::vector<std::pair<size_t, float>> expected) {
  for (size_t i = 0; i < out.size() / 2; ++i) {
    float value = 0; for (const auto &point : expected) if (point.first == i) value += point.second;
    check(std::abs(out[i * 2] - value) < 2e-6 && std::abs(out[i * 2 + 1] - value) < 2e-6,
          "Rendered impulse arrivals and gains match the independently calculated graph");
  }
}
std::vector<float> smooth(uint32_t block) {
  MixerGraph g; g.buses = {{1, 2, MixerBusKind::Track, "Track"}, {2, 0, MixerBusKind::Master, "Master"}};
  auto mixer = std::make_unique<MixerRuntime>(g, compileMixer(g, {1}, {}, 48000), 48000);
  std::vector<MixerControls> controls(2); controls[0].gainDB = -6; controls[0].pan = .5; controls[0].width = 0;
  check(mixer->controls(controls), "Live controls queue accepts a complete snapshot");
  std::array<float, 4096> l{}, r{}; l.fill(1); r.fill(.5);
  std::vector<float> out(2048);
  for (uint32_t p = 0; p < 1024; p += block) {
    auto n = std::min(block, 1024 - p);
    uint64_t a, f, locks; tracker_audit_begin();
    mixer->begin(n, p); mixer->process(0, l.data(), r.data(), nullptr, nullptr);
    const auto *master = mixer->process(1, nullptr, nullptr, nullptr, nullptr);
    std::copy_n(master, n * 2, out.data() + p * 2); mixer->complete();
    tracker_audit_end(&a, &f, &locks); check(a + f + locks == 0, "Control ramps remain realtime-safe");
  }
  check(std::abs(out[478] - .75 * std::pow(10, -.3) * .5) < 2e-7 && std::abs(out[479] - .75 * std::pow(10, -.3)) < 2e-7,
        "Gain, balance and width reach their target at five milliseconds");
  check(out[0] > .99 && out[0] < 1, "A control change begins smoothly instead of jumping");
  return out;
}
// A channel-swapping insert makes pre/post balance observably different.
// The pre-fader return and sidechain must both hear the processed input balance.
std::vector<float> inputBalance(uint32_t rate, uint32_t block, double balance, bool live) {
  MixerGraph g; g.buses = {{1, 4, MixerBusKind::Track, "Track"}, {2, 4, MixerBusKind::Return, "Return"},
                          {3, 4, MixerBusKind::Return, "Detector"}, {4, 0, MixerBusKind::Master, "Master"}};
  g.buses[0].preGainDB = -6; g.buses[0].gainDB = -12; g.buses[0].pan = -.25;
  g.buses[0].prePan = live ? 0 : balance; g.buses[0].inserts = {"swap"};
  g.buses[0].sends = {{2, 0, true, true}}; g.buses[2].inserts = {"detector"};
  g.sidechains = {{1, "detector", 1, 0, true, true}};
  std::vector<MixerProcessorInfo> p = {{"swap", 0, 0}, {"detector", 0, 0}};
  p[1].activeInputs = 2;
  auto mixer = std::make_unique<MixerRuntime>(g, compileMixer(g, {1}, p, rate), rate);
  if (live) {
    std::vector<MixerControls> controls(4);
    controls[0].preGainDB = -6; controls[0].gainDB = -12; controls[0].pan = -.25; controls[0].prePan = balance;
    check(mixer->controls(controls), "Input balance is accepted by live control snapshots");
  }
  const uint32_t total = rate / 50, ramp = uint32_t(std::round(rate * .005));
  std::vector<float> out(total * 2);
  std::array<float, 4096> left{}, right{}; left.fill(.8f); right.fill(.2f);
  auto process = [](void *context, size_t processor, float *audio, uint32_t frames, uint64_t) noexcept {
    if (processor == 0) for (uint32_t i = 0; i < frames; ++i) std::swap(audio[i * 2], audio[i * 2 + 1]);
    else {
      auto inputs = static_cast<MixerRuntime *>(context)->inputs(processor);
      if (inputs.size() != 1) return false;
      std::copy_n(inputs[0].samples, frames * 2, audio);
    }
    return true;
  };
  for (uint32_t pos = 0; pos < total; pos += block) {
    const auto count = std::min(block, total - pos);
    uint64_t a, f, locks; tracker_audit_begin();
    mixer->begin(count, pos);
    mixer->process(0, left.data(), right.data(), process, mixer.get());
    mixer->process(1, nullptr, nullptr, process, mixer.get());
    mixer->process(2, nullptr, nullptr, process, mixer.get());
    const float *master = mixer->process(3, nullptr, nullptr, process, mixer.get());
    std::copy_n(master, count * 2, out.data() + pos * 2); mixer->complete();
    tracker_audit_end(&a, &f, &locks);
    check(!mixer->failed() && a + f + locks == 0, "Input balance, sends and sidechains allocate/free/lock zero times on callback");
  }
  for (uint32_t i = 0; i < total; ++i) {
    const double b = balance * (live ? std::min(1.0, double(i + 1) / ramp) : 1.0);
    const double leftAfterSwap = .2 * std::pow(10, -.3) * (b < 0 ? 1 + b : 1);
    const double rightAfterSwap = .8 * std::pow(10, -.3) * (b > 0 ? 1 - b : 1);
    const double expectedL = leftAfterSwap * (2 + std::pow(10, -.6)), expectedR = rightAfterSwap * (2 + .75 * std::pow(10, -.6));
    if (std::abs(out[i * 2] - expectedL) >= 3e-7 || std::abs(out[i * 2 + 1] - expectedR) >= 3e-7)
      std::cerr << "Input balance reference: rate " << rate << " block " << block << " balance " << balance << " live " << live
                << " sample " << i << " actual " << out[i * 2] << "," << out[i * 2 + 1] << " expected " << expectedL << "," << expectedR << '\n';
    check(std::abs(out[i * 2] - expectedL) < 3e-7 && std::abs(out[i * 2 + 1] - expectedR) < 3e-7,
          "Independent sample reference verifies pre-insert balance, output balance, pre-fader return and sidechain");
  }
  return out;
}
std::vector<float> interruptedBalance(uint32_t block) {
  MixerGraph g; g.buses = {{1, 2, MixerBusKind::Track, "Track"}, {2, 0, MixerBusKind::Master, "Master"}};
  auto mixer = std::make_unique<MixerRuntime>(g, compileMixer(g, {1}, {}, 48000), 48000);
  std::vector<MixerControls> controls(2); controls[0].prePan = .8; check(mixer->controls(controls), "Initial balance gesture");
  std::array<float, 4096> left{}, right{}; left.fill(1); right.fill(.5);
  std::vector<float> out(1440);
  for (uint32_t pos = 0; pos < 720;) {
    if (pos == 93 || pos == 200) { controls[0].prePan = -.5; check(mixer->controls(controls), "Revised or duplicate gesture snapshot"); }
    const auto boundary = pos < 93 ? 93u : pos < 200 ? 200u : 720u;
    const auto count = std::min(block, boundary - pos);
    mixer->begin(count, pos); mixer->process(0, left.data(), right.data(), nullptr, nullptr);
    const auto *master = mixer->process(1, nullptr, nullptr, nullptr, nullptr);
    std::copy_n(master, count * 2, out.data() + pos * 2); mixer->complete(); pos += count;
  }
  const double origin = .8 * 93 / 240;
  for (size_t i = 0; i < 720; ++i) {
    const double balance = i < 93 ? .8 * (i + 1) / 240 : origin + (-.5 - origin) * std::min(1.0, double(i - 93 + 1) / 240);
    check(std::abs(out[i * 2] - (1 - std::max(0.0, balance))) < 2e-7 &&
          std::abs(out[i * 2 + 1] - .5 * (1 + std::min(0.0, balance))) < 2e-7,
          "A changed gesture starts at its last rendered value and a repeated target never restarts smoothing");
  }
  return out;
}
}
// Bus meters hold the block peak and release with a 200 ms time constant.
void meterDecay(uint32_t rate, uint32_t block) {
  MixerGraph g; g.buses = {{1, 2, MixerBusKind::Track, "Track"}, {2, 0, MixerBusKind::Master, "Master"}};
  auto mixer = std::make_unique<MixerRuntime>(g, compileMixer(g, {1}, {}, rate), rate);
  std::array<float, 4096> l{}, r{};
  auto run = [&](uint64_t position) {
    mixer->begin(block, position); mixer->process(0, l.data(), r.data(), nullptr, nullptr);
    mixer->process(1, nullptr, nullptr, nullptr, nullptr); mixer->complete();
  };
  auto bounded = [&](float left, float right) {
    for (const auto &meter : mixer->meters())
      check(std::isfinite(meter.left) && std::isfinite(meter.right) && meter.left >= 0 && meter.right >= 0 &&
            meter.left <= left + 1e-6f && meter.right <= right + 1e-6f, "Meters are finite and never exceed the actual peak");
  };
  l.fill(.5f); r.fill(.25f); run(0); bounded(.5f, .25f); run(block); bounded(.5f, .25f);
  check(std::abs(mixer->meters()[0].left - .5f) < 1e-6f && std::abs(mixer->meters()[1].right - .25f) < 1e-6f, "Meters report the block peak");
  l.fill(0); r.fill(0);
  uint64_t position = block * 2; float previous = mixer->meters()[1].left; uint32_t silent = 0;
  while (silent < rate / 5) {
    run(position); position += block; silent += block; bounded(.5f, .25f);
    check(mixer->meters()[1].left < previous, "Meters fall during silence"); previous = mixer->meters()[1].left;
  }
  const float expected = .5f * float(std::exp(-double(silent) / (rate * .2)));
  check(std::abs(mixer->meters()[1].left - expected) < 1e-4f && std::abs(mixer->meters()[0].right - expected * .5f) < 1e-4f,
        "Meter release follows its 200 ms time constant");
  check(!mixer->failed(), "Meter rendering completes without a fault");
}
static void patchingAudio(uint32_t rate,uint32_t block) {
  MixerGraph g;g.buses={{1,0,MixerBusKind::Track,"A"},{2,0,MixerBusKind::Track,"B"},{3,0,MixerBusKind::Track,"C"},{4,5,MixerBusKind::Return,"Return"},{5,0,MixerBusKind::Master,"Master"}};
  g.buses[0].inserts={"double"};g.buses[0].output=5;
  g.sidechains={{2,"double",0,0,false,true},{3,"double",0,0,false,true}};
  // One processed signal travels down its serial path and two extra branches.
  g.instruments={{"double",4,0},{"double",5,0}};
  auto mixer=std::make_unique<MixerRuntime>(g,compileMixer(g,{1,2,3},{{"double",0,0}},rate),rate);
  std::array<float,4096> one{},two{},three{};one.fill(.1f);two.fill(.2f);three.fill(.3f);
  struct Count{uint32_t calls=0;};Count count;
  auto process=[](void *p,size_t,float *audio,uint32_t n,uint64_t)noexcept{++static_cast<Count *>(p)->calls;for(uint32_t i=0;i<n*2;++i)audio[i]*=2;return true;};
  for(uint32_t position=0;position<2000;position+=block){auto frames=std::min(block,2000-position);uint64_t a,f,l;tracker_audit_begin();mixer->begin(frames,position);
    for(auto bus:mixer->plan().order){auto input=bus==0?one.data():bus==1?two.data():bus==2?three.data():nullptr;auto out=mixer->process(bus,input,input,process,&count);if(bus==4)for(uint32_t i=0;i<frames*2;++i)if(std::abs(out[i]-3.6f)>1e-6f)throw std::runtime_error("Main fan-in/fan-out sample reference differs");}
    mixer->complete();tracker_audit_end(&a,&f,&l);check(!mixer->failed()&&a+f+l==0,"Fan-in/fan-out has no callback allocations, frees, locks or faults");
  }
  check(count.calls==(2000+block-1)/block,"Fan-out must never run an effect twice");
}
int main() {
  try {
    for (uint32_t rate : {44100, 48000, 96000}) for (uint32_t block : {1, 17, 128, 4096}) meterDecay(rate, block);
    for (uint32_t rate : {44100, 48000, 96000}) {
      for(uint32_t block:{1u,17u,128u,512u})patchingAudio(rate,block);
      for (double balance : {-1., -.4, 0., .7, 1.}) for (bool live : {false, true}) {
        const auto reference = inputBalance(rate, 128, balance, live);
        for (uint32_t block : {1, 17, 512, 4096}) {
          const auto candidate = inputBalance(rate, block, balance, live);
          check(candidate == reference, "Input balance and ramps are exactly block-independent");
        }
      }
      const auto plain = render(graph(), 128, rate);
      impulse(plain, {{149, float(15 + std::pow(10, -.3))}});
      for (auto block : {17u, 512u, 4096u}) check(plain == render(graph(), block, rate), "Routing/PDC is exactly callback-size independent");
      auto g = graph(); g.buses[0].timingMS = -10; g.buses[1].timingMS = 20;
      impulse(render(g, 17, rate), {{149, float(1 + std::pow(10, -.3))}, {149 + rate / 100, 8}, {149 + rate * 3 / 100, 6}});
      g = graph(); g.buses[0].gainDB = -6;
      impulse(render(g, 128, rate), {{149, float(14 + 2 * std::pow(10, -.3))}});
      g.buses[0].sends[0].preFader = false;
      impulse(render(g, 128, rate), {{149, float(14 + std::pow(10, -.3) + std::pow(10, -.6))}});
      g.buses[0].mute = true;
      impulse(render(g, 128, rate), {{149, 14}});
      g = graph(); g.buses[0].solo = true;
      impulse(render(g, 128, rate), {{149, float(9 + std::pow(10, -.3))}});
    }
    const auto reference = smooth(128);
    const auto interrupted = interruptedBalance(128);
    for (uint32_t block : {1, 17, 512, 4096})
      check(interruptedBalance(block) == interrupted, "Interrupted ramps remain exactly callback-size independent");
    for (auto block : {17u, 512u, 4096u}) {
      auto candidate = smooth(block);
      for (size_t i = 0; i < reference.size(); ++i) check(std::abs(candidate[i] - reference[i]) < 3e-7, "Smoothed controls are callback-size independent within float precision");
    }
    auto g = graph(); auto mixer = std::make_unique<MixerRuntime>(g, compileMixer(g, {1, 2}, processors(), 48000), 48000);
    std::vector<MixerControls> controls(5);
    controls[0].pan = std::numeric_limits<double>::quiet_NaN();
    check(!mixer->controls(controls), "Non-finite controls are rejected before the audio thread");
    controls[0].pan = 0;
    controls[0].prePan = std::numeric_limits<double>::quiet_NaN();
    check(!mixer->controls(controls), "Invalid input balance never reaches the audio queue");
    controls[0].prePan = 0;
    for (int i = 0; i < 8; ++i) check(mixer->controls(controls), "Bounded queue capacity accepted");
    check(!mixer->controls(controls), "Full queue provides explicit backpressure");
    mixer->begin(4097, 0); check(mixer->failed(), "Oversized blocks fail without writing beyond buffers");
    MixerGraph toxic; toxic.buses = {{1, 2, MixerBusKind::Track, "Track"}, {2, 0, MixerBusKind::Master, "Master"}};
    toxic.buses[1].gainDB = 24; toxic.buses[1].inserts = {"toxic"};
    auto guard = std::make_unique<MixerRuntime>(toxic, compileMixer(toxic, {1}, {{"toxic", 0, 0}}, 48000), 48000);
    guard->begin(1, 0); guard->process(0, nullptr, nullptr, nullptr, nullptr);
    const auto *silenced = guard->process(1, nullptr, nullptr,
      [](void *, size_t, float *data, uint32_t frames, uint64_t) noexcept { std::fill_n(data, frames * 2, std::numeric_limits<float>::max()); return true; }, nullptr);
    guard->complete();
    check(guard->failed() && silenced[0] == 0 && silenced[1] == 0, "Post-fader overflow is silenced before reaching the integer output mixer");
    liveMeterRegistration();
    currentPlanMeters();
    exactRouteTaps();
    meterDecay();
    scopeCapture();
    listenCapture();
    processorBypass();
    std::cout << "PASS mixer runtime: exact routing/PDC, instrument and preview inputs, timing offsets, pre/post sends, mute/solo, smooth controls, meters and realtime audit\n";
    return 0;
  } catch (const std::exception &e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
