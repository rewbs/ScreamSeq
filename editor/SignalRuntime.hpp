#pragma once
#include "SignalGraph.hpp"
#include "MixerRuntime.hpp"
#include <array>
#include <memory>
#include <span>

namespace Tracker {
struct SignalClock { double beat = 0, tempo = 120; bool playing = true; uint64_t pattern = 0; double position = 0, unitsPerFrame = 0, endPosition = 0, rowsPerBeat = 4; };
struct SignalCallbacks {
  void *context = nullptr;
  bool (*process)(void *, uint64_t, float *, uint32_t, uint64_t, std::span<const MixerAudioInput>) noexcept = nullptr;
  const float *(*output)(void *, uint64_t, uint32_t) noexcept = nullptr;
  bool (*parameter)(void *, uint64_t, uint32_t, double, double, uint64_t, uint32_t) noexcept = nullptr;
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
  };
  struct Edge {
    const SignalAudioEdge *spec = nullptr;
    Port *source = nullptr, *target = nullptr;
    std::vector<float> delay;
    size_t cursor = 0;
    void add(uint32_t) noexcept;
  };
  SignalDefinition definition_;
  SignalPlan plan_;
  double sampleRate_;
  std::vector<Node> nodes_;
  std::vector<Edge> edges_;
  struct ModulationTarget { size_t node = 0; uint32_t parameter = 0; double base = 0; std::vector<std::pair<size_t,size_t>> sources; };
  std::vector<ModulationTarget> targets_;
  std::array<double,128> midi_{};
  double amount_ = 1;
  bool gate_ = false;
  std::vector<std::unique_ptr<Port>> result_;
  static Port *port(std::vector<std::unique_ptr<Port>> &,uint32_t);
  static Port *lookup(const std::vector<std::unique_ptr<Port>> &,uint32_t) noexcept;
  double source(size_t,double,double,const SignalClock &) const noexcept;
  const SignalPatternEnvelope *envelope(size_t,uint64_t) const noexcept;
public:
  SignalRuntime(SignalDefinition, SignalPlan, double sampleRate);
  bool render(float *main, uint32_t frames, uint64_t position, SignalClock,
              const SignalCallbacks &, std::span<const MixerAudioInput> inputs = {}) noexcept;
  const float *output(uint32_t bus) const noexcept;
  void amount(double value) noexcept { amount_ = value; }
  void note(bool gate, bool retrigger = false) noexcept;
  void controller(uint32_t cc,double value) noexcept { if(cc<128)midi_[cc]=value; }
  size_t storageBytes() const noexcept;
  uint32_t latency() const {return plan_.totalLatency;}
  const SignalDefinition &definition() const { return definition_; }
  void updateLatencyPlan(SignalPlan); // Control thread; retains modulation/gate state.
};
} // namespace Tracker
