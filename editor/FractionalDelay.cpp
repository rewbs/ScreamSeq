#include "FractionalDelay.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Tracker {
FractionalDelay::FractionalDelay(double maximumSamples) : maximum_(maximumSamples) {
  if (!std::isfinite(maximumSamples) || maximumSamples < 4 || maximumSamples > 768000)
    throw std::invalid_argument("Fractional delay capacity must be between 4 and 768000 samples");
  samples_.resize(size_t(std::ceil(maximumSamples)) + 5);
}
void FractionalDelay::clear() noexcept { std::fill(samples_.begin(), samples_.end(), std::array<double, 2>{}); write_ = 0; }
std::array<double, 2> FractionalDelay::read(double delay) noexcept {
  if (!std::isfinite(delay)) return {};
  delay = std::clamp(delay, 4., maximum_);
  if (delay != cachedDelay_) {
    cachedDelay_ = delay;
    const auto whole = size_t(delay); const auto fraction = delay - whole;
    offset_ = whole - 3;
    // Interpolate between the two middle samples of the eight-point stencil.
    // Products use only small, bounded factors. Exact integer delays select a
    // single tap; fixed delays reuse their weights for every subsequent sample.
    for (int k = -3; k <= 4; ++k) {
      double weight = 1;
      for (int j = -3; j <= 4; ++j) if (k != j) weight *= (fraction - j) / (k - j);
      weights_[k + 3] = weight;
    }
  }
  auto index = write_ >= offset_ ? write_ - offset_ : write_ + samples_.size() - offset_;
  std::array<double, 2> output{};
  for (size_t i = 0; i < weights_.size(); ++i) {
    for (size_t c = 0; c < 2; ++c) output[c] += samples_[index][c] * weights_[i];
    index = index ? index - 1 : samples_.size() - 1;
  }
  return output;
}
void FractionalDelay::push(double left, double right) noexcept {
  samples_[write_] = {std::abs(left) < 1e-30 ? 0 : left, std::abs(right) < 1e-30 ? 0 : right};
  if (++write_ == samples_.size()) write_ = 0;
}
}
