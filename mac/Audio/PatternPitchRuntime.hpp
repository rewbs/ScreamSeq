#pragma once
#include "AudioUnitHost.hpp"
#include "editor/NativeSong.hpp"
#include "soundlib/Sndfile.h"
namespace Tracker {
// Musical pitch is independent of tracker effect memory. Samples use a double
// precision semitone curve; native instruments receive MIDI pitch-wheel values.
class PatternPitchRuntime {
  struct Event {uint32_t position,duration;uint8_t column;double value;uint8_t pitchRange;};
  struct Curve {
    double from=0,to=0,start=0,end=0;
    double at(double p) const noexcept {
      if(p<start)return from;
      if(end<=start||p>=end)return to;
      return from+(to-from)*((p-start)/(end-start));
    }
  };
  struct Target {
    uint16_t channel=0;
    std::map<uint32_t,std::vector<Event>> patterns;
    const std::vector<Event> *events=nullptr;
    size_t next=0;
    Curve curve;
    double current=0;
    bool used=false;
    uint8_t pitchRange=2;
    std::array<double,4096> ratios{};
  };
  struct Instrument {
    const OpenMPT::ModInstrument *instrument=nullptr;
    std::shared_ptr<NativePlugin> plugin;
    uint8_t midiChannel=0;
  };
  std::vector<Target> targets_;
  std::vector<Instrument> instruments_;
  uint32_t pattern_=UINT32_MAX,order_=UINT32_MAX;
  double previousPosition_=0;
public:
  PatternPitchRuntime(const NativeSong &,OpenMPT::CSoundFile &,const std::vector<std::shared_ptr<NativePlugin>> &,const std::vector<bool> &);
  bool render(OpenMPT::CSoundFile &,uint32_t count,uint64_t absoluteFrame) noexcept;
};
}
