#include "NativePatternRuntime.hpp"
#include "soundlib/ModInstrument.h"
#include "soundlib/NativeNoteEffects.h"
#include <cmath>
namespace Tracker {
using namespace OpenMPT;
NativePatternRuntime::NativePatternRuntime(const NativeSong &native,CSoundFile &song) {
 song.nativeDelayedPatternRows.clear();
 for(const auto &c:native.performance.commands) if(c.kind==PatternCommandKind::Native && c.native<NativePatternOp::TempoSet) {
  const auto p=std::find_if(native.patterns.begin(),native.patterns.end(),[&](const auto &v){return v.second.id==c.pattern;});
  const auto t=std::find_if(native.tracks.begin(),native.tracks.end(),[&](const auto &v){return v.second.id==c.track;});
  if(p==native.patterns.end()||t==native.tracks.end())throw std::invalid_argument("Native effect has an unresolved pattern or track");
  if(c.native==NativePatternOp::Retrigger&&c.arguments[0]*60.0/512.0*song.GetSampleRate()<1.)throw std::invalid_argument("Native repeat interval must span at least one audio sample at 512 BPM");
  patterns_[p->first].push_back({c,t->first});
  if(c.native==NativePatternOp::NoteDelay)song.nativeDelayedPatternRows.emplace(CSoundFile::NativeEffectKey{p->first,ROWINDEX(c.position/performanceUnitsPerRow),t->first},true);
 }
 for(auto &[p,events]:patterns_)std::sort(events.begin(),events.end(),[](const auto &a,const auto &b){return std::tie(a.command.position,a.channel,a.command.column)<std::tie(b.command.position,b.channel,b.command.column);});
}
void NativePatternRuntime::apply(CSoundFile &song,const Event &e,double position,double rowsPerBeat) noexcept {
 (void)position;
 const auto &c=e.command;const auto &a=c.arguments;auto &channel=song.m_PlayState.Chn[e.channel];auto &voice=channel.nativePatternVoice;
 if(channel.dwFlags[CHN_MUTE|CHN_SYNCMUTE])return;
 const double duration=double(c.duration)/performanceUnitsPerRow;
 if(c.native==NativePatternOp::NoteDelay) {
  if(!channel.nativeHasDelayedNote)return;
  const auto note=channel.nativeDelayedNote;channel.nativeHasDelayedNote=false;
  const bool local=NativeNoteEffectSupported(note.command,note.param);
  song.TriggerNativeNote(e.channel,note.note,note.instr,127,local?note.command:0,local?note.param:0,false,&note);
  return;
 }
 if(c.native==NativePatternOp::NoteRelease){repeats_[e.channel].remaining=0;song.TriggerNativeNote(e.channel,NOTE_KEYOFF,0,127,0,0,true);return;}
 if(c.native==NativePatternOp::Retrigger) {
  if(!ModCommand::IsNote(channel.nLastNote)||(!channel.pCurrentSample&&!channel.HasMIDIOutput()))return;
  auto &r=repeats_[e.channel];r={a[0],a[0],a[2],a[3],channel.nVolume/256.,uint32_t(a[1]),channel.nativeNoteGeneration,channel.nLastNote,channel.nOldIns};return;
 }
 if(c.native==NativePatternOp::GainSet||c.native==NativePatternOp::GainSlide) {
  auto *curve=a[1]==2?&master_:a[1]==1?&gains_[e.channel]:&voice.gain;
  if(a[1]||channel.pCurrentSample)curve->Set(a[0],duration,1);
  return;
 }
 // Voice commands never prime a later sample and never change opaque MIDI voices.
 if(!channel.pCurrentSample||!channel.nLength||channel.isPreviewNote)return;
 auto oscillator=[&](NativePatternOscillator &o){const auto phase=o.phase;const auto cycles=o.cycle;const bool reset=a[5]!=0||!o.active;o={};o.active=true;o.depth=a[0];o.rate=a[1];o.hertz=a[2]!=0;o.shape=uint8_t(a[3]);o.phase=reset?a[4]-std::floor(a[4]):phase;o.cycle=reset?0:cycles;o.duration=duration;};
 switch(c.native) {
 case NativePatternOp::PanSet:case NativePatternOp::PanSlide:voice.pan.Set(a[0],duration,channel.nPan/128.-1);break;
 case NativePatternOp::PitchRelative:voice.pitch.Set((voice.pitch.active?voice.pitch.Value():0)+a[0],duration,0);break;
 case NativePatternOp::TonePortamento:voice.pitch.Set(a[0]-channel.nLastNote,duration,0);break;
 case NativePatternOp::Vibrato:oscillator(voice.vibrato);break;
 case NativePatternOp::Tremolo:oscillator(voice.tremolo);break;
 case NativePatternOp::Panbrello:oscillator(voice.panbrello);break;
 case NativePatternOp::Arpeggio:voice.arpeggio={};voice.arpeggio.active=true;voice.arpeggio.rate=a[2];voice.arpeggio.hertz=a[3]!=0;voice.arpeggio.phase=a[4]-std::floor(a[4]);voice.arpeggio.duration=duration;voice.arpeggioOffsets={a[0],a[1]};break;
 case NativePatternOp::Tremor:voice.tremor={};voice.tremor.active=true;voice.tremor.rate=1/(a[0]+a[1]);voice.tremor.depth=a[2];voice.tremor.phase=a[3]-std::floor(a[3]);voice.tremor.duration=duration;voice.tremorOn=a[0]/(a[0]+a[1]);break;
 case NativePatternOp::SampleOffset:{channel.ExitNativeReverseLoop();double frame=a[0];if(a[1]==1)frame*=channel.pModSample->nLength;else if(a[1]==2)frame=a[0]?channel.pModSample->cues[size_t(a[0])-1]:0;const double end=channel.nLength;if(frame>=end){channel.position.SetInt(channel.nLength);break;}channel.position=SamplePosition::FromDouble(std::max(0.,frame));break;}
 case NativePatternOp::SampleDirection:channel.ExitNativeReverseLoop();channel.dwFlags.set(CHN_PINGPONGFLAG,a[0]!=0);channel.increment=SamplePosition((a[0]?-1:1)*std::abs(channel.increment.GetRaw()));break;
 case NativePatternOp::EnvelopePosition:case NativePatternOp::EnvelopeEnable:{
  if(!channel.pModInstrument)break;
  for(size_t i=0;i<3;++i)if(a[0]==i||a[0]==3){auto &cursor=voice.envelopes[i];const bool wasActive=cursor.active;auto &env=channel.GetEnvelope(static_cast<EnvelopeType>(i));cursor.active=true;
   if(i==0&&!wasActive){
    // Remove just the old envelope factor from the existing mixer ramp once.
    // Leaving enveloped ramp state behind would reintroduce it at the next
    // ordinary tick, when the neutral cache is refreshed. No tracker tick runs.
    auto neutralize=[](int32 &current,int32 &target,int32 &ramp,int32 &delta,double neutral){
     const auto convert=[](double v){return int32(std::clamp(v,double(INT32_MIN),double(INT32_MAX)));};
     if(target){const double scale=neutral/target;current=convert(current*scale);ramp=convert(ramp*scale);delta=convert(delta*scale);}
     else {current=convert(neutral);ramp=convert(neutral*(1<<VOLUMERAMPPRECISION));delta=0;}
     target=convert(neutral);
    };
    neutralize(channel.leftVol,channel.newLeftVol,channel.rampLeftVol,channel.leftRamp,voice.neutralLeft);
    neutralize(channel.rightVol,channel.newRightVol,channel.rampRightVol,channel.rightRamp,voice.neutralRight);
    voice.legacyVolumeFactor=1;
   }
   if(c.native==NativePatternOp::EnvelopePosition)cursor.position=a[1]*rowsPerBeat*song.m_PlayState.m_nMusicSpeed;
   else {if(!wasActive)cursor.position=voice.legacyEnvelopePosition[i];env.flags.set(ENV_ENABLED,a[1]!=0);}
  }
  break;
 }
 default:break;
 }
}
uint32_t NativePatternRuntime::prepare(CSoundFile &song,uint32_t count) noexcept {
 auto &s=song.m_PlayState;song.nativePatternRowsPerFrame=song.nativePatternBeatsPerFrame=song.nativePatternTicksPerFrame=0;
 if(!count||s.m_flags[SONG_PAUSED|SONG_FADINGSONG])return count;
 const double position=s.NativeRowPosition(1),step=s.NativeRowStep(1),rowsPerBeat=std::max(1u,uint32_t(s.m_nCurrentRowsPerBeat));
 if(step<=0)return count;
 const bool jump=row_!=UINT32_MAX&&s.m_nRow!=row_&&s.m_nRow!=row_+1;
 const bool entering=pattern_!=s.m_nPattern||order_!=s.m_nCurrentOrder||position<=previous_||jump;
 if(entering){pattern_=s.m_nPattern;order_=s.m_nCurrentOrder;const auto found=patterns_.find(uint16_t(pattern_));events_=found==patterns_.end()?nullptr:&found->second;
  next_=events_?size_t(std::lower_bound(events_->begin(),events_->end(),position*performanceUnitsPerRow-1e-7,[](const auto &e,double p){return e.command.position<p;})-events_->begin()):0;repeats_.fill({});}
 previous_=position;row_=s.m_nRow;
 while(events_&&next_<events_->size()&&double((*events_)[next_].command.position)/performanceUnitsPerRow<=position+1e-10)apply(song,(*events_)[next_++],position,rowsPerBeat);
 for(size_t i=0;i<song.GetNumChannels();++i){auto &r=repeats_[i];auto &chn=s.Chn[i];if(chn.dwFlags[CHN_MUTE|CHN_SYNCMUTE]){r.remaining=0;continue;}if(r.remaining&&r.generation!=chn.nativeNoteGeneration)r.remaining=0;
  while(r.remaining&&r.next<=1e-10){r.gain=std::clamp(r.gain*r.factor+r.step,0.,4.);const uint8_t velocity=chn.HasMIDIOutput()?uint8_t(std::clamp(std::lround(r.gain*127),0l,127l)):127;
   song.TriggerNativeNote(CHANNELINDEX(i),r.note,r.instrument,velocity);const double delivered=chn.nVolume/256.;chn.nativePatternVoice.gain.Set(delivered>0?r.gain/delivered:0);r.generation=chn.nativeNoteGeneration;--r.remaining;r.next+=r.interval;}
 }
 auto limit=[&](double next){if(next>position+1e-10){const double distance=std::ceil((next-position)/step-1e-8);if(distance<count)count=uint32_t(std::max(1.,distance));}};
 if(events_&&next_<events_->size())limit(double((*events_)[next_].command.position)/performanceUnitsPerRow);
 const double beatStep=s.NativeBeatStep();
 for(const auto &r:repeats_)if(r.remaining&&r.next>1e-10&&beatStep>0){const double frames=std::ceil(r.next/beatStep-1e-8);if(frames<count)count=uint32_t(std::max(1.,frames));}
 song.nativePatternRowsPerFrame=step;song.nativePatternBeatsPerFrame=beatStep;song.nativePatternTicksPerFrame=beatStep*rowsPerBeat*s.m_nMusicSpeed;
 for(auto &r:repeats_)if(r.remaining)r.next-=count*beatStep;
 song.nativePatternMasterGain=master_;master_.Advance(step*count);
 for(size_t i=0;i<gains_.size();++i){song.nativePatternChannelGains[i]=gains_[i];gains_[i].Advance(step*count);}
 return count;
}
}
