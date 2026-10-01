#include "SampleClipboard.hpp"
#include "r8brain/CDSPResampler.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <numeric>
#include <stdexcept>

namespace Tracker {
namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::invalid_argument(message);
}
void put(std::vector<std::byte> &bytes, size_t at, uint8_t bits, int32_t value) {
  if (bits == 8) {
    auto x = int8_t(value);
    std::memcpy(bytes.data() + at, &x, 1);
  } else {
    auto x = int16_t(value);
    std::memcpy(bytes.data() + at, &x, 2);
  }
}
bool selected(SampleChannels choice, uint8_t c) {
  return choice == SampleChannels::Both || (choice == SampleChannels::Left ? c == 0 : c == 1);
}
void channelsValid(SampleChannels choice, uint8_t channels) {
  require(choice == SampleChannels::Both || choice == SampleChannels::Left || choice == SampleChannels::Right,
          "Unknown sample channel selection");
  require(choice != SampleChannels::Right || channels == 2, "This sample has no right channel");
}
void finish(SampleSplicePlan &plan) {
  auto &r = plan.result;
  const size_t stride = size_t(plan.bits / 8) * plan.channels;
  SamplePCMView before{plan.removed, r.removedFrames, plan.bits, plan.channels},
      after{plan.inserted, r.insertedFrames, plan.bits, plan.channels};
  r.resultFrames = plan.originalFrames - r.removedFrames + r.insertedFrames;
  r.historyBytes = plan.removed.size() + plan.inserted.size();
  r.preview.reserve(maximumSamplePreview);
  for (uint32_t f = 0; f < std::max(r.removedFrames, r.insertedFrames); ++f)
    for (uint8_t c = 0; c < plan.channels; ++c) {
      const auto a = f < r.removedFrames ? std::optional(before.value(f, c)) : std::nullopt;
      const auto b = f < r.insertedFrames ? std::optional(after.value(f, c)) : std::nullopt;
      if (b)
        r.peakAfter = std::max(r.peakAfter, std::abs(double(*b)) / after.scale());
      if (a != b) {
        ++r.changedSamples;
        if (r.preview.size() < maximumSamplePreview)
          r.preview.push_back({r.first + f, c, a, b});
      }
    }
  require(plan.removed.size() == size_t(r.removedFrames) * stride &&
              plan.inserted.size() == size_t(r.insertedFrames) * stride,
          "Invalid prepared splice layout");
}
// Resampling is prepared on the document worker. r8brain compensates its own
// algorithmic latency. Constant endpoint extension avoids artificial edge steps.
template <class Consume>
void resampledFrames(SamplePCMView source, uint32_t sourceRate, uint32_t targetRate, uint32_t frames, Consume consume) {
  if (sourceRate == targetRate) {
    for (uint32_t f = 0; f < frames; ++f)
      consume(f, source.value(f, 0) / source.scale(), source.value(f, source.channels - 1) / source.scale());
    return;
  }
  constexpr uint32_t capacity = 4096;
  const auto block =
      uint32_t(std::max<uint64_t>(1, std::min<uint64_t>(capacity, uint64_t(capacity) * sourceRate / targetRate)));
  r8b::CDSPResampler24 left(sourceRate, targetRate, int(block)), right(sourceRate, targetRate, int(block));
  std::array<double, capacity> a{}, b{};
  // Warm the complete impulse response, even at extreme ratios. Whole rational
  // periods keep the first real input frame aligned with an exact output frame.
  const uint64_t period = sourceRate / std::gcd(sourceRate, targetRate);
  const uint64_t warm = std::max<uint64_t>(sourceRate, uint64_t(left.getInputRequiredForOutput(1)) * 2);
  const uint64_t padding = (warm + period - 1) / period * period;
  uint64_t skip = padding * targetRate / sourceRate;
  require(skip + frames <= INT_MAX, "Resampler output position exceeds its supported range");
  const uint64_t required = uint64_t(left.getInputRequiredForOutput(int(skip + frames))) + block;
  uint64_t supplied = 0;
  uint32_t written = 0;
  auto feed = [&](uint32_t count) {
    supplied += count;
    double *l = nullptr, *r = nullptr;
    const auto produced = left.process(a.data(), int(count), l);
    if (source.channels == 2) {
      if (right.process(b.data(), int(count), r) != produced)
        throw std::runtime_error("Stereo resampler lost alignment");
    } else
      r = l;
    const auto ignored = std::min<uint64_t>(skip, produced);
    skip -= ignored;
    for (int i = int(ignored); i < produced && written < frames; ++i) {
      consume(written, l[i], r[i]);
      ++written;
    }
  };
  a.fill(source.value(0, 0) / source.scale());
  b.fill(source.value(0, source.channels - 1) / source.scale());
  for (uint64_t remaining = padding; remaining;) {
    const auto n = uint32_t(std::min<uint64_t>(remaining, block));
    feed(n);
    remaining -= n;
  }
  for (uint32_t first = 0; first < source.frames;) {
    const auto n = std::min(source.frames - first, block);
    for (uint32_t i = 0; i < n; ++i) {
      a[i] = source.value(first + i, 0) / source.scale();
      b[i] = source.value(first + i, source.channels - 1) / source.scale();
    }
    feed(n);
    first += n;
  }
  a.fill(source.value(source.frames - 1, 0) / source.scale());
  b.fill(source.value(source.frames - 1, source.channels - 1) / source.scale());
  while (written < frames) {
    if (supplied > required)
      throw std::runtime_error("Sample resampler did not complete");
    feed(block);
  }
}
} // namespace
SamplePCMView SampleClipboard::pcm() const {
  const size_t stride = size_t(bits / 8) * channels;
  return {data, stride ? uint32_t(data.size() / stride) : 0, bits, channels};
}
void SampleClipboard::validate() const {
  require(!data.empty() && data.size() <= maximumSampleClipboardBytes && rate >= 100 && rate <= 768000,
          "Clipboard audio is empty, oversized or has an invalid sample rate");
  pcm().validate();
}
SampleClipboard copySamplePCM(SamplePCMView source, uint32_t rate, uint32_t first, uint32_t last, SampleChannels choice,
                              std::string name) {
  source.validate();
  channelsValid(choice, source.channels);
  require(first < last && last <= source.frames, "Copy requires a nonempty frame range inside the sample");
  SampleClipboard clip;
  clip.bits = source.bits;
  clip.channels = choice == SampleChannels::Both ? source.channels : 1;
  clip.rate = rate;
  clip.name = std::move(name);
  const size_t stride = size_t(clip.bits / 8) * clip.channels, length = size_t(last - first) * stride;
  require(length <= maximumSampleClipboardBytes, "Clipboard exceeds 128 MiB; select a shorter region");
  clip.data.resize(length);
  if (choice == SampleChannels::Both)
    std::memcpy(clip.data.data(), source.data.data() + size_t(first) * source.stride(), length);
  else
    for (uint32_t f = first; f < last; ++f)
      put(clip.data, size_t(f - first) * stride, clip.bits, source.value(f, choice == SampleChannels::Right ? 1 : 0));
  clip.validate();
  return clip;
}
SampleSplicePlan planSamplePaste(SamplePCMView destination, uint32_t rate, const SampleClipboard &source,
                                 const SamplePasteOptions &o) {
  destination.validate();
  source.validate();
  channelsValid(o.channels, destination.channels);
  require(rate >= 100 && rate <= 768000, "Destination sample rate is outside the paste range");
  require(o.at <= destination.frames, "Paste position must be inside the sample or at its end");
  const bool structural = o.mode == SamplePasteMode::Insert || o.mode == SamplePasteMode::Replace;
  require(structural || o.mode == SamplePasteMode::Overwrite || o.mode == SamplePasteMode::Mix, "Unknown paste mode");
  require(!structural || o.channels == SampleChannels::Both, "Insert and Replace change both channels together");
  require(o.mode == SamplePasteMode::Replace ? o.end >= o.at && o.end <= destination.frames : o.end == 0,
          "An exclusive end is used only for Replace");
  require(o.rateMode == SampleRateMode::Resample || o.rateMode == SampleRateMode::KeepFrames, "Unknown rate mode");
  require(std::isfinite(o.sourceGainDB) && o.sourceGainDB >= -96 && o.sourceGainDB <= 24 &&
              std::isfinite(o.destinationGainDB) && o.destinationGainDB >= -96 && o.destinationGainDB <= 24 &&
              (o.mode == SamplePasteMode::Mix || o.destinationGainDB == 0),
          "Paste gain is invalid for this mode");
  const auto input = source.pcm();
  const uint32_t effectiveRate = o.rateMode == SampleRateMode::Resample ? rate : source.rate;
  const auto frames64 = std::max<uint64_t>(1, (uint64_t(input.frames) * effectiveRate + source.rate / 2) / source.rate);
  require(frames64 * destination.stride() <= maximumSampleClipboardBytes, "Converted clipboard exceeds 128 MiB");
  const uint32_t frames = uint32_t(frames64);
  SampleSplicePlan plan;
  plan.originalFrames = destination.frames;
  plan.bits = destination.bits;
  plan.channels = destination.channels;
  auto &r = plan.result;
  r.first = o.at;
  r.insertedFrames = frames;
  r.removedFrames = o.mode == SamplePasteMode::Insert    ? 0
                    : o.mode == SamplePasteMode::Replace ? o.end - o.at
                                                         : std::min(frames, destination.frames - o.at);
  require(uint64_t(r.removedFrames) * destination.stride() <= maximumSampleClipboardBytes,
          "Replaced region exceeds 128 MiB; select a shorter region");
  require(uint64_t(destination.frames) - r.removedFrames + frames <= 0x10000000u &&
              (uint64_t(destination.frames) - r.removedFrames + frames) * destination.stride() <= 512 * 1024 * 1024,
          "Resulting sample exceeds the native frame or 512 MiB limit");
  plan.removed.assign(destination.data.begin() + size_t(o.at) * destination.stride(),
                      destination.data.begin() + size_t(o.at + r.removedFrames) * destination.stride());
  plan.inserted.resize(size_t(frames) * destination.stride());
  if (!structural && r.removedFrames)
    std::copy(plan.removed.begin(), plan.removed.end(), plan.inserted.begin());
  const double sourceGain = std::pow(10., o.sourceGainDB / 20), destGain = std::pow(10., o.destinationGainDB / 20);
  const bool monoTarget = destination.channels == 1 || o.channels != SampleChannels::Both;
  auto write = [&](uint32_t f, uint8_t c, double v) {
    double result = v * sourceGain;
    if (o.mode == SamplePasteMode::Mix && f < r.removedFrames)
      result += destination.value(o.at + f, c) / destination.scale() * destGain;
    result *= destination.scale();
    const double minimum = -destination.scale(), maximum = destination.scale() - 1;
    if (result < minimum || result > maximum)
      ++r.clippedSamples;
    const auto rounded = int32_t(std::round(std::clamp(result, minimum, maximum)));
    put(plan.inserted, size_t(f) * destination.stride() + c * (destination.bits / 8), destination.bits, rounded);
  };
  resampledFrames(input, source.rate, effectiveRate, frames, [&](uint32_t f, double l, double r) {
    for (uint8_t c = 0; c < destination.channels; ++c)
      if (selected(o.channels, c))
        write(f, c, monoTarget ? (l + r) * .5 : c ? r : l);
  });
  finish(plan);
  return plan;
}
SampleSplicePlan planSampleErase(SamplePCMView pcm, uint32_t first, uint32_t last) {
  pcm.validate();
  require(first < last && last <= pcm.frames, "Delete requires a nonempty frame range inside the sample");
  require(uint64_t(last - first) * pcm.stride() <= maximumSampleClipboardBytes, "Deleted region exceeds 128 MiB");
  SampleSplicePlan plan;
  plan.originalFrames = pcm.frames;
  plan.bits = pcm.bits;
  plan.channels = pcm.channels;
  plan.result.first = first;
  plan.result.removedFrames = last - first;
  plan.removed.assign(pcm.data.begin() + size_t(first) * pcm.stride(), pcm.data.begin() + size_t(last) * pcm.stride());
  finish(plan);
  return plan;
}
SamplePCMPlan sampleSplicePatches(const SampleSplicePlan &s) {
  require(!s.changesLength(), "Length-changing splice cannot be a fixed PCM patch");
  SamplePCMPlan p;
  p.totalFrames = s.originalFrames;
  p.bits = s.bits;
  p.channels = s.channels;
  const size_t stride = size_t(s.bits / 8) * s.channels;
  for (uint32_t f = 0; f < s.result.insertedFrames;) {
    auto n = std::min(s.result.insertedFrames - f, samplePatchFrames - (s.result.first + f) % samplePatchFrames);
    const auto start = size_t(f) * stride, size = size_t(n) * stride;
    if (std::memcmp(s.removed.data() + start, s.inserted.data() + start, size)) {
      SamplePCMChunk chunk;
      chunk.first = s.result.first + f;
      chunk.frames = n;
      chunk.before.assign(s.removed.begin() + start, s.removed.begin() + start + size);
      chunk.after.assign(s.inserted.begin() + start, s.inserted.begin() + start + size);
      p.chunks.push_back(std::move(chunk));
    }
    f += n;
  }
  return p;
}
} // namespace Tracker
