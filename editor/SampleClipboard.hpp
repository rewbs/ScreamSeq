#pragma once
#include "SampleProcessing.hpp"
#include <optional>

namespace Tracker {
inline constexpr size_t maximumSampleClipboardBytes = 128 * 1024 * 1024;
struct SampleClipboard {
  std::vector<std::byte> data;
  uint32_t rate = 44100;
  uint8_t bits = 16, channels = 1;
  std::string name;
  SamplePCMView pcm() const;
  void validate() const;
};
SampleClipboard copySamplePCM(SamplePCMView pcm, uint32_t rate, uint32_t first, uint32_t last, SampleChannels channels,
                              std::string name = {});
enum class SamplePasteMode { Insert, Overwrite, Mix, Replace };
enum class SampleRateMode { Resample, KeepFrames };
struct SamplePasteOptions {
  SamplePasteMode mode = SamplePasteMode::Insert;
  uint32_t at = 0, end = 0; // end is used only for Replace.
  SampleChannels channels = SampleChannels::Both;
  SampleRateMode rateMode = SampleRateMode::Resample;
  double sourceGainDB = 0, destinationGainDB = 0; // destination gain is only for Mix.
};
struct SampleSpliceChange {
  uint32_t frame;
  uint8_t channel;
  std::optional<int32_t> before, after;
};
struct SampleEditGeometry {
  uint32_t frames = 0, loopStart = 0, loopEnd = 0, sustainStart = 0, sustainEnd = 0;
  uint16_t flags = 0;
  std::array<uint32_t, 9> cues{};
  uint8_t reverseLoops = 0;
  bool operator==(const SampleEditGeometry &) const = default;
};
struct SampleSpliceResult {
  uint32_t first = 0, removedFrames = 0, insertedFrames = 0, resultFrames = 0;
  uint64_t changedSamples = 0, clippedSamples = 0, historyBytes = 0;
  double peakAfter = 0;
  std::vector<SampleSpliceChange> preview;
  std::optional<SampleEditGeometry> before, after;
};
struct SampleSplicePlan {
  uint32_t originalFrames = 0;
  uint8_t bits = 16, channels = 1;
  std::vector<std::byte> removed, inserted;
  SampleSpliceResult result;
  bool changesLength() const { return result.removedFrames != result.insertedFrames; }
  bool changed() const { return changesLength() || result.changedSamples != 0; }
};
SampleSplicePlan planSamplePaste(SamplePCMView destination, uint32_t rate, const SampleClipboard &source,
                                 const SamplePasteOptions &options);
SampleSplicePlan planSampleErase(SamplePCMView destination, uint32_t first, uint32_t last);
SamplePCMPlan sampleSplicePatches(const SampleSplicePlan &splice);
} // namespace Tracker
