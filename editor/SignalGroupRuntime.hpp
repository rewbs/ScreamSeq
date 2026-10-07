#pragma once
#include "SignalGraph.hpp"
#include <array>
#include <memory>

namespace Tracker {
// Prepared boundary wrappers. Internal processors stay warm; only connections
// leaving a group are replaced, so internal fan-out remains the wet signal.
class SignalGroupRuntime {
  struct Ring {
    std::vector<float> samples;
    size_t cursor=0;
    float push(float value) noexcept;
  };
  struct Group {
    uint64_t id=0;size_t index=0,depth=0;
    double wet=1,from=1,to=1;uint32_t elapsed=0;
    std::array<float,4096> weights{};
    AudioTrimRuntime trims;
  };
  struct Mapping {
    uint64_t groupID=0;size_t group=0,source=SIZE_MAX,input=SIZE_MAX,output=0;
    SignalGroupDryRoute identity;
    Ring ingress,alignment;
    std::array<float,8192> samples{};
  };
  const SignalDefinition *controls_=nullptr;
  uint32_t fadeFrames_=1;
  std::vector<Group> groups_;
  std::vector<std::unique_ptr<Mapping>> mappings_;
  std::vector<std::vector<std::pair<size_t,size_t>>> audio_;
  std::vector<std::vector<size_t>> modulation_;
  std::vector<std::vector<std::pair<size_t,size_t>>> trimInputs_,trimOutputs_;
  uint64_t position_=0;
public:
  using Source=const float *(*)(void *,size_t,uint32_t) noexcept;
  SignalGroupRuntime(const SignalDefinition &,const SignalPlan &,double sampleRate);
  void trimSourceReader(AudioTrimSourceReader reader,void *context) noexcept {for(auto &g:groups_)g.trims.sourceReader(reader,context);}
  bool trimsValid() const noexcept {for(const auto &g:groups_)if(!g.trims.valid())return false;return true;}
  void begin(const SignalDefinition &,uint32_t frames,uint64_t position=0) noexcept;
  void capture(size_t node,uint32_t frames,void *,Source) noexcept;
  float audio(size_t edge,float value,uint32_t sample,size_t captureGroup=SIZE_MAX) const noexcept;
  double modulation(size_t edge,uint32_t frame) const noexcept;
  bool affectsModulation(size_t edge) const noexcept;
  void inheritState(SignalGroupRuntime &) noexcept;
  size_t storageBytes() const noexcept;
};
}
