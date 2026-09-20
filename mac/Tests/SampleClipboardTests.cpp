#include "editor/TrackerDocument.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <random>
using namespace Tracker;
static void check(bool v, const char *message) {
  if (!v)
    throw std::runtime_error(message);
}
template <class F> static void rejects(F f) {
  bool failed = false;
  try {
    f();
  } catch (const std::exception &) {
    failed = true;
  }
  check(failed, "Expected invalid edit rejection");
}
static SampleClipboard clip(int bits, int channels, int frames, uint32_t seed = 99) {
  SampleClipboard s;
  s.bits = bits;
  s.channels = channels;
  s.rate = 44100;
  s.data.resize(size_t(frames) * channels * bits / 8);
  std::mt19937 rng(seed);
  for (int i = 0; i < frames * channels; ++i) {
    const auto v = rng();
    if (bits == 8) {
      auto n = int8_t(v);
      std::memcpy(s.data.data() + i, &n, 1);
    } else {
      auto n = int16_t(v);
      std::memcpy(s.data.data() + i * 2, &n, 2);
    }
  }
  return s;
}
static std::vector<int32_t> values(SamplePCMView s) {
  std::vector<int32_t> result;
  for (uint32_t f = 0; f < s.frames; ++f)
    for (uint8_t c = 0; c < s.channels; ++c)
      result.push_back(s.value(f, c));
  return result;
}
static void install(Document &d, const SampleClipboard &clip) {
  auto &s = d.song().GetSample(1);
  s.FreeSample();
  s.Initialize(d.song().GetType());
  s.uFlags.set(CHN_16BIT, clip.bits == 16);
  s.uFlags.set(CHN_STEREO, clip.channels == 2);
  s.nLength = clip.pcm().frames;
  s.nC5Speed = clip.rate;
  if (d.song().GetType() & (MOD_TYPE_MOD | MOD_TYPE_XM))
    s.FrequencyToTranspose();
  check(s.AllocateSample() != 0, "Allocate sample fixture");
  std::memcpy(s.sampleb(), clip.data.data(), clip.data.size());
  s.PrecomputeLoops(d.song(), false);
  d.song().m_nSamples = 1;
  auto native = d.native();
  native.reconcile(d.song());
  d.restoreNative(std::move(native));
}
static std::vector<std::byte> pcm(Document &d) {
  auto &s = d.song().GetSample(1);
  if (!s.nLength)
    return {};
  return {s.sampleb(), s.sampleb() + size_t(s.nLength) * s.GetBytesPerSample()};
}
int main() {
  try {
    size_t cases = 0;
    for (int sourceBits : {8, 16})
      for (int destBits : {8, 16})
        for (int sourceChannels : {1, 2})
          for (int destChannels : {1, 2}) {
            const auto source = clip(sourceBits, sourceChannels, 521), dest = clip(destBits, destChannels, 1031, 871);
            for (auto choice : {SampleChannels::Both, SampleChannels::Left, SampleChannels::Right}) {
              if (destChannels == 1 && choice == SampleChannels::Right)
                continue;
              for (auto mode : {SamplePasteMode::Insert, SamplePasteMode::Replace, SamplePasteMode::Overwrite,
                                SamplePasteMode::Mix}) {
                if (choice != SampleChannels::Both &&
                    (mode == SamplePasteMode::Insert || mode == SamplePasteMode::Replace))
                  continue;
                for (uint32_t at : {0u, 255u, 997u, 1031u}) {
                  SamplePasteOptions o;
                  o.at = at;
                  o.mode = mode;
                  o.channels = choice;
                  o.end = mode == SamplePasteMode::Replace ? std::min(at + 33, 1031u) : 0;
                  o.sourceGainDB = mode == SamplePasteMode::Mix ? 6 : 0;
                  o.destinationGainDB = mode == SamplePasteMode::Mix ? -6 : 0;
                  auto plan = planSamplePaste(dest.pcm(), dest.rate, source, o);
                  const uint32_t removed = mode == SamplePasteMode::Insert    ? 0
                                           : mode == SamplePasteMode::Replace ? o.end - at
                                                                              : std::min(521u, 1031 - at);
                  auto expected = values(dest.pcm());
                  auto original = expected;
                  std::vector<int32_t> inserted;
                  uint64_t clips = 0;
                  for (uint32_t f = 0; f < 521; ++f)
                    for (int c = 0; c < destChannels; ++c) {
                      int old = f < removed ? original[(at + f) * destChannels + c] : 0;
                      if ((choice == SampleChannels::Left && c == 1) || (choice == SampleChannels::Right && c == 0)) {
                        inserted.push_back(old);
                        continue;
                      }
                      double x;
                      if (sourceChannels == 1)
                        x = source.pcm().value(f, 0) / source.pcm().scale();
                      else if (destChannels == 1 || choice != SampleChannels::Both)
                        x = (source.pcm().value(f, 0) + double(source.pcm().value(f, 1))) / (2 * source.pcm().scale());
                      else
                        x = source.pcm().value(f, c) / source.pcm().scale();
                      x *= std::pow(10., o.sourceGainDB / 20);
                      if (mode == SamplePasteMode::Mix)
                        x += old / dest.pcm().scale() * std::pow(10., o.destinationGainDB / 20);
                      x *= dest.pcm().scale();
                      const auto scale = dest.pcm().scale();
                      clips += x < -scale || x > scale - 1;
                      inserted.push_back(int32_t(std::round(std::clamp(x, -scale, scale - 1))));
                    }
                  expected.erase(expected.begin() + at * destChannels,
                                 expected.begin() + (at + removed) * destChannels);
                  expected.insert(expected.begin() + at * destChannels, inserted.begin(), inserted.end());
                  std::vector<std::byte> actual = dest.data;
                  actual.erase(actual.begin() + at * dest.pcm().stride(),
                               actual.begin() + (at + removed) * dest.pcm().stride());
                  actual.insert(actual.begin() + at * dest.pcm().stride(), plan.inserted.begin(), plan.inserted.end());
                  check(values({actual, plan.result.resultFrames, uint8_t(destBits), uint8_t(destChannels)}) ==
                            expected,
                        "Paste agrees with independent PCM reference");
                  check(plan.result.clippedSamples == clips && plan.result.removedFrames == removed &&
                            plan.result.insertedFrames == 521,
                        "Exact paste geometry and clipping");
                  size_t changes = 0, preview = 0;
                  for (uint32_t f = 0; f < std::max(removed, 521u); ++f)
                    for (int c = 0; c < destChannels; ++c) {
                      std::optional<int> a =
                          f < removed ? std::optional(original[(at + f) * destChannels + c]) : std::nullopt;
                      std::optional<int> b = f < 521 ? std::optional(inserted[f * destChannels + c]) : std::nullopt;
                      if (a != b) {
                        ++changes;
                        if (preview < maximumSamplePreview) {
                          const auto &v = plan.result.preview.at(preview++);
                          check(v.frame == at + f && v.channel == c && v.before == a && v.after == b,
                                "Independent exact preview");
                        }
                      }
                    }
                  check(plan.result.changedSamples == changes && plan.result.preview.size() == preview,
                        "Preview bounds/counts");
                  ++cases;
                }
              }
            }
            for (auto ch : {SampleChannels::Both, SampleChannels::Left, SampleChannels::Right}) {
              if (sourceChannels == 1 && ch == SampleChannels::Right)
                continue;
              auto copied = copySamplePCM(source.pcm(), source.rate, 5, 300, ch);
              for (uint32_t f = 0; f < 295; ++f)
                for (uint8_t c = 0; c < copied.channels; ++c)
                  check(copied.pcm().value(f, c) == source.pcm().value(f + 5, ch == SampleChannels::Both    ? c
                                                                              : ch == SampleChannels::Right ? 1
                                                                                                            : 0),
                        "Exact channel copy");
            }
          }
    for (auto type : {MOD_TYPE_MOD, MOD_TYPE_XM, MOD_TYPE_S3M, MOD_TYPE_IT, MOD_TYPE_MPT}) {
      Document d(type);
      auto original = clip(type == MOD_TYPE_MOD ? 8 : 16, type == MOD_TYPE_MOD ? 1 : 2, 1024);
      install(d, original);
      auto &s = d.song().GetSample(1);
      s.nLoopStart = 100;
      s.nLoopEnd = 900;
      s.nSustainStart = 200;
      s.nSustainEnd = 800;
      s.uFlags.set(CHN_LOOP | CHN_SUSTAINLOOP | CHN_PINGPONGLOOP | CHN_PINGPONGSUSTAIN);
      s.cues = {0, 100, 199, 200, 799, 800, 900, 1024, MAX_SAMPLE_LENGTH};
      s.PrecomputeLoops(d.song(), false);
      const auto cues = s.cues;
      const auto flags = s.uFlags;
      auto small = d.copySample(1, 7, 18, SampleChannels::Both);
      SamplePasteOptions o;
      o.at = 100;
      o.rateMode = SampleRateMode::KeepFrames;
      auto p = d.prepareSamplePaste(1, small, o);
      check(p.hasChanges(), "Prepare insertion");
      d.applySampleEdit(std::move(p));
      check(s.nLength == 1035 && s.nLoopStart == 111 && s.nLoopEnd == 911 && s.nSustainStart == 211 &&
                s.nSustainEnd == 811,
            "Insertion preserves original loop/sustain content at boundary");
      check(s.cues[1] == 111 && s.cues[7] == MAX_SAMPLE_LENGTH, "Cue positions and disabled sentinels");
      auto inserted = pcm(d);
      check(d.historyBytes() < 8192, "Small insertion has bounded history independent of sample size");
      d.undo();
      check(pcm(d) == original.data && s.cues == cues && s.uFlags == flags,
            "Exact insertion undo including cues/flags");
      d.redo();
      check(pcm(d) == inserted, "Exact insertion redo");
      d.applySampleEdit(d.prepareSampleErase(1, 90, 950));
      check(!s.uFlags[CHN_LOOP] && !s.uFlags[CHN_SUSTAINLOOP], "Deletion collapses covered loops");
      auto deleted = pcm(d);
      d.transaction([](auto &song) { song.m_songName = "Mixed history"; });
      d.undo();
      check(pcm(d) == deleted, "Structural song undo preserves splice PCM");
      d.undo();
      check(pcm(d) == inserted, "Undo deletion after song history");
      Document reopened(d.snapshotData());
      check(pcm(reopened) == inserted, "Native paste persistence");
      d.applySampleEdit(d.prepareSampleErase(1, 0, d.song().GetSample(1).nLength));
      check(d.song().GetSample(1).nLength == 0 && !d.song().GetSample(1).HasSampleData(),
            "Delete entire sample safely");
      d.undo();
      check(pcm(d) == inserted, "Undo entire sample deletion");
    }
    Document big;
    auto million = clip(16, 2, 1048576);
    install(big, million);
    big.waveform(1, 4096);
    auto reads = big.waveformReadFrames();
    SamplePasteOptions o;
    o.at = 8193;
    o.mode = SamplePasteMode::Overwrite;
    auto one = clip(16, 1, 1, 298);
    big.applySampleEdit(big.prepareSamplePaste(1, one, o));
    big.waveform(1, 4096);
    check(big.waveformReadFrames() == reads + 256 && big.historyBytes() < 8192,
          "Overwrite updates only one waveform tile and bounded history");
    big.undo();
    auto stale = big.prepareSamplePaste(1, one, o);
    big.annotate([](auto &n) { n.tracks.at(0).name = "edit"; });
    rejects([&] { big.applySampleEdit(std::move(stale)); });
    o.mode = SamplePasteMode::Insert;
    auto foreign = big.prepareSamplePaste(1, one, o);
    Document other;
    rejects([&] { other.applySampleEdit(std::move(foreign)); });
    auto tampered = big.prepareSamplePaste(1, one, o);
    big.song().GetSample(1).sample16()[0] ^= 7;
    rejects([&] { big.applySampleEdit(std::move(tampered)); });
    big.song().GetSample(1).sample16()[0] ^= 7;
    // Independent analytic resampling checks: duration, DC, phase, amplitude, channel isolation and alias rejection.
    double maxError = 0, maxAlias = 0;
    for (auto rates : {std::pair{22050u, 48000u}, std::pair{48000u, 44100u}, std::pair{96000u, 22050u}}) {
      auto wave = clip(16, 2, rates.first / 4);
      wave.rate = rates.first;
      for (uint32_t f = 0; f < wave.pcm().frames; ++f) {
        int16_t a = int16_t(std::round(12000 * std::sin(2 * 3.141592653589793 * 1000 * f / rates.first))), b = 4096;
        std::memcpy(wave.data.data() + f * 4, &a, 2);
        std::memcpy(wave.data.data() + f * 4 + 2, &b, 2);
      }
      SamplePCMView empty{{}, 0, 16, 2};
      SamplePasteOptions options;
      auto p = planSamplePaste(empty, rates.second, wave, options);
      check(p.result.resultFrames == uint32_t(std::round(wave.pcm().frames * double(rates.second) / rates.first)),
            "Resampled duration rounds to nearest frame");
      SamplePCMView result{p.inserted, p.result.resultFrames, 16, 2};
      for (uint32_t f = 1000; f + 1000 < result.frames; ++f) {
        const double reference = 12000 * std::sin(2 * 3.141592653589793 * 1000 * f / rates.second);
        maxError = std::max(maxError, std::abs(result.value(f, 0) - reference));
        check(result.value(f, 1) == 4096, "Resampling preserves DC and channel separation");
      }
      if (rates.first == 96000) {
        for (uint32_t f = 0; f < wave.pcm().frames; ++f) {
          int16_t a = int16_t(std::round(12000 * std::sin(2 * 3.141592653589793 * 18000 * f / rates.first)));
          std::memcpy(wave.data.data() + f * 4, &a, 2);
        }
        p = planSamplePaste(empty, rates.second, wave, options);
        result = {p.inserted, p.result.resultFrames, 16, 2};
        for (uint32_t f = 1000; f + 1000 < result.frames; ++f)
          maxAlias = std::max(maxAlias, double(std::abs(result.value(f, 0))));
      }
    }
    check(maxError < 2 && maxAlias < 2, "Resampling analytic accuracy and stopband alias rejection");
    // Endpoint extension must remain finite and preserve DC for tiny clips and
    // the most extreme accepted ratios, including rounded output shorter than a frame.
    for (auto rates : {std::pair{100u, 768000u}, std::pair{768000u, 100u}, std::pair{44100u, 48000u}}) {
      auto dc = clip(8, 1, 1);
      dc.data[0] = std::byte{64};
      dc.rate = rates.first;
      auto plan = planSamplePaste({{}, 0, 16, 2}, rates.second, dc, {});
      check(plan.result.insertedFrames ==
                std::max<uint32_t>(1, uint32_t(std::round(double(rates.second) / rates.first))),
            "One-frame conversion retains at least one frame");
      for (auto value : values({plan.inserted, plan.result.insertedFrames, 16, 2}))
        check(value == 16384, "Extreme-ratio endpoint extension preserves DC exactly");
    }
    auto fixture = clip(16, 2, 4);
    o = {};
    o.at = 5;
    rejects([&] { planSamplePaste(fixture.pcm(), 44100, one, o); });
    o.at = 0;
    o.channels = SampleChannels::Left;
    rejects([&] { planSamplePaste(fixture.pcm(), 44100, one, o); });
    o.channels = SampleChannels::Both;
    o.sourceGainDB = NAN;
    rejects([&] { planSamplePaste(fixture.pcm(), 44100, one, o); });
    rejects([&] { planSampleErase(fixture.pcm(), 1, 1); });
    std::cout << "PASS " << cases
              << " independent PCM paste cases, channel copy, exact loop/cue/history/persistence, bounded waveform "
                 "edits and resampling; maximum analytic error "
              << maxError << ", alias " << maxAlias << " PCM units\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL " << e.what() << '\n';
    return 1;
  }
}
