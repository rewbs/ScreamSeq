#pragma once
#include "MusicalAutomation.hpp"
#include <string>

namespace Tracker {
struct AutomationClip {
  uint32_t span = 0;
  std::vector<AutomationPoint> points; // Relative control-point positions; no synthesized edge points.
};
struct AutomationTool {
  std::string operation;
  uint32_t start = 0, end = 0; // Half-open range in 1/256-row units.
  int32_t shift = 0;
  double amount = 0, offset = 0, from = 0, to = 1, cycles = 1, phase = 0;
  uint32_t spacing = 256, jitter = 0, seed = 0, repeats = 1;
  AutomationCurve curve = AutomationCurve::Linear;
  AutomationClip clip;
};
struct AutomationToolResult {
  std::vector<AutomationPoint> points;
  uint32_t clipped = 0;
  // Original and resulting positions of retained nodes, for loop/sustain anchors.
  std::vector<std::pair<uint32_t, uint32_t>> preservedPositions;
};
AutomationCurve reversedAutomationCurve(AutomationCurve curve);
void validateAutomationPoints(const std::vector<AutomationPoint> &points, uint32_t length);
AutomationClip copyAutomationPoints(const std::vector<AutomationPoint> &points, uint32_t length,
                                    uint32_t start, uint32_t end);
AutomationToolResult transformAutomationPoints(const std::vector<AutomationPoint> &points, uint32_t length,
                                              const AutomationTool &tool);
} // namespace Tracker
