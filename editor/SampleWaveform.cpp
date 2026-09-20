#include "SampleWaveform.hpp"
#include <algorithm>
#include <stdexcept>

namespace Tracker {
void SampleWaveform::combine(Peak &a, const Peak &b) noexcept {
  for (size_t c = 0; c < 2; ++c) {
    a.low[c] = std::min(a.low[c], b.low[c]);
    a.high[c] = std::max(a.high[c], b.high[c]);
  }
}
void SampleWaveform::read(Peak &peak, SamplePCMView pcm, uint32_t first, uint32_t last) noexcept {
  readFrames_ += last - first;
  for (uint32_t n = first; n < last; ++n)
    for (uint8_t c = 0; c < pcm.channels; ++c) {
      const auto x = pcm.value(n, c);
      peak.low[c] = std::min(peak.low[c], x);
      peak.high[c] = std::max(peak.high[c], x);
    }
}
void SampleWaveform::leaf(SamplePCMView pcm, size_t index) noexcept {
  tree_[base_ + index] = Peak{};
  const auto first = uint32_t(index * samplePatchFrames);
  read(tree_[base_ + index], pcm, first, uint32_t(std::min<uint64_t>(uint64_t(first) + samplePatchFrames, pcm.frames)));
}
bool SampleWaveform::matches(SamplePCMView pcm) const noexcept {
  return source_ == pcm.data.data() && frames_ == pcm.frames && bits_ == pcm.bits && channels_ == pcm.channels &&
         !tree_.empty();
}
void SampleWaveform::prepare(SamplePCMView pcm) {
  if (matches(pcm))
    return;
  const size_t leaves = (size_t(pcm.frames) + samplePatchFrames - 1) / samplePatchFrames;
  size_t nextBase = 1;
  while (nextBase < leaves)
    nextBase *= 2;
  std::vector<Peak> next(nextBase * 2);
  tree_.swap(next);
  base_ = nextBase;
  source_ = pcm.data.data();
  frames_ = pcm.frames;
  bits_ = pcm.bits;
  channels_ = pcm.channels;
  for (size_t i = 0; i < leaves; ++i)
    leaf(pcm, i);
  for (size_t i = base_ - 1; i > 0; --i) {
    tree_[i] = tree_[2 * i];
    combine(tree_[i], tree_[2 * i + 1]);
  }
}
void SampleWaveform::changed(SamplePCMView pcm, uint32_t first, uint32_t last) noexcept {
  if (!matches(pcm) || first >= last)
    return;
  for (size_t i = first / samplePatchFrames; i <= (last - 1) / samplePatchFrames; ++i) {
    leaf(pcm, i);
    for (size_t node = (base_ + i) / 2; node; node /= 2) {
      tree_[node] = tree_[2 * node];
      combine(tree_[node], tree_[2 * node + 1]);
    }
  }
}
std::vector<float> SampleWaveform::query(SamplePCMView pcm, uint32_t first, uint32_t last, size_t bins,
                                         SampleChannels channels) {
  pcm.validate();
  if (first > last || last > pcm.frames || bins > 16384)
    throw std::invalid_argument("Invalid waveform range or bin count");
  if (channels != SampleChannels::Both && channels != SampleChannels::Left && channels != SampleChannels::Right)
    throw std::invalid_argument("Unknown waveform channel selection");
  if (channels == SampleChannels::Right && pcm.channels != 2)
    throw std::invalid_argument("This sample has no right channel");
  std::vector<float> result(bins * 2, 0);
  if (first == last || !bins)
    return result;
  prepare(pcm);
  for (size_t b = 0; b < bins; ++b) {
    uint32_t begin = first + uint32_t(uint64_t(b) * (last - first) / bins),
             end = first + uint32_t(uint64_t(b + 1) * (last - first) / bins);
    end = std::min(last, std::max(begin + 1, end));
    Peak peak;
    const auto head =
        std::min<uint64_t>(end, uint64_t(begin) + (samplePatchFrames - begin % samplePatchFrames) % samplePatchFrames);
    read(peak, pcm, begin, uint32_t(head));
    begin = uint32_t(head);
    const auto tail = std::max(begin, end - end % samplePatchFrames);
    read(peak, pcm, tail, end);
    end = tail;
    for (size_t left = base_ + begin / samplePatchFrames, right = base_ + end / samplePatchFrames; left < right;
         left /= 2, right /= 2) {
      if (left & 1)
        combine(peak, tree_[left++]);
      if (right & 1)
        combine(peak, tree_[--right]);
    }
    int32_t low = INT32_MAX, high = INT32_MIN;
    for (uint8_t c = 0; c < pcm.channels; ++c)
      if (channels == SampleChannels::Both || (channels == SampleChannels::Left ? c == 0 : c == 1)) {
        low = std::min(low, peak.low[c]);
        high = std::max(high, peak.high[c]);
      }
    result[2 * b] = float(low / pcm.scale());
    result[2 * b + 1] = float(high / pcm.scale());
  }
  return result;
}
} // namespace Tracker
