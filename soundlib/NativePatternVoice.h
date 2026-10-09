/* Native ScreamSeq sample controls. No state is shared between sounding voices. */
#pragma once
#include "openmpt/all/BuildSettings.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
OPENMPT_NAMESPACE_BEGIN
template<class Envelope> inline double NativePatternEnvelopeValue(const Envelope &env,double position) noexcept
{
 if(env.empty())return 1;
 const auto right=std::lower_bound(env.begin(),env.end(),position,[](const auto &point,double p){return point.tick<p;});
 double value=right==env.end()?env.back().value:right->value;
 if(right!=env.begin()&&right!=env.end()){const auto &left=*(right-1);if(right->tick>left.tick)value=left.value+(right->value-left.value)*(position-left.tick)/(right->tick-left.tick);}
 return std::clamp(value/64.,0.,1.);
}
struct NativePatternCurve
{
 double from=0,to=0,elapsed=0,duration=0;
 bool active=false;
 double Value(double offset=0) const noexcept { return duration>0 ? from+(to-from)*std::clamp((elapsed+offset)/duration,0.,1.) : to; }
 void Set(double value,double length=0,double initial=0) noexcept { from=active?Value():initial;to=value;elapsed=0;duration=length;active=true; }
 void Advance(double rows) noexcept { elapsed=std::min(duration,elapsed+rows); }
};
struct NativePatternOscillator
{
 double phase=0,rate=0,depth=0,elapsed=0,duration=0;
 uint64_t cycle=0;
 uint8_t shape=0;
 bool hertz=false,active=false;
 static double Random(uint64_t value) noexcept { value+=0x9e3779b97f4a7c15ULL;value=(value^(value>>30))*0xbf58476d1ce4e5b9ULL;value=(value^(value>>27))*0x94d049bb133111ebULL;value^=value>>31;return double(value>>11)*(2./9007199254740992.)-1.; }
 double Wave() const noexcept {
  switch(shape) {case 1:return 1-4*std::abs(phase-.5);case 2:return phase*2-1;case 3:return phase<.5-1e-12?1:-1;case 4:return Random(cycle);default:return std::sin(phase*6.2831853071795864769);}
 }
 double Value() const noexcept {return active&&(!duration||elapsed<duration)?depth*Wave():0;}
 void Advance(double rows,double beats,double seconds) noexcept {if(!active)return;const double next=phase+rate*(hertz?seconds:beats);const auto cycles=uint64_t(std::floor(next+1e-12));phase=std::max(0.,next-cycles);cycle+=cycles;elapsed+=rows;if(duration&&elapsed>=duration)active=false;}
};
struct NativePatternVoice
{
 NativePatternCurve gain,pan,pitch;
 NativePatternOscillator vibrato,tremolo,panbrello,arpeggio,tremor;
 std::array<double,2> arpeggioOffsets{};
 double tremorOn=.5;
 double legacyPanBefore=0,legacyPanEnvelope=0,legacyPanAfter=0;
 double legacyVolumeFactor=1,legacyPitchRatio=1,neutralLeft=0,neutralRight=0;
 int32_t beforeEnvelopeVolume=0,neutralRealVolume=0;
 std::array<double,3> legacyEnvelopePosition{};
 struct Envelope { double position=0,releaseScale=1;bool active=false; };
 std::array<Envelope,3> envelopes{};
 bool Active() const noexcept {return gain.active||pan.active||pitch.active||vibrato.active||tremolo.active||panbrello.active||arpeggio.active||tremor.active||envelopes[0].active||envelopes[1].active||envelopes[2].active;}
 double Pitch() const noexcept {double result=(pitch.active?pitch.Value():0)+vibrato.Value();if(arpeggio.active){const auto step=std::min(2,int((arpeggio.phase+1e-12)*3));if(step)result+=arpeggioOffsets[step-1];}return result;}
 double Gain() const noexcept {double result=gain.active?gain.Value():1;if(tremolo.active)result*=1-tremolo.depth*.5+tremolo.Value()*.5;if(tremor.active&&tremor.phase>=tremorOn-1e-12)result*=1-tremor.depth;return result;}
 void Advance(double rows,double beats,double seconds) noexcept {gain.Advance(rows);pan.Advance(rows);pitch.Advance(rows);for(auto *o:{&vibrato,&tremolo,&panbrello,&arpeggio,&tremor})o->Advance(rows,beats,seconds);}
};
OPENMPT_NAMESPACE_END
