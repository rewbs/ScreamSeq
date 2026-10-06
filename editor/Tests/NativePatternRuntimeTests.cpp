#include "editor/TrackerDocument.hpp"
#include "soundlib/ModInstrument.h"
#include <cmath>
#include <iostream>
#include <functional>
using namespace Tracker;
using namespace OpenMPT;
#if defined(TRACKER_REALTIME_AUDIT) && !defined(TRACKER_SANITIZER)
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *,uint64_t *,uint64_t *);
#else
static void tracker_audit_begin(){}
static void tracker_audit_end(uint64_t *a,uint64_t *f,uint64_t *l){*a=*f=*l=0;}
#endif
static void check(bool value,const char *why){if(!value)throw std::runtime_error(why);}
static void near(double value,double expected,double tolerance,const char *why){if(std::abs(value-expected)>tolerance){std::cerr<<why<<": got "<<value<<", expected "<<expected<<'\n';throw std::runtime_error(why);}}
static std::unique_ptr<Document> fixture(bool constant=false,bool instrument=false){
 auto d=std::make_unique<Document>();d->transaction([&](CSoundFile &s){
  for(auto &p:s.Patterns)if(p.IsValid())for(auto &c:p)c.Clear();
  check(s.Patterns[0].Resize(8),"Resize native effect fixture");s.Order().assign(1,0);s.Order().SetDefaultTempoInt(125);s.Order().SetDefaultSpeed(6);
  s.m_nSamples=1;auto &sample=s.GetSample(1);sample.Initialize();sample.nLength=131072;sample.nC5Speed=8192;sample.uFlags.set(CHN_16BIT);
  check(sample.AllocateSample()!=0,"Allocate fixture PCM");for(size_t i=0;i<sample.nLength;++i)sample.sample16()[i]=constant?1200:int16_t(2000*std::sin(i*.031));sample.PrecomputeLoops(s,false);
  s.ChnSettings[0].nPan=128;s.ChnSettings[1].nPan=256;
  if(instrument){s.m_nInstruments=1;s.Instruments[1]=new ModInstrument(1);s.Instruments[1]->nNNA=NewNoteAction::Continue;}
  auto &note=*s.Patterns[0].GetpModCommand(0,0);note.note=61;note.instr=1;
 });return d;
}
static PatternCommand command(const NativeSong &n,NativePatternOp op,uint32_t position=8192,uint32_t duration=0,uint8_t column=0){
 PatternCommand c;c.pattern=n.patterns.at(0).id;c.track=n.tracks.at(0).id;c.kind=PatternCommandKind::Native;c.native=op;c.position=position;c.duration=duration;c.column=column;c.arguments=nativePatternDefaults(op);return c;
}
static std::vector<float> render(Document &d,uint32_t rate,uint32_t block,double seconds=.24,const std::function<void(Renderer &,uint32_t)> &inspect={}){
 Renderer renderer(d.snapshotData(),rate);renderer.preparePreciseNotes(d.native());const uint32_t total=uint32_t(rate*seconds);std::vector<float> out(total*2);
 for(uint32_t f=0;f<total;f+=block){const auto count=std::min(block,total-f);uint64_t a,b,c;tracker_audit_begin();renderer.render(out.data()+2*f,count);tracker_audit_end(&a,&b,&c);
  check(!renderer.faulted()&&a+b+c==0,"Native sample effects render with zero allocations, frees and locks");if(inspect)inspect(renderer,f+count-1);
 }
 for(auto v:out)check(std::isfinite(v)&&std::abs(v)<4,"Finite bounded native PCM");return out;
}
static void partitions(Document &d,uint32_t rate,const std::vector<float> &one,double seconds=.24){for(uint32_t block:{17u,128u,4096u}){const auto actual=render(d,rate,block,seconds);check(actual.size()==one.size(),"Partition dimensions");for(size_t i=0;i<one.size();++i)if(std::abs(actual[i]-one[i])>3e-6){std::cerr<<"rate "<<rate<<" block "<<block<<" frame "<<i/2<<" error "<<actual[i]-one[i]<<'\n';check(false,"Native PCM does not depend on callback partition");}}}
static void gainPan(){
 for(auto rate:{44100u,48000u,96000u})for(auto op:{NativePatternOp::GainSet,NativePatternOp::GainSlide,NativePatternOp::PanSet,NativePatternOp::PanSlide}){
  auto d=fixture(true);const auto base=render(*d,rate,1);const bool gain=op==NativePatternOp::GainSet||op==NativePatternOp::GainSlide;const bool slide=op==NativePatternOp::GainSlide||op==NativePatternOp::PanSlide;
  d->annotate([&](NativeSong &n){auto c=command(n,op,8192,slide?32768:0);c.arguments[0]=gain?.123456789:.345678912;n.performance.commands={c};});
  const auto actual=render(*d,rate,1);const auto start=uint32_t(std::ceil(rate*.12/8));
  for(uint32_t f=start+1;f<actual.size()/2;++f){const double t=slide?std::min(1.,(f-start)/(rate*.06)):1;const double value=gain?1+(.123456789-1)*t:.345678912*t;
   near(actual[2*f],base[2*f]*(gain?value:1-value),2e-6,"Independent fractional left gain/pan oracle");near(actual[2*f+1],base[2*f+1]*(gain?value:1+value),2e-6,"Independent fractional right gain/pan oracle");}
  partitions(*d,rate,actual);
 }
 // Channel and song scopes persist across new notes; voice gain does not.
 for(int scope:{0,1,2}){auto d=fixture(true);d->transaction([](CSoundFile &s){*s.Patterns[0].GetpModCommand(1,0)=*s.Patterns[0].GetpModCommand(0,0);});const auto base=render(*d,48000,1);
  d->annotate([&](NativeSong &n){auto c=command(n,NativePatternOp::GainSet);c.arguments[0]=.333333333;c.arguments[1]=scope;n.performance.commands={c};});
  const auto actual=render(*d,48000,1);near(actual[15000],base[15000]*(scope?.333333333:1),2e-6,"Gain scope owns the intended sample lifetime");partitions(*d,48000,actual);
 }
}
static double wave(double phase,int shape){phase-=std::floor(phase);switch(shape){case 1:return phase<.5?4*phase-1:3-4*phase;case 2:return 2*phase-1;case 3:return phase<.5?1:-1;default:return std::sin(2*3.14159265358979323846*phase);}}
static void modulation(){
 for(auto rate:{44100u,48000u,96000u})for(auto op:{NativePatternOp::Tremolo,NativePatternOp::Panbrello,NativePatternOp::Tremor})for(int shape:{0,1,2,3,4}){
  auto d=fixture(true);const auto base=render(*d,rate,1);d->annotate([&](NativeSong &n){auto c=command(n,op);if(op==NativePatternOp::Tremor){c.arguments[0]=.03125;c.arguments[1]=.046875;c.arguments[2]=.75;c.arguments[3]=.125;}else{c.arguments[0]=.312345;c.arguments[1]=7.125;c.arguments[2]=shape%2;c.arguments[3]=shape;c.arguments[4]=.125;}n.performance.commands={c};});
  const auto actual=render(*d,rate,1);const auto start=uint32_t(std::ceil(rate*.12/8));
  if(shape!=4)for(uint32_t f=start+1;f<actual.size()/2;++f){const double time=double(f-start)/rate;double left=1,right=1;
   if(op==NativePatternOp::Tremor){const double phase=std::fmod(.125+time/.48/(.03125+.046875),1.);left=right=phase<.4?1:.25;if(std::min(std::abs(phase-.4),std::abs(phase))<1e-8)continue;}
   else {const double value=.312345*wave(.125+time*7.125*(shape%2?1:1/.48),shape);if(op==NativePatternOp::Tremolo)left=right=1-.312345*.5+value*.5;else{left=1-value;right=1+value;}}
   near(actual[2*f],base[2*f]*left,2e-6,"Independent sample-rate modulation left oracle");near(actual[2*f+1],base[2*f+1]*right,2e-6,"Independent sample-rate modulation right oracle");}
  partitions(*d,rate,actual);
 }
}
static void pitch(){
 for(auto rate:{44100u,48000u,96000u})for(auto op:{NativePatternOp::PitchRelative,NativePatternOp::TonePortamento,NativePatternOp::Vibrato,NativePatternOp::Arpeggio}){
  auto d=fixture();d->annotate([&](NativeSong &n){auto c=command(n,op,8192,(op==NativePatternOp::PitchRelative||op==NativePatternOp::TonePortamento)?32768:0);if(op==NativePatternOp::TonePortamento)c.arguments[0]=64.125;else if(op==NativePatternOp::PitchRelative)c.arguments[0]=3.125;else if(op==NativePatternOp::Vibrato){c.arguments[0]=2.125;c.arguments[1]=5.75;c.arguments[2]=1;}else{c.arguments[0]=3.125;c.arguments[1]=7.25;c.arguments[2]=3.75;c.arguments[3]=1;}n.performance.commands={c};});
  const auto start=uint32_t(std::ceil(rate*.12/8));double base=0,position=0;
  const auto actual=render(*d,rate,1,.24,[&](Renderer &r,uint32_t f){const auto &v=r.song().m_PlayState.Chn[0];if(!f){base=double(v.increment.GetRaw());position=v.position.ToDouble()-base/4294967296.;}
   double semitones=0;const double elapsed=f>=start?double(f-start)/rate:0;if(f>=start){if(op==NativePatternOp::PitchRelative||op==NativePatternOp::TonePortamento)semitones=3.125*std::min(1.,elapsed/.06);else if(op==NativePatternOp::Vibrato)semitones=2.125*std::sin(2*3.14159265358979323846*elapsed*5.75);else {int step=int(std::fmod(elapsed*3.75,1.)*3);semitones=step==1?3.125:step==2?7.25:0;}}
   position+=double(int64_t(base*std::exp2(semitones/12.)))/4294967296.;if(std::abs(v.position.ToDouble()-position)>3e-5)std::cerr<<"pitch rate "<<rate<<" op "<<int(op)<<" frame "<<f<<" phase "<<v.nativePatternVoice.arpeggio.phase<<" nativePitch "<<v.nativePatternVoice.Pitch()<<" lastNote "<<int(v.nLastNote)<<" nominal "<<base/4294967296.<<"\n";near(v.position.ToDouble(),position,3e-5,"Independent fractional pitch phase integral");});partitions(*d,rate,actual);
 }
}
static void actions(){
 for(auto rate:{44100u,48000u,96000u}){
  auto d=fixture();d->annotate([](NativeSong &n){auto delay=command(n,NativePatternOp::NoteDelay,12345);auto release=command(n,NativePatternOp::NoteRelease,70001);n.performance.commands={delay,release};});
  const auto onset=uint32_t(std::ceil(rate*.12*12345/65536.));uint64_t generation=0;const auto actual=render(*d,rate,1,.24,[&](Renderer &r,uint32_t frame){const auto &v=r.song().m_PlayState.Chn[0];if(frame<onset)check(v.nativeNoteGeneration==0,"NoteDelay never fires the original row onset");else{if(!generation)generation=v.nativeNoteGeneration;check(generation==1&&v.nativeNoteGeneration==generation,"NoteDelay emits exactly one note");}});
  for(size_t i=0;i<onset*2;++i)check(actual[i]==0,"Delayed note silence before exact onset");partitions(*d,rate,actual);
  d=fixture();d->annotate([](NativeSong &n){auto c=command(n,NativePatternOp::Retrigger,8192);c.arguments={.03125,3,.8,0,0,0,0,0};n.performance.commands={c};});const auto start=uint32_t(std::ceil(rate*.015));
  const auto repeated=render(*d,rate,1,.24,[&](Renderer &r,uint32_t f){const auto elapsed=f<start?0.:double(f-start)/rate;const auto repeats=f<start?0u:std::min(3u,uint32_t(std::floor(elapsed/.015+1e-8)));check(r.song().m_PlayState.Chn[0].nativeNoteGeneration==1+repeats,"Native repeats fire at sample-accurate beat intervals");});partitions(*d,rate,repeated);
  for(int mode:{0,1,2}){d=fixture();d->transaction([](CSoundFile &s){s.GetSample(1).cues[0]=2048;});d->annotate([&](NativeSong &n){auto c=command(n,NativePatternOp::SampleOffset,8192);c.arguments[0]=mode==0?2048.125:mode==1?2048.125/131072:1;c.arguments[1]=mode;n.performance.commands={c};});double increment=0;const auto seek=render(*d,rate,1,.24,[&](Renderer &r,uint32_t f){const auto &v=r.song().m_PlayState.Chn[0];if(!f)increment=v.increment.ToDouble();if(f==start)near(v.position.ToDouble(),(mode==2?2048:2048.125)+increment,1e-7,"Exact fractional sample seek / cue");});partitions(*d,rate,seek);}
  d=fixture();d->annotate([](NativeSong &n){n.performance.commands={command(n,NativePatternOp::SampleDirection,8192)};n.performance.commands[0].arguments[0]=1;});double previous=0;const auto reverse=render(*d,rate,1,.03,[&](Renderer &r,uint32_t f){const auto &v=r.song().m_PlayState.Chn[0];if(f==start)check(v.position.ToDouble()<previous&&v.increment.IsNegative(),"Native reverse keeps current sample position and changes direction");previous=v.position.ToDouble();});partitions(*d,rate,reverse,.03);
 }
}
static void envelopes(){
 for(auto rate:{44100u,48000u,96000u}){
  auto d=fixture(true,true);d->transaction([](CSoundFile &s){auto &e=s.Instruments[1]->VolEnv;e.clear();e.push_back(0,64);e.push_back(96,0);e.dwFlags.set(ENV_ENABLED);});
  d->annotate([](NativeSong &n){auto c=command(n,NativePatternOp::EnvelopePosition,8192);c.arguments={0,.123456,0,0,0,0,0,0};n.performance.commands={c};});
  const auto start=uint32_t(std::ceil(rate*.015));const auto actual=render(*d,rate,1,.24,[&](Renderer &r,uint32_t f){if(f>=start){const auto &v=r.song().m_PlayState.Chn[0];near(v.nativePatternVoice.envelopes[0].position,.123456*24+(double(f-start)+1)/rate*50,1e-8,"Envelope cursor advances by fractional musical time each sample");}});partitions(*d,rate,actual);
  d->annotate([](NativeSong &n){auto c=command(n,NativePatternOp::EnvelopeEnable,40000,0,1);c.arguments={0,0,0,0,0,0,0,0};n.performance.columns[c.track]=2;n.performance.commands.push_back(c);});d->annotate([](NativeSong &n){auto c=command(n,NativePatternOp::EnvelopeEnable,80000,0,1);c.arguments={0,1,0,0,0,0,0,0};n.performance.commands.push_back(c);});
  double held=-1;const auto disableFrame=uint32_t(std::ceil(rate*.12*40000/65536.)),enableFrame=uint32_t(std::ceil(rate*.12*80000/65536.));
  const auto disabled=render(*d,rate,1,.24,[&](Renderer &r,uint32_t f){const auto p=r.song().m_PlayState.Chn[0].nativePatternVoice.envelopes[0].position;if(f==disableFrame)held=p;if(f>disableFrame&&f<enableFrame)near(p,held,1e-10,"Disabled envelope freezes its fractional cursor");if(f==enableFrame)near(p,held+50./rate,1e-8,"Envelope resumes without losing the fractional cursor");});partitions(*d,rate,disabled);
 }
}
static void envelopeBoundaries(){
 for(auto rate:{44100u,48000u,96000u}){
  // A fractional seek out of a zero envelope must not stay in the mixer's
  // silent-channel shortcut until the next tracker tick.
  auto d=fixture(true,true);d->transaction([](CSoundFile &s){auto &e=s.Instruments[1]->VolEnv;e.clear();e.push_back(0,0);e.push_back(24,64);e.dwFlags.set(ENV_ENABLED);});
  d->annotate([](NativeSong &n){auto c=command(n,NativePatternOp::EnvelopePosition);c.arguments[1]=.5;n.performance.commands={c};});
  const auto start=uint32_t(std::ceil(rate*.015));const auto zero=render(*d,rate,1,.12);
  auto dry=fixture(true,true);const auto reference=render(*dry,rate,1,.12);
  for(uint32_t f=start;f<zero.size()/2;++f){const double envelope=(12+double(f-start)*50/rate)/24;near(zero[2*f],reference[2*f]*envelope,2e-6,"Envelope neutral gain survives the next ordinary tick without a level step");}
  check(std::abs(zero[2*(start+1)])>.001,"Envelope seek out of previous zero gain becomes audible at its exact sample");partitions(*d,rate,zero,.12);
  // Panning changes the base; the ordinary instrument envelope remains an
  // independent modulation. At base .25, envelope +.5 yields .25+.5*.75.
  d=fixture(true,true);d->transaction([](CSoundFile &s){auto &e=s.Instruments[1]->PanEnv;e.clear();e.push_back(0,48);e.push_back(96,48);e.dwFlags.set(ENV_ENABLED);});
  d->annotate([](NativeSong &n){auto c=command(n,NativePatternOp::PanSet);c.arguments[0]=.25;n.performance.commands={c};});
  const auto pan=render(*d,rate,1,.12);for(uint32_t f=start+100;f<pan.size()/2;++f)near((pan[2*f+1]-pan[2*f])/(pan[2*f+1]+pan[2*f]),.625,2e-4,"Native pan retains ordinary instrument pan envelope");partitions(*d,rate,pan,.12);
  d->annotate([](NativeSong &n){auto &c=n.performance.commands.front();c.native=NativePatternOp::PanSlide;c.duration=32768;});const auto slide=render(*d,rate,1,.12);
  for(uint32_t f=start;f<slide.size()/2;++f){const double base=.25*std::min(1.,double(f-start)/(rate*.06));const double target=base+.5*(1-base);near((slide[2*f+1]-slide[2*f])/(slide[2*f+1]+slide[2*f]),target,2e-4,"Native pan slide starts from the base before envelope modulation");}partitions(*d,rate,slide,.12);
  // Release jumps to the release node while scaling its value to the value
  // immediately before release. The fractional phase survives the jump.
  d=fixture(true,true);d->transaction([](CSoundFile &s){auto &i=*s.Instruments[1];i.nFadeOut=0;auto &e=i.VolEnv;e.clear();e.push_back(0,64);e.push_back(24,32);e.push_back(48,16);e.push_back(72,0);e.nReleaseNode=2;e.dwFlags.set(ENV_ENABLED);});
  d->annotate([](NativeSong &n){auto c=command(n,NativePatternOp::EnvelopePosition);c.arguments[1]=.25;auto r=command(n,NativePatternOp::NoteRelease,65536);n.performance.commands={c,r};});
  const auto release=uint32_t(std::ceil(rate*.12));double before=0;bool saw=false;
  const auto released=render(*d,rate,1,.3,[&](Renderer &r,uint32_t f){const auto &v=r.song().m_PlayState.Chn[0].nativePatternVoice;if(f+1==release)before=v.envelopes[0].position;if(f==release){near(v.envelopes[0].position,48+50./rate,1e-8,"Native release node exact fractional cursor");near(v.envelopes[0].releaseScale,(1-before/48.)/.25,1e-8,"Native release preserves preceding envelope value");saw=true;}});
  check(saw,"Release boundary inspected");partitions(*d,rate,released,.3);
 }
 // EP must not run another tracker tick for unrelated vibrato, pan envelope,
 // fade or auto-vibrato while changing just the volume-envelope cursor.
 auto a=fixture(true,true),b=fixture(true,true);
 for(auto *d:{a.get(),b.get()})d->transaction([](CSoundFile &s){auto &i=*s.Instruments[1];i.nFadeOut=256;i.VolEnv.push_back(0,64);i.VolEnv.push_back(96,32);i.VolEnv.dwFlags.set(ENV_ENABLED|ENV_LOOP);i.VolEnv.nLoopStart=0;i.VolEnv.nLoopEnd=1;i.PanEnv.push_back(0,32);i.PanEnv.push_back(96,48);i.PanEnv.dwFlags.set(ENV_ENABLED);auto &note=*s.Patterns[0].GetpModCommand(0,0);note.command=CMD_VIBRATO;note.param=0x47;s.Patterns[0].GetpModCommand(1,0)->note=NOTE_KEYOFF;});
 b->annotate([](NativeSong &n){auto c=command(n,NativePatternOp::EnvelopePosition,73728);c.arguments[1]=.123456;n.performance.commands={c};});
 Renderer ra(a->snapshotData(),48000),rb(b->snapshotData(),48000);ra.preparePreciseNotes(a->native());rb.preparePreciseNotes(b->native());float pa[2],pb[2];
 for(uint32_t f=0;f<8000;++f){ra.render(pa,1);rb.render(pb,1);const auto &x=ra.song().m_PlayState.Chn[0],&y=rb.song().m_PlayState.Chn[0];check(x.nVibratoPos==y.nVibratoPos&&x.nAutoVibPos==y.nAutoVibPos&&x.nFadeOutVol==y.nFadeOutVol&&x.PanEnv.nEnvPosition==y.PanEnv.nEnvPosition,"Envelope action never advances another legacy phase, envelope or fade tick");}
}
static void mutedCommands(){
 auto d=fixture(true);d->transaction([](CSoundFile &s){s.ChnSettings[0].dwFlags.set(CHN_MUTE);*s.Patterns[0].GetpModCommand(0,1)=*s.Patterns[0].GetpModCommand(0,0);s.ChnSettings[1].nPan=128;});
 const auto base=render(*d,48000,1);d->annotate([](NativeSong &n){auto c=command(n,NativePatternOp::GainSet);c.arguments[0]=0;c.arguments[1]=2;n.performance.commands={c};});const auto actual=render(*d,48000,1);check(base==actual,"Muted channel cannot change another channel through song-scope native gain");partitions(*d,48000,actual);
}
static void lifecycle(){
 auto d=fixture(false,true);d->transaction([](CSoundFile &s){*s.Patterns[0].GetpModCommand(1,0)=*s.Patterns[0].GetpModCommand(0,0);});
 d->annotate([](NativeSong &n){auto c=command(n,NativePatternOp::Vibrato);c.arguments={3.125,7,1,0,.125,1,0,0};n.performance.commands={c};});
 bool checked=false;const auto result=render(*d,48000,1,.24,[&](Renderer &r,uint32_t f){if(f==5760){const auto &s=r.song();check(!s.m_PlayState.Chn[0].nativePatternVoice.vibrato.active,"New note clears native oscillator");bool old=false;for(const auto &v:s.m_PlayState.Chn)if(v.nMasterChn==1&&v.nLength){old|=v.nativePatternVoice.vibrato.active;}check(old,"NNA continues its own native oscillator history");checked=true;}});check(checked,"NNA boundary rendered");partitions(*d,48000,result);
 d=fixture();d->transaction([](CSoundFile &s){s.Patterns[0].GetpModCommand(0,0)->Clear();auto &note=*s.Patterns[0].GetpModCommand(1,0);note.note=61;note.instr=1;});d->annotate([](NativeSong &n){auto c=command(n,NativePatternOp::PitchRelative,0);c.arguments[0]=12;n.performance.commands={c};});render(*d,48000,17,.24,[&](Renderer &r,uint32_t f){if(f>=5760)check(!r.song().m_PlayState.Chn[0].nativePatternVoice.pitch.active,"Idle voice command never primes a later note");});
}
int main(){try{gainPan();modulation();pitch();actions();envelopes();envelopeBoundaries();mutedCommands();lifecycle();std::cout<<"Native pattern runtime PCM, timing and lifecycle checks passed";
#if defined(TRACKER_REALTIME_AUDIT) && !defined(TRACKER_SANITIZER)
 std::cout<<"; allocation/free/lock audit passed";
#else
 std::cout<<"; realtime audit unavailable in this build";
#endif
 std::cout<<'\n';return 0;}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
