#include "PatternPitchRuntime.hpp"
#include "soundlib/ModInstrument.h"
namespace Tracker {
PatternPitchRuntime::PatternPitchRuntime(const NativeSong &native,OpenMPT::CSoundFile &song,
 const std::vector<std::shared_ptr<NativePlugin>> &plugins,const std::vector<bool> &bypass) {
  std::map<uint16_t,size_t> targets;
  for(const auto &command:native.performance.commands) {
    if(command.kind!=PatternCommandKind::PitchSet&&command.kind!=PatternCommandKind::PitchSlide)continue;
    const auto track=std::find_if(native.tracks.begin(),native.tracks.end(),[&](const auto &v){return v.second.id==command.track;});
    const auto pattern=std::find_if(native.patterns.begin(),native.patterns.end(),[&](const auto &v){return v.second.id==command.pattern;});
    auto [found,inserted]=targets.emplace(track->first,targets_.size());
    if(inserted){targets_.emplace_back();targets_.back().channel=track->first;}
    targets_[found->second].patterns[pattern->first].push_back({command.position,command.duration,command.column,command.value,command.pitchRange});
  }
  if(targets_.size()>16)throw std::invalid_argument("Use at most 16 tracks with native pitch commands");
  std::sort(targets_.begin(),targets_.end(),[](const auto &a,const auto &b){return a.channel<b.channel;});
  for(auto &target:targets_)for(auto &[pattern,events]:target.patterns)
    std::sort(events.begin(),events.end(),[](const auto &a,const auto &b){return std::tie(a.position,a.column)<std::tie(b.position,b.column);});
  if(targets_.empty())return;
  for(size_t slot=0;slot<plugins.size();++slot)if(!bypass[slot]&&plugins[slot]->isInstrument())
    for(const auto &assignment:plugins[slot]->assignments()) {
      if(assignment.instrument>song.GetNumInstruments()||!song.Instruments[assignment.instrument])continue;
      plugins[slot]->prepareMusicalMIDI();
      instruments_.push_back({song.Instruments[assignment.instrument],plugins[slot],uint8_t(assignment.channel-1)});
    }
}
bool PatternPitchRuntime::render(OpenMPT::CSoundFile &song,uint32_t count,uint64_t absoluteFrame) noexcept {
  using namespace OpenMPT;
  song.nativePitchRatios.fill(nullptr);
  const auto &state=song.m_PlayState;
  if(!count||state.m_flags[SONG_PAUSED|SONG_FADINGSONG]||!state.m_nSamplesPerTick||!state.TicksOnRow())return true;
  if(count>4096)return false;
  const double unitsPerSample=double(performanceUnitsPerRow)/(double(state.TicksOnRow())*state.m_nSamplesPerTick);
  const double tick=double(state.m_nRow)*performanceUnitsPerRow+double(state.m_nTickCount)*performanceUnitsPerRow/state.TicksOnRow();
  auto musical=[&](uint32_t offset){return tick+double(state.SamplesIntoTick()+offset)*unitsPerSample;};
  const auto begin=musical(0);
  const bool entering=pattern_!=state.m_nPattern||order_!=state.m_nCurrentOrder||begin<=previousPosition_;
  pattern_=state.m_nPattern;order_=state.m_nCurrentOrder;previousPosition_=begin;
  for(auto &target:targets_) {
    const auto &channel=state.Chn[target.channel];
    if(entering){const auto found=target.patterns.find(pattern_);target.events=found==target.patterns.end()?nullptr:&found->second;
      target.next=0;target.curve={target.current,target.current,begin,begin};}
    const bool muted=channel.dwFlags[CHN_MUTE|CHN_SYNCMUTE];
    if(muted){target.current=target.curve.at(begin);target.curve={target.current,target.current,begin,begin};}
    const auto instrument=std::find_if(instruments_.begin(),instruments_.end(),[&](const auto &v){return v.instrument==channel.pModInstrument;});
    int previousWheel=-1;
    for(uint32_t frame=0;frame<count;++frame) {
      const auto position=musical(frame);
      while(target.events&&target.next<target.events->size()&&(*target.events)[target.next].position<=position+1e-8) {
        const auto &event=(*target.events)[target.next++];if(muted)continue;
        target.curve={target.curve.at(event.position),event.value,double(event.position),double(event.position)+event.duration};target.used=true;target.pitchRange=event.pitchRange;
      }
      const auto semitones=target.curve.at(position);
      target.ratios[frame]=std::exp2(semitones/12.);
      if(target.used&&!muted&&instrument!=instruments_.end()) {
        const auto depth=target.pitchRange;
        const int wheel=int(std::lround(std::clamp(semitones/depth,-1.,1.)*8192))+8192;
        if(wheel!=previousWheel) {
          const auto value=std::clamp(wheel,0,16383);
          if(!instrument->plugin->scheduleMIDI(uint8_t(0xe0|instrument->midiChannel),uint8_t(value&127),uint8_t(value>>7),absoluteFrame+frame))return false;
          previousWheel=wheel;
        }
      }
    }
    target.current=target.curve.at(musical(count));
    if(target.used&&!muted)song.nativePitchRatios[target.channel]=target.ratios.data();
  }
  return true;
}
}
