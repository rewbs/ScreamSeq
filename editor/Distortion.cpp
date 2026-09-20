#include "Distortion.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace Tracker {
void Distortion::Ramp::set(double value, uint32_t frames) noexcept {
  if (frames && value == target) return;
  target = value; remaining = frames;
  if (!frames) { current = value; increment = 0; }
  else increment = (target - current) / frames;
}
double Distortion::Ramp::next() noexcept {
  if (remaining) { current += increment; if (!--remaining) current = target; }
  return current;
}
Distortion::Distortion(double rate) : rate_(rate), dcPole_(std::exp(-2 * std::numbers::pi * 5 / rate)) {
  // Keep the control timing contract explicit if the shared filter is revised.
  if (oversampler_.latencyFrames() != latency) throw std::logic_error("Distortion oversampling latency mismatch");
}
void Distortion::parameter(uint32_t id, double value, bool rendered) noexcept {
  const uint32_t frames = rendered ? uint32_t(std::ceil(rate_ * .005)) : 0;
  auto shape = [&](size_t field, double target) {
    shapeControls_[field].set(target, frames * factor);
    if (!rendered) shapeDelay_.fill(field, target);
  };
  auto output = [&](size_t field, double target) {
    outputControls_[field].set(target, frames);
    if (!rendered) outputDelay_.fill(field, target);
  };
  switch (id) {
    case 0: output(0, value); break;
    case 1: shape(0, std::pow(10., value / 20)); break;
    case 2: for (uint32_t mode = 0; mode < 4; ++mode) shape(1 + mode, mode == uint32_t(value) ? 1 : 0); break;
    case 3: tone_.configure(FilterShape::HighShelf, 2000, .7071067811865476, value * .12, rate_, frames); break;
    case 4: output(1, value / 100); break;
    case 5: output(2, value / 100); break;
    case 6: output(3, std::pow(10., value / 20)); break;
  }
}
Oversampler::Frame Distortion::process(Oversampler::Frame input) noexcept {
  const auto dry = dryDelay_.process(input);
  tone_.process(input[0], input[1]);
  const auto filtered = oversampler_.process(input, [&](auto &frame) noexcept {
    std::array<double, 5> next;
    for (size_t i = 0; i < next.size(); ++i) next[i] = shapeControls_[i].next();
    // Interpolation delays audio by 45 host samples. Delay drive/mode equally
    // before shaping, so compensated automation follows the intended sample.
    const auto controls = shapeDelay_.process(next);
    for (auto &sample : frame) {
      const double x = sample * controls[0]; double y = 0;
      if (controls[1]) y += controls[1] * std::tanh(x);
      if (controls[2]) y += controls[2] * std::clamp(x, -1., 1.);
      if (controls[3]) y += controls[3] * (1 - std::abs(std::remainder(x - 1, 4.)));
      if (controls[4]) y += controls[4] * (x - 2 * std::floor((x + 1) * .5));
      sample = y;
    }
  });
  std::array<double, 4> next;
  for (size_t i = 0; i < next.size(); ++i) next[i] = outputControls_[i].next();
  const auto controls = outputDelay_.process(next);
  Oversampler::Frame result;
  for (size_t c = 0; c < 2; ++c) {
    double high = (filtered[c] - dcInput_[c]) * ((1 + dcPole_) * .5) + dcPole_ * dcOutput_[c];
    if (std::abs(high) < 1e-30) high = 0;
    dcInput_[c] = filtered[c]; dcOutput_[c] = high;
    result[c] = dry[c] + controls[0] * (controls[3] * (controls[1] * dry[c] + controls[2] * high) - dry[c]);
  }
  return result;
}
}
