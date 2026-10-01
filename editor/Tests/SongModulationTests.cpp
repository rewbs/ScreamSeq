#include "editor/SongModulation.hpp"
#ifdef __APPLE__
#include "mac/Tests/GraphRealtimeAudit.hpp"
#endif
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace Tracker;
static void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class F>static void rejects(F action){bool rejected=false;try{action();}catch(const std::invalid_argument &){rejected=true;}check(rejected,"Invalid modulation configuration accepted");}
static std::array<SongModulationParameter,1> metadata{{{"effect",7,0,true,true}}};
static SignalGraph graph(SignalNodeKind kind) {
  SignalGraph g;SignalSongSource s;s.node.id=1;s.node.kind=kind;g.songSources.push_back(s);g.songModulation.push_back({1,"effect",7,0,1});return g;
}
static SignalClock clockAt(uint32_t at,bool playing=true) {return {.3+at/24000.,120,playing,91,.4+at*.8,.8,160,4};}
static void partitionInvariance() {
  for(auto kind:{SignalNodeKind::LFO,SignalNodeKind::Random,SignalNodeKind::Automation}) {
    auto g=graph(kind);g.songSources[0].node.rate=kind==SignalNodeKind::Random?1024:3;
    for(auto curve:{AutomationCurve::Linear,AutomationCurve::Smooth,AutomationCurve::Exponential,AutomationCurve::Logarithmic,AutomationCurve::Step,AutomationCurve::StepNext,AutomationCurve::Scripted}) {
      if(kind==SignalNodeKind::Automation)g.songSources[0].node.envelopes={{91,true,{{0,.2,curve},{21,.9,curve},{71,.1,curve}}}};
      if(curve==AutomationCurve::Scripted&&kind==SignalNodeKind::Automation)for(auto &p:g.songSources[0].node.envelopes[0].points)p.formula=CurveFormula("mix(start,end,t*t)+sin(beat)*0.02");
      std::array<double,128> reference{};bool referenceReady=false;
      for(auto block:{1u,7u,17u,128u}) {
        SongModulationRuntime r(g,metadata,48000);std::array<double,128> rendered{};
        for(uint32_t at=0;at<128;){const auto count=std::min(block,128-at);check(r.renderSource(0,count,13+at,clockAt(at)),"Render failed");
          for(uint32_t i=0;i<count;++i){double value;check(r.overlay(0,13+at+i,0,value),"Overlay source unavailable");rendered[at+i]=value;}
          at+=count;
        }
        if(referenceReady)for(size_t i=0;i<128;++i)check(std::abs(rendered[i]-reference[i])<1e-12,"Modulation depends on callback partition");else {reference=rendered;referenceReady=true;}
      }
      if(kind==SignalNodeKind::Random)for(uint32_t i=0;i<128;++i){const auto beat=clockAt(i).beat;uint64_t x=uint64_t(int64_t(std::floor(beat*1024)))^0x9e3779b97f4a7c15ULL;x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;x=(x^(x>>27))*0x94d049bb133111ebULL;x^=x>>31;check(std::abs(reference[i]-double(x>>11)/9007199254740991.)<1e-12,"Random boundary became an interpolated ramp");}
    }
  }
  auto step=graph(SignalNodeKind::Automation);step.songSources[0].node.envelopes={{91,true,{{0,.2,AutomationCurve::Step},{21,.9,AutomationCurve::StepNext},{71,.1,AutomationCurve::Linear}}}};
  SongModulationRuntime r(step,metadata,48000);SignalClock c{0,120,true,91,0,1,128,4};check(r.renderSource(0,128,13,c),"Step render failed");
  for(size_t i=0;i<128;++i)check(std::abs(r.values(0)[i]-automationValue(step.songSources[0].node.envelopes[0].points,i,128,4))<1e-12,"Point or step-next boundary is imprecise");
  c.position=21;check(r.rampFrames(0,34,64,c)==1,"Step-next exact point was not isolated");
  c.pattern=92;check(r.renderSource(0,128,0,c),"Empty pattern render failed");for(auto value:r.values(0))check(value==0,"Missing pattern retains a previous curve");
  step.songSources[0].node.envelopes={{91,true,{{0,0,AutomationCurve::Scripted,CurveFormula("t")}}}};
  SongModulationRuntime final(step,metadata,48000);c.pattern=91;c.position=0;check(final.renderSource(0,128,0,c),"Final script render failed");check(std::abs(final.values(0)[127]-127./128)<1e-12,"Final scripted segment ignored the pattern end");
}
static void overlaysAndValidation() {
  auto g=graph(SignalNodeKind::Amount);g.songModulation[0].maximum=.7;
  auto second=g.songSources[0];second.node.id=2;g.songSources.push_back(second);g.songModulation.push_back({2,"effect",7,0,-.8});
  SongModulationRuntime r(g,metadata,48000);double value=-7;check(!r.overlay(0,0,.8,value)&&value==-7,"Missing source changed an output value");
  for(size_t source=0;source<2;++source)check(r.renderSource(source,32,0,{}),"Amount render failed");
  for(unsigned repeat=0;repeat<50;++repeat){check(r.overlay(0,0,.8,value)&&std::abs(value-.7)<1e-12,"Contributions clamp before summing or accumulate over the baseline");}
  check(r.overlay(0,0,.2,value)&&std::abs(value-.1)<1e-12,"A changing pattern baseline was replaced by a fixed graph base");
  r.amount(2,.5);check(!r.overlay(0,0,.2,value),"Live control event exposed a stale future source buffer");r.renderSource(1,32,0,{});check(r.overlay(0,0,.2,value)&&std::abs(value-.5)<1e-12,"Inverse source range or live amount update failed");
  check(!r.overlay(0,32,.2,value),"Stale source frame accepted");
  auto stepped=metadata;stepped[0].normalizedStep=.25;stepped[0].continuous=false;rejects([&]{SongModulationRuntime invalid(g,stepped,48000);});
  for(auto &edge:g.songModulation)edge.quantized=true;
  SongModulationRuntime q(g,stepped,48000);for(size_t i=0;i<2;++i)q.renderSource(i,32,0,{});
  check(q.overlay(0,0,.72,value)&&value==.5&&q.rampFrames(0,0,32,{})==1,"Stepped target was not explicitly quantized at the final value");
  stepped[0].writable=false;rejects([&]{SongModulationRuntime invalid(g,stepped,48000);});
  auto malformed=graph(SignalNodeKind::LFO);malformed.songSources[0].node.attack=0;rejects([&]{SongModulationRuntime invalid(malformed,metadata,48000);});
  malformed=graph(SignalNodeKind::LFO);malformed.songModulation[0].source=999;rejects([&]{SongModulationRuntime invalid(malformed,metadata,48000);});
  malformed=graph(SignalNodeKind::LFO);malformed.songSources.resize(65);rejects([&]{SongModulationRuntime invalid(malformed,metadata,48000);});
  rejects([&]{SongModulationRuntime invalid(graph(SignalNodeKind::LFO),metadata,0);});
  rejects([&]{SongModulationRuntime invalid(graph(SignalNodeKind::LFO),{},48000);});
  SignalClock bad;bad.beat=std::numeric_limits<double>::infinity();check(!r.renderSource(0,1,0,bad),"Nonfinite clock accepted");
  check(!r.overlay(0,0,.2,value),"A failed render exposed stale source samples");
  check(!r.renderSource(0,4097,0,{})&&!r.renderSource(0,2,UINT64_MAX,{}),"Invalid render bounds accepted");
}
static void notesAndFollowers() {
  auto notes=graph(SignalNodeKind::NoteEnvelope);notes.songSources[0].noteTarget=12;
  SongModulationRuntime n(notes,metadata,48000);n.note(13,0,true,true);n.renderSource(0,32,0,{});check(n.values(0).back()==0,"Note scope admitted another channel");
  n.note(12,0,true,true);n.renderSource(0,64,32,{});const double peak=1-std::exp(-64./480);check(std::abs(n.values(0).back()-peak)<1e-12,"Attack is not sample timed");
  n.note(12,0,true,false);n.note(12,0,false);n.renderSource(0,64,96,{});check(n.values(0).back()>peak,"One released voice closed a still-held envelope");
  n.note(12,0,true,true);n.renderSource(0,64,160,{});check(std::abs(n.values(0).back()-peak)<1e-12,"Retrigger failed");
  n.panic();n.renderSource(0,64,224,{});check(std::abs(n.values(0).back()-peak*std::exp(-64./4800))<1e-12,"Panic/release is not sample timed");
  auto follower=graph(SignalNodeKind::Follower);follower.songSources[0].audioBus=12;
  std::array<float,512> pcm{};for(size_t i=0;i<64;++i){pcm[i*2]=.25f;pcm[i*2+1]=1;}
  std::array<double,256> reference{};bool referenceReady=false;
  for(auto block:{1u,7u,31u,256u}){SongModulationRuntime f(follower,metadata,48000);std::array<double,256> rendered{};
    for(uint32_t at=0;at<256;){const auto count=std::min(block,256-at);check(f.renderSource(0,count,at,{},pcm.data()+at*2),"PCM follower failed");for(uint32_t i=0;i<count;++i)rendered[at+i]=f.values(0)[i];at+=count;}
    check(std::abs(rendered[63]-peak)<1e-12&&std::abs(rendered[255]-peak*std::exp(-192./4800))<1e-12,"Follower did not read current stereo PCM with attack/release");
    if(referenceReady)for(size_t i=0;i<256;++i)check(std::abs(rendered[i]-reference[i])<1e-12,"Follower depends on callback partition");else {reference=rendered;referenceReady=true;}
  }
  SongModulationRuntime f(follower,metadata,48000);check(!f.renderSource(0,64,0,{}),"Connected follower accepted missing PCM");
  f.renderSource(0,64,0,{},pcm.data());SongModulationRuntime replacement(follower,metadata,48000);replacement.inheritState(f);replacement.renderSource(0,64,64,{},pcm.data()+128);check(std::abs(replacement.values(0).back()-peak*std::exp(-64./4800))<1e-12,"Control publication reset follower history");
  auto disconnected=follower;disconnected.songSources[0].audioBus=0;SongModulationRuntime silent(disconnected,metadata,48000);silent.renderSource(0,64,0,{},pcm.data());check(silent.values(0).back()==0,"Disconnected follower inherited an unrelated channel tap");
  for(unsigned mode=0;mode<4;++mode){auto rerouted=follower;auto &tap=rerouted.songSources[0];
    if(mode==0)tap.preFader=true;else if(mode==1)tap.audioBus=13;else if(mode==2){tap.audioBus=0;tap.audioPlugin="new-input";tap.output=1;}else tap.audioBus=0;
    SongModulationRuntime moved(rerouted,metadata,48000);moved.inheritState(f);moved.renderSource(0,64,64,{},mode==3?nullptr:pcm.data()+128);
    check(std::abs(moved.values(0).back()-peak*std::exp(-64./4800))<1e-12,"Moving/cutting a follower cable reset the retained AR processor");
  }
  pcm[0]=std::numeric_limits<float>::quiet_NaN();pcm[1]=std::numeric_limits<float>::infinity();check(f.renderSource(0,64,64,{},pcm.data())&&std::isfinite(f.values(0)[0]),"Invalid PCM poisoned a follower");
  auto midi=graph(SignalNodeKind::MIDI);midi.songSources[0].node.controller=74;SongModulationRuntime m(midi,metadata,48000);m.controller(74,64./127);m.renderSource(0,32,0,{});check(std::abs(m.values(0)[0]-64./127)<1e-12,"MIDI CC source failed");
  SongModulationRuntime next(midi,metadata,48000);next.inheritState(m);next.renderSource(0,32,32,{});check(next.values(0)[0]==m.values(0)[0],"Publication discarded held MIDI controls");
}
static void realtimeAudit() {
  auto g=graph(SignalNodeKind::Automation);g.songSources[0].node.envelopes={{91,true,{{0,.2,AutomationCurve::Scripted,CurveFormula("mix(start,end,t*t)+sin(beat)*0.01")},{127,.8,AutomationCurve::Smooth}}}};
  for(uint64_t id=2;id<=8;++id){auto source=g.songSources[0];source.node.id=id;source.node.kind=id%2?SignalNodeKind::LFO:SignalNodeKind::NoteEnvelope;source.node.envelopes.clear();g.songSources.push_back(source);g.songModulation.push_back({id,"effect",7,-.01,.01});}
  SongModulationRuntime r(g,metadata,48000),next(g,metadata,48000);r.renderSource(0,128,0,clockAt(0));bool success=true;double checksum=0;
#ifdef __APPLE__
  tracker_audit_begin();
#endif
  for(uint32_t block=0;block<500;++block){r.controller(1,.5);r.note(0,0,true,true);r.note(0,0,false);r.amount(1,.5);
    for(size_t source=0;source<r.sourceCount();++source)success&=r.renderSource(source,128,uint64_t(block)*128,clockAt(0));
    for(uint32_t i=0;i<128;++i){double value=0;success&=r.overlay(0,uint64_t(block)*128+i,.2,value);checksum+=value;}
    next.inheritState(r);r.panic();
  }
#ifdef __APPLE__
  uint64_t allocations,frees,locks;tracker_audit_end(&allocations,&frees,&locks);check(!allocations&&!frees&&!locks,"Song modulation callback allocated, freed or locked");
#endif
  check(success&&checksum>0&&r.storageBytes()<4*1024*1024,"Realtime evaluation failed or exceeded bounded storage");
}
int main(){try{partitionInvariance();overlaysAndValidation();notesAndFollowers();realtimeAudit();std::cout<<"PASS song modulation: absolute-grid/step/script/random invariance, live baseline additive overlays, explicit quantization, scoped held notes, current-PCM follower AR, publication inheritance, invalid/stale guards, zero callback allocations/frees/locks\n";return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
