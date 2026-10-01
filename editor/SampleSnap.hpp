#pragma once
#include "SampleProcessing.hpp"

namespace Tracker {
enum class SampleSnapMode { ZeroCrossing, Grid };
enum class SampleSnapDirection { Nearest, Before, After };
struct SampleSnapOptions {
  SampleSnapMode mode = SampleSnapMode::ZeroCrossing;
  SampleSnapDirection direction = SampleSnapDirection::Nearest;
  SampleChannels channels = SampleChannels::Both;
  uint32_t radius = 2048, step = 1, origin = 0;
};
struct SampleSnapResult {
  uint32_t before = 0, after = 0;
  bool matched = false;
};
inline constexpr uint32_t maximumSampleSnapPositions = 64, maximumSampleSnapRadius = 65536;
// Positions are insertion boundaries in [0,frames], including the exclusive end.
std::vector<SampleSnapResult> snapSampleBoundaries(SamplePCMView pcm, std::span<const uint32_t> positions,
                                                 const SampleSnapOptions &options);
} // namespace Tracker
