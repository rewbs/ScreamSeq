#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Tracker {
// Native OpenMPT PCM is signed 8- or 16-bit, interleaved, in host byte order.
// Keep range algorithms separate from song/history ownership for future assets.
struct SamplePCMView {
  std::span<const std::byte> data;
  uint32_t frames = 0;
  uint8_t bits = 16, channels = 1;
  size_t stride() const noexcept { return size_t(bits / 8) * channels; }
  double scale() const noexcept { return bits == 8 ? 128 : 32768; }
  void validate() const;
  int32_t value(uint32_t frame, uint8_t channel) const noexcept;
};
enum class SampleChannels { Both, Left, Right };
enum class SampleFadeCurve { Linear, Smooth, Exponential, Logarithmic };
struct SampleProcessOptions {
  std::string operation;
  uint32_t first = 0, last = 0; // Strict nonempty interval; end is exclusive.
  SampleChannels channels = SampleChannels::Both;
  SampleFadeCurve curve = SampleFadeCurve::Linear;
  double exponent = 3, gainDB = 0, targetDB = 0;
  uint32_t window = 5; // Odd, centered moving-average smoothing window.
};
inline constexpr uint32_t samplePatchFrames = 256, maximumSamplePreview = 256;
struct SamplePCMChange {
  uint32_t frame;
  uint8_t channel;
  int32_t before, after;
};
struct SamplePCMChunk {
  uint32_t first = 0, frames = 0;
  std::vector<std::byte> before, after;
};
struct SampleProcessResult {
  uint32_t first = 0, last = 0, changedFrames = 0;
  uint64_t changedSamples = 0, clippedSamples = 0, historyBytes = 0;
  double peakBefore = 0, peakAfter = 0;
  std::vector<SamplePCMChange> preview;
};
struct SamplePCMPlan {
  uint32_t totalFrames = 0;
  uint8_t bits = 16, channels = 1;
  std::vector<SamplePCMChunk> chunks;
  SampleProcessResult result;
};
SamplePCMPlan planSampleProcess(SamplePCMView pcm, const SampleProcessOptions &options);
struct SampleDrawPoint {
  uint32_t frame = 0;
  double value = 0; // Normalized amplitude, -1...1; converted to signed native PCM.
};
enum class SampleDrawInterpolation { Linear, Step };
struct SampleDrawOptions {
  std::vector<SampleDrawPoint> points;
  SampleChannels channels = SampleChannels::Both;
  SampleDrawInterpolation interpolation = SampleDrawInterpolation::Linear;
};
inline constexpr uint32_t maximumSampleDrawPoints = 4096, maximumSampleDrawFrames = 1048576;
SamplePCMPlan planSampleDraw(SamplePCMView pcm, const SampleDrawOptions &options);
} // namespace Tracker
