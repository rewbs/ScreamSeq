#include "RecordNudgeRuntime.hpp"
#include <cmath>
namespace Tracker {
double RecordNudgeCurve::at(double position) const noexcept {
  const double t=(position-start)/duration;
  if(t<=0)return from;
  if(t>=1-1e-12)return 0;
  if(t<attack)return from+(peak-from)*smooth(t/attack);
  const double x=(t-attack)/(1-attack);
  // Rounded release with a small shoulder, like elastic finger/platter drag.
  return peak*(1-smooth(x))*(1+.10*smooth(x)*std::sin(3*3.141592653589793*x)*std::sin(3.141592653589793*x));
}
void RecordNudgeCurve::trigger(double position,double length,double strength,bool reverse) noexcept {
  const double current=at(position);
  *this={position,length,current,std::clamp(current+(reverse?-4.:4.)*strength*strength,-16.,16.),.32-.16*strength};
}
RecordNudgeRuntime::RecordNudgeRuntime(const NativeSong &native) {
  std::map<uint16_t,size_t> targets;
  for(const auto &c:native.performance.commands) if(isNudge(c.kind)) {
    const auto p=std::find_if(native.patterns.begin(),native.patterns.end(),[&](const auto &v){return v.second.id==c.pattern;});
    const auto t=std::find_if(native.tracks.begin(),native.tracks.end(),[&](const auto &v){return v.second.id==c.track;});
    if(p==native.patterns.end()||t==native.tracks.end())throw std::invalid_argument("Nudge has an unresolved pattern or track");
    auto [found,inserted]=targets.emplace(t->first,targets_.size());
    if(inserted){targets_.emplace_back();targets_.back().channel=t->first;}
    targets_[found->second].patterns[p->first].push_back({c.position,c.durationBeats,c.column,c.value,c.kind==PatternCommandKind::NudgeReverse});
  }
  if(targets_.size()>16)throw std::invalid_argument("Use at most 16 tracks with record nudges");
  for(auto &t:targets_)for(auto &[p,events]:t.patterns)
    std::sort(events.begin(),events.end(),[](const auto &a,const auto &b){return std::tie(a.position,a.column)<std::tie(b.position,b.column);});
}
void RecordNudgeRuntime::prepare(OpenMPT::CSoundFile &song,uint32_t count) noexcept {
  using namespace OpenMPT;
  song.nativeNudgeForces.fill(nullptr);
  const auto &s=song.m_PlayState;
  if(!count||count>4096||s.m_flags[SONG_PAUSED|SONG_FADINGSONG]||!s.m_nSamplesPerTick||!s.TicksOnRow())return;
  const double units=s.NativeRowStep(performanceUnitsPerRow);
  const double begin=s.NativeRowPosition(performanceUnitsPerRow),beatStep=s.NativeBeatStep();
  if(units<=0||beatStep<=0)return;
  const bool entering=pattern_!=s.m_nPattern||order_!=s.m_nCurrentOrder||begin<=previous_;
  pattern_=s.m_nPattern;order_=s.m_nCurrentOrder;previous_=begin;
  if(entering)beatPosition_=0;
  for(auto &t:targets_) {
    if(entering){const auto found=t.patterns.find(uint16_t(pattern_));t.events=found==t.patterns.end()?nullptr:&found->second;t.curve={};
      t.next=t.events?size_t(std::lower_bound(t.events->begin(),t.events->end(),begin-1e-8,[](const auto &e,double p){return e.position<p;})-t.events->begin()):0;}
    bool active=false;
    for(uint32_t f=0;f<count;++f){const double p=begin+f*units,beat=beatPosition_+f*beatStep;
      while(t.events&&t.next<t.events->size()&&(*t.events)[t.next].position<=p+1e-8){const auto &e=(*t.events)[t.next++];t.curve.trigger(beat-(p-e.position)*beatStep/units,e.durationBeats,e.strength,e.reverse);}
      t.forces[f]=t.curve.at(beat);active|=t.forces[f]!=0;
    }
    if(active&&!s.Chn[t.channel].dwFlags[CHN_MUTE|CHN_SYNCMUTE])song.nativeNudgeForces[t.channel]=t.forces.data();
  }
  beatPosition_+=count*beatStep;
}
}
