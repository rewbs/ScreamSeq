#pragma once
#include <array>
#include <cstddef>
#include <vector>

namespace Tracker {
// Stereo delay with eight-tap, seventh-order Lagrange interpolation. Construct
// and clear outside rendering. Reads/pushes are allocation-free. A minimum of
// four samples makes every interpolation tap causal; the stored history is never
// resampled or moved when retuning. The explicit delay is a musical effect, not
// hidden processing latency. See Julius O. Smith, Physical Audio Signal Processing:
// https://www.dsprelated.com/freebooks/pasp/Delay_Line_Signal_Interpolation.html
class FractionalDelay {
  std::vector<std::array<double, 2>> samples_;
  size_t write_ = 0;
  double maximum_, cachedDelay_ = -1;
  size_t offset_ = 0;
  std::array<double, 8> weights_{};
public:
  explicit FractionalDelay(double maximumSamples);
  void clear() noexcept;
  std::array<double, 2> read(double samples) noexcept;
  void push(double left, double right) noexcept;
  double maximum() const noexcept { return maximum_; }
};
}
