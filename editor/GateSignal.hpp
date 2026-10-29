#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Tracker {
enum class GateTarget : uint8_t { Volume, Pitch, Pan, Parameter, All };
enum class GateShape : uint8_t { Direct, Linear, EaseIn, EaseOut, EaseInOut };
namespace GateArgument {
inline constexpr size_t target=0,floor=1,ceiling=2,period=3,phase=4,width=5,
  rise=6,fall=7,riseShape=8,fallShape=9,linked=10,binding=11,pitchRange=12;
}
// Musical units throughout. Transitions start at the rhythmic edges and never
// move the next edge. This evaluator is also the inspector's preview contract.
struct GateSignal {
  double floor=0,ceiling=1,period=.5,phase=0,width=.5,rise=1./32,fall=1./32;
  GateShape riseShape=GateShape::Linear,fallShape=GateShape::Linear;
  static double ease(double t,GateShape shape) noexcept {
    switch(shape){case GateShape::EaseIn:return t*t;case GateShape::EaseOut:return t*(2-t);
    case GateShape::EaseInOut:return t*t*(3-2*t);default:return t;}
  }
  double value(double beats) const noexcept {
    if(width<=0)return floor;if(width>=1)return ceiling;
    double p=std::fmod(beats+phase,period);if(p<0)p+=period;
    // Treat roundoff at an exact boundary consistently across callback sizes.
    const double edge=period*width,epsilon=period*1e-10;
    if(p>=period-epsilon)p=0;else if(std::abs(p-edge)<epsilon)p=edge;
    const double attack=riseShape==GateShape::Direct?0:std::min(rise,edge);
    const double release=fallShape==GateShape::Direct?0:std::min(fall,period-edge);
    if(attack>0&&p<attack)return floor+(ceiling-floor)*ease(p/attack,riseShape);
    if(p<edge)return ceiling;
    if(release>0&&p<edge+release)return ceiling+(floor-ceiling)*ease((p-edge)/release,fallShape);
    return floor;
  }
};
struct GateSnapshot {
  GateSignal signal;
  double elapsed=0,beatsPerFrame=0;
  uint64_t serial=0;
  uint8_t pitchRange=2;
  bool active=false;
  double value(uint64_t frame=0) const noexcept {return signal.value(elapsed+frame*beatsPerFrame);}
};
}
