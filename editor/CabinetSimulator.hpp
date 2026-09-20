#pragma once
#include "CabinetVoicing.hpp"
#include "EffectUtilities.hpp"
#include <memory>

namespace Tracker {
class CabinetSimulator {
  struct Path;
  double rate_;
  uint32_t factor_, latency_;
  double cabinetTail_;
  std::array<std::unique_ptr<Path>, 6> paths_;
  // Enabled, mono, dry, wet, output gain, six routing weights.
  std::array<EffectRamp, 11> controls_;
  EffectRamp inputMono_;
  EffectDelay<std::array<double, 11>, 90> controlDelay_;
  EffectDelay<std::array<double, 2>, 90> dryDelay_;
public:
  explicit CabinetSimulator(double rate);
  ~CabinetSimulator();
  void parameter(uint32_t id, float value, bool rendered) noexcept;
  std::array<double, 2> process(std::array<double, 2> input) noexcept;
  uint32_t latencyFrames() const noexcept { return latency_; }
  uint32_t oversamplingFactor() const noexcept { return factor_; }
  double cabinetTail() const noexcept { return cabinetTail_; }
};
}
