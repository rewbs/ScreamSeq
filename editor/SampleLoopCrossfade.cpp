#include "SampleLoopCrossfade.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <stdexcept>

namespace Tracker {
SampleCrossfadePlan planSampleCrossfade(SamplePCMView pcm, uint32_t start, uint32_t end,
                                       const SampleCrossfadeOptions &o) {
  pcm.validate();
  auto require = [](bool ok, const char *why) { if (!ok) throw std::invalid_argument(why); };
  require(start < end && end <= pcm.frames, "Crossfade needs a valid nonempty loop inside the sample");
  require(o.frames >= 2 && o.frames <= maximumSampleCrossfadeFrames, "Crossfade length must be 2...1048576 frames");
  require(o.mode == SampleCrossfadeMode::Preserve || o.mode == SampleCrossfadeMode::Overlap, "Unknown crossfade mode");
  require(o.curve == SampleCrossfadeCurve::Linear || o.curve == SampleCrossfadeCurve::EqualPower, "Unknown crossfade curve");
  require(o.frames <= end - start, "Crossfade is longer than the loop");
  if (o.mode == SampleCrossfadeMode::Preserve)
    require(o.frames <= start, "Preserve mode needs enough audio before the loop start; use overlap or move the start");
  else
    require(uint64_t(o.frames) * 2 <= end - start, "Overlap requires a loop at least twice the fade length");
  SampleCrossfadePlan plan;
  plan.sourceStart = o.mode == SampleCrossfadeMode::Preserve ? start - o.frames : start;
  plan.loopStart = o.mode == SampleCrossfadeMode::Preserve ? start : start + o.frames;
  auto &patches = plan.pcm;
  patches.totalFrames = pcm.frames; patches.bits = pcm.bits; patches.channels = pcm.channels;
  auto &r = patches.result;
  r.first = end - o.frames; r.last = end;
  r.preview.reserve(maximumSamplePreview);
  for (uint32_t first = r.first; first < r.last;) {
    const auto last = std::min(r.last, first + samplePatchFrames - first % samplePatchFrames);
    SamplePCMChunk chunk;
    chunk.first = first; chunk.frames = last - first;
    chunk.before.assign(pcm.data.begin() + size_t(first) * pcm.stride(), pcm.data.begin() + size_t(last) * pcm.stride());
    chunk.after = chunk.before;
    bool changed = false;
    for (uint32_t frame = first; frame < last; ++frame) {
      const auto i = frame - r.first;
      const double t = double(i) / double(o.frames - 1);
      // Exact endpoint weights avoid trigonometric residue and preserve seam samples.
      const double in = !i ? 0 : i == o.frames - 1 ? 1 : o.curve == SampleCrossfadeCurve::Linear ? t : std::sin(t * std::numbers::pi / 2);
      const double out = !i ? 1 : i == o.frames - 1 ? 0 : o.curve == SampleCrossfadeCurve::Linear ? 1 - t : std::cos(t * std::numbers::pi / 2);
      bool changedFrame = false;
      for (uint8_t channel = 0; channel < pcm.channels; ++channel) {
        const auto before = pcm.value(frame, channel), incoming = pcm.value(plan.sourceStart + i, channel);
        const int64_t denominator = o.frames - 1;
        const int64_t numerator = int64_t(before) * (denominator - i) + int64_t(incoming) * i;
        const double value = o.curve == SampleCrossfadeCurve::Linear ? double(numerator) / denominator
                                                                   : before * out + incoming * in;
        // Linear weights are exact rational integers: rounding ties cannot drift with frame count.
        const auto after = o.curve == SampleCrossfadeCurve::Linear
          ? int32_t(numerator < 0 ? -((-numerator + denominator / 2) / denominator) : (numerator + denominator / 2) / denominator)
          : int32_t(std::round(std::clamp(value, -pcm.scale(), pcm.scale() - 1)));
        r.clippedSamples += value < -pcm.scale() || value > pcm.scale() - 1;
        r.peakBefore = std::max(r.peakBefore, std::abs(double(before)) / pcm.scale());
        r.peakAfter = std::max(r.peakAfter, std::abs(double(after)) / pcm.scale());
        if (after == before) continue;
        changed = changedFrame = true; ++r.changedSamples;
        auto *target = chunk.after.data() + size_t(frame - first) * pcm.stride() + channel * (pcm.bits / 8);
        if (pcm.bits == 8) { const auto v = int8_t(after); std::memcpy(target, &v, 1); }
        else { const auto v = int16_t(after); std::memcpy(target, &v, 2); }
        if (r.preview.size() < maximumSamplePreview) r.preview.push_back({frame, channel, before, after});
      }
      r.changedFrames += changedFrame;
    }
    if (changed) {
      r.historyBytes += sizeof(SamplePCMChunk) + chunk.before.size() + chunk.after.size();
      patches.chunks.push_back(std::move(chunk));
    }
    first = last;
  }
  return plan;
}
} // namespace Tracker
