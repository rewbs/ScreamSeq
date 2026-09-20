#include "SampleSnap.hpp"
#include <algorithm>
#include <stdexcept>

namespace Tracker {
std::vector<SampleSnapResult> snapSampleBoundaries(SamplePCMView pcm, std::span<const uint32_t> positions,
                                                 const SampleSnapOptions &o) {
  pcm.validate();
  if (positions.empty() || positions.size() > maximumSampleSnapPositions)
    throw std::invalid_argument("Snap needs between 1 and 64 positions");
  if (o.mode != SampleSnapMode::ZeroCrossing && o.mode != SampleSnapMode::Grid)
    throw std::invalid_argument("Unknown sample snapping mode");
  if (o.direction != SampleSnapDirection::Nearest && o.direction != SampleSnapDirection::Before &&
      o.direction != SampleSnapDirection::After)
    throw std::invalid_argument("Unknown sample snapping direction");
  if (o.channels != SampleChannels::Both && o.channels != SampleChannels::Left && o.channels != SampleChannels::Right)
    throw std::invalid_argument("Unknown sample channel selection");
  if (o.channels == SampleChannels::Right && pcm.channels != 2)
    throw std::invalid_argument("Right channel requires a stereo sample");
  if (o.radius > maximumSampleSnapRadius || !o.step || o.origin > pcm.frames)
    throw std::invalid_argument("Invalid snap radius, grid step or origin");
  for (const auto position : positions)
    if (position > pcm.frames)
      throw std::invalid_argument("Snap positions must be boundaries inside the sample");
  const auto crossing = [&](uint32_t frame) {
    // Outside the asset is silence; its two endpoint boundaries are valid.
    if (!frame || frame == pcm.frames)
      return true;
    for (uint8_t channel = 0; channel < pcm.channels; ++channel) {
      if (o.channels != SampleChannels::Both && channel != (o.channels == SampleChannels::Left ? 0 : 1))
        continue;
      const auto left = pcm.value(frame - 1, channel), right = pcm.value(frame, channel);
      if (left && right && (left < 0) == (right < 0))
        return false;
    }
    return true;
  };
  std::vector<SampleSnapResult> result;
  result.reserve(positions.size());
  for (const auto position : positions) {
    SampleSnapResult snapped{position, position, false};
    if (o.mode == SampleSnapMode::Grid) {
      const auto distance = int64_t(position) - o.origin;
      auto division = distance / o.step;
      if (distance < 0 && distance % o.step)
        --division; // Floor, including grids whose origin follows the target.
      const auto lower = int64_t(o.origin) + division * o.step;
      const auto upper = lower == position ? lower : lower + o.step;
      const bool haveLower = lower >= 0 && o.direction != SampleSnapDirection::After;
      const bool haveUpper = upper <= pcm.frames && o.direction != SampleSnapDirection::Before;
      if (haveLower || haveUpper) {
        snapped.after = uint32_t(haveLower && (!haveUpper || int64_t(position) - lower <= upper - position) ? lower : upper);
        snapped.matched = true;
      }
    } else {
      for (uint32_t distance = 0; distance <= o.radius; ++distance) {
        if (o.direction != SampleSnapDirection::After && distance <= position && crossing(position - distance)) {
          snapped.after = position - distance;
          snapped.matched = true;
          break;
        }
        if (o.direction != SampleSnapDirection::Before && distance <= pcm.frames - position && crossing(position + distance)) {
          snapped.after = position + distance;
          snapped.matched = true;
          break;
        }
      }
    }
    result.push_back(snapped);
  }
  return result;
}
} // namespace Tracker
