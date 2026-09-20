#include "SampleProcessing.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace Tracker {
SamplePCMPlan planSampleDraw(SamplePCMView pcm, const SampleDrawOptions &o) {
  auto require = [](bool ok, const char *why) {
    if (!ok)
      throw std::invalid_argument(why);
  };
  pcm.validate();
  require(!o.points.empty() && o.points.size() <= maximumSampleDrawPoints, "Drawing needs 1...4096 ordered points");
  require(o.channels == SampleChannels::Both || o.channels == SampleChannels::Left ||
              o.channels == SampleChannels::Right,
          "Unknown drawing channel selection");
  require(o.channels != SampleChannels::Right || pcm.channels == 2, "This sample has no right channel");
  require(o.interpolation == SampleDrawInterpolation::Linear || o.interpolation == SampleDrawInterpolation::Step,
          "Unknown drawing interpolation");
  uint32_t previous = 0;
  for (size_t i = 0; i < o.points.size(); ++i) {
    const auto &p = o.points[i];
    require(p.frame < pcm.frames && (!i || p.frame > previous),
            "Drawing frame positions must be distinct, increasing and inside the sample");
    require(std::isfinite(p.value) && p.value >= -1 && p.value <= 1,
            "Drawing amplitudes must be finite and between -1 and 1");
    previous = p.frame;
  }
  require(uint64_t(o.points.back().frame) - o.points.front().frame + 1 <= maximumSampleDrawFrames,
          "A drawing gesture spans at most 1048576 frames");
  SamplePCMPlan plan;
  plan.totalFrames = pcm.frames;
  plan.bits = pcm.bits;
  plan.channels = pcm.channels;
  auto &r = plan.result;
  r.first = o.points.front().frame;
  r.last = o.points.back().frame + 1;
  r.preview.reserve(maximumSamplePreview);
  size_t point = 0;
  for (uint32_t first = r.first; first < r.last;) {
    const auto end = std::min(r.last, first + samplePatchFrames - first % samplePatchFrames);
    SamplePCMChunk chunk;
    chunk.first = first;
    chunk.frames = end - first;
    chunk.before.assign(pcm.data.begin() + size_t(first) * pcm.stride(), pcm.data.begin() + size_t(end) * pcm.stride());
    chunk.after = chunk.before;
    bool changed = false;
    for (uint32_t frame = first; frame < end; ++frame) {
      while (point + 1 < o.points.size() && o.points[point + 1].frame <= frame)
        ++point;
      const auto &a = o.points[point];
      double value = a.value;
      if (o.interpolation == SampleDrawInterpolation::Linear && point + 1 < o.points.size()) {
        const auto &b = o.points[point + 1];
        value = std::lerp(a.value, b.value, double(frame - a.frame) / double(b.frame - a.frame));
      }
      value *= pcm.scale();
      const auto after = int32_t(std::round(std::clamp(value, -pcm.scale(), pcm.scale() - 1)));
      bool changedFrame = false;
      for (uint8_t channel = 0; channel < pcm.channels; ++channel) {
        if (o.channels != SampleChannels::Both && channel != (o.channels == SampleChannels::Left ? 0 : 1))
          continue;
        const auto before = pcm.value(frame, channel);
        r.clippedSamples += value < -pcm.scale() || value > pcm.scale() - 1;
        r.peakBefore = std::max(r.peakBefore, std::abs(double(before)) / pcm.scale());
        r.peakAfter = std::max(r.peakAfter, std::abs(double(after)) / pcm.scale());
        if (before == after)
          continue;
        changed = changedFrame = true;
        ++r.changedSamples;
        auto *target = chunk.after.data() + size_t(frame - first) * pcm.stride() + channel * (pcm.bits / 8);
        if (pcm.bits == 8) {
          const auto sample = int8_t(after);
          std::memcpy(target, &sample, 1);
        } else {
          const auto sample = int16_t(after);
          std::memcpy(target, &sample, 2);
        }
        if (r.preview.size() < maximumSamplePreview)
          r.preview.push_back({frame, channel, before, after});
      }
      r.changedFrames += changedFrame;
    }
    if (changed) {
      r.historyBytes += sizeof(SamplePCMChunk) + chunk.before.size() + chunk.after.size();
      plan.chunks.push_back(std::move(chunk));
    }
    first = end;
  }
  return plan;
}
} // namespace Tracker
