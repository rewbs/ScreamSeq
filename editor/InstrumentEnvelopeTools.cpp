#include "InstrumentEnvelopeTools.hpp"
#include <stdexcept>

namespace Tracker {
namespace {
std::vector<AutomationPoint> normalized(const OpenMPT::InstrumentEnvelope &envelope) {
  std::vector<AutomationPoint> points;
  for (const auto &p : envelope) points.push_back({p.tick, p.value / 64.0, AutomationCurve::Linear});
  if (!points.empty()) validateAutomationPoints(points, instrumentEnvelopeLength);
  return points;
}
}
bool sameInstrumentEnvelope(const OpenMPT::InstrumentEnvelope &a, const OpenMPT::InstrumentEnvelope &b) {
  return static_cast<const std::vector<OpenMPT::EnvelopeNode>&>(a) == static_cast<const std::vector<OpenMPT::EnvelopeNode>&>(b)
    && a.dwFlags == b.dwFlags && a.nLoopStart == b.nLoopStart && a.nLoopEnd == b.nLoopEnd
    && a.nSustainStart == b.nSustainStart && a.nSustainEnd == b.nSustainEnd && a.nReleaseNode == b.nReleaseNode;
}
AutomationClip copyInstrumentEnvelope(const OpenMPT::InstrumentEnvelope &envelope, uint32_t start, uint32_t end) {
  return copyAutomationPoints(normalized(envelope), instrumentEnvelopeLength, start, end);
}
InstrumentEnvelopeToolResult transformInstrumentEnvelope(const OpenMPT::InstrumentEnvelope &before, uint32_t maximumPoints,
                                                         const AutomationTool &tool) {
  if (tool.curve != AutomationCurve::Linear || std::any_of(tool.clip.points.begin(), tool.clip.points.end(), [](const auto &p) {
      return p.curve != AutomationCurve::Linear;
    })) throw std::invalid_argument("Instrument envelopes use linear segments");
  auto transformed = transformAutomationPoints(normalized(before), instrumentEnvelopeLength, tool);
  if (!maximumPoints || transformed.points.size() > maximumPoints)
    throw std::invalid_argument("Envelope exceeds this format's point limit; use wider spacing or fewer repetitions");
  if (transformed.points.front().position != 0)
    throw std::invalid_argument("An instrument envelope must retain a point at tick zero");
  InstrumentEnvelopeToolResult result{before, transformed.clipped};
  auto &after = result.envelope; after.clear();
  for (const auto &p : transformed.points) {
    const double value = p.value * 64;
    const auto rounded = uint8_t(std::lround(value));
    if (std::abs(value - rounded) > 1e-10) ++result.rounded;
    after.push_back(uint16_t(p.position), rounded);
  }
  // Retained nodes keep their marker ownership even after reordering. Replaced
  // nodes attach to the nearest resulting tick (earlier on ties), reported in preview.
  auto anchor = [&](uint8_t index, bool active) -> uint8_t {
    if (before.empty()) return 0;
    if (index >= before.size()) throw std::invalid_argument("Invalid instrument envelope anchor");
    const auto old = before[index].tick;
    const auto moved = std::find_if(transformed.preservedPositions.begin(), transformed.preservedPositions.end(),
      [&](const auto &pair) { return pair.first == old; });
    uint32_t target = moved == transformed.preservedPositions.end() ? old : moved->second;
    auto closest = std::min_element(after.begin(), after.end(), [&](const auto &a, const auto &b) {
      return std::abs(int(a.tick) - int(target)) < std::abs(int(b.tick) - int(target));
    });
    if (active && moved == transformed.preservedPositions.end()) ++result.reanchored;
    return uint8_t(closest - after.begin());
  };
  after.nLoopStart = anchor(before.nLoopStart, before.dwFlags[OpenMPT::ENV_LOOP]);
  after.nLoopEnd = anchor(before.nLoopEnd, before.dwFlags[OpenMPT::ENV_LOOP]);
  after.nSustainStart = anchor(before.nSustainStart, before.dwFlags[OpenMPT::ENV_SUSTAIN]);
  after.nSustainEnd = anchor(before.nSustainEnd, before.dwFlags[OpenMPT::ENV_SUSTAIN]);
  if (after.nLoopStart > after.nLoopEnd) std::swap(after.nLoopStart, after.nLoopEnd);
  if (after.nSustainStart > after.nSustainEnd) std::swap(after.nSustainStart, after.nSustainEnd);
  if (before.nReleaseNode != ENV_RELEASE_NODE_UNSET) after.nReleaseNode = anchor(before.nReleaseNode, true);
  return result;
}
}
