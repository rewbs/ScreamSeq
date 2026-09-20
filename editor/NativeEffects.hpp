#pragma once
#include "StateVariableFilter.hpp"
#include "FractionalDelay.hpp"
#include "Distortion.hpp"
#include "LofiMat.hpp"
#include "CabinetSimulator.hpp"
#include "Dynamics.hpp"
#include "Maximizer.hpp"
#include <optional>
#include <memory>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace Tracker {
enum class EffectUnit { Generic, Decibels, Percent, Boolean, Choice, Hertz, Q, MidiNote, Semitones, Milliseconds, Bits };
struct EffectParameter {
  uint32_t id;
  std::string_view name;
  float minimum, maximum, initial;
  EffectUnit unit = EffectUnit::Generic;
  std::span<const std::string_view> choices{};
  float step = 0;
};
enum class EffectKind { Gainer, DCOffset, StereoExpander, DigitalFilter, Equalizer, Comb, Distortion, LofiMat, Cabinet, Compressor, Gate, Maximizer, BusCompressor };
struct EffectDefinition {
  std::string_view identifier, name, description;
  std::span<const EffectParameter> parameters;
  double tail;
  EffectKind kind;
  uint32_t bands = 0;
  bool sidechain = false;
};
std::span<const EffectDefinition> nativeEffects();
const EffectDefinition &nativeEffect(std::string_view identifier);

// Configuration/state allocation belongs to the document thread. Parameter
// changes and DSP belong to the single render thread; readers see atomic targets.
class NativeEffect {
  struct Ramp {
    double current = 0, target = 0, increment = 0;
    uint32_t remaining = 0;
    void set(double value, uint32_t frames, bool restart = false) noexcept;
    double next() noexcept;
  };
  const EffectDefinition &definition_;
  EffectKind kind_;
  double rate_, dcPole_, phaseCoefficient_;
  uint32_t smoothingFrames_;
  bool rendered_ = false;
  std::array<std::atomic<float>, 64> values_{};
  std::array<Ramp, 12> ramps_{};
  double dcInput_[2]{}, dcOutput_[2]{}, phaseMemory_ = 0;
  std::array<StateVariableFilter, 10> filters_;
  std::unique_ptr<FractionalDelay> comb_;
  std::unique_ptr<Distortion> distortion_;
  std::unique_ptr<LofiMat> lofi_;
  std::unique_ptr<CabinetSimulator> cabinet_;
  std::unique_ptr<Dynamics> dynamics_;
  std::unique_ptr<Maximizer> maximizer_;
  std::atomic<uint64_t> meterReduction_{0}, meterDetector_{0xc3200000c3200000ULL};
  std::array<float, 64> minima_{}, maxima_{};
  std::array<double, 10> bandTails_{};
  std::atomic<double> tail_{0};
  std::atomic<uint64_t> tailRevision_{0};
  bool rangesReady_ = false;
  void update(uint32_t id = UINT32_MAX) noexcept;
  void updateTail(uint32_t id = UINT32_MAX) noexcept;

public:
  NativeEffect(std::string_view identifier, double rate, std::span<const std::byte> state = {});
  const EffectDefinition &definition() const noexcept { return definition_; }
  bool parameter(uint32_t id, float value) noexcept;
  float value(uint32_t id) const noexcept;
  // Prepare future automation without changing the current target or saved state.
  bool includeParameterRange(uint32_t id, float minimum, float maximum) noexcept;
  double tail() const noexcept { return tail_.load(std::memory_order_relaxed); }
  double latency() const noexcept { return dynamics_ ? double(dynamics_->latencyFrames()) / rate_ : maximizer_ ? double(maximizer_->latencyFrames()) / rate_ : cabinet_ ? double(cabinet_->latencyFrames()) / rate_ : distortion_ ? double(Distortion::latencyFrames()) / rate_ : 0; }
  uint64_t tailRevision() const noexcept { return tailRevision_.load(std::memory_order_relaxed); }
  std::optional<EffectMeters> meters() const noexcept;
  bool process(float *interleaved, uint32_t frames, const float *sidechain = nullptr) noexcept;
  std::vector<std::byte> state() const;
};
}
