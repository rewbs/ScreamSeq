#pragma once
#include "SampleProcessing.hpp"

namespace Tracker {
enum class SampleCrossfadeMode { Preserve, Overlap };
enum class SampleCrossfadeCurve { Linear, EqualPower };
struct SampleCrossfadeOptions {
  uint32_t frames = 64;
  SampleCrossfadeMode mode = SampleCrossfadeMode::Preserve;
  SampleCrossfadeCurve curve = SampleCrossfadeCurve::Linear;
  bool sustain = false;
};
inline constexpr uint32_t maximumSampleCrossfadeFrames = 1048576;
struct SampleCrossfadePlan {
  SamplePCMPlan pcm;
  uint32_t sourceStart = 0, loopStart = 0;
};
// Both channels share each frame's weights. All inputs are read from original PCM.
SampleCrossfadePlan planSampleCrossfade(SamplePCMView pcm, uint32_t loopStart, uint32_t loopEnd,
                                       const SampleCrossfadeOptions &options);
} // namespace Tracker
