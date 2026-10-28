#include "editor/TrackerDocument.hpp"
#include <iostream>
#include <stdexcept>
using namespace Tracker;
using namespace OpenMPT;
#ifdef TRACKER_REALTIME_AUDIT
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *,uint64_t *,uint64_t *);
#else
static void tracker_audit_begin(){}
static void tracker_audit_end(uint64_t *a,uint64_t *f,uint64_t *l){*a=*f=*l=0;}
#endif
static void check(bool v,const char *why){if(!v)throw std::runtime_error(why);}
static void checkNear(double a,double b,double e,const char *why){if(std::abs(a-b)>e){std::cerr<<a<<" != "<<b<<'\n';throw std::runtime_error(why);}}
static std::unique_ptr<Document> fixture(){auto d=std::make_unique<Document>();d->transaction([](CSoundFile &s){
  for(auto &p:s.Patterns)if(p.IsValid())for(auto &c:p)c.Clear();s.Patterns[0].Resize(8);s.Order().assign(1,0);s.Order().SetDefaultTempoInt(125);s.Order().SetDefaultSpeed(6);
  s.m_nSamples=1;auto &sample=s.GetSample(1);sample.Initialize();sample.nLength=131072;sample.nC5Speed=8192;sample.uFlags.set(CHN_16BIT);check(sample.AllocateSample()!=0,"PCM allocation");for(size_t i=0;i<sample.nLength;++i)sample.sample16()[i]=1200;sample.PrecomputeLoops(s,false);
  s.ChnSettings[0].nPan=128;auto &note=*s.Patterns[0].GetpModCommand(0,0);note.note=61;note.instr=1;
});return d;}
static PatternCommand gate(const NativeSong &n,uint32_t position=0,uint8_t column=0,GateTarget target=GateTarget::Volume){PatternCommand c;c.pattern=n.patterns.at(0).id;c.track=n.tracks.at(0).id;c.position=position;c.column=column;c.kind=PatternCommandKind::Native;c.native=NativePatternOp::Gate;c.arguments=nativePatternDefaults(c.native);c.arguments[0]=int(target);return c;}
static std::vector<float> render(Document &d,uint32_t rate,uint32_t block,double seconds=.7,uint32_t order=0,PlaybackRegion region={}){
  Renderer r(d.snapshotData(),rate,order,false,{},0,region);r.preparePreciseNotes(d.native());std::vector<float> pcm(size_t(rate*seconds)*2);
  for(size_t f=0;f<pcm.size()/2;f+=block){uint64_t a,b,c;tracker_audit_begin();r.render(pcm.data()+2*f,uint32_t(std::min<size_t>(block,pcm.size()/2-f)));tracker_audit_end(&a,&b,&c);check(a+b+c==0&&!r.faulted(),"Gate callback must not allocate, free, lock or fault");}
  return pcm;
}
static void partitions(Document &d,uint32_t rate,const std::vector<float> &expected,double duration=.7){for(auto block:{17u,128u,4096u}){const auto pcm=render(d,rate,block,duration);for(size_t i=0;i<pcm.size();++i)checkNear(pcm[i],expected[i],3e-6,"Gate PCM is independent of callback size");}}
int main(){try{
  GateSignal s;s.period=1;s.width=.25;s.rise=s.fall=8;
  checkNear(s.value(0),0,0,"Rising edge begins at floor");checkNear(s.value(.125),.5,1e-12,"Rise shortens to ceiling window");checkNear(s.value(.25),1,0,"Falling edge stays at one quarter beat");checkNear(s.value(.625),.5,1e-12,"Fall shortens independently");
  s.width=0;checkNear(s.value(.25),0,0,"Zero width is constant floor");s.width=1;checkNear(s.value(.25),1,0,"Full width is constant ceiling");
  s.width=.5;s.riseShape=s.fallShape=GateShape::Direct;s.phase=-.25;checkNear(s.value(0),0,0,"Negative phase wraps");checkNear(s.value(.25),1,0,"Phase is measured in beats");
  for(auto shape:{GateShape::Linear,GateShape::EaseIn,GateShape::EaseOut,GateShape::EaseInOut})for(auto rate:{44100u,48000u,96000u}){
    auto d=fixture();const auto base=render(*d,rate,1);GateSignal signal;signal.period=.375;signal.phase=.0625;signal.width=.3;signal.rise=.08;signal.fall=.08;signal.riseShape=signal.fallShape=shape;
    d->annotate([&](NativeSong &n){auto c=gate(n,8192);c.arguments[3]=signal.period;c.arguments[4]=signal.phase;c.arguments[5]=signal.width;c.arguments[6]=c.arguments[7]=signal.rise;c.arguments[8]=c.arguments[9]=int(shape);n.performance.commands={c};});
    const auto actual=render(*d,rate,1);const auto start=size_t(std::ceil(rate*.015));
    for(size_t f=0;f<actual.size()/2;++f){const auto gain=f<start?1:signal.value((double(f)/rate-.015)/.48);checkNear(actual[2*f],base[2*f]*gain,2e-6,"Rendered gain follows beat waveform oracle");}
    partitions(*d,rate,actual);
  }
  auto d=fixture();d->transaction([](CSoundFile &s){*s.Patterns[0].GetpModCommand(1,0)=*s.Patterns[0].GetpModCommand(0,0);});const auto base=render(*d,48000,1);
  d->annotate([](NativeSong &n){auto v=gate(n);v.arguments[1]=.25;v.arguments[2]=.25;auto pan=gate(n,0,1,GateTarget::Pan);pan.arguments[1]=pan.arguments[2]=.5;n.performance.columns[v.track]=2;
    auto stop=v;stop.native=NativePatternOp::GateStop;stop.arguments=nativePatternDefaults(stop.native);stop.arguments[0]=0;stop.position=2*65536;n.performance.commands={v,pan,stop};});
  const auto both=render(*d,48000,1);checkNear(both[14000],base[14000]*.25*.5,2e-6,"Volume and pan survive new note simultaneously");checkNear(both[26000],base[26000]*.5,2e-6,"Stopping volume preserves pan and reveals current base gain");partitions(*d,48000,both);
  // An octave gate follows the pitch and phase of octave tuning. Compare
  // zero crossings because the engine quantizes tuning and sample increments;
  // equivalent pitches need not produce bit-identical interpolated PCM.
  auto pitched=fixture(),tuned=fixture();
  for(auto *song:{pitched.get(),tuned.get()})song->transaction([](CSoundFile &s){auto &sample=s.GetSample(1);for(size_t i=0;i<sample.nLength;++i)sample.sample16()[i]=int16_t((int(i%32)-16)*1000);sample.PrecomputeLoops(s,false);});
  tuned->transaction([](CSoundFile &s){s.GetSample(1).nC5Speed*=2;});
  pitched->annotate([](NativeSong &n){auto c=gate(n,0,0,GateTarget::Pitch);c.arguments[1]=c.arguments[2]=12;n.performance.commands={c};});
  const auto crossings=[](const std::vector<float> &pcm){std::vector<size_t> at;for(size_t f=100;f<pcm.size()/2;++f)if(pcm[2*(f-1)]<0&&pcm[2*f]>=0)at.push_back(f);return at;};
  for(auto rate:{44100u,48000u,96000u}){const auto reference=render(*tuned,rate,1),actual=render(*pitched,rate,1);const auto expectedCrossings=crossings(reference),actualCrossings=crossings(actual);check(expectedCrossings.size()>200&&expectedCrossings.size()==actualCrossings.size(),"Sample pitch gate has the octave reference frequency");for(size_t i=0;i<actualCrossings.size();++i)checkNear(double(actualCrossings[i]),double(expectedCrossings[i]),1,"Sample pitch gate keeps octave reference phase to one frame");partitions(*pitched,rate,actual);}
  // A gate crosses order boundaries; starting directly at the second order
  // reconstructs the definition and elapsed phase rather than starting over.
  d=fixture();d->addPattern(8,true,0);d->transaction([](CSoundFile &s){s.Order().assign(2,0);s.Order()[1]=1;});
  d->annotate([](NativeSong &n){auto c=gate(n);c.arguments[3]=.7;c.arguments[6]=c.arguments[7]=.08;n.performance.commands={c};});
  Renderer r(d->snapshotData(),48000,1);r.preparePreciseNotes(d->native());std::array<float,34> out{};r.render(out.data(),17);check(r.song().nativeGateVolumes[0],"Seek restores active preceding gate");GateSignal expected;expected.period=.7;expected.rise=expected.fall=.08;
  checkNear(r.song().nativeGateVolumes[0]->value(),expected.value(2),1e-9,"Seek phase uses preceding musical beats");
  d->annotate([](NativeSong &n){auto length=gate(n,2*65536);length.native=NativePatternOp::RowLength;length.arguments=nativePatternDefaults(length.native);length.arguments[0]=.5;n.performance.commands.push_back(length);length.track=n.tracks.at(1).id;length.arguments[0]=.75;n.performance.commands.push_back(length);});
  Renderer varied(d->snapshotData(),48000,1);varied.preparePreciseNotes(d->native());varied.render(out.data(),17);
  checkNear(varied.song().nativeGateVolumes[0]->value(),expected.value(2.5),1e-9,"Seek phase includes row lengths with playback channel precedence");
  auto n=d->native();auto invalid=gate(n);invalid.arguments[0]=3;invalid.arguments[11]=7;n.performance.commands.push_back(invalid);bool rejected=false;try{n.validate(d->song());}catch(const std::invalid_argument &){rejected=true;}check(rejected,"Missing stable gate binding rejects atomically");
  std::cout<<"PASS gate edge clipping, shapes, phase, simultaneous targets, persistence, stop, seek, PCM partitions and realtime audit\n";return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
