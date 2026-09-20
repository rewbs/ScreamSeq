#pragma once
#include "EffectUtilities.hpp"
#include "StateVariableFilter.hpp"
#include <array>
#include <vector>

namespace Tracker {
struct EffectMeters {
  std::array<float, 2> reductionDB{0, 0}, detectorDB{-160, -160};
};
enum class DynamicsKind { Compressor, Gate, BusCompressor };
// Compressors / hysteretic gate. Controls and processing belong to
// one render thread. NativeEffect publishes the meter pairs atomically.
class Dynamics {
  using Frame = std::array<double, 2>;
  struct Envelope {
    double released = 0, smoothed = 0, openness = 0;
    double feedback = 0, feedbackWeight = 0;
    uint64_t belowFrames = 0;
    bool open = false;
  };
  double rate_;
  DynamicsKind kind_;
  double crestPole_;
  std::array<EffectRamp, 20> controls_;
  std::array<StateVariableFilter, 2> filters_;
  Frame power_{};
  Frame heldPeak_{};
  std::array<Envelope, 3> envelopes_;
  std::array<EffectRamp, 3> responses_;
  struct Delayed {
    Frame program{}, key{};
    std::array<double, 4> controls{1,1,0,1}; // Enabled, makeup, listen, wet.
  };
  std::vector<Delayed> delay_;
  size_t delayCursor_ = 0;
  EffectMeters meters_;
public:
  Dynamics(double rate, DynamicsKind kind);
  void parameter(uint32_t id, float value, bool rendered) noexcept;
  Frame process(Frame program, Frame external) noexcept;
  EffectMeters meters() const noexcept { return meters_; }
  uint32_t latencyFrames() const noexcept { return uint32_t(delay_.size()); }
};
}
