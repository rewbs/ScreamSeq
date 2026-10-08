#include "SongModulation.hpp"
#include <cmath>
#include <limits>
#include <numbers>
#include <set>
#include <stdexcept>

namespace Tracker {
namespace {
void require(bool value,const char *message) { if(!value)throw std::invalid_argument(message); }
bool validClock(const SignalClock &c) noexcept {
  return std::isfinite(c.beat)&&std::abs(c.beat)<=1e12&&std::isfinite(c.tempo)&&c.tempo>0&&c.tempo<=1e6&&
    std::isfinite(c.position)&&std::abs(c.position)<=1e15&&std::isfinite(c.unitsPerFrame)&&c.unitsPerFrame>=0&&c.unitsPerFrame<=1e9&&
    std::isfinite(c.endPosition)&&c.endPosition>=0&&c.endPosition<=1e15&&std::isfinite(c.rowsPerBeat)&&c.rowsPerBeat>=1;
}
// Reuse the document's source/edge validation without pretending to validate
// host-owned ports or the surrounding song's pattern identities here.
void validateControls(const SignalGraph &graph) {
  SignalGraph controls;controls.songSources=graph.songSources;controls.songModulation=graph.songModulation;
  std::vector<uint64_t> buses,instruments;std::map<uint64_t,uint32_t> patterns;
  for(const auto &s:controls.songSources) {
    if(s.audioBus)buses.push_back(s.audioBus);if(s.noteTarget)buses.push_back(s.noteTarget);if(s.noteInstrument)instruments.push_back(s.noteInstrument);
    for(const auto &lane:s.node.envelopes){auto &rows=patterns[lane.pattern];rows=std::max(rows,1u);for(const auto &p:lane.points)rows=std::max(rows,p.position/256+1);}
  }
  controls.validate(buses,patterns,instruments);
}
}
SongModulationRuntime::SongModulationRuntime(const SignalGraph &graph,std::span<const SongModulationParameter> parameters,double rate):sampleRate_(rate) {
  require(std::isfinite(rate)&&rate>=8000&&rate<=384000,"Invalid song modulation sample rate");validateControls(graph);
  std::set<std::pair<std::string,uint32_t>> known;
  for(const auto &p:parameters)require(known.emplace(p.plugin,p.parameter).second,"Duplicate song modulation parameter metadata");
  sources_.reserve(graph.songSources.size());
  for(const auto &s:graph.songSources){sources_.emplace_back();auto &prepared=sources_.back();prepared.spec=s;prepared.amount=s.amount;
    auto &lanes=prepared.spec.node.envelopes;std::sort(lanes.begin(),lanes.end(),[](const auto &a,const auto &b){return a.pattern<b.pattern;});
    prepared.attack=std::exp(-1/(rate*s.node.attack));prepared.release=std::exp(-1/(rate*s.node.release));
  }
  for(const auto &edge:graph.songModulation) {
    const auto metadata=std::find_if(parameters.begin(),parameters.end(),[&](const auto &p){return p.plugin==edge.plugin&&p.parameter==edge.parameter;});
    require(metadata!=parameters.end(),"Song modulation target parameter is unavailable");
    require(metadata->writable,"Read-only parameters cannot be modulated");
    require(std::isfinite(metadata->normalizedStep)&&metadata->normalizedStep>=0&&metadata->normalizedStep<=1,"Invalid parameter quantization step");
    require(metadata->continuous||(edge.quantized&&metadata->normalizedStep>0),"Stepped parameters require explicit quantized modulation");
    require(!edge.quantized||metadata->normalizedStep>0,"Quantized modulation requires a parameter with discrete steps");
    if(!edge.enabled)continue;
    const auto sourceIndex=size_t(std::find_if(sources_.begin(),sources_.end(),[&](const auto &s){return s.spec.node.id==edge.source;})-sources_.begin());
    auto target=std::find_if(targets_.begin(),targets_.end(),[&](const auto &t){return t.plugin==edge.plugin&&t.parameter==edge.parameter;});
    if(target==targets_.end()){targets_.push_back({edge.plugin,edge.parameter,metadata->normalizedStep,edge.quantized,{}});target=targets_.end()-1;}
    target->contributions.push_back({sourceIndex,edge.minimum,edge.maximum});
  }
}
const SignalPatternEnvelope *SongModulationRuntime::envelope(const Source &s,uint64_t pattern) const noexcept {
  const auto &lanes=s.spec.node.envelopes;
  const auto found=std::lower_bound(lanes.begin(),lanes.end(),pattern,[](const auto &lane,uint64_t id){return lane.pattern<id;});
  return found!=lanes.end()&&found->pattern==pattern&&found->enabled?&*found:nullptr;
}
double SongModulationRuntime::source(const Source &s,double beat,double position,const SignalClock &clock) const noexcept {
  const auto &n=s.spec.node;
  switch(n.kind) {
  case SignalNodeKind::LFO:return .5+.5*std::sin(2*std::numbers::pi*(beat*n.rate+n.phase));
  case SignalNodeKind::Random:{uint64_t x=uint64_t(int64_t(std::floor(beat*n.rate+n.phase)))^(n.id*0x9e3779b97f4a7c15ULL);x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;x=(x^(x>>27))*0x94d049bb133111ebULL;x^=x>>31;return double(x>>11)*(1.0/9007199254740991.0);}
  case SignalNodeKind::MIDI:return midi_[n.controller];
  case SignalNodeKind::Amount:return s.amount;
  case SignalNodeKind::Automation:{const auto *lane=envelope(s,clock.pattern);return lane?automationValue(lane->points,position,clock.endPosition,clock.rowsPerBeat):0;}
  default:return s.envelope;
  }
}
double SongModulationRuntime::sampledSource(const Source &s,uint64_t frame,double beat,double position,double beatsPerFrame,const SignalClock &clock) const noexcept {
  const auto kind=s.spec.node.kind;
  if(kind!=SignalNodeKind::Automation&&kind!=SignalNodeKind::LFO)return source(s,beat,position,clock);
  // Same absolute 32-sample approximation as reusable SignalRuntime. Point
  // boundaries shorten the grid, never the caller's callback partition.
  double first=-double(frame%quantum),last=first+quantum-1;
  if(kind==SignalNodeKind::Automation&&clock.playing&&clock.unitsPerFrame>0) {
    const auto *lane=envelope(s,clock.pattern);if(!lane)return 0;
    const auto sampleAt=[&](double p){return std::ceil((p-position)/clock.unitsPerFrame-1e-9);};
    first=std::max(first,sampleAt(0));if(clock.endPosition>position)last=std::min(last,sampleAt(clock.endPosition)-1);
    auto right=std::upper_bound(lane->points.begin(),lane->points.end(),position,[](double p,const auto &point){return p<point.position;});
    if(right!=lane->points.end())last=std::min(last,sampleAt(right->position)-1);
    if(right!=lane->points.begin()) {const auto &left=*(right-1);double boundary=sampleAt(left.position);
      if(left.curve==AutomationCurve::StepNext&&std::abs(position+boundary*clock.unitsPerFrame-left.position)<1e-8){if(boundary==0)last=std::min(last,0.);else boundary+=1;}
      first=std::max(first,boundary);
    }
  }
  if(first>=last)return source(s,beat,position,clock);
  const auto a=source(s,beat+first*beatsPerFrame,position+first*clock.unitsPerFrame,clock);
  const auto b=source(s,beat+last*beatsPerFrame,position+last*clock.unitsPerFrame,clock);
  return a+(b-a)*(-first)/(last-first);
}
uint32_t SongModulationRuntime::segmentFrames(const Source &s,uint64_t frame,uint32_t maximum,const SignalClock &clock) const noexcept {
  auto count=std::min(maximum,quantum-uint32_t(frame%quantum));
  if(!clock.playing)return count;
  if(s.spec.node.kind==SignalNodeKind::Automation&&clock.unitsPerFrame>0)if(const auto *lane=envelope(s,clock.pattern)) {
    auto right=std::upper_bound(lane->points.begin(),lane->points.end(),clock.position,[](double p,const auto &point){return p<point.position;});
    if(right!=lane->points.end()){const auto distance=std::ceil((right->position-clock.position)/clock.unitsPerFrame-1e-9);if(distance>=1&&distance<count)count=uint32_t(distance);}
    if(right!=lane->points.begin()&&(right-1)->curve==AutomationCurve::StepNext&&std::abs(clock.position-(right-1)->position)<1e-8)count=std::min(count,1u);
    if(clock.endPosition>clock.position){const auto distance=std::ceil((clock.endPosition-clock.position)/clock.unitsPerFrame-1e-9);if(distance>=1&&distance<count)count=uint32_t(distance);}
  }
  if(s.spec.node.kind==SignalNodeKind::Random) {
    const auto cycle=clock.beat*s.spec.node.rate+s.spec.node.phase;
    const auto rate=clock.tempo*s.spec.node.rate/(60*sampleRate_);
    const auto distance=std::ceil((std::floor(cycle)+1-cycle)/rate-1e-9);
    if(distance>=1&&distance<count)count=uint32_t(distance);
  }
  return count;
}
bool SongModulationRuntime::renderSource(size_t index,uint32_t frames,uint64_t absoluteFrame,SignalClock clock,const float *audio) noexcept {
  if(index>=sources_.size())return false;auto &s=sources_[index];s.frames=0;
  if(frames>maximumFrames||absoluteFrame>UINT64_MAX-frames||!validClock(clock))return false;
  const auto kind=s.spec.node.kind;
  if(frames&&kind==SignalNodeKind::Follower&&(s.spec.audioBus||!s.spec.audioPlugin.empty())&&!audio)return false;
  const auto beatsPerFrame=clock.playing?clock.tempo/(60*sampleRate_):0.;if(!clock.playing)clock.unitsPerFrame=0;
  for(uint32_t offset=0;offset<frames;) {
    auto at=clock;at.beat+=offset*beatsPerFrame;at.position+=offset*clock.unitsPerFrame;
    const auto count=segmentFrames(s,absoluteFrame+offset,frames-offset,at);
    if(kind==SignalNodeKind::Follower||kind==SignalNodeKind::NoteEnvelope) {
      for(uint32_t i=0;i<count;++i){double target=s.held?1:0;
        if(kind==SignalNodeKind::Follower){target=0;if(audio&&(s.spec.audioBus||!s.spec.audioPlugin.empty()))for(size_t channel=0;channel<2;++channel){const auto sample=audio[(offset+i)*2+channel];if(std::isfinite(sample))target=std::max(target,std::min(1.,std::abs(double(sample))));}}
        s.envelope=target+(target>s.envelope?s.attack:s.release)*(s.envelope-target);s.values[offset+i]=s.envelope;
      }
    } else {
      const auto first=sampledSource(s,absoluteFrame+offset,at.beat,at.position,beatsPerFrame,clock);
      const auto last=sampledSource(s,absoluteFrame+offset+count-1,at.beat+(count-1)*beatsPerFrame,at.position+(count-1)*clock.unitsPerFrame,beatsPerFrame,clock);
      for(uint32_t i=0;i<count;++i)s.values[offset+i]=count>1?first+(last-first)*double(i)/(count-1):first;
    }
    offset+=count;
  }
  s.frame=absoluteFrame;s.frames=frames;return true;
}
std::span<const double> SongModulationRuntime::values(size_t index) const noexcept {
  return index<sources_.size()?std::span<const double>(sources_[index].values.data(),sources_[index].frames):std::span<const double>{};
}
bool SongModulationRuntime::contribution(size_t index,uint64_t frame,double &value) const noexcept {
  if(index>=sources_.size())return false;const auto &s=sources_[index];
  if(frame<s.frame||frame-s.frame>=s.frames)return false;value=s.values[size_t(frame-s.frame)];return true;
}
bool SongModulationRuntime::overlay(size_t index,uint64_t frame,double baseline,double &result) const noexcept {
  if(index>=targets_.size()||!std::isfinite(baseline))return false;
  const auto &target=targets_[index];double value=baseline;
  for(const auto &c:target.contributions){double source;if(!contribution(c.source,frame,source))return false;value+=c.minimum+(c.maximum-c.minimum)*source;}
  value=std::clamp(value,0.,1.);
  if(target.quantized)value=std::clamp(std::round(value/target.normalizedStep)*target.normalizedStep,0.,1.);
  result=value;return true;
}
uint32_t SongModulationRuntime::rampFrames(size_t index,uint64_t frame,uint32_t maximum,SignalClock clock) const noexcept {
  if(index>=targets_.size()||!maximum||!validClock(clock))return 0;
  const auto &target=targets_[index];auto count=std::min(maximum,maximumFrames);
  if(target.quantized)count=1;
  for(const auto &c:target.contributions){const auto &s=sources_[c.source];
    if(frame<s.frame||frame-s.frame>=s.frames)return 0;
    count=std::min(count,s.frames-uint32_t(frame-s.frame));count=segmentFrames(s,frame,count,clock);
    // Stateful AR curves are exact per sample. Do not approximate against an
    // endpoint that moves when the host splits an audio callback.
    if(s.spec.node.kind==SignalNodeKind::Follower||s.spec.node.kind==SignalNodeKind::NoteEnvelope)count=1;
  }
  return count;
}
void SongModulationRuntime::controller(uint32_t cc,double value) noexcept {
  if(cc>=128||!std::isfinite(value))return;value=std::clamp(value,0.,1.);if(midi_[cc]==value)return;midi_[cc]=value;
  for(auto &s:sources_)if(s.spec.node.kind==SignalNodeKind::MIDI&&s.spec.node.controller==cc)s.frames=0;
}
void SongModulationRuntime::amount(uint64_t id,double value) noexcept {if(std::isfinite(value))for(auto &s:sources_)if(s.spec.node.id==id){value=std::clamp(value,0.,1.);if(s.amount!=value){s.amount=value;s.frames=0;}}}
void SongModulationRuntime::note(uint64_t target,uint64_t instrument,bool on,bool retrigger) noexcept {
  for(auto &s:sources_)if(s.spec.node.kind==SignalNodeKind::NoteEnvelope&&(!s.spec.noteTarget||s.spec.noteTarget==target)&&(!s.spec.noteInstrument||s.spec.noteInstrument==instrument)) {
    if(on){if(s.held<UINT32_MAX)++s.held;if(retrigger)s.envelope=0;}else if(s.held)--s.held;s.frames=0;
  }
}
void SongModulationRuntime::gate(uint64_t id,bool on,bool retrigger) noexcept {for(auto &s:sources_)if(s.spec.node.id==id&&s.spec.node.kind==SignalNodeKind::NoteEnvelope){s.held=on?1:0;if(retrigger)s.envelope=0;s.frames=0;}}
void SongModulationRuntime::panic() noexcept {for(auto &s:sources_)if(s.spec.node.kind==SignalNodeKind::NoteEnvelope){s.held=0;s.frames=0;}}
void SongModulationRuntime::inheritState(const SongModulationRuntime &old) noexcept {
  midi_=old.midi_;
  for(auto &s:sources_)for(const auto &p:old.sources_)if(s.spec.node.id==p.spec.node.id && s.spec.node.kind==p.spec.node.kind){
    // A follower is the same running AR processor when its input cable moves.
    // Let the new input attack/release from its current level instead of
    // introducing an unrelated zero-value discontinuity at publication.
    s.envelope=p.envelope;
    if(s.spec.noteTarget==p.spec.noteTarget && s.spec.noteInstrument==p.spec.noteInstrument)s.held=p.held;
    break;
  }
}
size_t SongModulationRuntime::storageBytes() const noexcept {
  size_t result=sizeof(*this)+sources_.capacity()*sizeof(Source)+targets_.capacity()*sizeof(SongModulationTarget);
  for(const auto &s:sources_){result+=s.spec.audioPlugin.capacity()+s.spec.node.name.capacity();for(const auto &e:s.spec.node.envelopes){result+=sizeof(e)+e.points.capacity()*sizeof(AutomationPoint);for(const auto &p:e.points)result+=p.formula.bytes();}}
  for(const auto &t:targets_)result+=t.plugin.capacity()+t.contributions.capacity()*sizeof(SongModulationTarget::Contribution);return result;
}
} // namespace Tracker
