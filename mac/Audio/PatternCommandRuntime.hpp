#pragma once
#include "AudioUnitHost.hpp"
#include "editor/NativeSong.hpp"
namespace Tracker {
// Prepared on the control thread. Rendering neither queries plugin metadata nor allocates.
class PatternCommandRuntime {
  struct Event {uint32_t position,duration;uint16_t channel;uint8_t column;double value;};
  struct Curve {
    double from=0,to=0,start=0,end=0;
    double at(double position) const noexcept {
      if(position<start)return from;
      if(end<=start||position>=end)return to;
      return from+(to-from)*((position-start)/(end-start));
    }
  };
  struct Target {
    std::shared_ptr<NativePlugin> plugin;
    uint32_t parameter=0;
    double minimum=0,maximum=1,current=0;
    std::map<uint32_t,std::vector<Event>> patterns;
    const std::vector<Event> *events=nullptr;
    size_t next=0;
    Curve curve;
    uint16_t activeChannel=UINT16_MAX;
    bool used=false;
  };
  std::vector<Target> targets_;
  uint32_t pattern_=UINT32_MAX,order_=UINT32_MAX;
  double previousPosition_=0;
public:
  PatternCommandRuntime(const NativeSong &, const std::vector<std::shared_ptr<NativePlugin>> &,
                        const std::vector<std::string> &,const std::vector<bool> &,const std::vector<ParameterChange> &);
  bool render(const OpenMPT::PlayState &,uint32_t frames,uint64_t absoluteFrame) noexcept;
};
}
