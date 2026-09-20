#pragma once
#include "Dynamics.hpp"
#include <vector>

namespace Tracker {
// Stereo-linked sample-peak limiter. A five millisecond lookahead, sliding
// minimum and nonnegative gain average constrain every delayed program sample.
// Storage is fixed before rendering; both tree updates take O(log lookahead).
class Maximizer {
  using Frame = std::array<double, 2>;
  struct Packet {
    Frame dry{}, driven{};
    double threshold = 1, ceiling = 1, enabled = 1;
  };
  double rate_, memoryAttack_, peakGain_ = 1, slowReduction_ = 0;
  uint32_t latency_;
  size_t treeBase_ = 1, windowCursor_ = 0, delayCursor_ = 0;
  std::array<EffectRamp, 6> controls_;
  std::vector<double> minimum_, sum_;
  std::vector<Packet> delay_;
  EffectMeters meters_;
public:
  explicit Maximizer(double rate);
  void parameter(uint32_t id, double value, bool rendered) noexcept;
  Frame process(Frame input) noexcept;
  uint32_t latencyFrames() const noexcept { return latency_; }
  EffectMeters meters() const noexcept { return meters_; }
};
}
