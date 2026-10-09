#pragma once
#include "SignalGraph.hpp"
#include "MixerRuntime.hpp"
#include "SignalRuntimeObserver.hpp"
#include "SignalGroupRuntime.hpp"
#include <array>
#include <memory>
#include <span>
#include <tuple>

namespace Tracker {
struct SignalControls {
  SignalDefinition definition;
  std::vector<std::array<double,2>> envelopeCoefficients;
  SignalControls(SignalDefinition,double sampleRate);
};
struct SignalClock { double beat = 0, tempo = 120; bool playing = true; uint64_t pattern = 0; double position = 0, unitsPerFrame = 0, endPosition = 0, rowsPerBeat = 4; };
struct SignalParameterInfo {uint64_t node=0;uint32_t parameter=0;double normalizedStep=0;};
struct SignalCallbacks {
  void *context = nullptr;
  bool (*process)(void *, uint64_t, float *, uint32_t, uint64_t, std::span<const MixerAudioInput>) noexcept = nullptr;
  const float *(*output)(void *, uint64_t, uint32_t) noexcept = nullptr;
  bool (*parameter)(void *, uint64_t, uint32_t, double, double, uint64_t, uint32_t) noexcept = nullptr;
  void (*contribution)(void *,uint64_t,uint32_t,uint64_t,double,uint64_t) noexcept = nullptr;
  bool (*parameterSamples)(void *,uint64_t,uint32_t,std::span<const double>,uint64_t) noexcept = nullptr;
};
// One prepared instance. All vectors/delays/ports are allocated in the constructor;
// render, note, controller and amount are audio-thread-only, allocation-free calls.
class SignalRuntime {
  static constexpr size_t maximumFrames = 4096, quantum = 32;
  struct Port { uint32_t index = 0; std::array<float, maximumFrames*2> samples{}; };
  struct Node {
    std::vector<std::unique_ptr<Port>> inputs, outputs;
    std::vector<MixerAudioInput> auxiliary;
    double envelope = 0, first = 0, last = 0, attackCoefficient = 0, releaseCoefficient = 0;
    std::array<double,quantum> sampled{};
    SignalNoteGate noteGate;
    AudioTrimRuntime trims;
    bool pendingNoteEvent=false;
  };
  struct Edge {
    const SignalAudioEdge *spec = nullptr;
    Port *source = nullptr, *target = nullptr;
    std::vector<float> delay;
    size_t cursor = 0;
    void add(uint32_t,float *,const SignalGroupRuntime *,size_t) noexcept;
  };
  SignalDefinition definition_;
  const SignalDefinition *controls_ = &definition_; // Plan lifetime is owned by the control publisher.
  SignalPlan plan_;
  double sampleRate_;
  std::vector<Node> nodes_;
  std::vector<Edge> edges_;
  std::vector<std::pair<uint64_t,size_t>> nodeIndex_;
  using EdgeIdentity=std::tuple<uint64_t,uint32_t,uint64_t,uint32_t>;
  std::vector<std::pair<EdgeIdentity,size_t>> edgeIndex_;
  size_t preparedBytes_=0;
  std::shared_ptr<SignalRuntimeObserver> observer_;
  std::unique_ptr<std::array<float,maximumFrames*2>> observationScratch_;
  std::unique_ptr<SignalGroupRuntime> groups_;
  uint64_t trimPosition_=0;uint32_t trimFrames_=0;
  bool hasTrimModulation_=false;
  static bool readTrimSource(void *,uint64_t,uint64_t,double &,bool &) noexcept;
  size_t measureStorage() const noexcept;
  struct ModulationTarget { size_t node = 0; uint32_t parameter = 0; double base = 0; std::vector<std::pair<size_t,size_t>> sources; double step=0; std::array<double,quantum> sampled{}; };
  std::vector<ModulationTarget> targets_;
  std::array<double,128> midi_{};
  double amount_ = 1;
  bool gate_ = false;
  bool watchesNotes_ = false;
  std::vector<std::unique_ptr<Port>> result_;
  static Port *port(std::vector<std::unique_ptr<Port>> &,uint32_t);
  static Port *lookup(const std::vector<std::unique_ptr<Port>> &,uint32_t) noexcept;
  double source(size_t,double,double,const SignalClock &) const noexcept;
  double sampledSource(size_t,uint64_t,double,double,double,const SignalClock &) const noexcept;
  const SignalPatternEnvelope *envelope(size_t,uint64_t) const noexcept;
public:
  SignalRuntime(SignalDefinition, SignalPlan, double sampleRate,std::span<const SignalParameterInfo> parameters={});
  SignalRuntime(const SignalRuntime &)=delete;
  SignalRuntime &operator=(const SignalRuntime &)=delete;
  SignalRuntime(SignalRuntime &&)=delete;
  SignalRuntime &operator=(SignalRuntime &&)=delete;
  bool render(float *main, uint32_t frames, uint64_t position, SignalClock,
              const SignalCallbacks &, std::span<const MixerAudioInput> inputs = {}) noexcept;
  const float *output(uint32_t bus) const noexcept;
  // Only an unpublished runtime may be configured. Ownership is retired with
  // its prepared runtime, never released by the audio callback.
  void observer(std::shared_ptr<SignalRuntimeObserver> value) {observer_=std::move(value);if(observer_&&!observationScratch_)observationScratch_=std::make_unique<std::array<float,maximumFrames*2>>();preparedBytes_=measureStorage();}
  const SignalPlan &plan() const noexcept {return plan_;}
  void amount(double value) noexcept { amount_ = value; }
  void note(bool gate, bool retrigger = false) noexcept;
  void parameterBase(uint64_t node,uint32_t parameter,double value) noexcept;
  void controls(const SignalControls &) noexcept; // Same nodes/edges/ports, no allocation.
  bool watchesNotes() const noexcept { return watchesNotes_; }
  bool sameLayout(const SignalDefinition &) const noexcept;
  bool compatibleHistory(const SignalRuntime &) const noexcept;
  // Adopt on the audio owner. Scratch buffers are new; common delay rings and
  // stable source histories transfer without allocation, copying or disposal.
  void inheritState(SignalRuntime &) noexcept;
  void controller(uint32_t cc,double value) noexcept { if(cc<128)midi_[cc]=value; }
  size_t storageBytes() const noexcept {return preparedBytes_;}
  uint32_t latency() const {return plan_.totalLatency;}
  const SignalDefinition &definition() const { return definition_; }
  void updateLatencyPlan(SignalPlan); // Control thread; retains modulation/gate state.
};
} // namespace Tracker
