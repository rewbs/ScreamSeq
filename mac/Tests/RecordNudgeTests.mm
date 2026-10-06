#include "editor/TrackerDocument.hpp"
#include "editor/SongTiming.hpp"
#import "../Bridge/TrackerSession.h"
#include <cmath>
#include <iostream>
#include <iomanip>
using namespace Tracker;
using namespace OpenMPT;
#ifdef TRACKER_SANITIZER
static void tracker_audit_begin() {}
static void tracker_audit_end(uint64_t *a,uint64_t *f,uint64_t *l){*a=*f=*l=0;}
#else
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *,uint64_t *,uint64_t *);
#endif
static void check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
static double force(double t,double strength,bool reverse) {
  if(t<=0||t>=1)return 0;
  const double attack=.32-.16*strength;
  auto ease=[](double x){return 6*std::pow(x,5)-15*std::pow(x,4)+10*std::pow(x,3);};
  const double x=(t-attack)/(1-attack);
  const double shape=t<attack?ease(t/attack):(1-ease(x))*(1+.1*ease(x)*std::sin(3*M_PI*x)*std::sin(M_PI*x));
  return (reverse?-4:4)*strength*strength*shape;
}
static PatternCommand nudge(uint64_t pattern,uint64_t track,uint32_t position,double beats,uint8_t column,bool reverse,double strength){
 PatternCommand c;c.pattern=pattern;c.track=track;c.position=position;c.durationBeats=beats;c.column=column;c.kind=reverse?PatternCommandKind::NudgeReverse:PatternCommandKind::NudgeForward;c.value=strength;return c;
}
static void audioTest() {
  for(uint32_t rate:{44100u,48000u,96000u})for(bool reverse:{false,true})for(double strength:{0.,.25,.5,1.})for(bool backwards:{false,true}) {
    auto doc=Document::demo();doc->transaction([&](CSoundFile &s){
      for(auto &p:s.Patterns)if(p.IsValid())for(auto &cell:p)cell.Clear();
      s.Order().assign(1,0);s.Order().SetDefaultTempoInt(125);s.Order().SetDefaultSpeed(6);
      auto &note=*s.Patterns[0].GetpModCommand(0,0);note.note=61;note.instr=2;
      if(backwards){note.command=CMD_S3MCMDEX;note.param=0x9f;}
      auto &other=*s.Patterns[0].GetpModCommand(0,1);other.note=61;other.instr=2;
    });
    doc->annotate([&](NativeSong &n){n.performance.commands={nudge(n.patterns.at(0).id,n.tracks.at(0).id,8192,.375,0,reverse,strength)};});
    auto render=[&](uint32_t block,bool inspect){Renderer renderer(doc->snapshotData(),rate);renderer.preparePreciseNotes(doc->native());
      const auto total=rate*3/10;std::vector<float> out(total*2);double expected=0,base=0,initial=0;
      bool slowed=false,sped=false,reversed=false;
      for(uint32_t frame=0;frame<total;frame+=block){const auto count=std::min(block,total-frame);uint64_t a,f,l;tracker_audit_begin();
        renderer.render(out.data()+frame*2,count);tracker_audit_end(&a,&f,&l);
        check(!renderer.faulted()&&a+f+l==0,"Record nudges allocate/free/lock zero times in render");
        if(inspect){const auto &voice=renderer.song().m_PlayState.Chn[0];
          if(!frame){base=std::abs(double(voice.increment.GetRaw()));initial=voice.position.ToDouble()-double(voice.increment.GetRaw())/4294967296.;expected=initial;}
          const double t=(double(frame)/(rate*.48)-.03125)/.375;
          const double velocity=(backwards?-1.:1.)+force(t,strength,reverse);
          const double relative=velocity*(backwards?-1:1);
          slowed|=relative>0&&relative<.9;sped|=relative>1.1;reversed|=relative<-.01;
          expected+=double(int64_t(base*velocity))/4294967296.;
          if(force(t,strength,reverse)!=0 && std::abs(std::remainder(voice.position.ToDouble()-expected,256.))>3e-5) {
            std::cerr<<"rate "<<rate<<" strength "<<strength<<" reverse "<<reverse<<" backwards "<<backwards<<" frame "<<frame<<" position "<<voice.position.ToDouble()<<" expected "<<std::fmod(expected,256.)<<'\n';
            check(false,"Fractional sample phase integrates signed scratch velocity including zero and reverse wraps");
          }
          if(force(t,strength,reverse)==0)expected=voice.position.ToDouble();
          check(renderer.song().m_PlayState.Chn[1].increment.IsPositive(),"Adjacent channel stays forward");
        }
      }
      if(inspect&&strength){if(reverse!=backwards){check(slowed,"Opposing nudge slows sample");check(reversed==(strength>.5),"Only a hard opposing nudge reverses");}else check(sped&&!reversed,"Same-direction nudge speeds up");}
      check(std::all_of(out.begin(),out.end(),[](float v){return std::isfinite(v)&&std::abs(v)<2;}),"Finite bounded scratch audio");
      return out;};
    auto reference=render(1,true);for(auto block:{17u,128u,4096u}){auto other=render(block,false);
      for(size_t i=0;i<other.size();++i)if(std::abs(reference[i]-other[i])>3e-6){std::cerr<<"partition rate "<<rate<<" strength "<<strength<<" backwards "<<backwards<<" reverse "<<reverse<<" block "<<block<<" sample "<<i<<" delta "<<other[i]-reference[i]<<'\n';check(false,"Scratch PCM independent of callback partition");}}
  }
  RecordNudgeCurve curve;curve.trigger(0,1,1,true);const auto before=curve.at(.2);curve.trigger(.2,.5,.75,false);
  check(curve.at(.2)==before&&std::abs(curve.at(.2+1e-8)-before)<1e-10,"Alternating pushes preserve velocity and have a rounded attack");
  check(curve.at(.7)==0,"Curve fully recovers to nominal playback");
  curve.trigger(0,1,1,true);const double reversing=curve.at(.2);curve.trigger(.2,1,.25,true);
  check(curve.peak<reversing && reversing < -1,"Gentle reverse push increases an already reversing sample speed");
}
static void boundariesTest(){
  for(uint32_t rate:{8000u,48000u,192000u})for(int mode:{0,1,2,3,4})for(uint32_t length:{1,2,7,257})for(int bits:{8,16})for(int channels:{1,2}) {
    Document doc;auto &sample=doc.song().GetSample(1);sample.Initialize();sample.nLength=1024;sample.nC5Speed=48000;
    sample.uFlags.set(CHN_16BIT,bits==16);sample.uFlags.set(CHN_STEREO,channels==2);
    check(sample.AllocateSample()!=0,"Allocate boundary PCM");
    for(size_t i=0;i<1024*channels;++i){if(bits==16)sample.sample16()[i]=int16_t(12000*std::sin(i*.07));else sample.sample8()[i]=int8_t(50*std::sin(i*.07));}
    if(mode){sample.nLoopStart=129;sample.nLoopEnd=129+length;sample.uFlags.set(CHN_LOOP);sample.uFlags.set(CHN_PINGPONGLOOP,mode==2);if(mode==3)sample.nativeReverseLoops=1;
      if(mode==4){sample.uFlags.set(CHN_SUSTAINLOOP);sample.nSustainStart=130;sample.nSustainEnd=130+length;}}
    sample.PrecomputeLoops(doc.song(),false);doc.song().m_nSamples=1;
    auto &note=*doc.song().Patterns[0].GetpModCommand(0,0);note.note=61;note.instr=1;
    auto native=doc.native();native.reconcile(doc.song());const auto p=native.patterns.at(0).id,t=native.tracks.at(0).id;
    native.performance.columns[t]=2;
    native.performance.commands={nudge(p,t,512,60000./262144,0,true,1),nudge(p,t,20000,90000./262144,1,false,.875)};
    native.preciseNotes={{p,t,50000,0,255,127}};doc.restoreNative(native);
    auto render=[&](uint32_t block){Renderer r(doc.snapshotData(),rate);r.preparePreciseNotes(doc.native());std::vector<float> out(8192);
      for(uint32_t f=0;f<4096;f+=block){uint64_t a,b,c;tracker_audit_begin();r.render(out.data()+2*f,std::min(block,4096-f));tracker_audit_end(&a,&b,&c);check(!r.faulted()&&a+b+c==0,"Loop scratch is realtime-safe");
        if(mode && f+std::min(block,4096-f)<rate*.12*.7 && !r.song().m_PlayState.Chn[0].nLength){std::cerr<<"Premature stop rate "<<rate<<" mode "<<mode<<" length "<<length<<" frame "<<f<<'\n';check(false,"Looping voice survives before note-off");}}
      check(std::all_of(out.begin(),out.end(),[](float v){return std::isfinite(v);}),"Loop scratch stays finite");
      return out;};
    auto reference=render(1),actual=render(127);
    for(size_t i=0;i<reference.size();++i)if(std::abs(reference[i]-actual[i])>3e-6){std::cerr<<"boundary rate "<<rate<<" mode "<<mode<<" length "<<length<<" bits "<<bits<<" channels "<<channels<<" sample "<<i<<" error "<<actual[i]-reference[i]<<'\n';check(false,"Loop scratch partition invariance");}
  }
  auto doc=Document::demo();doc->transaction([](CSoundFile &s){for(auto &p:s.Patterns)if(p.IsValid())for(auto &c:p)c.Clear();auto &c=*s.Patterns[0].GetpModCommand(0,0);c.note=61;c.instr=2;});
  Renderer r(doc->snapshotData(),48000);std::array<float,256> pcm{};r.render(pcm.data(),128);const auto position=r.song().m_PlayState.Chn[0].position;
  std::array<double,4096> hold;hold.fill(-1);r.song().nativeNudgeForces[0]=hold.data();r.render(pcm.data(),128);
  check(r.song().m_PlayState.Chn[0].position==position&&r.song().m_PlayState.Chn[0].nLength>0,"Exact zero velocity holds sample without killing voice");
  hold.fill(0);r.render(pcm.data(),128);check(r.song().m_PlayState.Chn[0].position!=position,"Stopped platter resumes playback");
}
static void beatClockTest(){
 for(uint32_t rate:{44100u,48000u,96000u})for(uint32_t division:{2u,4u,8u})for(bool timed:{false,true}){
  if(timed&&division!=4)continue;
  auto doc=Document::demo();doc->transaction([&](CSoundFile &s){for(auto &p:s.Patterns)if(p.IsValid())for(auto &c:p)c.Clear();s.Order().assign(1,0);auto timing=songTiming(s);timing.mode=TempoMode::Modern;timing.rowsPerBeat=division;timing.rowsPerMeasure=4*division;timing.sequences[0]={1200000,6};timing.groove.clear();applySongTiming(s,timing);auto &note=*s.Patterns[0].GetpModCommand(0,0);note.note=61;note.instr=2;});
  const double duration=timed?.9:.75;
  doc->annotate([&](NativeSong &n){const auto p=n.patterns.at(0).id,t=n.tracks.at(0).id;n.performance.commands={nudge(p,t,0,duration,0,true,.8)};if(timed){n.performance.columns[t]=2;PatternCommand c;c.pattern=p;c.track=t;c.column=1;c.kind=PatternCommandKind::Native;c.native=NativePatternOp::RowLength;c.arguments=nativePatternDefaults(c.native);c.arguments[0]=.5;n.performance.commands.push_back(c);c.position=65536;c.native=NativePatternOp::TempoSet;c.arguments=nativePatternDefaults(c.native);c.arguments[0]=240;n.performance.commands.push_back(c);}});
  const uint32_t frames=uint32_t(rate*.5);std::vector<double> beats(frames);
  // For an unmodified clock all divisions have exact binary tick durations,
  // so the oracle independently places each rounded tick from musical time.
  {double exact=0,beat=0;const double step=1./division/6;for(uint32_t tick=0;exact<frames;++tick){const auto begin=uint32_t(std::floor(exact+1e-9));exact+=rate*60*step/120;const auto end=uint32_t(std::floor(exact+1e-9));for(auto f=begin;f<std::min(frames,end);++f)beats[f]=beat+step*double(f-begin)/(end-begin);beat+=step;}}
  auto render=[&](uint32_t block,bool inspect){Renderer r(doc->snapshotData(),rate,0,false,{},UINT32_MAX,{},&doc->native());r.preparePreciseNotes(doc->native());std::vector<float> out(frames*2);bool pushed=false,recovered=false;uint32_t recovery=UINT32_MAX;
   for(uint32_t frame=0;frame<frames;frame+=block){const auto count=std::min(block,frames-frame);uint64_t a,b,c;tracker_audit_begin();r.render(out.data()+frame*2,count);tracker_audit_end(&a,&b,&c);check(!r.faulted()&&a+b+c==0,"Beat-duration nudge is realtime-safe");if(inspect){
    double beat=beats[frame];
    if(timed){
     // RL/TS keeps fractional sample carry in the timing engine. Derive the
     // requested musical phase from raw tick counters, independently of both
     // NativeBeatStep and the nudge curve; a rounded tick can differ by one
     // sample from rounding its rational closed-form cumulative duration.
     const auto &clock=r.song().m_PlayState;const double rowBeats=clock.m_nRow==0?.5:1./division;
     const double prior=clock.m_nRow==0?0:.5+(clock.m_nRow-1.)/division;
     check(clock.SamplesIntoTick()>0,"Rendered frame advances raw tick sample counter");
     beat=prior+rowBeats*(clock.m_nTickCount+double(clock.SamplesIntoTick()-1)/clock.m_nSamplesPerTick)/clock.TicksOnRow();
    }
    const auto *values=r.song().nativeNudgeForces[0];const double actual=values?values[0]:0,expected=force(beat/duration,.8,true);if(std::abs(actual-expected)>2e-8){std::cerr<<std::setprecision(17)<<"beat curve rate "<<rate<<" division "<<division<<" timed "<<timed<<" frame "<<frame<<" actual "<<actual<<" expected "<<expected<<" beatOracle "<<beat<<" rowAfter "<<r.song().m_PlayState.NativeRowPosition(1)<<" beatStep "<<r.song().m_PlayState.NativeBeatStep()<<" tick "<<r.song().m_PlayState.m_nTickCount<<" tickSamples "<<r.song().m_PlayState.m_nSamplesPerTick<<'\n';check(false,"Nudge recovery follows actual musical beats across signatures, RL and tempo");}pushed|=actual<-.5;if(pushed&&actual==0&&recovery==UINT32_MAX)recovery=frame;if(beat>duration+1e-6){check(actual==0,"Nudge fully recovers after its beat duration");recovered=true;}}}
   if(inspect){check(pushed&&recovered,"Beat fixture renders both push and complete recovery");const double seconds=timed?.25+(duration-.5)*.25:duration*.5;check(std::abs(double(recovery)-rate*seconds)<=1.01,"Beat recovery time agrees across signatures within one audio sample");}return out;};
  const auto reference=render(1,true);for(auto block:{17u,128u,4096u}){const auto actual=render(block,false);for(size_t i=0;i<actual.size();++i)check(std::abs(actual[i]-reference[i])<3e-6,"Beat-duration scratch PCM is callback-partition independent");}
 }
}
static void apiTest(){
  TrackerSession *session=[TrackerSession new];NSError *error=nil;
  auto call=[&](NSString *method,NSDictionary *params,bool write=false)->NSDictionary * {
    auto p=[params mutableCopy];if(write)p[@"expectedRevision"]=session.automationRevision;
    auto result=[session automationMethod:method params:p error:&error];if(!result)throw std::runtime_error(error.localizedDescription.UTF8String);return result;
  };
  auto commands=@[@{@"channel":@0,@"column":@0,@"position":@12345,@"durationBeats":@.375123456,@"kind":@"nudge-forward",@"value":@.23456789},
    @{@"channel":@1,@"column":@0,@"position":@80000,@"durationBeats":@.500234567,@"kind":@"nudge-reverse",@"value":@.87654321}];
  NSString *before=session.automationRevision;
  call(@"pattern.effects.set",@{@"pattern":@0,@"commands":commands,@"dryRun":@YES},true);check([before isEqual:session.automationRevision],"Dry run unchanged");
  call(@"pattern.effects.set",@{@"pattern":@0,@"commands":commands},true);
  const auto expected=call(@"pattern.effects.get",@{@"pattern":@0})[@"data"];
  NSString *revision=session.automationRevision;
  call(@"pattern.effects.set",@{@"pattern":@0,@"commands":commands},true);check([revision isEqual:session.automationRevision],"Identical nudge is no-op");
  for(NSDictionary *invalid in @[@{@"value":@(-.1)},@{@"value":@1.1},@{@"value":@YES},@{@"durationBeats":@0},@{@"durationBeats":@(-.1)},@{@"durationBeats":@YES},@{@"duration":@65536},@{@"binding":@1},@{@"pitchRange":@12},@{@"effect":@1},@{@"durationBeats":@(UINT32_MAX)}]){
    auto c=[commands[0] mutableCopy];[c addEntriesFromDictionary:invalid];NSError *failure=nil;
    check(![session automationMethod:@"pattern.effects.set" params:@{@"pattern":@0,@"commands":@[c],@"expectedRevision":revision} error:&failure],"Invalid scratch edit rejected atomically");
    check([revision isEqual:session.automationRevision],"Invalid edit leaves revision unchanged");
  }
  check(![session automationMethod:@"pattern.effects.set" params:@{@"pattern":@0,@"commands":@[],@"expectedRevision":before} error:&error],"Stale revision rejected");
  call(@"history.undo",@{@"domain":@"document"},true);check([call(@"pattern.effects.get",@{@"pattern":@0})[@"data"][@"commands"] count]==0,"Scratch Undo");
  call(@"history.redo",@{@"domain":@"document"},true);
  NSString *path=[NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".screamseq"]];
  check([session savePath:path error:&error]&&[session openPath:path error:&error],"Scratch native save/reopen");
  check([expected isEqual:call(@"pattern.effects.get",@{@"pattern":@0})[@"data"]],"Scratch exact persistence");
  [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
}
int main(){@autoreleasepool{try{audioTest();boundariesTest();beatClockTest();apiTest();std::cout<<"PASS record nudge signed phase, intensity, direction, beat recovery across signatures/tempo/row length, partition invariance, realtime audit, API/Undo/persistence\n";return 0;}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}}
