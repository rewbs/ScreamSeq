#pragma once
#include "SampleProcessing.hpp"
#include <limits>

namespace Tracker {
// One selected sample's min/max pyramid. Fixed-size PCM edits rebuild only
// touched 256-frame leaves; arbitrary views combine cached complete tiles with
// exact boundary samples. Document owns invalidation for structural mutations.
class SampleWaveform {
  struct Peak {
    std::array<int32_t, 2> low{INT32_MAX, INT32_MAX}, high{INT32_MIN, INT32_MIN};
  };
  const std::byte *source_ = nullptr;
  uint32_t frames_ = 0;
  uint8_t bits_ = 0, channels_ = 0;
  size_t base_ = 0;
  uint64_t readFrames_ = 0;
  std::vector<Peak> tree_;
  static void combine(Peak &into, const Peak &other) noexcept;
  void read(Peak &peak, SamplePCMView pcm, uint32_t first, uint32_t last) noexcept;
  void leaf(SamplePCMView pcm, size_t index) noexcept;
  bool matches(SamplePCMView pcm) const noexcept;
  void prepare(SamplePCMView pcm);

public:
  void invalidate() noexcept {
    source_ = nullptr;
    frames_ = 0;
    bits_ = channels_ = 0;
    tree_.clear();
  }
  void changed(SamplePCMView pcm, uint32_t first, uint32_t last) noexcept;
  std::vector<float> query(SamplePCMView pcm, uint32_t first, uint32_t last, size_t bins,
                           SampleChannels channels = SampleChannels::Both);
  uint64_t readFrames() const noexcept { return readFrames_; }
};
} // namespace Tracker
