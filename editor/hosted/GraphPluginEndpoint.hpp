#pragma once
#include "HostedAudio.hpp"
namespace Tracker {
double graphModulationStep(const PluginParameter &,bool quantized);
// One immutable, prepared vendor incarnation. Only applied baselines and DSP
// are audio-owned; catalogue defaults and the opaque recipe stay immutable.
struct GraphPluginState {
  std::shared_ptr<NativePlugin> plugin;
  GraphPluginRecipe recipe;
  std::shared_ptr<PluginParameterQueue> scheduling; // Control owner; assigned only after successful publication.
  std::vector<uint32_t> initialModulated;
  std::vector<PluginParameter> parameters;
  std::vector<double> initialBaselines;
  std::unique_ptr<double[]> baselines;
  uint64_t inputs=1,outputs=1;
  uint32_t latency=0;
  double preparedTail=0;
  size_t storageBytes() const noexcept;
  GraphPluginState(const SignalDefinition &,const SignalNode &,double,bool);
};
// Stable logical target shared by every control snapshot. Audio uses raw
// pointers only; the initial state and producer-owned snapshots own all vendors.
class GraphPluginEndpoint {
  std::shared_ptr<GraphPluginState> initial_;
  GraphPluginState *current_=nullptr,*previous_=nullptr;
  std::atomic<GraphPluginState *> published_{nullptr};
  std::atomic<bool> settled_{true};
  uint32_t fadeFrames_=1,fade_=0;
  std::array<float,8192> oldAudio_{};
  std::array<std::unique_ptr<std::array<float,8192>>,64> outputs_;
  bool blended_=false;
  uint64_t observedTailRevision_=0;
  std::atomic<uint64_t> tailEpoch_{0};
  void observeTail() noexcept;
  std::array<PluginParameterSamples,64> sampled_{};
  size_t sampledCount_=0;
  ParameterActivity *activity_=nullptr;
  uint32_t activityToken_=0;
public:
  GraphPluginEndpoint(std::shared_ptr<GraphPluginState>,double);
  const auto &initial() const noexcept {return initial_;}
  GraphPluginState &current() const noexcept {return *current_;} // Audio owner, or stopped.
  bool ready(const GraphPluginState *expected) const noexcept;
  double tail() const noexcept {return published_.load(std::memory_order_acquire)->plugin->tail();}
  double tailGrowth() const noexcept;
  uint64_t tailRevision() const noexcept {return tailEpoch_.load(std::memory_order_relaxed);}
  bool latencyChangePending() const noexcept {return published_.load(std::memory_order_acquire)->plugin->latencyChangePending();}
  void observe(ParameterActivity *,uint32_t) noexcept;
  void observePrepared(GraphPluginState &) const noexcept; // Only an unpublished candidate.
  void adopt(GraphPluginState &) noexcept;
  void bypass(bool) noexcept;
  void settleStopped() noexcept {previous_=nullptr;blended_=false;settled_.store(true,std::memory_order_release);}
  bool process(float *,uint32_t,uint64_t,std::span<const MixerAudioInput>) noexcept;
  const float *output(uint32_t) const noexcept;
  void transport(PluginTransport,bool) noexcept;
  bool ramp(uint32_t,double,double,uint64_t,uint32_t,ParameterSource) noexcept;
  bool parameter(uint32_t,double,uint64_t) noexcept;
  void contribution(uint32_t,uint64_t,double,uint64_t) noexcept;
  bool parameterSamples(uint32_t,double,double,std::span<const double>,uint64_t) noexcept;
  size_t storageBytes() const noexcept;
};
}
