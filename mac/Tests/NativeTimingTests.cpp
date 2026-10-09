#include "editor/TrackerDocument.hpp"
#include "editor/SongTiming.hpp"
#include <cmath>
#include <iostream>
using namespace Tracker;
using namespace OpenMPT;
#if defined(TRACKER_SANITIZER) || defined(_WIN32)
static void tracker_audit_begin() {}
static void tracker_audit_end(uint64_t *a,uint64_t *f,uint64_t *l){*a=*f=*l=0;}
#else
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *,uint64_t *,uint64_t *);
#endif
static void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
struct Tick {uint32_t row,tick,length;uint64_t sample;bool operator==(const Tick &)const=default;};
struct Probe {
  uint64_t sample=0;std::vector<Tick> ticks;
  double previous=-1;bool monotonic=true;double maximumStep=0,badBefore=0,badAfter=0;
  static void observe(void *context,const PlayState &s,uint32_t frames)noexcept {
    auto &p=*static_cast<Probe *>(context);
    if(s.AtStartOfTick())p.ticks.push_back({s.m_nRow,s.m_nTickCount,s.m_nSamplesPerTick,p.sample});
    const double row=s.NativeRowPosition(1);
    if(row+1e-10<p.previous){p.monotonic=false;p.badBefore=p.previous;p.badAfter=row;}p.previous=row;
    p.maximumStep=std::max(p.maximumStep,s.NativeRowStep(1));p.sample+=frames;
  }
};
static PatternCommand command(const NativeSong &n,uint32_t position,NativePatternOp op,double value,uint32_t duration=0,uint8_t column=0) {
  PatternCommand c;c.pattern=n.patterns.at(0).id;c.track=n.tracks.at(0).id;c.position=position;c.kind=PatternCommandKind::Native;c.native=op;c.arguments=nativePatternDefaults(op);c.arguments[0]=value;c.duration=duration;c.column=column;return c;
}
static double integral(double start,double end,double from,double slope) {
  return std::abs(slope)<1e-12?(end-start)/from:std::log((from+slope*end)/(from+slope*start))/slope;
}
int main(){try {
  for(auto mode:{TempoMode::Classic,TempoMode::Alternative,TempoMode::Modern})for(uint32_t rate:{44100u,48000u,96000u})for(bool slide:{false,true}) {
    auto doc=Document::demo();
    doc->transaction([&](CSoundFile &s){
      for(auto &p:s.Patterns)if(p.IsValid())for(auto &c:p)c.Clear();
      s.Order().assign(1,0);auto t=songTiming(s);t.mode=mode;t.rowsPerBeat=4;t.rowsPerMeasure=16;t.sequences[0]={1251250,6};t.groove.clear();applySongTiming(s,t);
      auto &note=*s.Patterns[0].GetpModCommand(0,0);note.note=61;note.instr=2;
    });
    doc->annotate([&](NativeSong &n){n.performance.commands={command(n,0,slide?NativePatternOp::TempoSlide:NativePatternOp::TempoSet,slide?250.25:125.125,slide?8*65536:0)};});
    auto render=[&](uint32_t block){Renderer r(doc->snapshotData(),rate,0,false,{},UINT32_MAX,{},&doc->native());r.preparePreciseNotes(doc->native());
      Probe p;p.ticks.reserve(4096);r.song().nativeMixContext=&p;r.song().nativeMixObserver=Probe::observe;
      std::vector<float> pcm(rate*2);
      for(uint32_t frame=0;frame<rate;frame+=block){uint64_t a,f,l;tracker_audit_begin();r.render(pcm.data()+frame*2,std::min(block,rate-frame));tracker_audit_end(&a,&f,&l);check(!r.faulted()&&a+f+l==0,"Native tempo render is allocation/free/lock-free");}
      if(!p.monotonic)std::cerr<<"Clock reversal mode "<<int(mode)<<" rate "<<rate<<" slide "<<slide<<" block "<<block<<" from "<<p.badBefore<<" to "<<p.badAfter<<'\n';
      check(p.monotonic&&p.maximumStep>0,"Native musical clock moves continuously forward");return std::pair{std::move(pcm),std::move(p.ticks)};};
    const auto reference=render(1);
    for(auto block:{17u,128u,4096u}){auto actual=render(block);check(actual.second==reference.second,"Tempo tick onsets independent of audio callback partition");double delta=0;for(size_t i=0;i<actual.first.size();++i)delta=std::max(delta,std::abs(double(actual.first[i])-reference.first[i]));check(delta<1e-6,"Tempo PCM independent of callback partition");}
    const double factor=rate*(mode==TempoMode::Alternative?6.:15.);
    for(const auto &tick:reference.second){const double position=tick.row+double(tick.tick)/6;
      const double duration=slide?integral(0,std::min(8.,position),125.125,125.125/8)+std::max(0.,position-8)/250.25:position/125.125;
      if(std::abs(tick.sample-factor*duration)>1.001){std::cerr<<"rate "<<rate<<" mode "<<int(mode)<<" row "<<position<<" sample "<<tick.sample<<" expected "<<factor*duration<<'\n';check(false,"Native tempo onsets agree with independent analytic integration within one sample");}}
    Renderer seek(doc->snapshotData(),rate,0,false,{},UINT32_MAX,{UINT32_MAX,0,64,8,false},&doc->native());
    check(std::abs(seek.song().m_PlayState.m_nMusicTempo.ToDouble()-(slide?250.25:125.125))<.00011,"Seeking restores final native tempo");
    if(slide){Renderer middle(doc->snapshotData(),rate,0,false,{},UINT32_MAX,{UINT32_MAX,0,64,4,false},&doc->native());
      check(std::abs(middle.song().m_PlayState.m_nMusicTempo.ToDouble()-187.6875)<.00011,"Seeking into a native tempo ramp restores its current value");}
    auto &song=doc->song();doc->native().prepareEffects(song);auto settings=song.m_MixerSettings;settings.gdwMixingFreq=rate;song.SetMixerSettings(settings);
    const auto length=song.GetLength(eNoAdjust,GetLengthTarget(0,8));
    check(!length.empty(),"Native length walk reaches target");
    const double expected=factor*(slide?integral(0,8,125.125,125.125/8):8/125.125);
    check(std::abs(length.back().duration*rate-expected)<1.001,"Song length walker agrees with native tempo integration");
  }
  for(double beats:{1./65536,.0001,.01}) {
    Document doc;doc.annotate([&](NativeSong &n){n.performance.commands={command(n,0,NativePatternOp::RowLength,beats)};});
    Renderer r(doc.snapshotData(),48000,0,false,{},UINT32_MAX,{},&doc.native());r.preparePreciseNotes(doc.native());Probe p;p.ticks.reserve(4096);r.song().nativeMixContext=&p;r.song().nativeMixObserver=Probe::observe;std::array<float,4096> pcm{};r.render(pcm.data(),2048);
    const auto row1=std::find_if(p.ticks.begin(),p.ticks.end(),[](const auto &t){return t.row==1&&t.tick==0;});check(row1!=p.ticks.end(),"Tiny row length reaches following row");
    auto &s=doc.song();doc.native().prepareEffects(s);auto settings=s.m_MixerSettings;settings.gdwMixingFreq=48000;s.SetMixerSettings(settings);auto length=s.GetLength(eNoAdjust,GetLengthTarget(0,1));
    check(!length.empty()&&std::abs(length.back().duration*48000-row1->sample)<1e-6,"Length walker matches per-tick sample floor for very short RL");
  }
  {
    Document doc;doc.annotate([&](NativeSong &n){n.performance.columns[n.tracks.at(0).id]=2;n.performance.commands={command(n,0,NativePatternOp::TempoSlide,180,5461),command(n,5461,NativePatternOp::TempoSlide,250,5461,1)};});
    Renderer r(doc.snapshotData(),48000,0,false,{},UINT32_MAX,{},&doc.native());r.preparePreciseNotes(doc.native());Probe p;p.ticks.reserve(512);r.song().nativeMixContext=&p;r.song().nativeMixObserver=Probe::observe;std::array<float,16384> pcm{};r.render(pcm.data(),8192);
    const auto row1=std::find_if(p.ticks.begin(),p.ticks.end(),[](const auto &t){return t.row==1&&!t.tick;});check(row1!=p.ticks.end(),"Contiguous ramps reach next row");
    const double d=5461./65536,initial=doc.song().Order().GetDefaultTempo().ToDouble();
    const double expected=48000*15*(std::log(180/initial)*d/(180-initial)+std::log(250./180)*d/70+(1-2*d)/250);
    check(std::abs(row1->sample-expected)<1.001,"A contiguous TL starts at the previous target even inside one tick");
  }
  // Multiple commands in a row, fractional onset, row-only stretch, and a
  // subsequent legacy tempo command exercise shared clock precedence.
  {
    auto doc=Document::demo();doc->transaction([](CSoundFile &s){for(auto &p:s.Patterns)if(p.IsValid())for(auto &c:p)c.Clear();s.Order().assign(1,0);s.Order().SetDefaultTempoInt(120);s.Order().SetDefaultSpeed(6);
      auto &legacy=*s.Patterns[0].GetpModCommand(3,0);legacy.command=CMD_TEMPO;legacy.param=150;});
    doc->annotate([&](NativeSong &n){n.performance.columns[n.tracks.at(0).id]=2;n.performance.commands={command(n,24576,NativePatternOp::TempoSet,240),command(n,65536,NativePatternOp::RowLength,.375,0,1)};});
    Renderer r(doc->snapshotData(),48000,0,false,{},UINT32_MAX,{},&doc->native());r.preparePreciseNotes(doc->native());Probe p;p.ticks.reserve(256);r.song().nativeMixContext=&p;r.song().nativeMixObserver=Probe::observe;std::array<float,96000> pcm{};r.render(pcm.data(),48000);
    const double row1=48000*15*(.375/120+.625/240),row2=row1+48000*60*.375/240,row3=row2+48000*15/240;
    for(const auto &[row,tick,length,sample]:p.ticks)if(!tick&&row<=4){double expected=row==0?0:row==1?row1:row==2?row2:row==3?row3:row3+48000*15/150;check(std::abs(double(sample)-expected)<1.001,"Fractional TS onset, row-only RL and legacy tempo precedence");}
  }
  std::cout<<"PASS native timing: exact decimal tempo, analytic continuous tempo slides, fractional command onset, row-only beat lengths, legacy override, length/seek agreement, 3 rates and 4 callback sizes\n";
#if !defined(TRACKER_SANITIZER) && !defined(_WIN32)
  std::cout<<"Callback audit: zero allocation/free/lock\n";
#endif
  return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
