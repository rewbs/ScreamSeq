#include "PatternGateRuntime.hpp"
#include <stdexcept>

namespace Tracker {
using namespace OpenMPT;
PatternGateRuntime::PatternGateRuntime(const NativeSong &native,CSoundFile &song) {
  for(const auto &c:native.performance.commands)if(isGate(c)){
    const auto p=std::find_if(native.patterns.begin(),native.patterns.end(),[&](const auto &v){return v.second.id==c.pattern;});
    const auto t=std::find_if(native.tracks.begin(),native.tracks.end(),[&](const auto &v){return v.second.id==c.track;});
    if(p==native.patterns.end()||t==native.tracks.end())throw std::invalid_argument("Gate has an unresolved pattern or track");
    Event e{c,t->first};const auto kind=GateTarget(uint8_t(c.arguments[0]));
    if(kind!=GateTarget::All){
      Target target{t->first,kind,{},{}};
      if(kind==GateTarget::Parameter){const auto &b=native.performance.bindings.at(gateBinding(c));target.plugin=b.plugin;target.parameter=b.parameter;}
      const auto found=std::find_if(slots_.begin(),slots_.end(),[&](const auto &s){const auto &v=s.target;return std::tie(v.channel,v.kind,v.plugin,v.parameter)==std::tie(target.channel,target.kind,target.plugin,target.parameter);});
      e.slot=size_t(found-slots_.begin());if(found==slots_.end())slots_.push_back({std::move(target),{}, {}});
    }
    events_[p->first].push_back(std::move(e));
  }
  if(slots_.size()>256)throw std::invalid_argument("Use at most 256 channel/destination gate targets");
  for(auto &[p,events]:events_)std::sort(events.begin(),events.end(),[](const auto &a,const auto &b){return std::tie(a.command.position,a.channel,a.command.column)<std::tie(b.command.position,b.channel,b.command.column);});
  if(!slots_.empty())chase(native,song);
}
void PatternGateRuntime::apply(const Event &e,double late) noexcept {
  if(e.command.native==NativePatternOp::GateStop){if(e.slot==SIZE_MAX){for(auto &s:slots_)if(s.target.channel==e.channel)s.state.active=false;}else slots_[e.slot].state.active=false;return;}
  auto &s=slots_[e.slot].state;const auto &a=e.command.arguments;using namespace GateArgument;
  s.signal={a[GateArgument::floor],a[ceiling],a[period],a[phase],a[width],a[rise],a[fall],GateShape(uint8_t(a[riseShape])),GateShape(uint8_t(a[fallShape]))};
  s.elapsed=late;s.serial=++serial_;s.pitchRange=uint8_t(a[pitchRange]);s.active=true;
}
void PatternGateRuntime::chase(const NativeSong &native,CSoundFile &song){
  const auto order=song.m_PlayState.m_nCurrentOrder;const auto row=song.m_PlayState.m_nNextRow;
  if(!order&&!row)return;
  // Prepare row-length overrides once; a long seek must not scan the entire
  // song's command list for every visited row. Match playback's channel/FX order.
  std::map<std::pair<uint64_t,uint32_t>,std::tuple<uint16_t,uint8_t,double>> lengths;
  for(const auto &c:native.performance.commands)if(c.kind==PatternCommandKind::Native&&c.native==NativePatternOp::RowLength){
    const auto track=std::find_if(native.tracks.begin(),native.tracks.end(),[&](const auto &t){return t.second.id==c.track;});
    const auto key=std::pair(c.pattern,c.position/performanceUnitsPerRow);
    const auto found=lengths.find(key);
    if(found==lengths.end()||std::tie(track->first,c.column)>std::tie(std::get<0>(found->second),std::get<1>(found->second)))lengths[key]={track->first,c.column,c.arguments[0]};
  }
  GetLengthTarget target(order,row);
  target.onRow=[&](ORDERINDEX atOrder,ROWINDEX atRow,double){
    if(atOrder==order&&atRow==row)return;
    if(!song.Order().IsValidPat(atOrder))return;
    const auto pattern=song.Order()[atOrder];const auto &p=song.Patterns[pattern];
    double beats=1./std::max(1u,unsigned(p.GetOverrideSignature()?p.GetRowsPerBeat():song.m_nDefaultRowsPerBeat));
    const auto length=lengths.find({native.patterns.at(pattern).id,atRow});if(length!=lengths.end())beats=std::get<2>(length->second);
    const auto found=events_.find(pattern);double at=0;
    if(found!=events_.end())for(auto event=std::lower_bound(found->second.begin(),found->second.end(),uint32_t(atRow)*performanceUnitsPerRow,[](const auto &e,uint32_t position){return e.command.position<position;});event!=found->second.end()&&event->command.position/performanceUnitsPerRow==atRow;++event){const auto &e=*event;
      const double next=double(e.command.position%performanceUnitsPerRow)/performanceUnitsPerRow*beats;
      for(auto &s:slots_)if(s.state.active)s.state.elapsed+=next-at;
      apply(e);at=next;
    }
    for(auto &s:slots_)if(s.state.active)s.state.elapsed=std::fmod(s.state.elapsed+beats-at,s.state.signal.period);
  };
  const auto result=song.GetLength(eNoAdjust,target);
  if(result.empty()||!result.back().targetReached)for(auto &s:slots_)s.state.active=false;
}
uint32_t PatternGateRuntime::limit(const CSoundFile &song,uint32_t count)const noexcept {
  const auto &s=song.m_PlayState;if(!count||s.m_flags[SONG_PAUSED|SONG_FADINGSONG])return count;
  const auto found=events_.find(s.m_nPattern);if(found==events_.end())return count;
  const double begin=s.NativeRowPosition(performanceUnitsPerRow),step=s.NativeRowStep(performanceUnitsPerRow);if(step<=0)return count;
  const auto event=std::upper_bound(found->second.begin(),found->second.end(),begin+1e-7,[](double p,const Event &e){return p<e.command.position;});
  if(event!=found->second.end()){const auto frames=std::ceil((event->command.position-begin)/step-1e-8);if(frames<count)count=uint32_t(std::max(1.,frames));}
  return count;
}
void PatternGateRuntime::prepare(CSoundFile &song,uint32_t count)noexcept {
  song.nativeGateVolumes.fill(nullptr);song.nativeGatePans.fill(nullptr);song.nativeGatePitches.fill(nullptr);
  auto &s=song.m_PlayState;const bool advancing=count&&!s.m_flags[SONG_PAUSED|SONG_FADINGSONG]&&s.NativeBeatStep()>0;
  if(advancing){
    const double position=s.NativeRowPosition(performanceUnitsPerRow),step=s.NativeRowStep(performanceUnitsPerRow);
    const bool entering=pattern_!=s.m_nPattern||order_!=s.m_nCurrentOrder||position<=previous_||(row_!=UINT32_MAX&&s.m_nRow!=row_&&s.m_nRow!=row_+1);
    if(entering){const auto found=events_.find(s.m_nPattern);current_=found==events_.end()?nullptr:&found->second;
      next_=current_?size_t(std::lower_bound(current_->begin(),current_->end(),position-1e-7,[](const Event &e,double at){return e.command.position<at;})-current_->begin()):0;}
    pattern_=s.m_nPattern;order_=s.m_nCurrentOrder;row_=s.m_nRow;previous_=position;
    while(current_&&next_<current_->size()&&(*current_)[next_].command.position<=position+1e-7){const auto &e=(*current_)[next_++];apply(e,std::max(0.,(position-e.command.position)/step*s.NativeBeatStep()));}
  }
  for(auto &slot:slots_){auto &v=slot.snapshot;v=slot.state;v.beatsPerFrame=advancing?s.NativeBeatStep():0;
    // Clock continues through muted channels; their output remains muted by the mixer.
    if(advancing&&v.active)slot.state.elapsed=std::fmod(slot.state.elapsed+count*v.beatsPerFrame,v.signal.period);
    if(!v.active||!advancing)continue;
    switch(slot.target.kind){case GateTarget::Volume:song.nativeGateVolumes[slot.target.channel]=&v;break;
    case GateTarget::Pan:song.nativeGatePans[slot.target.channel]=&v;break;
    case GateTarget::Pitch:song.nativeGatePitches[slot.target.channel]=&v;break;default:break;}
  }
}
const GateSnapshot *PatternGateRuntime::channel(uint16_t channel,GateTarget kind)const noexcept {
  for(const auto &s:slots_)if(s.target.channel==channel&&s.target.kind==kind)return s.snapshot.active?&s.snapshot:nullptr;return nullptr;
}
size_t PatternGateRuntime::storageBytes()const noexcept{size_t bytes=sizeof(*this)+slots_.capacity()*sizeof(Slot);for(const auto &[id,v]:events_)bytes+=sizeof(v)+v.capacity()*sizeof(Event)+64;for(const auto &s:slots_)bytes+=s.target.plugin.capacity();return bytes;}
}
