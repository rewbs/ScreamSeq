#include "NativePatternCommands.hpp"
#include "PatternPerformance.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Tracker {
namespace {
using T=NativePatternFieldType;
using D=NativePatternDuration;
constexpr std::string_view scopes[]{"voice","channel","song"}, rates[]{"beat","hz"};
constexpr std::string_view shapes[]{"sine","triangle","saw","square","random"};
constexpr std::string_view offsets[]{"frames","normalized","cue"}, directions[]{"forward","reverse"};
constexpr std::string_view envelopes[]{"volume","pan","pitch","all"}, singleEnvelopes[]{"volume","pan","pitch"};
constexpr double minimumBeat=1.0/65536;
constexpr NativePatternField gain[]{
  {"gain","Gain","linear-gain","percent",0,4,1},
  {"scope","Scope","choice","choice",0,2,0,T::Choice,scopes,7}
};
constexpr NativePatternField pan[]{{"pan","Pan","bipolar","percent",-1,1,0}};
constexpr NativePatternField relativePitch[]{{"semitones","Pitch change","semitones","semitones",-96,96,0}};
constexpr NativePatternField portamento[]{{"note","Target note","tracker-note","tracker-note",1,120,49}};
constexpr auto lfo(double depth,std::string_view unit) {
  return std::array<NativePatternField,6>{{
    {"depth","Depth",unit,unit=="semitones"?unit:"percent",0,depth,0},
    {"rate","Rate","cycles-per-beat-or-hz","cycles-per-beat-or-hz",0,1000,1},
    {"rateMode","Rate unit","choice","choice",0,1,0,T::Choice,rates,5,false},
    {"shape","Waveform","choice","choice",0,4,0,T::Choice,shapes,8,false},
    {"phase","Phase","cycles","cycles",0,1,0,T::Number,{},8,false},
    {"reset","Restart phase","boolean","boolean",0,1,1,T::Boolean,{},5,false}
  }};
}
constexpr auto vibrato=lfo(96,"semitones"), tremolo=lfo(1,"normalized"), panbrello=lfo(1,"bipolar");
constexpr NativePatternField arpeggio[]{
  {"offset1","Second pitch","semitones","semitones",-96,96,4},
  {"offset2","Third pitch","semitones","semitones",-96,96,7},
  {"rate","Cycle rate","cycles-per-beat-or-hz","cycles-per-beat-or-hz",0,1000,1},
  {"rateMode","Rate unit","choice","choice",0,1,0,T::Choice,rates,5,false},
  {"phase","Phase","cycles","cycles",0,1,0,T::Number,{},8,false}
};
constexpr NativePatternField tremor[]{
  {"onBeats","On time","beats","beats",minimumBeat,256,.25},
  {"offBeats","Off time","beats","beats",minimumBeat,256,.25},
  {"depth","Depth","normalized","percent",0,1,1},
  {"phase","Phase","cycles","cycles",0,1,0,T::Number,{},8,false}
};
constexpr NativePatternField retrigger[]{
  {"intervalBeats","Interval","beats","beats",minimumBeat,256,.25},
  {"count","Repeats","count","integer",1,65536,1,T::Integer},
  {"volumeFactor","Volume multiplier","linear-gain","percent",0,4,1},
  {"volumeStep","Volume change","normalized","percent",-1,1,0}
};
constexpr NativePatternField sampleOffset[]{
  {"value","Position","sample-position","sample-position",0,4294967295.0,0},
  {"mode","Position unit","choice","choice",0,2,0,T::Choice,offsets,10}
};
constexpr NativePatternField sampleDirection[]{{"direction","Direction","choice","choice",0,1,0,T::Choice,directions,7}};
constexpr NativePatternField envelopePosition[]{
  {"envelope","Envelope","choice","choice",0,3,0,T::Choice,envelopes,7},
  {"position","Position","beats","beats",0,65536,0}
};
constexpr NativePatternField envelopeEnable[]{
  {"envelope","Envelope","choice","choice",0,2,0,T::Choice,singleEnvelopes,7},
  {"enabled","Enabled","boolean","boolean",0,1,1,T::Boolean,{},5}
};
constexpr NativePatternField tempo[]{{"bpm","Tempo","bpm","bpm",32,512,125}};
constexpr NativePatternField rowLength[]{{"beats","Row length","beats","beats",minimumBeat,256,.25}};
constexpr NativePatternField scratch[]{
  {"gesture","Gesture","scratch-gesture","integer",1,255,1,T::Integer,{},4},
  {"beats","Duration","beats","beats",minimumBeat,256,1},
  {"travelMs","Travel","milliseconds","milliseconds",0,10000,200},
  {"repeats","Repeats","count","integer",1,128,1,T::Integer,{},4},
  {"reverse","Reverse motion","boolean","boolean",0,1,0,T::Boolean,{},5,false}
};
constexpr NativePatternCommandInfo commands[]{
  {NativePatternOp::GainSet,"gain-set","GS","Gain set","volume","Set exact sample gain; channel/song scope affects sample voices, not plugin outputs.","sample voice/channel/song","until replaced or scope reset","volume, channel volume, global volume",gain},
  {NativePatternOp::GainSlide,"gain-slide","GL","Gain slide","volume","Slide sample gain over the duration; channel/song scope affects sample voices, not plugin outputs.","sample voice/channel/song","duration then hold","volume slide, fine volume slide, channel/global volume slide",gain,D::Required},
  {NativePatternOp::PanSet,"pan-set","PN","Pan set","panning","Set exact panning from -1 left to +1 right.","sample voice","until next note or replacement","set panning",pan},
  {NativePatternOp::PanSlide,"pan-slide","PA","Pan slide","panning","Slide to exact panning over the specified duration.","sample voice","duration then hold","panning slide",pan,D::Required},
  {NativePatternOp::PitchRelative,"pitch-relative","PR","Relative pitch","pitch","Change pitch by semitones immediately or over a duration.","sample voice","duration then hold","portamento up/down, fine portamento, finetune",relativePitch,D::Optional},
  {NativePatternOp::TonePortamento,"tone-portamento","PT","Tone portamento","pitch","Move the current sample voice to a fractional tracker note (C4 = 49) without retriggering.","sample voice","duration then hold","tone portamento",portamento,D::Required},
  {NativePatternOp::Vibrato,"vibrato","VB","Vibrato","pitch","Continuous pitch modulation with independent depth, rate and shape.","sample voice","until replaced; optional duration","vibrato, fine vibrato, vibrato waveform",vibrato,D::Optional},
  {NativePatternOp::Tremolo,"tremolo","TM","Tremolo","volume","Continuous gain modulation with independent depth, rate and shape.","sample voice","until replaced; optional duration","tremolo, tremolo waveform",tremolo,D::Optional},
  {NativePatternOp::Panbrello,"panbrello","PB","Panbrello","panning","Continuous pan modulation with independent depth, rate and shape.","sample voice","until replaced; optional duration","panbrello, panbrello waveform",panbrello,D::Optional},
  {NativePatternOp::Arpeggio,"arpeggio","AR","Arpeggio","pitch","Cycle the base pitch and two independent fractional semitone offsets.","sample voice","until replaced; optional duration","arpeggio",arpeggio,D::Optional},
  {NativePatternOp::Tremor,"tremor","TR","Tremor","volume","Alternate independently timed on/off phases with continuous depth.","sample voice","until replaced; optional duration","tremor",tremor,D::Optional},
  {NativePatternOp::Retrigger,"retrigger","RT","Retrigger","sound","Repeat the current note after each beat interval; sample gain is precise, plugin repeats use bounded integer MIDI velocity.","sample/plugin note","repeat count","retrigger note",retrigger},
  {NativePatternOp::NoteRelease,"note-release","NO","Note release","sound","Release the current sample or plugin note at the exact command position.","sample/plugin note","instant","key off",{}},
  {NativePatternOp::NoteDelay,"note-delay","ND","Note delay","sound","Delay this row's note to the exact command offset, with one onset.","row note","instant","note delay",{},D::None,true},
  {NativePatternOp::SampleOffset,"sample-offset","SO","Sample offset","sound","Seek a sample voice by exact frame, normalized position or discrete cue.","sample voice","instant","sample offset, high offset, percentage offset, sample cue",sampleOffset},
  {NativePatternOp::SampleDirection,"sample-direction","DR","Sample direction","sound","Choose forward or reverse sample playback at the exact command position.","sample voice","until next note or replacement","sound control forward/reverse",sampleDirection},
  {NativePatternOp::EnvelopePosition,"envelope-position","EP","Envelope position","sound","Move the selected instrument envelope clock to an exact beat position.","sample voice","instant","set envelope position",envelopePosition},
  {NativePatternOp::EnvelopeEnable,"envelope-enable","EN","Envelope enable","sound","Enable or disable an instrument envelope at the exact command position.","sample voice","until next note or replacement","instrument envelope control",envelopeEnable},
  {NativePatternOp::TempoSet,"tempo-set","TS","Tempo set","timing","Set tempo without integer BPM quantization.","song","until replaced","set tempo",tempo},
  {NativePatternOp::TempoSlide,"tempo-slide","TL","Tempo slide","timing","Move tempo to a precise target over musical time.","song","duration then hold","tempo slide",tempo,D::Required},
  {NativePatternOp::RowLength,"row-length","RL","Row length","timing","Set the current row length in beats; only valid at the row boundary.","row","one row","speed, pattern delay, fine pattern delay",rowLength},
  {NativePatternOp::Scratch,"scratch","SK","Scratch gesture","sound","Play a song-local record-motion and fader gesture on the current sample. Duration is total beats; travel follows the sample's captured pitch. Closed gestures return to their cue. The next note cancels it.","sample voice","duration or next note","record scratching",scratch},
  {NativePatternOp::ScratchStop,"scratch-stop","SX","Stop scratch","sound","Release the current scratch gesture at an exact offset. Ordinary sample playback resumes from its current position and the fader opens smoothly.","sample voice","instant","stop record scratching",{},D::None,true}
};
constexpr std::string_view kindNames[]{"parameter-set","parameter-slide","pitch-set","pitch-slide","note-cut","tracker","nudge-forward","nudge-reverse","native"};
constexpr NativePatternField parameterFields[]{
  {"binding","Binding","binding","integer",1,255,1,T::Integer,{},4},
  {"value","Value","normalized","percent",0,1,0}
};
constexpr NativePatternField pitchFields[]{
  {"value","Pitch","semitones","semitones",-96,96,0},
  {"pitchRange","MIDI bend range","semitones","semitones",1,96,2,T::Integer,{},5,false}
};
constexpr NativePatternField nudgeFields[]{
  {"value","Strength","normalized","percent",0,1,.75},
  {"durationBeats","Duration","beats","beats",minimumBeat,65536,1}
};
}
std::span<const NativePatternCommandInfo> nativePatternCommands(){return commands;}
const NativePatternCommandInfo &nativePatternCommand(NativePatternOp op){
  for(const auto &c:commands)if(c.operation==op)return c;
  throw std::invalid_argument("Unknown native pattern operation");
}
const NativePatternCommandInfo &nativePatternCommand(std::string_view id){
  for(const auto &c:commands)if(c.identifier==id)return c;
  throw std::invalid_argument("Unknown native pattern operation");
}
std::array<double,maximumNativePatternParameters> nativePatternDefaults(NativePatternOp op){
  std::array<double,maximumNativePatternParameters> result{};size_t i=0;
  for(const auto &p:nativePatternCommand(op).parameters)result[i++]=p.initial;
  return result;
}
void validateNativePatternCommand(const PatternCommand &c){
  if(isNudge(c.kind)){
    if(c.duration||!std::isfinite(c.durationBeats)||c.durationBeats<minimumBeat||c.durationBeats>65536)
      throw std::invalid_argument("NF/NR require durationBeats from 1/65536 to 65536 and zero row-unit duration");
  }else if(c.durationBeats!=0)throw std::invalid_argument("Only NF/NR use durationBeats");
  if(c.kind!=PatternCommandKind::Native){
    if(c.native!=NativePatternOp::None||std::any_of(c.arguments.begin(),c.arguments.end(),[](double v){return v!=0;}))
      throw std::invalid_argument("Only native operations have named parameters");
    return;
  }
  const auto &info=nativePatternCommand(c.native);
  if(c.binding||c.value||c.effect||c.parameter||c.pitchRange!=2)
    throw std::invalid_argument("Native operations use named parameters, not legacy value or effect bytes");
  if((info.duration==D::None&&c.duration)||(info.duration==D::Required&&!c.duration))
    throw std::invalid_argument("Native operation has an invalid duration");
  for(size_t i=0;i<c.arguments.size();++i){
    const double v=c.arguments[i];
    if(!std::isfinite(v))throw std::invalid_argument("Native parameter must be finite");
    if(i>=info.parameters.size()){if(v!=0)throw std::invalid_argument("Unexpected native parameter payload");continue;}
    const auto &p=info.parameters[i];
    if(v<p.minimum||v>p.maximum||(p.type!=T::Number&&v!=std::floor(v)))throw std::invalid_argument("Native parameter outside its allowed range");
  }
  if(c.native==NativePatternOp::SampleOffset&&((c.arguments[1]==1&&c.arguments[0]>1)||(c.arguments[1]==2&&(c.arguments[0]>9||c.arguments[0]!=std::floor(c.arguments[0])))))
    throw std::invalid_argument("Sample offset must fit its selected unit");
  if(c.native==NativePatternOp::RowLength&&c.position%performanceUnitsPerRow)
    throw std::invalid_argument("Row length commands must start at a row boundary");
}
std::string_view patternCommandKindName(PatternCommandKind kind){
  const auto index=size_t(kind);if(index>=std::size(kindNames))throw std::invalid_argument("Unknown pattern effect");return kindNames[index];
}
PatternCommandKind patternCommandKind(std::string_view name){
  for(size_t i=0;i<std::size(kindNames);++i)if(kindNames[i]==name)return PatternCommandKind(i);
  throw std::invalid_argument("Unknown pattern effect");
}
std::span<const NativePatternField> patternCommandFields(PatternCommandKind kind){
  switch(kind){case PatternCommandKind::ParameterSet:case PatternCommandKind::ParameterSlide:return parameterFields;
  case PatternCommandKind::PitchSet:case PatternCommandKind::PitchSlide:return pitchFields;
  case PatternCommandKind::NudgeForward:case PatternCommandKind::NudgeReverse:return nudgeFields;default:return {};}
}
NativePatternDuration patternCommandDuration(PatternCommandKind kind){
  return kind==PatternCommandKind::ParameterSlide||kind==PatternCommandKind::PitchSlide?D::Required:D::None;
}
}
