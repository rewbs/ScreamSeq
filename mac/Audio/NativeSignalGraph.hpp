#pragma once
#include "AudioUnitHost.hpp"
#include "editor/SignalRuntime.hpp"
#include "editor/NativeSong.hpp"
namespace Tracker {
// Prepared on the control thread; owns all independent subgraph instances.
class NativeSignalGraph {
  struct Instance;
  struct Bus;
  std::vector<std::unique_ptr<Bus>> buses_;
  double rate_;
  MixerGraph routedMixer_;
  uint32_t pattern_ = UINT32_MAX, order_ = UINT32_MAX, row_ = UINT32_MAX;
  double previous_ = -1;
  std::array<std::atomic<uint8_t>,128> controllers_{};
  std::array<uint8_t,128> appliedControllers_{};
public:
  NativeSignalGraph(const NativeSong &,double sampleRate,bool offline);
  ~NativeSignalGraph();
  void compile(MixerGraph &,std::vector<MixerProcessorInfo> &);
  void begin(const OpenMPT::PlayState &,uint32_t frames,uint64_t position,PluginTransport) noexcept;
  void tail() noexcept;
  std::vector<SignalActivity> activity() const;
  std::span<const uint32_t> outputs(size_t index) const noexcept;
  const float *output(size_t index,uint32_t port) const noexcept;
  void controller(uint8_t cc,uint8_t value) noexcept {if(cc<128)controllers_[cc].store(value,std::memory_order_relaxed); }
  bool process(size_t index,float *,uint32_t,uint64_t,std::span<const MixerAudioInput>) noexcept;
};
}
