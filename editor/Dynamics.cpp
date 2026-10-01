#include "Dynamics.hpp"
#include "FeedbackEnvelope.hpp"
#include <algorithm>
#include <cmath>

namespace Tracker {
namespace {
double decibels(double amplitude) noexcept { return 20 * std::log10(std::max(1e-8, amplitude)); }
double compression(double level, double threshold, double ratio, double knee) noexcept {
  const double above = level - threshold, slope = 1 - 1 / ratio;
  if (above <= -knee * .5) return 0;
  if (above >= knee * .5) return slope * above;
  return slope * (above + knee * .5) * (above + knee * .5) / (2 * knee);
}
}
Dynamics::Dynamics(double rate, DynamicsKind kind) : rate_(rate), kind_(kind), crestPole_(std::exp(-1 / (.005 * rate))) {
  if (!std::isfinite(rate) || rate < 8000 || rate > 384000) throw std::invalid_argument("Unsupported dynamics sample rate");
  filters_[0].configure(FilterShape::HighPass, 20, std::sqrt(.5), 0, rate);
  filters_[1].configure(FilterShape::LowPass, 20000, std::sqrt(.5), 0, rate);
  if (kind == DynamicsKind::BusCompressor) delay_.resize(size_t(std::ceil(.005 * rate)));
}
void Dynamics::parameter(uint32_t id, float value, bool rendered) noexcept {
  if (id >= controls_.size()) return;
  const auto frames = rendered ? uint32_t(std::ceil(rate_ * .005)) : 0;
  if (kind_ == DynamicsKind::BusCompressor && id == 7) {
    for (size_t i = 0; i < responses_.size(); ++i) responses_[i].set(i == uint32_t(value) ? 1 : 0, frames);
    return;
  }
  if (id == 10 || id == 11) {
    filters_[id - 10].configure(id == 10 ? FilterShape::HighPass : FilterShape::LowPass,
      value, std::sqrt(.5), 0, rate_, frames);
    return;
  }
  double target = value;
  if (id == 3 || id == 4 || id == 15) target = std::exp(-1000 / (value * rate_));
  else if (id == 5) target = std::pow(10., value / 20.);
  else if (id == 8 || id == 14) target = value / 100.;
  else if (id == 17) target = value <= -96 ? 0 : std::pow(10., value / 20.);
  controls_[id].set(target, frames);
  if (kind_ == DynamicsKind::BusCompressor && !rendered) {
    const int field = id == 0 ? 0 : id == 5 ? 1 : id == 13 ? 2 : id == 14 ? 3 : -1;
    if (field >= 0) for (auto &frame : delay_) frame.controls[size_t(field)] = target;
  }
}
Dynamics::Frame Dynamics::process(Frame program, Frame external) noexcept {
  const bool bus = kind_ == DynamicsKind::BusCompressor;
  std::array<double, 20> p;
  for (size_t i = 0; i < p.size(); ++i) p[i] = controls_[i].next();
  Frame key;
  for (size_t c = 0; c < 2; ++c) key[c] = program[c] + p[9] * (external[c] - program[c]);
  auto filtered = key;
  for (auto &filter : filters_) filter.process(filtered[0], filtered[1]);
  Frame level, rmsLevel{};
  for (size_t c = 0; c < 2; ++c) {
    key[c] += p[12] * (filtered[c] - key[c]);
    power_[c] = p[15] * power_[c] + (1 - p[15]) * key[c] * key[c];
    if (power_[c] < 1e-30) power_[c] = 0;
    const double peak = std::abs(key[c]);
    level[c] = decibels(bus ? peak : peak + p[7] * (std::sqrt(power_[c]) - peak));
    if (bus) {
      rmsLevel[c] = decibels(std::sqrt(power_[c]));
      heldPeak_[c] = std::max(peak, crestPole_ * heldPeak_[c]);
      if (heldPeak_[c] < 1e-30) heldPeak_[c] = 0;
    }
  }
  const std::array<double, 3> levels{level[0], level[1], std::max(level[0], level[1])};
  const std::array<double, 3> rmsLevels{rmsLevel[0], rmsLevel[1], std::max(rmsLevel[0], rmsLevel[1])};
  const std::array<double, 3> peaks{heldPeak_[0], heldPeak_[1], std::max(heldPeak_[0], heldPeak_[1])};
  std::array<double, 3> response{};
  if (bus) for (size_t i = 0; i < response.size(); ++i) response[i] = responses_[i].next();
  std::array<double, 3> gain;
  for (size_t c = 0; c < 3; ++c) {
    auto &e = envelopes_[c];
    if (kind_ != DynamicsKind::Gate) {
      const double desired = compression(bus ? rmsLevels[c] : levels[c], p[1], p[2], p[6]);
      // Smooth decoupled detector in the gain-reduction (dB) domain.
      // Giannoulis/Massberg/Reiss JAES 2012, equations 4, 17 and 23.
      e.released = std::max(desired, p[4] * e.released + (1 - p[4]) * desired);
      e.smoothed = p[3] * e.smoothed + (1 - p[3]) * e.released;
      if (e.smoothed < 1e-12) e.smoothed = 0;
      if (e.released < 1e-12) e.released = 0;
      gain[c] = std::pow(10., -e.smoothed / 20);
      if (bus) {
        e.feedback = feedbackReduction(e.feedback, levels[c], p[1], p[2], p[6], p[3], p[4]);
        if (e.feedback < 1e-12) e.feedback = 0;
        const double crest = decibels(peaks[c]) - rmsLevels[c];
        const double target = std::clamp((crest - 6) / 12, 0., 1.);
        e.feedbackWeight = target + crestPole_ * (e.feedbackWeight - target);
        const double mix = response[0] * e.feedbackWeight + response[1];
        gain[c] = std::lerp(gain[c], std::pow(10., -e.feedback / 20), mix);
      }
    } else {
      if (levels[c] >= p[1]) { e.open = true; e.belowFrames = 0; }
      else if (e.open) {
        if (levels[c] >= p[1] - p[18]) e.belowFrames = 0;
        else if (e.belowFrames < uint64_t(std::ceil(p[16] * rate_ / 1000))) ++e.belowFrames;
        else e.open = false;
      }
      const double target = e.open ? 1 : 0;
      const double pole = target > e.openness ? p[3] : p[4];
      e.openness = target + pole * (e.openness - target);
      if (e.openness < 1e-12) e.openness = 0;
      if (1 - e.openness < 1e-12) e.openness = 1;
      const double opened = e.openness + p[19] * (1 - 2 * e.openness);
      gain[c] = p[17] + (1 - p[17]) * opened;
    }
  }
  if (bus) {
    const auto delayed = delay_[delayCursor_];
    delay_[delayCursor_] = {program, key, {p[0],p[5],p[13],p[14]}};
    if (++delayCursor_ == delay_.size()) delayCursor_ = 0;
    program = delayed.program; key = delayed.key;
    p[0] = delayed.controls[0]; p[5] = delayed.controls[1];
    p[13] = delayed.controls[2]; p[14] = delayed.controls[3];
  }
  Frame result;
  for (size_t c = 0; c < 2; ++c) {
    // Link gain, keeping independent and linked detectors continuously warm.
    const double g = gain[c] + p[8] * (gain[2] - gain[c]);
    const double effected = program[c] * g * p[5];
    const double wet = bus ? std::lerp(effected, key[c], p[13]) : effected + p[13] * (key[c] - effected);
    const double mixed = bus ? std::lerp(program[c], wet, p[14]) : program[c] + p[14] * (wet - program[c]);
    result[c] = bus ? std::lerp(program[c], mixed, p[0]) : program[c] + p[0] * (mixed - program[c]);
    // Instant attack and a 30 dB/s fall make short transients visible without
    // changing the audio envelope. Reports applied attenuation before makeup.
    const double applied = 1 + p[0] * p[14] * (1 - p[13]) * (g - 1);
    meters_.reductionDB[c] = float(std::max(-decibels(applied), double(meters_.reductionDB[c]) - 30 / rate_));
    meters_.detectorDB[c] = float(std::max(level[c], double(meters_.detectorDB[c]) - 30 / rate_));
  }
  return result;
}
}
