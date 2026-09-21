#include "PreciseNoteRuntime.hpp"
namespace Tracker {
PreciseNoteRuntime::PreciseNoteRuntime(const NativeSong &native) {
  for(const auto &note:native.preciseNotes) {
    const auto pattern=std::find_if(native.patterns.begin(),native.patterns.end(),[&](const auto &v){return v.second.id==note.pattern;});
    const auto track=std::find_if(native.tracks.begin(),native.tracks.end(),[&](const auto &v){return v.second.id==note.track;});
    if(pattern==native.patterns.end()||track==native.tracks.end())throw std::invalid_argument("Precise note has an unresolved pattern or track");
    patterns_[pattern->first].push_back({note.position,track->first,note.instrument,note.note,note.velocity,note.effect,note.parameter});
  }
  for(const auto &command:native.performance.commands) if(command.kind==PatternCommandKind::NoteCut) {
    const auto pattern=std::find_if(native.patterns.begin(),native.patterns.end(),[&](const auto &v){return v.second.id==command.pattern;});
    const auto track=std::find_if(native.tracks.begin(),native.tracks.end(),[&](const auto &v){return v.second.id==command.track;});
    if(pattern==native.patterns.end()||track==native.tracks.end())throw std::invalid_argument("Note cut has an unresolved pattern or track");
    patterns_[pattern->first].push_back({command.position,track->first,0,254,127,0,0,true});
  }
  for(auto &[pattern,events]:patterns_)std::sort(events.begin(),events.end(),[](const auto &a,const auto &b){
    // Release before retrigger when two events share the same sample position.
    return std::tuple(a.position,a.channel,a.cutCommand?2:int(a.note<128))<std::tuple(b.position,b.channel,b.cutCommand?2:int(b.note<128));
  });
}
uint32_t PreciseNoteRuntime::prepare(OpenMPT::CSoundFile &song,uint32_t count) noexcept {
  using namespace OpenMPT;const auto &state=song.m_PlayState;
  if(!count||state.m_flags[SONG_PAUSED|SONG_FADINGSONG]||!state.m_nSamplesPerTick||!state.TicksOnRow())return count;
  const double unitsPerSample=double(performanceUnitsPerRow)/(double(state.TicksOnRow())*state.m_nSamplesPerTick);
  const double tick=double(state.m_nRow)*performanceUnitsPerRow+double(state.m_nTickCount)*performanceUnitsPerRow/state.TicksOnRow();
  const double position=tick+double(state.SamplesIntoTick())*unitsPerSample;
  const bool restart=pattern_!=state.m_nPattern||order_!=state.m_nCurrentOrder||position<=previous_;
  if(restart || row_!=state.m_nRow) {
    row_=state.m_nRow;
    effectOverrides_.fill(false);
  }
  if(restart) {
    pattern_=state.m_nPattern;order_=state.m_nCurrentOrder;
    auto found=patterns_.find(uint16_t(pattern_));events_=found==patterns_.end()?nullptr:&found->second;
    // A jump into the middle of a pattern must not replay historical notes.
    next_=events_?size_t(std::lower_bound(events_->begin(),events_->end(),position-1e-8,[](const auto &event,double p){return event.position<p;})-events_->begin()):0;
  }
  previous_=position;
  if(!events_)return count;
  while(next_<events_->size()&&(*events_)[next_].position<=position+1e-8) {
    const auto &event=(*events_)[next_++];
    if(event.note<128 && event.channel<effectOverrides_.size() && (event.effect || effectOverrides_[event.channel])) {
      auto &channel=song.m_PlayState.Chn[event.channel];
      channel.rowCommand.command=CMD_NONE;channel.rowCommand.param=0;
      channel.dwFlags.reset(CHN_VIBRATO|CHN_TREMOLO);channel.nPanbrelloOffset=0;channel.nCommand=CMD_NONE;
      effectOverrides_[event.channel]=true;
    }
    song.TriggerNativeNote(event.channel,event.note,event.instrument,event.velocity,event.effect,event.parameter,event.cutCommand);
  }
  if(next_<events_->size()) {
    const double distance=std::ceil(((*events_)[next_].position-tick)/unitsPerSample-1e-9)-state.SamplesIntoTick();
    if(distance<count)count=uint32_t(std::max(1.,distance));
  }
  return count;
}
}
