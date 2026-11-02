#pragma once
#include "TrackerDocument.hpp"
#include <chrono>
#include <set>

namespace Tracker {
// One cooperative budget covers preparation, both bounded engine walks and
// audio replay. Row visits also make early-target regressions deterministic.
struct PatternActivityBudget {
  struct Expired {};
  std::chrono::steady_clock::time_point deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
  size_t rowVisits=0;
  void check()const {if(std::chrono::steady_clock::now()>=deadline)throw Expired{};}
  void visit(double seconds){++rowVisits;check();if(seconds>=3600)throw Expired{};}
};
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
inline void activityLimit(PatternActivity &result){result.complete=false;result.limitation="Analysis time or song-length limit reached; move closer to the song start";}
inline PatternActivity patternActivity(Document &document,uint16_t pattern,uint32_t order,uint16_t row,uint16_t channel,const std::set<uint16_t> &pluginInstruments={},PatternActivityBudget *sharedBudget=nullptr) {
  using namespace OpenMPT;
  PatternActivityBudget localBudget;auto &budget=sharedBudget?*sharedBudget:localBudget;
  const auto &source=document.song();
  if(!source.Patterns.IsValidPat(pattern)||row>=source.Patterns[pattern].GetNumRows()||channel>=source.GetNumChannels()||order>=source.Order().size()||source.Order()[ORDERINDEX(order)]!=pattern)
    throw std::invalid_argument("Choose a valid pattern, order occurrence, row and channel");
  PatternActivity result;
  try {
  budget.check();document.native().prepareEffects(document.song());budget.check();
  GetLengthTarget target{ORDERINDEX(order),ROWINDEX(row)};
  target.onRow=[&](ORDERINDEX,ROWINDEX,double seconds){budget.visit(seconds);};
  const auto reached=document.song().GetLength(eNoAdjust,target);budget.check();
  if(reached.empty()||!reached.back().targetReached){result.complete=true;result.limitation="Row is unreachable from the song start";return result;}
  result.reachable=true;
  Renderer replay(document.snapshotData(),48000,0,false,{},source.Order.GetCurrentSequenceIndex(),{},&document.native());
  replay.preparePreciseNotes(document.native());replay.applyColumnMutes(document.native(),source);
  budget.check();
  auto &song=replay.song();std::map<const ModInstrument *,uint16_t> instruments;
  for(INSTRUMENTINDEX i=1;i<=song.GetNumInstruments();++i)if(song.Instruments[i])instruments[song.Instruments[i]]=i;
  std::array<float,512> pcm{};bool entered=false;
  const uint64_t maximumFrames=uint64_t(48000)*60*60;
  for(uint64_t frames=0;frames<maximumFrames;){
    if((frames&16383)<256)budget.check();
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
  budget.check();
  }catch(const PatternActivityBudget::Expired &){activityLimit(result);}
  return result;
}
}
