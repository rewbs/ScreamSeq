#pragma once
#include "common/BuildSettings.h"
#include "mpt/random/engine_lcg.hpp"
#include <array>
#include <cstdint>

namespace Tracker {
// Deliberate amplitude/time quantization, not a transparent sample-rate
// converter. A shared capture clock preserves stereo timing; consecutive draws
// from a private deterministic engine provide separate L/R noise samples.
class LofiMat {
  struct Ramp {
    double current = 0, target = 0, increment = 0;
    uint32_t remaining = 0;
    void set(double value, uint32_t frames) noexcept;
    double next() noexcept;
  };
  double rate_, phase_ = 0, phaseCorrection_ = 0;
  double cachedBits_ = -1, quantization_ = 1, cachedRate_ = -1, smoothing_ = 1;
  bool first_ = true;
  std::array<Ramp, 8> controls_;
  std::array<double, 2> held_{}, filtered_{};
  mpt::lcg_musl random_{1};
public:
  using Frame = std::array<double, 2>;
  explicit LofiMat(double rate) : rate_(rate) {}
  // Values and parameter IDs are validated by NativeEffect before this call.
  void parameter(uint32_t id, double value, bool rendered) noexcept;
  Frame process(Frame input) noexcept;
};
}
