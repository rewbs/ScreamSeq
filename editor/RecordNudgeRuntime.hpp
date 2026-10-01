#pragma once
#include "NativeSong.hpp"
#include <array>
namespace Tracker {
// A bounded, sample-clocked record push. Velocity returns to the underlying
// tracker speed; repeated pushes start at the current velocity without a step.
struct RecordNudgeCurve {
  double start=0, duration=1, from=0, peak=0, attack=.24;
  static double smooth(double x) noexcept { return x*x*x*(10+x*(-15+6*x)); }
  double at(double position) const noexcept;
  void trigger(double position, double length, double strength, bool reverse) noexcept;
};
class RecordNudgeRuntime {
  struct Event {uint32_t position,duration;uint8_t column;double strength;bool reverse;};
  struct Target {
    uint16_t channel;
    std::map<uint16_t,std::vector<Event>> patterns;
    const std::vector<Event> *events=nullptr;
    size_t next=0;
    RecordNudgeCurve curve;
    std::array<double,4096> forces{};
  };
  std::vector<Target> targets_;
  uint32_t pattern_=UINT32_MAX,order_=UINT32_MAX;
  double previous_=-1;
public:
  explicit RecordNudgeRuntime(const NativeSong &);
  void prepare(OpenMPT::CSoundFile &,uint32_t count) noexcept;
};
}
