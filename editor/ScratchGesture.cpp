#include "ScratchGesture.hpp"
#include <stdexcept>

namespace Tracker {
void validateScratchGesture(const ScratchGesture &gesture) {
  if(gesture.name.empty() || gesture.name.size() > 256 || gesture.name.find('\0') != std::string::npos)
    throw std::invalid_argument("Scratch gesture needs a name of 1..256 bytes");
  for(const auto *points : {&gesture.motion, &gesture.fader}) {
    if(points->size() < 2 || points->size() > maximumScratchPoints || points->front().position != 0 || points->back().position != scratchCycleUnits)
      throw std::invalid_argument("Scratch envelopes need 2..256 points, including positions 0 and 65536");
    uint32_t previous = 0;
    for(size_t i = 0; i < points->size(); ++i) {
      const auto &point = (*points)[i];
      if(point.position > scratchCycleUnits || (i && point.position <= previous) || !std::isfinite(point.value) || point.value < 0 || point.value > 1 || unsigned(point.curve) > unsigned(AutomationCurve::Scripted) ||
         (point.curve == AutomationCurve::Scripted && point.formula.source().empty()) || (point.curve != AutomationCurve::Scripted && !point.formula.source().empty()))
        throw std::invalid_argument("Invalid scratch envelope position, value, curve or formula");
      previous = point.position;
    }
  }
}
double scratchEnvelopeValue(const std::vector<AutomationPoint> &points, double phase, double beats) noexcept {
  // Here one normalized cycle is 256 synthetic rows. Set its signature to
  // preserve the public formula beat variables without changing their meaning.
  const double position = std::clamp(phase, 0., 1.) * scratchCycleUnits;
  if(points.empty()) return 0;
  auto right = std::upper_bound(points.begin(), points.end(), position, [](double x, const auto &p) { return x < p.position; });
  if(right != points.begin()) {
    const auto &left = *(right - 1);
    if(left.curve == AutomationCurve::Scripted) {
      const double finish = right == points.end() ? scratchCycleUnits : right->position;
      if(finish <= left.position) return left.value;
      const double end = right == points.end() ? left.value : right->value;
      const double scale = beats / scratchCycleUnits;
      return left.formula.evaluate({left.value,end,std::clamp((position-left.position)/(finish-left.position),0.,1.),position/256,position*scale,(position-left.position)*scale,(finish-left.position)*scale,left.position*scale,finish*scale});
    }
  }
  return automationValue(points, position, scratchCycleUnits);
}
size_t scratchGestureBytes(const ScratchGesture &gesture) noexcept {
  size_t result = sizeof(gesture) + gesture.name.size();
  for(const auto *points : {&gesture.motion, &gesture.fader}) for(const auto &point : *points) result += sizeof(point) + point.formula.bytes();
  return result;
}
std::vector<ScratchPreset> scratchPresets() {
  using C = AutomationCurve;
  auto point = [](double phase, double value, C curve = C::Smooth) { return AutomationPoint{uint32_t(std::lround(phase * scratchCycleUnits)),value,curve,{}}; };
  const std::vector<AutomationPoint> baby{point(0,0),point(.5,1),point(1,0)};
  const std::vector<AutomationPoint> open{point(0,1),point(1,1)};
  auto cuts = [&](std::initializer_list<std::pair<double,double>> values) { std::vector<AutomationPoint> result; for(auto [p,v] : values) result.push_back(point(p,v,C::Step)); return result; };
  std::vector<ScratchGesture> result{
    {"Baby",baby,open},
    {"Chirp",baby,cuts({{0,1},{.32,0},{.58,1},{.92,0},{1,1}})},
    {"Transform",baby,cuts({{0,0},{.06,1},{.17,0},{.29,1},{.40,0},{.56,1},{.67,0},{.79,1},{.90,0},{1,0}})},
    {"One-click flare",baby,cuts({{0,1},{.22,0},{.29,1},{.72,0},{.79,1},{1,1}})},
    {"Two-click flare",baby,cuts({{0,1},{.14,0},{.20,1},{.31,0},{.37,1},{.64,0},{.70,1},{.81,0},{.87,1},{1,1}})},
    {"Crab",baby,cuts({{0,0},{.08,1},{.15,0},{.23,1},{.30,0},{.38,1},{.45,0},{.58,1},{.65,0},{.73,1},{.80,0},{.88,1},{.95,0},{1,0}})},
    {"Scribble",{point(0,.5),point(.125,1),point(.25,0),point(.375,1),point(.5,0),point(.625,1),point(.75,0),point(.875,1),point(1,.5)},open}
  };
  for(const auto &gesture : result) validateScratchGesture(gesture);
  static constexpr const char *ids[]{"baby","chirp","transform","one-click-flare","two-click-flare","crab","scribble"};
  std::vector<ScratchPreset> presets;
  for(size_t i=0;i<result.size();++i)presets.push_back({ids[i],std::move(result[i])});
  return presets;
}
}
