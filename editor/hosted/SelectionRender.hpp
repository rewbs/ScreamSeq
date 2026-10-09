#pragma once
#include "editor/Sampling.hpp"
#include "editor/hosted/PluginTypes.hpp"

namespace Tracker {
inline constexpr uint32_t selectionRenderRate = 48000;
struct SelectionRenderResult {
  std::vector<float> pcm;
  uint64_t preRollFrames = 0, selectedFrames = 0, tailFrames = 0, latencyFrames = 0;
  uint64_t automationStartFrame = 0;
};
// Private offline processors; no live device is touched. Warm selected channels
// from row zero; ignore transport jumps/loops but retain delays/tempo/native FX.
// Nonselected sources are muted everywhere, including sidechain contributions.
SelectionRenderResult renderPatternSelection(const PreparedSamplingSelection &,
                                             const std::vector<PluginState> &,
                                             const std::vector<ParameterChange> &,
                                             double tailSeconds = 0, uint32_t blockFrames = 512);
}
