#pragma once
#include "AudioUnitHost.hpp"
#include "editor/SignalRuntime.hpp"
#include "editor/NativeSong.hpp"
namespace Tracker {
struct SignalSampleSource {uint64_t target=0;const OpenMPT::ModInstrument *instrument=nullptr;uint16_t channel=0,channels=0;uint64_t instrumentID=0;};
// Prepared on the control thread; owns all independent subgraph instances.
class NativeSignalGraph {
  struct Instance;
  struct Bus;
  std::vector<std::shared_ptr<Bus>> buses_; // Initial snapshot owns stopped preparation.
public:
  struct CopySet; // Producer-owned membership/commands/buffers; audio borrows.
private:
  std::shared_ptr<CopySet> initialCopies_,controlCopies_;
  CopySet *audioCopies_=nullptr;
  std::atomic<CopySet *> publishedCopies_{nullptr};
  const std::vector<std::shared_ptr<Bus>> &controlBuses() const noexcept;
  const std::vector<std::shared_ptr<Bus>> &audioBuses() const noexcept;
  const std::vector<std::shared_ptr<Bus>> &publishedBuses() const noexcept;
  double rate_;
  bool offline_=false;
  ParameterActivity *activity_=nullptr;
  SignalObservation *observation_=nullptr;
  size_t storageBytes_=0,processors_=0;
  std::map<uint16_t,uint64_t> patternIDs_;
  MixerGraph routedMixer_;
  uint32_t pattern_ = UINT32_MAX, order_ = UINT32_MAX, row_ = UINT32_MAX;
  double previous_ = -1;
  std::array<std::atomic<uint8_t>,128> controllers_{};
  std::array<uint8_t,128> appliedControllers_{};
public:
  NativeSignalGraph(const NativeSong &,double sampleRate,bool offline,std::span<const SignalSampleSource> sampleSources={},size_t storageLimit=256*1024*1024,size_t processorLimit=256,ParameterActivity *activity=nullptr,SignalObservation *observation=nullptr);
  ~NativeSignalGraph();
  size_t storageBytes() const {return storageBytes_;}
  size_t processors() const {return processors_;}
  void compile(MixerGraph &,std::vector<MixerProcessorInfo> &);
  std::shared_ptr<CopySet> prepareCopies(const NativeSong &,GraphControlPlan &,const GraphControlPlan *,std::span<const SignalSampleSource> = {}) const;
  void compileCopies(const CopySet &,MixerGraph &,std::vector<MixerProcessorInfo> &) const;
  size_t copyIndex(const CopySet &,uint64_t target) const noexcept;
  std::span<const uint32_t> copyOutputs(const CopySet &,size_t index) const noexcept;
  size_t copyStorageBytes(const CopySet &) const noexcept;
  size_t copyProcessors(const CopySet &) const noexcept;
  void acceptCopies(std::shared_ptr<CopySet>) noexcept; // Control owner, after publication.
  void activateCopies(CopySet &) noexcept; // Quiet boundary, before new rendering.
  // Control thread reads only immutable membership, never live note watches.
  bool sameNoteMembership(const NativeSong &) const;
  void prepareParameters(const SignalGraph &,GraphControlPlan &,const GraphControlPlan *previous=nullptr,const CopySet *copies=nullptr) const;
  void begin(const OpenMPT::PlayState &,uint32_t frames,uint64_t position,PluginTransport,uint32_t patternRows=64) noexcept;
  void tail() noexcept;
  uint64_t tailFrames(size_t) const noexcept; // Audio owner.
  double tailGrowth() const noexcept;
  uint64_t tailRevision() const noexcept;
  std::vector<SignalActivity> activity() const;
  std::span<const uint32_t> outputs(size_t index) const noexcept;
  const float *output(size_t index,uint32_t port) const noexcept;
  void controller(uint8_t cc,uint8_t value) noexcept {if(cc<128)controllers_[cc].store(value,std::memory_order_relaxed); }
  bool process(size_t index,float *,uint32_t,uint64_t,std::span<const MixerAudioInput>) noexcept;
  bool latencyChangePending() const noexcept;
  void refreshLatencies(std::vector<MixerProcessorInfo> &); // Audio is stopped.
};
}
