#include "AutomationTools.hpp"
#include <numbers>
#include <stdexcept>

namespace Tracker {
namespace {
void require(bool value, const char *message) { if (!value) throw std::invalid_argument(message); }
void range(uint32_t length, uint32_t start, uint32_t end) {
  require(length && length <= 1048576 && start < end && end <= length, "Automation range must be nonempty and inside the pattern");
}
}
void validateAutomationPoints(const std::vector<AutomationPoint> &points, uint32_t length) {
  require(length && length <= 1048576 && !points.empty() && points.size() <= 4096, "Automation requires 1..4096 points inside a bounded pattern");
  uint32_t previous = 0;
  for (size_t i = 0; i < points.size(); ++i) {
    const auto &p = points[i];
    require(p.position < length && (!i || p.position > previous), "Automation points must be ordered, distinct and inside the pattern");
    require(std::isfinite(p.value) && p.value >= 0 && p.value <= 1 && uint8_t(p.curve) <= uint8_t(AutomationCurve::Scripted), "Invalid automation value or curve");
    require(p.curve != AutomationCurve::Scripted || !p.formula.source().empty(), "Scripted points require a formula");
    previous = p.position;
  }
}
AutomationCurve reversedAutomationCurve(AutomationCurve c) {
  switch (c) {
  case AutomationCurve::Step: return AutomationCurve::StepNext;
  case AutomationCurve::StepNext: return AutomationCurve::Step;
  case AutomationCurve::Exponential: return AutomationCurve::ExponentialReverse;
  case AutomationCurve::ExponentialReverse: return AutomationCurve::Exponential;
  case AutomationCurve::Logarithmic: return AutomationCurve::LogarithmicReverse;
  case AutomationCurve::LogarithmicReverse: return AutomationCurve::Logarithmic;
  default: return c;
  }
}
AutomationClip copyAutomationPoints(const std::vector<AutomationPoint> &points, uint32_t length, uint32_t start, uint32_t end) {
  validateAutomationPoints(points, length); range(length, start, end);
  AutomationClip clip; clip.span = end - start;
  for (auto p : points) if (p.position >= start && p.position < end) { p.position -= start; clip.points.push_back(p); }
  require(!clip.points.empty(), "No automation points in the selected range");
  return clip;
}
AutomationToolResult transformAutomationPoints(const std::vector<AutomationPoint> &points, uint32_t length, const AutomationTool &t) {
  if (!points.empty()) validateAutomationPoints(points, length);
  else require(t.operation == "paste" || t.operation == "insert" || t.operation == "ramp" || t.operation == "sine", "Create envelope points before transforming them");
  range(length, t.start, t.end);
  require(uint8_t(t.curve) <= uint8_t(AutomationCurve::Scripted), "Unknown automation curve");
  AutomationToolResult result;
  auto bounded = [&](double value) {
    require(std::isfinite(value), "Non-finite automation result");
    if (value < 0 || value > 1) ++result.clipped;
    return std::clamp(value, 0.0, 1.0);
  };
  std::vector<AutomationPoint> selected;
  for (auto p : points) {
    if (p.position >= t.start && p.position < t.end) selected.push_back(p);
    else { result.points.push_back(p); result.preservedPositions.emplace_back(p.position, p.position); }
  }
  auto &out = result.points;
  if(t.operation == "flip-time" || t.operation == "flip-values" || t.operation == "scale" || t.operation == "humanize")
    require(std::none_of(selected.begin(),selected.end(),[](const auto &p){return p.curve==AutomationCurve::Scripted;}), "Edit the formula directly before transforming scripted segments");
  if (t.operation == "paste" || t.operation == "insert") {
    validateAutomationPoints(t.clip.points, t.clip.span);
    require(t.repeats >= 1 && t.repeats <= 4096, "Use 1..4096 clipboard repetitions");
    const uint64_t span = uint64_t(t.clip.span) * t.repeats;
    require(uint64_t(t.start) + span <= length, "Pasted automation extends past the pattern");
    require(uint64_t(t.clip.points.size()) * t.repeats <= 4096, "Pasted automation exceeds 4096 points");
    out.clear(); result.preservedPositions.clear();
    for (auto p : points) {
      const auto original = p.position;
      if (t.operation == "insert" && p.position >= t.start) {
        require(uint64_t(p.position) + span < length, "Inserting would discard existing tail points");
        p.position += uint32_t(span);
      } else if (t.operation == "paste" && p.position >= t.start && p.position < t.start + span) continue;
      out.push_back(p);
      result.preservedPositions.emplace_back(original, p.position);
    }
    for (uint32_t repeat = 0; repeat < t.repeats; ++repeat)
      for (auto p : t.clip.points) { p.position += t.start + repeat * t.clip.span; out.push_back(p); }
  } else if (t.operation == "ramp") {
    require(std::isfinite(t.from) && std::isfinite(t.to) && t.from >= 0 && t.from <= 1 && t.to >= 0 && t.to <= 1, "Ramp values must be normalized");
    out.push_back({t.start, t.from, t.curve});
    if (t.end - t.start > 1) out.push_back({t.end - 1, t.to, t.curve});
  } else if (t.operation == "sine") {
    require(std::isfinite(t.amount) && t.amount >= 0 && t.amount <= 1 && std::isfinite(t.offset) && t.offset >= 0 && t.offset <= 1 &&
            std::isfinite(t.cycles) && t.cycles > 0 && t.cycles <= 1024 && std::isfinite(t.phase) && t.phase >= -360 && t.phase <= 360 && t.spacing && t.spacing <= length, "Invalid sine settings");
    require(uint64_t(t.end - t.start - 1) / t.spacing + 2 + out.size() <= 4097, "Generated automation exceeds 4096 points");
    for (uint32_t p = t.start;; p = std::min(t.end - 1, p + t.spacing)) {
      const double x = double(p - t.start) / std::max(1u, t.end - t.start - 1);
      out.push_back({p, bounded(t.offset + t.amount * std::sin(2 * std::numbers::pi * (t.cycles * x + t.phase / 360))), t.curve});
      if (p == t.end - 1) break;
    }
  } else {
    require(!selected.empty(), "No automation points in the selected range");
    if (t.operation == "flip-time") {
      for (size_t i = selected.size(); i-- > 0;) {
        auto p = selected[i]; p.position = t.start + t.end - 1 - p.position;
        p.curve = i ? reversedAutomationCurve(selected[i - 1].curve) : selected.back().curve;
        out.push_back(p);
        result.preservedPositions.emplace_back(selected[i].position, p.position);
      }
    } else {
      require(t.operation == "shift" || t.operation == "flip-values" || t.operation == "scale" || t.operation == "humanize", "Unknown automation operation");
      if (t.operation == "scale") require(std::isfinite(t.amount) && t.amount >= -16 && t.amount <= 16 && std::isfinite(t.offset) && t.offset >= -1 && t.offset <= 1, "Invalid automation scaling");
      if (t.operation == "humanize") require(std::isfinite(t.amount) && t.amount >= 0 && t.amount <= 1 && t.jitter <= length, "Invalid automation humanization");
      uint32_t random = t.seed;
      auto next = [&]() { random = 1664525u * random + 1013904223u; return random; };
      for (auto p : selected) {
        const auto original = p.position;
        if (t.operation == "shift") {
          const int64_t at = int64_t(p.position) + t.shift;
          require(at >= 0 && at < length, "Shift would move a point outside the pattern"); p.position = uint32_t(at);
        } else if (t.operation == "flip-values") p.value = 1 - p.value;
        else if (t.operation == "scale") p.value = bounded(p.value * t.amount + t.offset);
        else {
          if (t.amount) p.value = bounded(p.value + t.amount * (2 * (double(next()) / UINT32_MAX) - 1));
          if (t.jitter) p.position = uint32_t(std::clamp<int64_t>(int64_t(p.position) + int64_t(next() % (2 * t.jitter + 1)) - t.jitter, t.start, t.end - 1));
        }
        out.push_back(p);
        result.preservedPositions.emplace_back(original, p.position);
      }
    }
  }
  std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.position < b.position; });
  validateAutomationPoints(out, length); // Collisions and overflow reject atomically, never silently drop points.
  return result;
}
} // namespace Tracker
