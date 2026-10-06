#include "NativeTimingRuntime.hpp"
#include "NativeSong.hpp"
#include <array>
#include <cmath>
#include <limits>
#include <tuple>

namespace Tracker {
namespace {
using namespace OpenMPT;
struct Event {
  double position, duration, value;
  uint16_t channel;
  uint8_t column;
  NativePatternOp operation;
};
struct Segment {
  double row, end, bpm, slope, factor, startSample, endSample;
  double position(double sample) const noexcept {
    const double elapsed=std::clamp(sample,startSample,endSample)-startSample;
    return std::clamp(row+(std::abs(slope)<1e-12 ? elapsed*bpm/factor : bpm*std::expm1(elapsed*slope/factor)/slope),row,end);
  }
};
struct NativeTimingRuntime {
  CSoundFile &song;
  std::map<uint16_t,std::vector<Event>> patterns;
  // Each cell has at most one effect. One boundary for every event and ramp
  // end, plus endpoints. No storage changes on the audio thread.
  std::array<Segment,MAX_BASECHANNELS*maximumEffectColumns*2+2> segments{};
  size_t segmentCount=0;
  double exactSamples=0;
  uint32_t tickSamples=0;
  double beatsPerRow=0;
  explicit NativeTimingRuntime(CSoundFile &s,const NativeSong &native):song(s) {
    for(const auto &command:native.performance.commands) {
      if(command.kind!=PatternCommandKind::Native || (command.native!=NativePatternOp::TempoSet && command.native!=NativePatternOp::TempoSlide && command.native!=NativePatternOp::RowLength))continue;
      const auto p=std::find_if(native.patterns.begin(),native.patterns.end(),[&](const auto &v){return v.second.id==command.pattern;});
      const auto t=std::find_if(native.tracks.begin(),native.tracks.end(),[&](const auto &v){return v.second.id==command.track;});
      if(p==native.patterns.end() || t==native.tracks.end())throw std::invalid_argument("Native timing has an unresolved pattern or track");
      patterns[p->first].push_back({double(command.position)/performanceUnitsPerRow,double(command.duration)/performanceUnitsPerRow,command.arguments[0],t->first,command.column,command.native});
    }
    for(auto &[p,events]:patterns)std::sort(events.begin(),events.end(),[](const auto &a,const auto &b){return std::tie(a.position,a.channel,a.column)<std::tie(b.position,b.channel,b.column);});
  }
  static double tempo(const PlayState::NativeTempoState &s,double row) noexcept {
    if(s.end<=s.begin)return s.bpm;
    if(row>=s.end)return s.target;
    return s.from+(s.target-s.from)*std::clamp((row-s.begin)/(s.end-s.begin),0.,1.);
  }
  double factor(const PlayState &s,double rowLength) const noexcept {
    double f;
    if(rowLength>0) f=60*rowLength;
    else switch(song.m_nTempoMode) {
      case TempoMode::Classic: f=2.5*s.TicksOnRow();break;
      case TempoMode::Alternative: f=s.TicksOnRow();break;
      case TempoMode::Modern: {
        f=60.*s.TicksOnRow()/(std::max(1u,s.m_nMusicSpeed)*std::max(1u,uint32_t(s.m_nCurrentRowsPerBeat)));
        const auto &swing=song.Patterns.IsValidPat(s.m_nPattern)&&song.Patterns[s.m_nPattern].HasTempoSwing()?song.Patterns[s.m_nPattern].GetTempoSwing():song.m_tempoSwing;
        if(!swing.empty())f*=double(swing[s.m_nRow%swing.size()])/TempoSwing::Unity;
        break;
      }
      default:f=2.5*s.TicksOnRow();break;
    }
    return f*song.GetSampleRate()*song.m_nTempoFactor/65536.;
  }
  double advance(PlayState &s,double start,double end,bool audio) noexcept {
    auto &clock=s.nativeTempo;
    const auto found=patterns.find(s.m_nPattern);
    const auto *events=found==patterns.end()?nullptr:&found->second;
    const auto first=events?std::lower_bound(events->begin(),events->end(),start-1e-10,[](const auto &e,double p){return e.position<p;}):std::vector<Event>::const_iterator{};
    auto next=first;
    bool hasEvent=events && next!=events->end() && next->position<end-1e-10;
    double rowLength=0;
    if(events) {
      auto rowEvent=std::lower_bound(events->begin(),events->end(),double(s.m_nRow)-1e-10,[](const auto &e,double p){return e.position<p;});
      for(;rowEvent!=events->end() && rowEvent->position<double(s.m_nRow)+1e-10;++rowEvent)
        if(rowEvent->operation==NativePatternOp::RowLength)rowLength=rowEvent->value;
    }
    const bool external=clock.published && clock.published!=s.m_nMusicTempo.GetRaw();
    const bool jump=clock.pattern!=s.m_nPattern || clock.order!=s.m_nCurrentOrder || start<clock.previous-1e-9;
    if(!clock.bpm || external) {clock.bpm=s.m_nMusicTempo.ToDouble();clock.begin=clock.end=0;}
    if(jump)clock.begin=clock.end=0;
    clock.pattern=s.m_nPattern;clock.order=s.m_nCurrentOrder;clock.previous=end;
    if(!clock.active&&!hasEvent&&!rowLength)return -1;
    const double f=factor(s,rowLength);
    if(audio)beatsPerRow=rowLength>0?rowLength:1./std::max(1u,uint32_t(s.m_nCurrentRowsPerBeat));
    double row=start,total=0;
    while(row<end-1e-12) {
      while(events && next!=events->end() && next->position<=row+1e-10) {
        const auto &event=*next++;
        if(event.operation==NativePatternOp::TempoSet) {clock.bpm=event.value;clock.begin=clock.end=0;clock.active=true;}
        if(event.operation==NativePatternOp::TempoSlide) {
          clock.from=tempo(clock,row);clock.target=event.value;clock.begin=event.position;clock.end=event.position+event.duration;clock.bpm=clock.from;clock.active=true;
        }
      }
      if(clock.end>clock.begin && row>=clock.end-1e-10) {clock.bpm=clock.target;clock.begin=clock.end=0;}
      double boundary=end;
      if(events&&next!=events->end())boundary=std::min(boundary,next->position);
      if(clock.end>row+1e-10)boundary=std::min(boundary,clock.end);
      const double bpm=std::max(1.,tempo(clock,row));
      const double slope=clock.end>clock.begin?(clock.target-clock.from)/(clock.end-clock.begin):0;
      const double elapsed=std::abs(slope)<1e-12?f*(boundary-row)/bpm:f*std::log1p(slope*(boundary-row)/bpm)/slope;
      if(audio && segmentCount<segments.size())segments[segmentCount++]={row,boundary,bpm,slope,f,total,total+elapsed};
      total+=elapsed;row=boundary;
    }
    clock.bpm=tempo(clock,end);
    if(clock.end>clock.begin && end>=clock.end-1e-10){clock.bpm=clock.target;clock.begin=clock.end=0;}
    s.m_nMusicTempo=TEMPO(clock.bpm);clock.published=s.m_nMusicTempo.GetRaw();
    return total;
  }
  static uint32_t rounded(PlayState &s,double length) noexcept {
    const double count=length+s.nativeTempo.error;
    const auto result=uint32_t(std::clamp(std::floor(count),1.,double(std::numeric_limits<uint32_t>::max())));
    s.nativeTempo.error=std::clamp(count-result,-1.,1.);
    return result;
  }
  uint32_t tick(PlayState &s,uint32_t fallback) noexcept {
    segmentCount=0;tickSamples=0;s.nativeClockActive=false;
    if(s.m_flags[SONG_PAUSED|SONG_FADINGSONG]||!s.TicksOnRow())return fallback;
    const double start=s.m_nRow+double(s.m_nTickCount)/s.TicksOnRow();
    exactSamples=advance(s,start,s.m_nRow+double(s.m_nTickCount+1)/s.TicksOnRow(),true);
    if(exactSamples<=0)return fallback;
    tickSamples=rounded(s,exactSamples);
    return tickSamples;
  }
  uint32_t row(PlayState &s,uint32_t fallback) noexcept {
    // Match the renderer's per-tick one-sample floor as well as its fractional
    // carry. Extremely short, valid RL values must not make seeking disagree
    // with playback by collapsing several tracker ticks into one sample.
    const uint32_t ticks=std::max(1u,s.TicksOnRow());
    uint64_t total=0;
    for(uint32_t tick=0;tick<ticks;++tick) {
      const double length=advance(s,s.m_nRow+double(tick)/ticks,s.m_nRow+double(tick+1)/ticks,false);
      total+=length>0?rounded(s,length):std::max(1u,fallback/ticks);
    }
    return uint32_t(std::min(total,uint64_t(std::numeric_limits<uint32_t>::max())));
  }
  double at(double sample) const noexcept {
    const double actual=sample*exactSamples/tickSamples;
    for(size_t i=0;i<segmentCount;++i)if(actual<segments[i].endSample || i+1==segmentCount)return segments[i].position(actual);
    return 0;
  }
  uint32_t prepare(PlayState &s,uint32_t count) noexcept {
    if(!tickSamples||!segmentCount||s.m_flags[SONG_PAUSED|SONG_FADINGSONG]){s.nativeClockActive=false;return count;}
    const auto offset=s.SamplesIntoTick();
    // Fixed audio-clock windows, independent of host callback partition. Native
    // curves see a common linear clock approximation within each <=32 samples.
    const uint32_t begin=offset/32*32,end=std::min(tickSamples,begin+32);
    const double first=at(begin),last=at(end);
    s.nativeClockActive=true;s.nativeClockAnchorSample=begin;s.nativeClockRow=first;
    s.nativeClockBeatsPerRow=beatsPerRow;
    s.nativeClockRowsPerSample=(last-first)/std::max(1u,end-begin);
    const double position=s.NativeRowPosition(1);
    for(size_t i=0;i<segmentCount;++i)if(position<segments[i].end || i+1==segmentCount){
      s.m_nMusicTempo=TEMPO(segments[i].bpm+segments[i].slope*(position-segments[i].row));break;
    }
    s.nativeTempo.published=s.m_nMusicTempo.GetRaw();
    return std::min(count,std::max(1u,end-offset));
  }
};
}
void prepareNativeTiming(OpenMPT::CSoundFile &song,const NativeSong &native) {
  auto runtime=std::make_shared<NativeTimingRuntime>(song,native);
  if(runtime->patterns.empty()){
    song.nativeTimingTick=nullptr;song.nativeTimingPrepare=nullptr;song.nativeTimingRow=nullptr;song.nativeTimingContext=nullptr;song.nativeTimingOwner.reset();song.m_PlayState.nativeClockActive=false;return;
  }
  song.nativeTimingOwner=runtime;song.nativeTimingContext=runtime.get();
  song.nativeTimingTick=[](void *p,OpenMPT::PlayState &s,uint32_t n)noexcept{return static_cast<NativeTimingRuntime *>(p)->tick(s,n);};
  song.nativeTimingPrepare=[](void *p,OpenMPT::PlayState &s,uint32_t n)noexcept{return static_cast<NativeTimingRuntime *>(p)->prepare(s,n);};
  song.nativeTimingRow=[](void *p,OpenMPT::PlayState &s,uint32_t n)noexcept{return static_cast<NativeTimingRuntime *>(p)->row(s,n);};
}
}
