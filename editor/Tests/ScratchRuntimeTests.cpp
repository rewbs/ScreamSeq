#include "editor/TrackerDocument.hpp"
#include "editor/SongTiming.hpp"
#include "editor/PatternTools.hpp"
#include "soundlib/ModInstrument.h"
#include <cmath>
#include <iostream>
#include <functional>
#include <chrono>
#include <iomanip>
using namespace Tracker;
using namespace OpenMPT;
#if defined(TRACKER_REALTIME_AUDIT) && !defined(TRACKER_SANITIZER)
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *,uint64_t *,uint64_t *);
#else
static void tracker_audit_begin(){}
static void tracker_audit_end(uint64_t *a,uint64_t *f,uint64_t *l){*a=*f=*l=0;}
#endif
static void check(bool v,const char *message){if(!v)throw std::runtime_error(message);}
static void near(double a,double b,double tolerance,const char *message){if(std::abs(a-b)>tolerance){std::cerr<<message<<": "<<a<<" != "<<b<<'\n';throw std::runtime_error(message);}}
template<class F> static void rejects(F f){bool rejected=false;try{f();}catch(const std::exception &){rejected=true;}check(rejected,"Invalid scratch operation accepted");}
static std::unique_ptr<Document> fixture(bool constant=false,bool instrument=false){
 auto d=std::make_unique<Document>();d->transaction([&](CSoundFile &s){
  for(auto &p:s.Patterns)if(p.IsValid())for(auto &c:p)c.Clear();
  s.Order().assign(1,0);auto timing=songTiming(s);timing.mode=TempoMode::Modern;timing.rowsPerBeat=4;timing.rowsPerMeasure=16;timing.sequences[0]={1200000,6};timing.groove.clear();applySongTiming(s,timing);
  s.m_nSamples=1;auto &sample=s.GetSample(1);sample.Initialize();sample.nLength=131072;sample.nC5Speed=48000;sample.uFlags.set(CHN_16BIT);
  check(sample.AllocateSample()!=0,"Allocate scratch PCM");for(size_t i=0;i<sample.nLength;++i)sample.sample16()[i]=constant?2000:int16_t(4000*std::sin(i*.031));sample.PrecomputeLoops(s,false);
  if(instrument){s.m_nInstruments=1;s.Instruments[1]=new ModInstrument(1);s.Instruments[1]->nNNA=NewNoteAction::Continue;}
  auto &note=*s.Patterns[0].GetpModCommand(0,0);note.note=61;note.instr=1;
 });return d;
}
static PatternCommand command(const NativeSong &native,uint32_t position=8192,double beats=.5,double travel=100,uint16_t repeats=1,bool reverse=false){
 PatternCommand c;c.pattern=native.patterns.at(0).id;c.track=native.tracks.at(0).id;c.kind=PatternCommandKind::Native;c.native=NativePatternOp::Scratch;c.position=position;c.arguments=nativePatternDefaults(c.native);c.arguments={1,beats,travel,double(repeats),reverse?1.:0.};return c;
}
static void add(Document &doc,const ScratchGesture &gesture,uint32_t at=8192,double beats=.5,double travel=100,uint16_t repeats=1,bool reverse=false){doc.annotate([&](NativeSong &n){n.scratchGestures[1]=gesture;n.performance.commands.push_back(command(n,at,beats,travel,repeats,reverse));});}
static std::vector<float> render(Document &d,uint32_t rate,uint32_t block,double seconds=.35,const std::function<void(Renderer &,uint32_t)> &inspect={}){
 Renderer renderer(d.snapshotData(),rate);renderer.preparePreciseNotes(d.native());const uint32_t total=uint32_t(rate*seconds);std::vector<float> out(total*2);
 for(uint32_t f=0;f<total;f+=block){const auto count=std::min(block,total-f);uint64_t a,b,c;tracker_audit_begin();renderer.render(out.data()+2*f,count);tracker_audit_end(&a,&b,&c);check(!renderer.faulted()&&a+b+c==0,"Scratch render allocated, freed, locked or faulted");if(inspect)inspect(renderer,f+count-1);}
 for(auto value:out)check(std::isfinite(value)&&std::abs(value)<4,"Finite bounded scratch PCM");return out;
}
static void partitions(Document &d,uint32_t rate,const std::vector<float> &reference,double seconds=.35){for(auto block:{17u,128u,4096u}){const auto actual=render(d,rate,block,seconds);for(size_t i=0;i<actual.size();++i)if(std::abs(actual[i]-reference[i])>3e-6){std::cerr<<"rate "<<rate<<" block "<<block<<" sample "<<i<<" delta "<<actual[i]-reference[i]<<'\n';check(false,"Scratch PCM depends on callback partition");}}}
static void model(){
 const auto presets=scratchPresets();check(presets.size()==7,"Seven starter gestures");for(const auto &preset:presets)validateScratchGesture(preset.gesture);
 auto bad=presets[0].gesture;bad.motion[1].position=0;rejects([&]{validateScratchGesture(bad);});bad=presets[0].gesture;bad.fader.back().position--;rejects([&]{validateScratchGesture(bad);});bad=presets[0].gesture;bad.motion[1].value=2;rejects([&]{validateScratchGesture(bad);});
 auto d=fixture();const auto original=d->native();rejects([&]{d->annotate([&](NativeSong &n){n.performance.commands.push_back(command(n));});});check(d->native()==original,"Missing gesture rejection is atomic");add(*d,presets[0].gesture);const auto accepted=d->native();
 rejects([&]{d->annotate([](NativeSong &n){n.scratchGestures.clear();});});check(d->native()==accepted,"Referenced deletion is atomic");
 rejects([&]{d->annotate([](NativeSong &n){n.performance.commands[0].arguments[1]=100;});});check(d->native()==accepted,"Beyond-pattern rejection is atomic");
 d->undo();check(d->native()==original,"Undo scratch bank and command");d->redo();check(d->native()==accepted,"Redo scratch bank and command");
 auto shape=presets[0].gesture;shape.motion={{0,.25,AutomationCurve::Scripted,CurveFormula("0.5 + 0.25*sin(t*tau)")},{65536,.25,AutomationCurve::Linear,{}}};validateScratchGesture(shape);near(scratchEnvelopeValue(shape.motion,.25,2),.75,1e-12,"Scripted motion uses normalized phase");
 shape.motion[0].formula=CurveFormula("beat/duration");near(scratchEnvelopeValue(shape.motion,.25,2),.25,1e-12,"Script beat and duration retain musical units");
}
static void audio(){
 for(uint32_t rate:{44100u,48000u,96000u})for(size_t preset:{0u,1u,2u,3u,4u,5u,6u}){
  auto d=fixture();add(*d,scratchPresets()[preset].gesture,8192,.5,100,3);
  bool reverse=false,forward=false,held=false,returned=false;double cue=0,last=0;bool active=false;
  const auto reference=render(*d,rate,1,.35,[&](Renderer &r,uint32_t){const auto *positions=r.song().nativeScratchPositions[0];const double now=r.song().m_PlayState.Chn[0].position.ToDouble();if(positions){if(!active){cue=positions[0];active=true;}forward|=positions[1]>positions[0]+.001;reverse|=positions[1]<positions[0]-.001;held|=positions[1]==positions[0];last=positions[1];near(now,last,2e-8,"Sample follows absolute scratch trajectory");}else if(active && !returned){near(last,cue,1e-6,"Closed scratch returns exactly to captured cue");returned=true;}});
  check(forward&&reverse&&returned,"Gesture moves both directions and completes");(void)held;partitions(*d,rate,reference);
 }
 // Independent linear triangular path: 4800 frames of travel from row start,
 // then exact return; every 48kHz tick is exactly 1000 samples at 120 BPM.
 auto d=fixture();ScratchGesture triangle{"Triangle",{{0,0,AutomationCurve::Linear,{}},{32768,1,AutomationCurve::Linear,{}},{65536,0,AutomationCurve::Linear,{}}},{{0,1,AutomationCurve::Linear,{}},{65536,1,AutomationCurve::Linear,{}}}};add(*d,triangle,0,.5,100);
 render(*d,48000,1,.25,[&](Renderer &r,uint32_t frame){const double t=double(frame+1)/12000;near(r.song().m_PlayState.Chn[0].position.ToDouble(),4800*(t<=.5?t*2:2-t*2),2e-6,"Independent absolute-position triangle oracle");});
 // A closed fader moves the record without ending it; its cut ramps over 0.5ms.
 auto gated=fixture(true);triangle.fader={{0,0,AutomationCurve::Step,{}},{65536,0,AutomationCurve::Step,{}}};add(*gated,triangle,8192,.5,100);
 const auto muted=render(*gated,48000,1);for(size_t f=800;f<12000;++f)near(muted[2*f],0,1e-8,"Closed scratch fader is silent while motion continues");
}
static void lifetime(){
 const auto gesture=scratchPresets()[0].gesture;
 for(bool instrument:{false,true}){
  auto d=fixture(false,instrument);d->transaction([](CSoundFile &s){auto &n=*s.Patterns[0].GetpModCommand(1,0);n.note=65;n.instr=1;});add(*d,gesture,0,1,100);
  bool active=false,cancelled=false;render(*d,48000,1,.3,[&](Renderer &r,uint32_t frame){if(frame<6000)active|=r.song().nativeScratchPositions[0]!=nullptr;else {check(!r.song().nativeScratchPositions[0],"Next onset cancels SK");check(!r.song().nativeScratchGains[0],"New note inherits no fader");cancelled=true;}});check(active&&cancelled,"Scratch lifecycle exercised");
 }
 auto d=fixture();add(*d,gesture,0,1,100);d->annotate([](NativeSong &n){auto stop=command(n,32768);stop.native=NativePatternOp::ScratchStop;stop.arguments={};n.performance.columns[stop.track]=2;stop.column=1;n.performance.commands.push_back(stop);});
 render(*d,48000,1,.1,[](Renderer &r,uint32_t frame){if(frame>=3000)check(!r.song().nativeScratchPositions[0],"SX releases trajectory at exact offset");});
 // A stationary endpoint must remain alive even when reverse mode is selected.
 auto edge=fixture();ScratchGesture hold{"Hold",{{0,0,AutomationCurve::Step,{}},{65536,0,AutomationCurve::Step,{}}},gesture.fader};add(*edge,hold,0,.5,100,1,true);
 render(*edge,48000,1,.25,[](Renderer &r,uint32_t){check(r.song().m_PlayState.Chn[0].nLength>0,"Zero speed retains boundary voice");near(r.song().m_PlayState.Chn[0].position.ToDouble(),0,1e-12,"Stationary gesture holds physical sample start");});
}
static void edgesAndPrecedence(){
 const auto baby=scratchPresets()[0].gesture;
 for(uint32_t length:{1u,2u,7u,257u})for(bool stereo:{false,true})for(bool sixteen:{false,true})for(bool loop:{false,true}){
  auto d=fixture();d->transaction([&](CSoundFile &song){auto &sample=song.GetSample(1);sample.FreeSample();sample.nLength=length;sample.uFlags.set(CHN_16BIT,sixteen);sample.uFlags.set(CHN_STEREO,stereo);check(sample.AllocateSample()!=0,"Allocate boundary sample");for(uint32_t i=0;i<length*(stereo?2:1);++i){if(sixteen)sample.sample16()[i]=int16_t(1000+300*std::sin(i*.3));else sample.sample8()[i]=int8_t(12+3*std::sin(i*.3));}if(loop)sample.SetLoop(0,length,true,true,song);sample.PrecomputeLoops(song,false);});
  add(*d,baby,0,.03125,10000,2);
  const auto reference=render(*d,48000,1,.015625,[](Renderer &r,uint32_t){check(r.song().m_PlayState.Chn[0].nLength>0,"Scratch physical endpoint stays alive");});partitions(*d,48000,reference,.015625);
 }
 auto held=fixture(true);ScratchGesture hold{"Held",{{0,.5,AutomationCurve::Step,{}},{65536,.5,AutomationCurve::Step,{}}},baby.fader};add(*held,hold,0,.5,100);
 const auto quiet=render(*held,48000,1,.25);for(size_t frame=9600;frame<12000;++frame){near(quiet[2*frame],0,1e-8,"Held record decays to silence rather than DC");near(quiet[2*frame+1],0,1e-8,"Held stereo record decays to silence rather than DC");}partitions(*held,48000,quiet,.25);
 auto a=fixture(),b=fixture();add(*a,baby,0,.5,100);add(*b,baby,0,.5,100);b->annotate([](NativeSong &n){const auto p=n.patterns.at(0).id,t=n.tracks.at(0).id;PatternCommand c;c.pattern=p;c.track=t;c.column=1;c.kind=PatternCommandKind::NudgeReverse;c.value=1;c.durationBeats=1;n.performance.columns[t]=2;n.performance.commands.push_back(c);});
 check(render(*a,48000,17,.25)==render(*b,48000,17,.25),"SK takes precedence over NF/NR during its duration");
 // Open trajectories join end-to-start continuously instead of jumping to cue.
 auto open=fixture();ScratchGesture ramp{"Open",{{0,0,AutomationCurve::Linear,{}},{65536,1,AutomationCurve::Linear,{}}},baby.fader};add(*open,ramp,0,.5,20,2);
 const auto pcm=render(*open,48000,1,.25,[](Renderer &r,uint32_t frame){near(r.song().m_PlayState.Chn[0].position.ToDouble(),1920.*(frame+1)/12000.,2e-6,"Open repeated trajectory is continuous");});partitions(*open,48000,pcm,.25);
}
static void reverseAndBeatClock(){
 const ScratchGesture triangle{"Reverse oracle",{{0,0,AutomationCurve::Linear,{}},{32768,1,AutomationCurve::Linear,{}},{65536,0,AutomationCurve::Linear,{}}},{{0,1,AutomationCurve::Linear,{}},{65536,1,AutomationCurve::Linear,{}}}};
 constexpr double cue=32768;
 for(uint32_t rate:{44100u,48000u,96000u}) {
  std::array<std::vector<double>,2> paths;
  for(bool reverse:{false,true}) {
   auto d=fixture();add(*d,triangle,0,.5,100,1,reverse);
   d->annotate([](NativeSong &n){auto offset=command(n,0);offset.native=NativePatternOp::SampleOffset;offset.arguments=nativePatternDefaults(offset.native);offset.arguments[0]=cue;offset.column=1;n.performance.columns[offset.track]=2;n.performance.commands.push_back(offset);});
   paths[reverse].resize(uint32_t(rate*.25));double travel=0;
   const auto reference=render(*d,rate,1,.30,[&](Renderer &r,uint32_t frame){
    const auto &clock=r.song().m_PlayState;const auto &voice=clock.Chn[0];
    if(frame==0)travel=voice.increment.ToDouble()*rate*.1;
    if(frame<paths[reverse].size()) {
     // Independent raw tick counters describe the end of this output frame.
     // No scratch trajectory or native clock helper is used in this oracle.
     const double beat=(clock.m_nRow+(clock.m_nTickCount+double(clock.SamplesIntoTick())/clock.m_nSamplesPerTick)/clock.TicksOnRow())/4.;
     const double phase=std::min(1.,beat/.5),distance=travel*(phase<=.5?phase*2:2-phase*2);
     const double expected=cue+(reverse?-distance:distance);
     near(voice.position.ToDouble(),expected,3e-6,"Reverse option mirrors an independent trajectory around its nonzero cue");
     paths[reverse][frame]=voice.position.ToDouble();
    }
   });
   partitions(*d,rate,reference,.30);
  }
  check(paths[0][paths[0].size()/2]>cue+4000 && paths[1][paths[1].size()/2]<cue-4000,"Reverse option actually moves opposite to the forward baseline");
  for(size_t frame=0;frame<paths[0].size();++frame)near(paths[0][frame]+paths[1][frame],2*cue,3e-6,"Forward and reverse paths remain exact cue reflections");
  // Row zero lasts half a beat at 120 BPM; row one changes tempo to 240.
  // A .9-beat gesture therefore lasts .25 + .4*.25 = .35 seconds.
  auto timed=fixture();add(*timed,triangle,0,.9,100);
  timed->annotate([](NativeSong &n){auto c=command(n,0);n.performance.columns[c.track]=2;c.column=1;c.native=NativePatternOp::RowLength;c.arguments=nativePatternDefaults(c.native);c.arguments[0]=.5;n.performance.commands.push_back(c);c.position=65536;c.native=NativePatternOp::TempoSet;c.arguments=nativePatternDefaults(c.native);c.arguments[0]=240;n.performance.commands.push_back(c);});
  auto timedRender=[&](uint32_t block,bool inspect){
   Renderer renderer(timed->snapshotData(),rate,0,false,{},UINT32_MAX,{},&timed->native());renderer.preparePreciseNotes(timed->native());const auto frames=uint32_t(rate*.42);std::vector<float> pcm(frames*2);uint32_t released=UINT32_MAX;double travel=0;bool sawTempo=false,sawExtendedRow=false;
   for(uint32_t frame=0;frame<frames;frame+=block){const auto count=std::min(block,frames-frame);uint64_t a,b,c;tracker_audit_begin();renderer.render(pcm.data()+2*frame,count);tracker_audit_end(&a,&b,&c);check(!renderer.faulted()&&a+b+c==0,"Tempo/row-length scratch render remains realtime safe");if(!inspect)continue;
    const auto &clock=renderer.song().m_PlayState;const auto &voice=clock.Chn[0];if(!frame)travel=voice.increment.ToDouble()*rate*.1;
    const bool active=renderer.song().nativeScratchPositions[0]!=nullptr;
    if(active){
     const double rowBeats=clock.m_nRow==0?.5:.25,prior=clock.m_nRow==0?0:.5+(clock.m_nRow-1.)*.25;
     const double elapsed=prior+rowBeats*(clock.m_nTickCount+double(clock.SamplesIntoTick())/clock.m_nSamplesPerTick)/clock.TicksOnRow();
     const double phase=std::min(1.,elapsed/.9),expected=travel*(phase<=.5?phase*2:2-phase*2);
     near(voice.position.ToDouble(),expected,4e-6,"Scratch phase follows independent beat time across RL and tempo changes");
     sawExtendedRow|=clock.m_nRow==0&&elapsed>.3;sawTempo|=clock.m_nRow>0;
    } else if(released==UINT32_MAX)released=frame;
   }
   if(inspect){check(sawExtendedRow&&sawTempo,"Scratch crosses both the extended row and changed-tempo rows");check(released!=UINT32_MAX,"Timed scratch releases");near(released,rate*.35,1.01,"Scratch duration remains .9 beats across row length and tempo");}
   return pcm;
  };
  const auto reference=timedRender(1,true);for(auto block:{17u,128u,4096u}){const auto actual=timedRender(block,false);for(size_t i=0;i<actual.size();++i)near(actual[i],reference[i],3e-6,"Tempo/row-length scratch PCM is callback-partition independent");}
 }
}
static void live(){
 auto d=fixture(true);add(*d,scratchPresets()[0].gesture,0,1,100,2);Renderer renderer(d->snapshotData(),48000);renderer.preparePreciseNotes(d->native());std::array<float,512> out{};renderer.render(out.data(),256);const auto before=renderer.song().m_PlayState.Chn[0].position.ToDouble();
 auto updated=d->native();updated.scratchGestures[1].motion=scratchPresets()[6].gesture.motion;updated.scratchGestures[1].fader={{0,0,AutomationCurve::Step,{}},{65536,0,AutomationCurve::Step,{}}};auto plan=renderer.prepareScratchUpdate(updated);check(renderer.publishScratchUpdate(plan)&&!plan,"Publish immutable live bank update");
 uint64_t a,b,c;tracker_audit_begin();renderer.render(out.data(),256);tracker_audit_end(&a,&b,&c);check(!renderer.faulted()&&a+b+c==0,"Live curve adoption is realtime-safe");check(renderer.song().nativeScratchPositions[0],"Live bank update retains active gesture");near(renderer.song().nativeScratchPositions[0][0],before,1e-7,"Live update preserves captured trajectory at handoff");for(size_t i=80;i<out.size();++i)near(out[i],0,1e-8,"Live fader edits apply immediately after short ramp");
 // Rapid pending updates coalesce; producer reclaim never occurs in callback.
 for(int i=0;i<12;++i){updated.scratchGestures[1].name="Live "+std::to_string(i);auto p=renderer.prepareScratchUpdate(updated);check(renderer.publishScratchUpdate(p),"Pending update coalescing");}
 tracker_audit_begin();renderer.render(out.data(),256);tracker_audit_end(&a,&b,&c);check(a+b+c==0,"Coalesced update allocates/frees/locks zero times");
}
static void benchmark(){
 auto d=fixture();d->transaction([](CSoundFile &song){Document::resizeChannels(song,16);for(uint16_t channel=0;channel<16;++channel){auto &note=*song.Patterns[0].GetpModCommand(0,channel);note.note=uint8_t(61+channel%5);note.instr=1;note.volcmd=VOLCMD_VOLUME;note.vol=16;}});
 ScratchGesture gesture{"Scripted performance",{{0,.5,AutomationCurve::Scripted,CurveFormula("0.5 + 0.45*sin(t*tau)*sin(t*3*tau)")},{65536,.5,AutomationCurve::Linear,{}}},{{0,.5,AutomationCurve::Scripted,CurveFormula("0.5 + 0.5*tanh(12*sin(t*12*tau))")},{65536,.5,AutomationCurve::Linear,{}}}};
 d->annotate([&](NativeSong &n){n.scratchGestures[1]=gesture;for(uint16_t channel=0;channel<16;++channel){auto c=command(n,0,16,30,16);c.track=n.tracks.at(channel).id;c.column=1;n.performance.columns[c.track]=2;auto offset=c;offset.native=NativePatternOp::SampleOffset;offset.column=0;offset.arguments=nativePatternDefaults(offset.native);offset.arguments[0]=32768;n.performance.commands.push_back(offset);n.performance.commands.push_back(c);}});
 Renderer renderer(d->snapshotData(),48000);renderer.preparePreciseNotes(d->native());std::array<float,1024> out{};for(unsigned i=0;i<48;++i)renderer.render(out.data(),512);
 constexpr uint32_t callbacks=563;std::array<double,callbacks> times{};double peak=0,energy=0,mean=0;uint64_t allocations=0,frees=0,locks=0,misses=0;
 for(uint32_t i=0;i<callbacks;++i){uint64_t a,b,c;const auto start=std::chrono::steady_clock::now();tracker_audit_begin();renderer.render(out.data(),512);tracker_audit_end(&a,&b,&c);times[i]=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();allocations+=a;frees+=b;locks+=c;mean+=times[i];misses+=times[i]>512e6/48000;for(auto v:out){check(std::isfinite(v),"Finite benchmark audio");peak=std::max(peak,double(std::abs(v)));energy+=v*v;}}
 check(!renderer.faulted()&&!allocations&&!frees&&!locks,"Benchmark realtime audit failed");check(energy>0,"Benchmark audio must not be silent");for(size_t channel=0;channel<16;++channel)check(renderer.song().nativeScratchPositions[channel],"All sixteen scratch voices remain active");std::sort(times.begin(),times.end());
 std::cout<<std::setprecision(10)<<"{\"benchmark\":\"scratch-scripted\",\"scope\":\"Offline renderer; no device or UI; two compiled scripted curves per voice; timing includes audit guard\",\"sampleRate\":48000,\"bufferFrames\":512,\"voices\":16,\"audioSeconds\":"<<double(callbacks*512)/48000<<",\"meanUs\":"<<mean/callbacks<<",\"p99Us\":"<<times[size_t(std::ceil(.99*callbacks))-1]<<",\"maxUs\":"<<times.back()<<",\"deadlineUs\":"<<512e6/48000<<",\"deadlineMisses\":"<<misses<<",\"allocations\":"<<allocations<<",\"frees\":"<<frees<<",\"locks\":"<<locks<<",\"peak\":"<<peak<<"}\n";
}
int main(int argc,char **argv){try{if(argc>1 && std::string(argv[1])=="--benchmark"){benchmark();return 0;}model();audio();lifetime();edgesAndPrecedence();reverseAndBeatClock();live();std::cout<<"PASS scratch gestures, absolute cue return, sample-rate motion/fader, voice ownership, stop, realtime live publication and partition invariance\n";return 0;}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
