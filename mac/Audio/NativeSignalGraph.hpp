#pragma once
#include "AudioUnitHost.hpp"
#include "editor/SignalRuntime.hpp"
#include "editor/NativeSong.hpp"
namespace Tracker {
struct SignalSampleSource {uint64_t target=0;const OpenMPT::ModInstrument *instrument=nullptr;uint16_t channel=0,channels=0;};
// Prepared on the control thread; owns all independent subgraph instances.
class NativeSignalGraph {
  struct Instance;
  struct Bus;
  std::vector<std::unique_ptr<Bus>> buses_;
  double rate_;
  size_t storageBytes_=0,processors_=0;
  std::map<uint16_t,uint64_t> patternIDs_;
  MixerGraph routedMixer_;
  uint32_t pattern_ = UINT32_MAX, order_ = UINT32_MAX, row_ = UINT32_MAX;
  double previous_ = -1;
  std::array<std::atomic<uint8_t>,128> controllers_{};
  std::array<uint8_t,128> appliedControllers_{};
public:
  NativeSignalGraph(const NativeSong &,double sampleRate,bool offline,std::span<const SignalSampleSource> sampleSources={},size_t storageLimit=256*1024*1024,size_t processorLimit=256);
  ~NativeSignalGraph();
  size_t storageBytes() const {return storageBytes_;}
  size_t processors() const {return processors_;}
  void compile(MixerGraph &,std::vector<MixerProcessorInfo> &);
  void begin(const OpenMPT::PlayState &,uint32_t frames,uint64_t position,PluginTransport,uint32_t patternRows=64) noexcept;
  void tail() noexcept;
  std::vector<SignalActivity> activity() const;
  std::span<const uint32_t> outputs(size_t index) const noexcept;
  const float *output(size_t index,uint32_t port) const noexcept;
  void controller(uint8_t cc,uint8_t value) noexcept {if(cc<128)controllers_[cc].store(value,std::memory_order_relaxed); }
  bool process(size_t index,float *,uint32_t,uint64_t,std::span<const MixerAudioInput>) noexcept;
  bool latencyChangePending() const noexcept;
  void refreshLatencies(std::vector<MixerProcessorInfo> &); // Audio is stopped.
};
}
