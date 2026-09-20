#pragma once
#include "editor/StateVariableFilter.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
namespace FilterReference {
using Tracker::FilterShape;
// Independent RBJ direct-form reference (not used in production). Formulae:
// https://www.w3.org/TR/audio-eq-cookbook/ . This checks the SVF transform and
// shelf/bell gain conventions through their complete impulse responses.
struct Reference {
  double b0, b1, b2, a1, a2, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
  Reference(FilterShape type, double frequency, double q, double gain, double rate) {
    const double w = 2 * std::numbers::pi * std::min(frequency, rate * .45) / rate;
    const double c = std::cos(w), alpha = std::sin(w) / (2 * q), a = std::pow(10., gain / 40), s = 2 * std::sqrt(a) * alpha;
    double a0 = 1 + alpha; a1 = -2 * c; a2 = 1 - alpha;
    switch (type) {
      case FilterShape::LowPass: b0 = b2 = (1 - c) / 2; b1 = 1 - c; break;
      case FilterShape::HighPass: b0 = b2 = (1 + c) / 2; b1 = -1 - c; break;
      case FilterShape::BandPass: b0 = alpha; b1 = 0; b2 = -alpha; break;
      case FilterShape::Notch: b0 = b2 = 1; b1 = -2 * c; break;
      case FilterShape::AllPass: b0 = 1 - alpha; b1 = -2 * c; b2 = 1 + alpha; break;
      case FilterShape::Bell:
        b0 = 1 + alpha * a; b1 = -2 * c; b2 = 1 - alpha * a; a0 = 1 + alpha / a; a2 = 1 - alpha / a; break;
      case FilterShape::LowShelf:
        b0 = a * (a + 1 - (a - 1) * c + s); b1 = 2 * a * (a - 1 - (a + 1) * c); b2 = a * (a + 1 - (a - 1) * c - s);
        a0 = a + 1 + (a - 1) * c + s; a1 = -2 * (a - 1 + (a + 1) * c); a2 = a + 1 + (a - 1) * c - s; break;
      case FilterShape::HighShelf:
        b0 = a * (a + 1 + (a - 1) * c + s); b1 = -2 * a * (a - 1 + (a + 1) * c); b2 = a * (a + 1 + (a - 1) * c - s);
        a0 = a + 1 - (a - 1) * c + s; a1 = 2 * (a - 1 - (a + 1) * c); a2 = a + 1 - (a - 1) * c - s; break;
    }
    b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
  }
  double process(double x) {
    const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1; x1 = x; y2 = y1; y1 = y; return y;
  }
};
}
