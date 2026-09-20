#include "LofiMat.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace Tracker {
void LofiMat::Ramp::set(double value, uint32_t frames) noexcept {
  if (frames && value == target) return;
  target = value; remaining = frames;
  if (!frames) { current = value; increment = 0; }
  else increment = (target - current) / frames;
}
double LofiMat::Ramp::next() noexcept {
  if (remaining) { current += increment; if (!--remaining) current = target; }
  return current;
}
void LofiMat::parameter(uint32_t id, double value, bool rendered) noexcept {
  if (id == 8) {
    // Reset the noise stream only. A seed edit does not reset the capture clock,
    // held audio or filter. Repeated unchanged targets are skipped by the owner.
    random_ = mpt::lcg_musl(uint64_t(value)); return;
  }
  if (id == 2) value = std::min(value, rate_);
  else if (id == 3 || id == 5 || id == 6) value /= 100;
  else if (id == 7) value = std::pow(10., value / 20);
  controls_[id].set(value, rendered ? uint32_t(std::ceil(rate_ * .005)) : 0);
}
LofiMat::Frame LofiMat::process(Frame input) noexcept {
  std::array<double, 8> controls;
  for (size_t i = 0; i < controls.size(); ++i) controls[i] = controls_[i].next();
  if (controls[1] != cachedBits_) { cachedBits_ = controls[1]; quantization_ = std::exp2(cachedBits_ - 1); }
  if (controls[2] != cachedRate_) {
    cachedRate_ = controls[2]; smoothing_ = -std::expm1(-2 * std::numbers::pi * .45 * cachedRate_ / rate_);
  }
  bool capture = first_;
  if (first_) first_ = false;
  else {
    // Accumulate Hz rather than a rounded rate ratio: integer-Hz settings have
    // exact capture times even for periods such as 48000/11025. Compensated
    // addition limits drift while continuous automation uses fractional Hz.
    const double increment = controls[2] - phaseCorrection_, next = phase_ + increment;
    phaseCorrection_ = (next - phase_) - increment; phase_ = next;
    if (phase_ >= rate_) { phase_ -= rate_; capture = true; }
  }
  if (capture) for (size_t c = 0; c < 2; ++c) {
    // Advance even at zero Noise/Enabled so a control gesture cannot change
    // the random sequence's position relative to the capture clock.
    const double noise = (double(random_()) + .5) / 2147483648. - 1;
    const double x = std::clamp(input[c] + controls[3] * noise, -1., 1.);
    held_[c] = std::clamp(std::round(x * quantization_) / quantization_, -1., 1.);
  }
  Frame result;
  for (size_t c = 0; c < 2; ++c) {
    filtered_[c] += smoothing_ * (held_[c] - filtered_[c]);
    if (std::abs(filtered_[c]) < 1e-30) filtered_[c] = 0;
    const double wet = held_[c] + controls[4] * (filtered_[c] - held_[c]);
    const double mixed = controls[7] * (controls[5] * input[c] + controls[6] * wet);
    result[c] = input[c] + controls[0] * (mixed - input[c]);
  }
  return result;
}
}
