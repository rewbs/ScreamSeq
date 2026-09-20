#pragma once
#include <algorithm>
#include <cmath>

namespace Tracker {
// Positive soft-knee response in the detector's post-gain dB domain.
inline double feedbackOver(double above, double knee) noexcept {
  if (above <= -knee * .5) return 0;
  if (above >= knee * .5) return above;
  const double x = above + knee * .5;
  return x * x / (2 * knee);
}
// Solve the implicit feedback envelope exactly, with a rationalized quadratic
// root inside the knee. Unlike a one-sample explicit loop, this stays monotone
// for fast attacks and large ratios. Attack/release are poles inside the loop;
// the resulting closed-loop settling time also depends on ratio and knee.
inline double feedbackReduction(double previous, double level, double threshold,
    double ratio, double knee, double attack, double release) noexcept {
  const double z = level - threshold;
  const double wanted = (ratio - 1) * feedbackOver(z - previous, knee);
  const double pole = wanted > previous ? attack : release;
  const double b = pole * previous, c = (1 - pole) * (ratio - 1);
  if (z - b <= -knee * .5) return b;
  const double high = (b + c * z) / (1 + c);
  if (knee == 0 || z - high >= knee * .5) return high;
  const double v = z + knee * .5 - b;
  const double q = 2 * v / (1 + std::sqrt(1 + 2 * c * v / knee));
  return b + c * q * q / (2 * knee);
}
}
