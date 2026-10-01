#include "SampleProcessing.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace Tracker {
void SamplePCMView::validate() const {
  if ((bits != 8 && bits != 16) || channels < 1 || channels > 2 || data.size() != size_t(frames) * stride())
    throw std::invalid_argument("Invalid native sample PCM layout");
}
int32_t SamplePCMView::value(uint32_t frame, uint8_t channel) const noexcept {
  const auto at = size_t(frame) * stride() + channel * (bits / 8);
  if (bits == 8) {
    int8_t v;
    std::memcpy(&v, data.data() + at, 1);
    return v;
  }
  int16_t v;
  std::memcpy(&v, data.data() + at, 2);
  return v;
}
namespace {
bool selected(SampleChannels choice, uint8_t channel) noexcept {
  return choice == SampleChannels::Both || (choice == SampleChannels::Left ? channel == 0 : channel == 1);
}
double fade(double x, const SampleProcessOptions &o) noexcept {
  if (o.curve == SampleFadeCurve::Smooth)
    return x * x * (3 - 2 * x);
  if (o.curve == SampleFadeCurve::Exponential)
    return std::pow(x, o.exponent);
  if (o.curve == SampleFadeCurve::Logarithmic)
    return 1 - std::pow(1 - x, o.exponent);
  return x;
}
void put(std::vector<std::byte> &data, size_t index, uint8_t bits, int32_t value) noexcept {
  if (bits == 8) {
    const auto x = int8_t(value);
    std::memcpy(data.data() + index, &x, 1);
  } else {
    const auto x = int16_t(value);
    std::memcpy(data.data() + index, &x, 2);
  }
}
} // namespace
SamplePCMPlan planSampleProcess(SamplePCMView pcm, const SampleProcessOptions &o) {
  pcm.validate();
  if (o.first >= o.last || o.last > pcm.frames)
    throw std::invalid_argument("Sample range must be nonempty with an exclusive end inside the sample");
  if (o.channels != SampleChannels::Both && o.channels != SampleChannels::Left && o.channels != SampleChannels::Right)
    throw std::invalid_argument("Unknown sample channel selection");
  if (o.channels == SampleChannels::Right && pcm.channels != 2)
    throw std::invalid_argument("This sample has no right channel");
  if (o.curve != SampleFadeCurve::Linear && o.curve != SampleFadeCurve::Smooth &&
      o.curve != SampleFadeCurve::Exponential && o.curve != SampleFadeCurve::Logarithmic)
    throw std::invalid_argument("Unknown sample fade curve");
  if (!std::isfinite(o.exponent) || o.exponent < .1 || o.exponent > 8 || !std::isfinite(o.gainDB) || o.gainDB < -96 ||
      o.gainDB > 24 || !std::isfinite(o.targetDB) || o.targetDB < -96 || o.targetDB > 0 || o.window < 3 ||
      o.window > 255 || o.window % 2 == 0)
    throw std::invalid_argument("Sample processing value is outside its range");
  const auto &op = o.operation;
  const bool channelOperation =
      op == "swap-channels" || op == "copy-left" || op == "copy-right" || op == "stereo-average";
  if (channelOperation && (pcm.channels != 2 || o.channels != SampleChannels::Both))
    throw std::invalid_argument("This operation requires a stereo sample and Both channels");
  if (!channelOperation && op != "reverse" && op != "normalize" && op != "silence" && op != "invert" &&
      op != "fade-in" && op != "fade-out" && op != "gain" && op != "remove-dc" && op != "smooth")
    throw std::invalid_argument("Unknown sample operation");
  SamplePCMPlan plan;
  plan.totalFrames = pcm.frames;
  plan.bits = pcm.bits;
  plan.channels = pcm.channels;
  auto &result = plan.result;
  result.first = o.first;
  result.last = o.last;
  result.preview.reserve(maximumSamplePreview);
  const auto length = o.last - o.first;
  const int32_t minimum = -int32_t(pcm.scale()), maximum = int32_t(pcm.scale()) - 1;
  std::array<int64_t, 2> sums{}, rolling{};
  int32_t peak = 0;
  for (uint32_t n = o.first; n < o.last; ++n)
    for (uint8_t c = 0; c < pcm.channels; ++c)
      if (selected(o.channels, c)) {
        const auto value = pcm.value(n, c);
        sums[c] += value;
        peak = std::max(peak, std::abs(value));
      }
  result.peakBefore = peak / pcm.scale();
  const double gain = std::pow(10., o.gainDB / 20),
               normalize = peak ? maximum * std::pow(10., o.targetDB / 20) / peak : 1;
  const auto half = int64_t(o.window / 2);
  auto smoothingSample = [&](int64_t frame, uint8_t channel) {
    return pcm.value(uint32_t(std::clamp(frame, int64_t(o.first), int64_t(o.last) - 1)), channel);
  };
  if (op == "smooth")
    for (int64_t j = -half; j <= half; ++j)
      for (uint8_t c = 0; c < pcm.channels; ++c)
        rolling[c] += smoothingSample(int64_t(o.first) + j, c);
  // Preparation allocates and evaluates every change before the document is
  // touched. Chunks align with waveform tiles and retain only modified regions.
  for (uint32_t first = o.first; first < o.last;) {
    const auto end =
        uint32_t(std::min<uint64_t>(o.last, uint64_t(first) + samplePatchFrames - first % samplePatchFrames));
    SamplePCMChunk chunk;
    chunk.first = first;
    chunk.frames = end - first;
    chunk.before.assign(pcm.data.begin() + size_t(first) * pcm.stride(), pcm.data.begin() + size_t(end) * pcm.stride());
    chunk.after = chunk.before;
    bool changed = false;
    for (uint32_t n = first; n < end; ++n) {
      bool changedFrame = false;
      for (uint8_t c = 0; c < pcm.channels; ++c)
        if (selected(o.channels, c)) {
          const auto before = pcm.value(n, c);
          double value = before;
          if (op == "reverse")
            value = pcm.value(o.last - 1 - (n - o.first), c);
          else if (op == "normalize")
            value *= normalize;
          else if (op == "silence")
            value = 0;
          else if (op == "invert")
            value = -value;
          else if (op == "gain")
            value *= gain;
          else if (op == "remove-dc")
            value -= double(sums[c]) / length;
          else if (op == "smooth")
            value = double(rolling[c]) / o.window;
          else if (op == "swap-channels")
            value = pcm.value(n, 1 - c);
          else if (op == "copy-left")
            value = pcm.value(n, 0);
          else if (op == "copy-right")
            value = pcm.value(n, 1);
          else if (op == "stereo-average")
            value = (double(pcm.value(n, 0)) + pcm.value(n, 1)) * .5;
          else {
            const double x = length == 1 ? 0 : double(n - o.first) / (length - 1);
            value *= length == 1 ? 0 : fade(op == "fade-in" ? x : 1 - x, o);
          }
          if (value < minimum || value > maximum)
            ++result.clippedSamples;
          const auto after = int32_t(std::round(std::clamp(value, double(minimum), double(maximum))));
          result.peakAfter = std::max(result.peakAfter, std::abs(after) / pcm.scale());
          if (after != before) {
            put(chunk.after, size_t(n - first) * pcm.stride() + c * (pcm.bits / 8), pcm.bits, after);
            changed = changedFrame = true;
            ++result.changedSamples;
            if (result.preview.size() < maximumSamplePreview)
              result.preview.push_back({n, c, before, after});
          }
        }
      if (changedFrame)
        ++result.changedFrames;
      if (op == "smooth")
        for (uint8_t c = 0; c < pcm.channels; ++c)
          rolling[c] += int64_t(smoothingSample(int64_t(n) + half + 1, c)) - smoothingSample(int64_t(n) - half, c);
    }
    if (changed) {
      result.historyBytes += sizeof(SamplePCMChunk) + chunk.before.size() + chunk.after.size();
      plan.chunks.push_back(std::move(chunk));
    }
    first = end;
  }
  return plan;
}
} // namespace Tracker
