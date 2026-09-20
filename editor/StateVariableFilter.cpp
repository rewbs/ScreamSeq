#include "StateVariableFilter.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace Tracker {
bool StateVariableFilter::configure(FilterShape shape, double frequency, double q, double gainDB,
                                     double rate, uint32_t frames) noexcept {
  if (!std::isfinite(frequency) || frequency < 10 || frequency > 40000 ||
      !std::isfinite(q) || q < .1 || q > 20 || !std::isfinite(gainDB) || std::abs(gainDB) > 24 ||
      !std::isfinite(rate) || rate < 8000 || rate > 384000) return false;
  Coefficients next;
  next.conductance = std::tan(std::numbers::pi * std::min(frequency, rate * .45) / rate);
  next.damping = 1 / q;
  const double amplitude = std::pow(10., gainDB / 40);
  switch (shape) {
    case FilterShape::LowPass: next.direct = 0; next.low = 1; break;
    case FilterShape::HighPass: next.band = -next.damping; next.low = -1; break;
    case FilterShape::BandPass: next.direct = 0; next.band = next.damping; break;
    case FilterShape::Notch: next.band = -next.damping; break;
    case FilterShape::AllPass: next.band = -2 * next.damping; break;
    case FilterShape::Bell:
      next.damping /= amplitude; next.band = next.damping * (amplitude * amplitude - 1); break;
    case FilterShape::LowShelf:
      next.conductance /= std::sqrt(amplitude); next.band = next.damping * (amplitude - 1); next.low = amplitude * amplitude - 1; break;
    case FilterShape::HighShelf:
      next.conductance *= std::sqrt(amplitude); next.direct = amplitude * amplitude;
      next.band = next.damping * (1 - amplitude) * amplitude; next.low = 1 - amplitude * amplitude; break;
    default: return false;
  }
  // Repeated automation values must not restart an in-progress transition.
  if (frames && next.conductance == target_.conductance && next.damping == target_.damping &&
      next.direct == target_.direct && next.band == target_.band && next.low == target_.low) return true;
  target_ = next; remaining_ = frames;
  if (!frames) { current_ = next; increment_ = {}; }
  else {
    increment_.conductance = (next.conductance - current_.conductance) / frames;
    increment_.damping = (next.damping - current_.damping) / frames;
    increment_.direct = (next.direct - current_.direct) / frames;
    increment_.band = (next.band - current_.band) / frames;
    increment_.low = (next.low - current_.low) / frames;
  }
  return true;
}
void StateVariableFilter::process(double &left, double &right) noexcept {
  if (remaining_) {
    current_.conductance += increment_.conductance; current_.damping += increment_.damping;
    current_.direct += increment_.direct; current_.band += increment_.band; current_.low += increment_.low;
    if (!--remaining_) current_ = target_;
  }
  const double g = current_.conductance, k = current_.damping;
  const double a = 1 / (1 + g * (g + k)), b = g * a, c = g * b;
  double *audio[]{&left, &right};
  for (size_t channel = 0; channel < 2; ++channel) {
    const double input = *audio[channel], difference = input - second_[channel];
    const double band = a * first_[channel] + b * difference;
    const double low = second_[channel] + b * first_[channel] + c * difference;
    first_[channel] = 2 * band - first_[channel]; second_[channel] = 2 * low - second_[channel];
    if (std::abs(first_[channel]) < 1e-30) first_[channel] = 0;
    if (std::abs(second_[channel]) < 1e-30) second_[channel] = 0;
    *audio[channel] = current_.direct * input + current_.band * band + current_.low * low;
  }
}
namespace {
double decayForCoefficients(double g, double k, double marginDB, double rate) noexcept {
  double radius;
  if (k < 2) radius = std::sqrt(std::max(0., (1 - g * k + g * g) / (1 + g * k + g * g)));
  else {
    const double sum = k + std::sqrt(k * k - 4);
    const double slow = -2 * g / sum, fast = -g * sum / 2;
    radius = std::max(std::abs((1 + slow) / (1 - slow)), std::abs((1 + fast) / (1 - fast)));
  }
  if (radius <= 0) return 2 / rate;
  return std::min(60., (marginDB * std::numbers::ln10 / -20) / std::log(std::min(radius, 1 - 1e-15)) / rate + 2 / rate);
}
}
double StateVariableFilter::decaySeconds(FilterShape shape, double frequency, double q, double gainDB, double rate) noexcept {
  if ((shape == FilterShape::Bell || shape == FilterShape::LowShelf || shape == FilterShape::HighShelf) && gainDB == 0) return 0;
  StateVariableFilter filter;
  if (!filter.configure(shape, frequency, q, gainDB, rate)) return 0;
  return decayForCoefficients(filter.target_.conductance, filter.target_.damping,
    160 + std::abs(gainDB) + 20 * std::log10(std::max(1., q)) + 36, rate);
}
double StateVariableFilter::decaySecondsRange(uint32_t shapes, double minF, double maxF,
    double minQ, double maxQ, double minGain, double maxGain, double rate) noexcept {
  constexpr auto neutralShapes = (1u << unsigned(FilterShape::Bell)) | (1u << unsigned(FilterShape::LowShelf)) | (1u << unsigned(FilterShape::HighShelf));
  if (!shapes || (!(shapes & ~neutralShapes) && minGain == 0 && maxGain == 0)) return 0;
  double minG = INFINITY, maxG = 0, minK = INFINITY, maxK = 0;
  for (unsigned shape = 0; shape < 8; ++shape) if (shapes & (1u << shape))
    for (double f : {minF, maxF}) for (double q : {minQ, maxQ}) for (double gain : {minGain, maxGain}) {
      StateVariableFilter filter;
      if (!filter.configure(FilterShape(shape), f, q, gain, rate)) return 0;
      minG = std::min(minG, filter.target_.conductance); maxG = std::max(maxG, filter.target_.conductance);
      minK = std::min(minK, filter.target_.damping); maxK = std::max(maxK, filter.target_.damping);
    }
  // Conductance/damping interpolate inside this box. For each fixed damping,
  // pole radius is maximal at a conductance endpoint, and conversely for fixed
  // conductance at a damping endpoint (critical damping is a minimum).
  double seconds = 0;
  const double margin = 196 + std::max(std::abs(minGain), std::abs(maxGain)) + 20 * std::log10(std::max(1., maxQ));
  for (double g : {minG, maxG}) for (double k : {minK, maxK})
    seconds = std::max(seconds, decayForCoefficients(g, k, margin, rate));
  return seconds;
}
}
