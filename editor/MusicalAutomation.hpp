#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include "CurveFormula.hpp"

namespace Tracker {
enum class AutomationCurve : uint8_t { Step, Linear, Smooth, Exponential, Logarithmic, StepNext, ExponentialReverse, LogarithmicReverse, Scripted };
struct AutomationPoint {
  uint32_t position = 0; // Pattern rows in units of 1/256 row.
  double value = 0; // Normalized parameter range, inclusive [0, 1].
  AutomationCurve curve = AutomationCurve::Linear; // Segment leaving this point.
  CurveFormula formula;
  bool operator==(const AutomationPoint &) const = default;
};
struct MusicalAutomationLane {
  uint64_t id = 0, pattern = 0;
  std::string plugin; // Stable PluginState.instanceID, never a rack index.
  uint32_t parameter = 0;
  bool enabled = true;
  std::vector<AutomationPoint> points;
  bool operator==(const MusicalAutomationLane &) const = default;
};
inline double automationValue(const std::vector<AutomationPoint> &points, double position, double endPosition = 0, double rowsPerBeat = 4) noexcept {
  if (points.empty()) return 0;
  auto right = std::upper_bound(points.begin(), points.end(), position,
                               [](double x, const AutomationPoint &point) { return x < point.position; });
  if (right == points.begin()) return points.front().value;
  const auto &left = *(right - 1);
  if(left.curve == AutomationCurve::Scripted) {
    const double finish = right == points.end() ? std::max(double(left.position)+1,endPosition) : right->position;
    const double endValue = right == points.end() ? left.value : right->value;
    const double scale = 256 * std::max(1.0,rowsPerBeat);
    return left.formula.evaluate({left.value,endValue,std::clamp((position-left.position)/(finish-left.position),0.0,1.0),position/256,position/scale,(position-left.position)/scale,(finish-left.position)/scale,left.position/scale,finish/scale});
  }
  if (right == points.end() || left.curve == AutomationCurve::Step) return left.value;
  if (left.curve == AutomationCurve::StepNext) return position == left.position ? left.value : right->value;
  double fraction = std::clamp((position - left.position) / (right->position - left.position), 0.0, 1.0);
  switch (left.curve) {
  case AutomationCurve::Smooth: fraction = fraction * fraction * (3 - 2 * fraction); break;
  case AutomationCurve::Exponential: fraction = std::expm1(4 * fraction) / std::expm1(4.0); break;
  case AutomationCurve::Logarithmic: fraction = std::log1p(std::expm1(4.0) * fraction) / 4; break;
  case AutomationCurve::ExponentialReverse: fraction = -std::expm1(-4 * fraction) / -std::expm1(-4.0); break;
  case AutomationCurve::LogarithmicReverse: fraction = 1 - std::log1p(std::expm1(4.0) * (1 - fraction)) / 4; break;
  default: break;
  }
  return left.value + (right->value - left.value) * fraction;
}
} // namespace Tracker
