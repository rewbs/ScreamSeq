#include "ScratchRuntime.hpp"
#include <cmath>
#include <set>

namespace Tracker {
std::unique_ptr<ScratchGestureLibrary> ScratchRuntime::prepareUpdate(const NativeSong &native) {
  for(const auto &[slot,gesture]:native.scratchGestures) {
    if(!slot || slot>255)throw std::invalid_argument("Scratch gesture slot must be 1..255");
    validateScratchGesture(gesture);
  }
  return std::make_unique<ScratchGestureLibrary>(native.scratchGestures);
}
ScratchRuntime::ScratchRuntime(const NativeSong &native):library_(prepareUpdate(native)) {
  std::set<uint16_t> tracks;
  for(const auto &command:native.performance.commands)if(command.kind==PatternCommandKind::Native && (command.native==NativePatternOp::Scratch || command.native==NativePatternOp::ScratchStop)) {
    validateNativePatternCommand(command);
    const auto pattern=std::find_if(native.patterns.begin(),native.patterns.end(),[&](const auto &p){return p.second.id==command.pattern;});
    const auto track=std::find_if(native.tracks.begin(),native.tracks.end(),[&](const auto &t){return t.second.id==command.track;});
    if(pattern==native.patterns.end() || track==native.tracks.end() || (command.native==NativePatternOp::Scratch && !native.scratchGestures.contains(uint16_t(command.arguments[0]))))throw std::invalid_argument("Scratch command needs an existing gesture, pattern and track");
    const auto &a=command.arguments;
    patterns_[pattern->first].push_back({command.position,track->first,uint16_t(a[0]),command.column,a[1],a[2],uint16_t(a[3]),a[4]!=0});
    if(command.native==NativePatternOp::Scratch)tracks.insert(track->first);
  }
  if(tracks.size()>16)throw std::invalid_argument("Use at most 16 tracks with scratch gestures");
  voices_.reserve(tracks.size());
  for(auto channel:tracks){voices_.emplace_back();voices_.back().channel=channel;}
  for(auto &[pattern,events]:patterns_)std::sort(events.begin(),events.end(),[](const auto &a,const auto &b){return std::tie(a.position,a.channel,a.column)<std::tie(b.position,b.channel,b.column);});
}
bool ScratchRuntime::publishUpdate(std::unique_ptr<ScratchGestureLibrary> &update) noexcept {
  library_.collect();
  return library_.publish(update,library_.requestedRevision()+1);
}
double ScratchRuntime::position(const Voice &voice,const ScratchGesture &gesture,double elapsed) noexcept {
  const double cycle=std::clamp(elapsed/voice.beats,0.,1.)*voice.repeats;
  const auto completed=std::min<double>(voice.repeats-1,std::floor(cycle));
  const double phase=cycle-completed;
  const double motion=scratchEnvelopeValue(gesture.motion,phase,voice.beats/voice.repeats);
  return voice.cue+voice.travel*(motion-voice.motionStart+completed*voice.motionDelta);
}
uint32_t ScratchRuntime::limit(const OpenMPT::CSoundFile &song,uint32_t count) const noexcept {
  const auto &state=song.m_PlayState;
  const double begin=state.NativeRowPosition(performanceUnitsPerRow),step=state.NativeRowStep(performanceUnitsPerRow),beatStep=state.NativeBeatStep();
  if(step<=0 || beatStep<=0)return count;
  auto shorten=[&](double frames){if(frames>0 && frames<count)count=uint32_t(std::max(1.,std::ceil(frames-1e-8)));};
  const auto found=patterns_.find(uint16_t(state.m_nPattern));
  if(found!=patterns_.end()) {
    const auto &events=found->second;
    auto event=std::lower_bound(events.begin(),events.end(),begin-1e-7,[](const auto &e,double p){return e.position<p;});
    for(;event!=events.end();++event) {
      if(event->position>begin+1e-7){shorten((event->position-begin)/step);break;}
      shorten(event->beats/beatStep);
    }
  }
  for(const auto &voice:voices_)if(voice.active && voice.elapsed<voice.beats-1e-12)shorten((voice.beats-voice.elapsed)/beatStep);
  return count;
}
uint32_t ScratchRuntime::prepare(OpenMPT::CSoundFile &song,uint32_t count) noexcept {
  using namespace OpenMPT;
  song.nativeScratchPositions.fill(nullptr);song.nativeScratchGains.fill(nullptr);
  if(!count || count>4096)return count;
  const auto smoothFrames=std::max(1u,uint32_t(std::lround(song.GetSampleRate()*.0005)));
  if(library_.begin()) {
    for(auto &voice:voices_)if(voice.active) {
      const auto old=library_.previous()->find(voice.gesture),now=library_.current().find(voice.gesture);
      if(now==library_.current().end()){voice.active=false;continue;}
      if(old!=library_.previous()->end()) {
        const double prior=position(voice,old->second,voice.elapsed);
        voice.motionStart=scratchEnvelopeValue(now->second.motion,0,voice.beats/voice.repeats);
        voice.motionDelta=scratchEnvelopeValue(now->second.motion,1,voice.beats/voice.repeats)-voice.motionStart;
        voice.correction+=prior-position(voice,now->second,voice.elapsed);
        voice.correctionFrames=smoothFrames;
      }
    }
    library_.finish();
  }
  auto &state=song.m_PlayState;
  if(state.m_flags[SONG_PAUSED|SONG_FADINGSONG])return count;
  const double begin=state.NativeRowPosition(performanceUnitsPerRow),step=state.NativeRowStep(performanceUnitsPerRow),beatStep=state.NativeBeatStep();
  if(step<=0 || beatStep<=0)return count;
  const bool jump=row_!=UINT32_MAX && state.m_nRow!=row_ && state.m_nRow!=row_+1;
  const bool entering=pattern_!=state.m_nPattern || order_!=state.m_nCurrentOrder || begin<=previous_ || jump;
  if(entering) {
    pattern_=state.m_nPattern;order_=state.m_nCurrentOrder;
    const auto found=patterns_.find(uint16_t(pattern_));events_=found==patterns_.end()?nullptr:&found->second;
    next_=events_?size_t(std::lower_bound(events_->begin(),events_->end(),begin-1e-7,[](const auto &e,double p){return e.position<p;})-events_->begin()):0;
    for(auto &voice:voices_)voice.active=false;
  }
  previous_=begin;row_=state.m_nRow;
  for(auto &voice:voices_) {
    const auto &channel=state.Chn[voice.channel];
    if(voice.generation!=channel.nativeNoteGeneration || channel.isPreviewNote || !channel.pCurrentSample || !channel.nLength || channel.dwFlags[CHN_MUTE|CHN_SYNCMUTE]) {
      voice.active=false;voice.gain=1;voice.correction=0;voice.correctionFrames=0;
    }
    if(voice.elapsed>=voice.beats-1e-12)voice.active=false;
  }
  while(events_ && next_<events_->size() && (*events_)[next_].position<=begin+1e-7) {
    const auto &event=(*events_)[next_++];auto &channel=state.Chn[event.channel];
    if(!event.gesture) { for(auto &voice:voices_)if(voice.channel==event.channel)voice.active=false; continue; }
    if(channel.isPreviewNote || !channel.pCurrentSample || !channel.pModSample || !channel.nLength || channel.dwFlags[CHN_MUTE|CHN_SYNCMUTE])continue;
    auto found=std::find_if(voices_.begin(),voices_.end(),[&](const auto &v){return v.channel==event.channel;});if(found==voices_.end())continue;
    auto &voice=*found;
    channel.ExitNativeReverseLoop();
    voice.gesture=event.gesture;voice.repeats=event.repeats;voice.generation=channel.nativeNoteGeneration;
    voice.elapsed=0;voice.beats=event.beats;voice.cue=channel.position.ToDouble();
    voice.travel=channel.increment.ToDouble()*song.GetSampleRate()*.001*event.travelMs*(event.reverse?-1:1);
    const auto definition=library_.current().find(voice.gesture);
    if(definition==library_.current().end()){voice.active=false;continue;}
    voice.motionStart=scratchEnvelopeValue(definition->second.motion,0,voice.beats/voice.repeats);
    voice.motionDelta=scratchEnvelopeValue(definition->second.motion,1,voice.beats/voice.repeats)-voice.motionStart;
    voice.correction=0;voice.correctionFrames=0;voice.active=true;
  }
  for(auto &voice:voices_) {
    if(!voice.active && voice.gain==1)continue;
    const auto found=library_.current().find(voice.gesture);
    if(found==library_.current().end()){voice.active=false;voice.gain=1;continue;}
    const auto &gesture=found->second;auto &channel=state.Chn[voice.channel];
    const double end=channel.pModSample?std::max(0.,double(channel.pModSample->nLength)-1):0;
    if(voice.active)song.nativeScratchPositions[voice.channel]=voice.positions.data();
    song.nativeScratchGains[voice.channel]=voice.gains.data();song.nativeScratchGenerations[voice.channel]=voice.generation;
    for(uint32_t frame=0;frame<count;++frame) {
      double target=1;
      if(voice.active) {
        const double cycle=std::min(1.,voice.elapsed/voice.beats)*voice.repeats;
        const double phase=cycle-std::min<double>(voice.repeats-1,std::floor(cycle));
        target=scratchEnvelopeValue(gesture.fader,phase,voice.beats/voice.repeats);
        voice.positions[frame]=std::clamp(position(voice,gesture,voice.elapsed)+voice.correction,0.,end);
        voice.elapsed+=beatStep;
        if(voice.correctionFrames){voice.correction*=double(voice.correctionFrames-1)/voice.correctionFrames;--voice.correctionFrames;}
      }
      const double maximum=1./smoothFrames;
      voice.gain+=std::clamp(target-voice.gain,-maximum,maximum);
      voice.gains[frame]=voice.gain;
    }
    if(voice.active)voice.positions[count]=std::clamp(position(voice,gesture,voice.elapsed)+voice.correction,0.,end);
  }
  return count;
}
}
