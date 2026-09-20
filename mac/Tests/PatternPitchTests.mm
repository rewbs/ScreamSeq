#include "../Audio/AudioUnitHost.hpp"
#import "../Bridge/TrackerSession.h"
#include "editor/TrackerDocument.hpp"
#include "soundlib/ModInstrument.h"
#include <dlfcn.h>
#include <iostream>
using namespace Tracker;
using namespace OpenMPT;
std::vector<PluginDescriptor> registerFixtureAUs();
void setFixtureAUPitchMode(bool);
#ifdef TRACKER_SANITIZER
static void tracker_audit_begin() {}
static void tracker_audit_end(uint64_t *a,uint64_t *f,uint64_t *l){*a=*f=*l=0;}
#else
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *,uint64_t *,uint64_t *);
#endif
static void check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
static double referencePitch(double position) {
  double value=0;
  if(position>=8192)value=2*std::min(1.,(position-8192)/98304);
  if(position>=75536){const double previous=2*(75536.-8192)/98304;value=previous+(-1-previous)*std::min(1.,(position-75536)/50000);}
  if(position>=196608)value=0;
  return value;
}
static void commands(Document &doc) {
  doc.annotate([](NativeSong &n){const auto pattern=n.patterns.at(0).id,track=n.tracks.at(0).id;
    n.performance.columns[track]=2;n.performance.commands={
      {pattern,track,0,0,0,PatternCommandKind::PitchSet,0,0},
      {pattern,track,8192,98304,1,PatternCommandKind::PitchSlide,0,2},
      {pattern,track,75536,50000,0,PatternCommandKind::PitchSlide,0,-1},
      {pattern,track,196608,0,0,PatternCommandKind::PitchSet,0,0}};
  });
}
static void sessionTest() {
  TrackerSession *session=[TrackerSession new];NSError *error=nil;
  auto call=[&](NSString *method,NSDictionary *params,bool write=false)->NSDictionary * {
    auto request=[params mutableCopy];if(write)request[@"expectedRevision"]=session.automationRevision;
    auto result=[session automationMethod:method params:request error:&error];
    if(!result)throw std::runtime_error(error.localizedDescription.UTF8String);return result;
  };
  NSDictionary *bend=@{@"channel":@0,@"position":@12345,@"duration":@90001,@"column":@0,@"kind":@"pitch-slide",@"value":@(-3.125),@"pitchRange":@12};
  NSDictionary *request=@{@"pattern":@0,@"columns":@[@{@"channel":@0,@"count":@1}],@"commands":@[bend]};
  NSString *before=session.automationRevision;auto preview=[request mutableCopy];preview[@"dryRun"]=@YES;
  check(![call(@"pattern.performance.set",preview,true)[@"changed"] boolValue]&&[before isEqual:session.automationRevision],"Pitch preview leaves document intact");
  call(@"pattern.performance.set",request,true);
  const auto expected=call(@"pattern.performance.get",@{@"pattern":@0})[@"data"];
  check([expected[@"commands"][0][@"pitchRange"] intValue]==12&&[expected[@"commands"][0][@"value"] doubleValue]==-3.125,"Pitch API retains signed value and explicit range");
  for(NSDictionary *invalid in @[@{@"pitchRange":@0},@{@"pitchRange":@97},@{@"value":@97},@{@"value":@(-97)},@{@"duration":@0},@{@"binding":@1}]) {
    auto command=[bend mutableCopy];[command addEntriesFromDictionary:invalid];
    NSError *failure=nil;NSString *revision=session.automationRevision;
    check(![session automationMethod:@"pattern.performance.set" params:@{@"pattern":@0,@"commands":@[command],@"expectedRevision":revision} error:&failure],"Invalid pitch fields rejected");
    check([revision isEqual:session.automationRevision],"Rejected pitch edit preserves revision");
  }
  call(@"history.undo",@{@"domain":@"document"},true);check([call(@"pattern.performance.get",@{@"pattern":@0})[@"data"][@"commands"] count]==0,"Pitch Undo removes commands");
  call(@"history.redo",@{@"domain":@"document"},true);
  NSString *path=[NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".resonance"]];
  check([session savePath:path error:&error]&&[session openPath:path error:&error],"Pitch project saves and reopens");
  check([expected isEqual:call(@"pattern.performance.get",@{@"pattern":@0})[@"data"]],"Version 8 preserves exact pitch metadata");
  [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
}
int main(int argc,char **argv){@autoreleasepool{try{
  check(argc==2,"Fixture bundle required");
  void *bundle=dlopen((std::string(argv[1])+"/Contents/MacOS/ResonanceFixture").c_str(),RTLD_NOW);
  check(bundle,"Open local pitch fixture");
  auto mode=reinterpret_cast<void(*)(bool)>(dlsym(bundle,"ResonanceFixturePitchMode"));check(mode,"Pitch fixture hook");mode(true);setFixtureAUPitchMode(true);
  const auto vst=NativePlugin::discoverVST3(argv[1]),au=registerFixtureAUs();
  for(const auto &descriptor:{vst[1],au[1]})for(uint32_t rate:{44100u,48000u,96000u}) {
    Document doc;doc.transaction([](CSoundFile &s){check(s.Patterns[0].Resize(4),"Resize pitch pattern");s.Order().assign(3,0);
      s.Order().SetDefaultTempoInt(125);s.Order().SetDefaultSpeed(6);s.m_nInstruments=1;s.Instruments[1]=new ModInstrument(0);
      auto &note=*s.Patterns[0].GetpModCommand(0,0);note.note=61;note.instr=1;});
    commands(doc);
    PluginState state{descriptor};state.instanceID="pitch-synth";state.instrument=1;
    auto render=[&](uint32_t block){Renderer renderer(doc.snapshotData(),rate);PluginChain chain({state},rate,true);
      chain.attachInstruments(renderer);chain.attachMusicalAutomation(renderer,doc.native());
      const auto total=rate*9/10;std::vector<float> out(total*2);
      for(uint32_t frame=0;frame<total;frame+=block){const auto count=std::min(block,total-frame);uint64_t a,f,l;tracker_audit_begin();
        renderer.render(out.data()+frame*2,count);const bool ok=chain.process(out.data()+frame*2,count);tracker_audit_end(&a,&f,&l);
        check(ok&&!renderer.faulted(),"Pitch-controlled native instrument renders");check(a+f+l==0,"Pitch event scheduling allocates, frees and locks zero times");}
      return out;};
    const auto expected=render(1);const auto rowFrames=rate*12/100;
    for(size_t frame=0;frame<expected.size()/2;++frame){
      const double position=double(frame%(rowFrames*4))*65536/rowFrames;
      const int wheel=std::clamp(int(std::lround(referencePitch(position)/2*8192))+8192,0,16383);
      const double value=.1*wheel/8192;
      if(std::abs(expected[frame*2]-value)>3e-7){std::cerr<<descriptor.format<<" rate "<<rate<<" frame "<<frame<<" got "<<expected[frame*2]<<" expected "<<value<<'\n';check(false,"Every sample receives the independently expected MIDI bend");}
    }
    for(auto block:{17u,128u,4096u})check(render(block)==expected,"Pitch delivery is bit-exact across callback sizes");
    doc.transaction([](CSoundFile &s){s.Patterns[0].GetpModCommand(0,0)->Clear();});
    doc.annotate([&](NativeSong &n){const auto p=n.patterns.at(0).id,t=n.tracks.at(0).id;
      n.preciseNotes={{p,t,1234,1,61,93},{p,t,30001,0,255,127},{p,t,70000,1,65,77},{p,t,180003,0,255,127}};
      n.performance.columns[t]=4;n.performance.bindings[1]={state.instanceID,7,"Gain"};
      n.performance.commands.push_back({p,t,0,0,2,PatternCommandKind::ParameterSet,1,.2});
      n.performance.commands.push_back({p,t,3000,120000,3,PatternCommandKind::ParameterSlide,1,.8});
    });
    const auto combined=render(1);
    for(size_t frame=0;frame<combined.size()/2;++frame) {
      const double p=double(frame%(rowFrames*4))*65536/rowFrames;bool active=false;
      for(const auto &note:doc.native().preciseNotes)if(p+1e-8>=note.position)active=note.note<128;
      const auto wheel=std::clamp(int(std::lround(referencePitch(p)/2*8192))+8192,0,16383);
      const auto gain=.2+.6*std::clamp((p-3000)/120000,0.,1.);
      check(std::abs(combined[frame*2]-(active?.2*gain*wheel/8192:0))<3e-7,"Precise notes, parameter slides and pitch bends share the same independent sample clock");
    }
    for(auto block:{17u,128u,4096u}){const auto other=render(block);for(size_t i=0;i<other.size();++i)
      check(std::abs(other[i]-combined[i])<3e-8,"Combined native control paths retain sample accuracy across callback sizes");}

  }
  mode(false);setFixtureAUPitchMode(false);
  for(uint32_t rate:{44100u,48000u,96000u}) {
    auto doc=Document::demo();doc->transaction([](CSoundFile &s){
      for(auto &p:s.Patterns)if(p.IsValid())for(auto &cell:p)cell.Clear();
      s.Order().assign(1,0);s.Order().SetDefaultTempoInt(125);s.Order().SetDefaultSpeed(6);
      auto &note=*s.Patterns[0].GetpModCommand(0,0);note.note=61;note.instr=2;
    });commands(*doc);
    auto render=[&](uint32_t block,bool verify){Renderer renderer(doc->snapshotData(),rate);PluginChain chain({},rate,true);chain.attachInstruments(renderer);chain.attachMusicalAutomation(renderer,doc->native());
      const auto total=rate*4/10;std::vector<float> out(total*2);double expectedPosition=0,base=0;
      for(uint32_t frame=0;frame<total;frame+=block){const auto count=std::min(block,total-frame);uint64_t a,f,l;tracker_audit_begin();
        renderer.render(out.data()+frame*2,count);const bool ok=chain.process(out.data()+frame*2,count);tracker_audit_end(&a,&f,&l);
        check(ok&&!renderer.faulted()&&a+f+l==0,"Sample bend uses safe native interpolation");
        if(verify){const auto &voice=renderer.song().m_PlayState.Chn[0];if(!frame)base=voice.increment.GetRaw();
          for(uint32_t i=0;i<count;++i){const auto semitones=referencePitch(double(frame+i)*65536/(rate*.12));
            expectedPosition+=double(int64_t(base*std::exp2(semitones/12)))/4294967296.;}
          const auto expected=std::fmod(expectedPosition,256.);
          if(std::abs(std::remainder(voice.position.ToDouble()-expected,256.))>2e-5){std::cerr<<"Sample position "<<voice.position.ToDouble()<<" expected "<<expected<<'\n';check(false,"Sample phase integrates independent per-sample pitch curve");}
        }
      }return out;};
    const auto reference=render(1,true);
    for(auto block:{17u,128u,4096u}){const auto other=render(block,false);
      for(size_t i=0;i<reference.size();++i)check(std::abs(reference[i]-other[i])<2e-6,"Bent sample audio is callback-size independent");}
    check(std::any_of(reference.begin(),reference.end(),[](float sample){return std::abs(sample)>.001f;}),"Pitch test produces real sample audio");
  }
  sessionTest();
  std::cout<<"PASS sub-tick sample phase, continuous pitch interpolation, AU/VST3 MIDI pitch at every sample, interruption, repeated patterns, three rates, four callback sizes and realtime audit\n";
  return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}}
