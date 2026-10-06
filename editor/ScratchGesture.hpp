#pragma once
#include "MusicalAutomation.hpp"
#include <span>
#include <map>

namespace Tracker {
inline constexpr uint32_t scratchCycleUnits = 65536;
inline constexpr size_t maximumScratchPoints = 256;
// A song-local gesture is independent of sample, pitch and musical duration.
// Motion is normalized record travel; fader is linear amplitude. Points include
// both cycle endpoints. Formulas are compiled before audio rendering begins.
struct ScratchGesture {
  std::string name;
  std::vector<AutomationPoint> motion, fader;
  bool operator==(const ScratchGesture &) const = default;
};
using ScratchGestureLibrary = std::map<uint16_t, ScratchGesture>;
void validateScratchGesture(const ScratchGesture &);
struct ScratchPreset { std::string id; ScratchGesture gesture; };
std::vector<ScratchPreset> scratchPresets();
double scratchEnvelopeValue(const std::vector<AutomationPoint> &, double phase, double beats = 1) noexcept;
size_t scratchGestureBytes(const ScratchGesture &) noexcept;
}
