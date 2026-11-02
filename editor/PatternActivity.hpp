#pragma once
#include "TrackerDocument.hpp"
#include "PatternTimeline.hpp"
#include <chrono>
#include <set>

namespace Tracker {
inline int activityNativeFamily(const PatternCommand &c){
  auto op=c.native;
  if(op==NativePatternOp::GainSlide)op=NativePatternOp::GainSet;
  if(op==NativePatternOp::PanSlide)op=NativePatternOp::PanSet;
  if(op==NativePatternOp::TonePortamento)op=NativePatternOp::PitchRelative;
  return int(op)+(op==NativePatternOp::GainSet?100*int(c.arguments[1]):0);
}
// Disposable, unhosted engine replay on the control worker. Never inspect the
// musician's renderer, instantiate vendors or touch an audio device. A row means
// its first visit in the selected order occurrence, including fractional notes.
struct PatternActivity {
  bool reachable=false,complete=false;
  std::set<uint16_t> samples,instruments;
  std::set<int> nativeFamilies;
  std::string limitation;
};
inline PatternActivity patternActivity(Document &document,uint16_t pattern,uint32_t order,uint16_t row,uint16_t channel,const std::set<uint16_t> &pluginInstruments={}) {
  using namespace OpenMPT;
  const auto &source=document.song();
  if(!source.Patterns.IsValidPat(pattern)||row>=source.Patterns[pattern].GetNumRows()||channel>=source.GetNumChannels()||order>=source.Order().size()||source.Order()[ORDERINDEX(order)]!=pattern)
    throw std::invalid_argument("Choose a valid pattern, order occurrence, row and channel");
  PatternActivity result;
  const auto timeline=patternTimeline(document,pattern,order);
  if(!timeline.positions[row].songSeconds){result.complete=true;result.limitation="Row is unreachable from the song start";return result;}
  result.reachable=true;
  Renderer replay(document.snapshotData(),48000,0,false,{},source.Order.GetCurrentSequenceIndex(),{},&document.native());
  replay.preparePreciseNotes(document.native());replay.applyColumnMutes(document.native(),source);
  auto &song=replay.song();std::map<const ModInstrument *,uint16_t> instruments;
  for(INSTRUMENTINDEX i=1;i<=song.GetNumInstruments();++i)if(song.Instruments[i])instruments[song.Instruments[i]]=i;
  std::array<float,512> pcm{};bool entered=false;
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
  const uint64_t maximumFrames=uint64_t(48000)*60*60;
  for(uint64_t frames=0;frames<maximumFrames;){
    if((frames&16383)<256&&std::chrono::steady_clock::now()>deadline){result.limitation="Analysis time limit reached; move closer to the song start";return result;}
    // Never cross a tracker tick in one observation. Precise notes are observed
    // at most 32 frames apart. Vendor-private tails are not measured here.
    const auto remaining=song.m_PlayState.SamplesRemainingInTick();
    const auto count=replay.render(pcm.data(),remaining?std::min<uint32_t>(32,remaining):1);
    if(!count)break;frames+=count;
    const auto at=replay.telemetry();const bool here=at.order==order&&at.pattern==pattern&&at.row==row;
    if(entered&&!here){result.complete=true;break;}if(!here)continue;entered=true;
    for(const auto &voice:replay.voicePositions()){
      const auto &state=song.m_PlayState.Chn[voice.channel];
      const auto parent=voice.channel<song.GetNumChannels()?voice.channel:state.nMasterChn?state.nMasterChn-1:UINT32_MAX;
      if(parent==channel){result.samples.insert(uint16_t(voice.sample));if(voice.instrument)result.instruments.insert(uint16_t(voice.instrument));
        const auto &v=state.nativePatternVoice;
        for(const auto &[active,op]:std::initializer_list<std::pair<bool,NativePatternOp>>{
          {v.gain.active,NativePatternOp::GainSet},{v.pan.active,NativePatternOp::PanSet},{v.pitch.active,NativePatternOp::PitchRelative},
          {v.vibrato.active,NativePatternOp::Vibrato},{v.tremolo.active,NativePatternOp::Tremolo},{v.panbrello.active,NativePatternOp::Panbrello},
          {v.arpeggio.active,NativePatternOp::Arpeggio},{v.tremor.active,NativePatternOp::Tremor}})if(active)result.nativeFamilies.insert(int(op));
      }
    }
    if(song.nativePatternChannelGains[channel].active)result.nativeFamilies.insert(100+int(NativePatternOp::GainSet));
    if(song.nativePatternMasterGain.active)result.nativeFamilies.insert(200+int(NativePatternOp::GainSet));
    // Plugin voices have no sample pointer. This describes held note ownership,
    // not vendor-private release tails, which cannot be inferred without hosting.
    const auto &voice=song.m_PlayState.Chn[channel];
    if(voice.pModInstrument&&voice.nNote>=NOTE_MIN&&voice.nNote<=NOTE_MAX&&!voice.dwFlags[CHN_KEYOFF|CHN_NOTEFADE|CHN_MUTE]&&instruments.contains(voice.pModInstrument)&&pluginInstruments.contains(instruments.at(voice.pModInstrument)))result.instruments.insert(instruments.at(voice.pModInstrument));
  }
  if(entered)result.complete=true;
  if(!result.complete&&result.limitation.empty())result.limitation="Replay ended before reaching the selected row";
  return result;
}
}
