#pragma once
#include "EffectUtilities.hpp"
#include "StateVariableFilter.hpp"
#include <array>
#include <span>
#include <string_view>
#include <vector>

namespace Tracker {
struct CabinetModel {
  std::string_view name;
  double lowCut, lowShelf, bodyFrequency, bodyGain, bodyQ;
  double coneFrequency, coneGain, coneQ, biteFrequency, biteGain, biteQ, highCut;
  std::array<double, 3> reflectionMS, reflectionGain;
};
std::span<const CabinetModel> cabinetModels() noexcept;
// Original compact cabinet voicings, not measurements of named commercial
// products. Seven linear sections and three short feed-forward reflections.
// Histories are retained while coefficients/delay/gains morph between models.
class CabinetVoicing {
  double rate_;
  std::array<StateVariableFilter, 7> filters_;
  std::array<EffectRamp, 3> delays_, gains_;
  std::vector<std::array<double, 2>> history_;
  size_t cursor_ = 0;
public:
  using Frame = std::array<double, 2>;
  explicit CabinetVoicing(double rate);
  void model(uint32_t index, uint32_t transitionFrames = 0) noexcept;
  Frame process(Frame input) noexcept;
  // Includes every model and all coefficient interpolation between them.
  static double decaySeconds(double rate) noexcept;
};
}
