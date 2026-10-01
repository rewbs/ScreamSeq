#include "Maximizer.hpp"
#include <algorithm>
#include <cmath>

namespace Tracker {
Maximizer::Maximizer(double rate) : rate_(rate), memoryAttack_(std::exp(-1 / (.05 * rate))), latency_(0) {
  if (!std::isfinite(rate) || rate < 8000 || rate > 384000) throw std::invalid_argument("Unsupported maximizer sample rate");
  latency_ = uint32_t(std::ceil(.005 * rate));
  const auto window = latency_ + 1;
  while (treeBase_ < window) treeBase_ *= 2;
  minimum_.resize(treeBase_ * 2, 1);
  sum_.resize(treeBase_ * 2, 0);
  for (size_t i = 0; i < window; ++i) sum_[treeBase_ + i] = 1;
  for (size_t i = treeBase_ - 1; i > 0; --i) sum_[i] = sum_[2*i] + sum_[2*i+1];
  delay_.resize(latency_);
}
void Maximizer::parameter(uint32_t id, double value, bool rendered) noexcept {
  if (id >= controls_.size()) return;
  if (id == 1 || id == 2 || id == 5) value = std::pow(10., value / 20);
  if (id == 3 || id == 4) value = std::exp(-1000 / (value * rate_));
  controls_[id].set(value, rendered ? uint32_t(std::ceil(.005 * rate_)) : 0);
}
Maximizer::Frame Maximizer::process(Frame input) noexcept {
  std::array<double, 6> p;
  for (size_t i = 0; i < p.size(); ++i) p[i] = controls_[i].next();
  const Frame driven{input[0] * p[1], input[1] * p[1]};
  const double peak = std::max(std::abs(driven[0]), std::abs(driven[1]));
  const double desired = peak > p[2] ? p[2] / peak : 1;
  size_t node = treeBase_ + windowCursor_;
  minimum_[node] = desired;
  for (node /= 2; node; node /= 2) minimum_[node] = std::min(minimum_[2*node], minimum_[2*node+1]);
  const double bound = minimum_[1];
  peakGain_ = std::min(bound, p[3] * peakGain_ + (1 - p[3]));
  // Brief peaks mostly use the fast release. Sustained reduction charges a
  // 50 ms memory which then recovers using the independently set slow release.
  const double wanted = 1 - bound;
  const double pole = wanted > slowReduction_ ? memoryAttack_ : p[4];
  slowReduction_ = wanted + pole * (slowReduction_ - wanted);
  if (slowReduction_ < 1e-15) slowReduction_ = 0;
  node = treeBase_ + windowCursor_;
  sum_[node] = std::min(peakGain_, 1 - slowReduction_);
  for (node /= 2; node; node /= 2) sum_[node] = sum_[2*node] + sum_[2*node+1];
  if (++windowCursor_ == latency_ + 1) windowCursor_ = 0;
  const auto delayed = delay_[delayCursor_];
  delay_[delayCursor_] = {input, driven, p[2], p[5], p[0]};
  if (++delayCursor_ == latency_) delayCursor_ = 0;
  // For input n, each raw gain from n through n+L is <= its desired gain.
  // Their average at n+L is therefore safe for the matching delayed sample,
  // even while boost, threshold and ceiling are automated. Recompute its bound
  // only as a floating-point guard; the tests independently verify the proof.
  const double delayedPeak = std::max(std::abs(delayed.driven[0]), std::abs(delayed.driven[1]));
  const double guard = delayedPeak > delayed.threshold ? delayed.threshold / delayedPeak : 1;
  const double gain = std::clamp(sum_[1] / (latency_ + 1), 0., guard);
  Frame result;
  for (size_t c = 0; c < 2; ++c) {
    const double wet = delayed.driven[c] * gain * (delayed.ceiling / delayed.threshold);
    result[c] = std::lerp(delayed.dry[c], wet, delayed.enabled);
    const double applied = 1 + delayed.enabled * (gain - 1);
    const double reduction = -20 * std::log10(std::max(1e-8, applied));
    const double detector = 20 * std::log10(std::max(1e-8, std::abs(driven[c])));
    meters_.reductionDB[c] = float(std::max(reduction, double(meters_.reductionDB[c]) - 30 / rate_));
    meters_.detectorDB[c] = float(std::max(detector, double(meters_.detectorDB[c]) - 30 / rate_));
  }
  return result;
}
}
