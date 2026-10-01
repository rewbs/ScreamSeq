#include "editor/TrackerDocument.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <random>
using namespace Tracker;
static void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
template <class F> static void rejects(F f) {
  bool failed = false;
  try {
    f();
  } catch (const std::exception &) {
    failed = true;
  }
  check(failed, "Invalid operation must fail");
}
static std::vector<std::byte> bytes(const std::vector<int32_t> &values, uint8_t bits) {
  std::vector<std::byte> data(values.size() * bits / 8);
  for (size_t i = 0; i < values.size(); ++i) {
    if (bits == 8) {
      auto x = int8_t(values[i]);
      std::memcpy(data.data() + i, &x, 1);
    } else {
      auto x = int16_t(values[i]);
      std::memcpy(data.data() + i * 2, &x, 2);
    }
  }
  return data;
}
static SamplePCMView view(const std::vector<std::byte> &data, uint8_t bits, uint8_t channels) {
  return {data, uint32_t(data.size() / (bits / 8 * channels)), bits, channels};
}
static std::vector<int32_t> values(SamplePCMView pcm) {
  std::vector<int32_t> result;
  for (uint32_t f = 0; f < pcm.frames; ++f)
    for (uint8_t c = 0; c < pcm.channels; ++c)
      result.push_back(pcm.value(f, c));
  return result;
}
// Deliberately naive whole-selection oracle: no chunks, cache or rolling sums.
static std::vector<int32_t> reference(SamplePCMView pcm, const SampleProcessOptions &o, uint64_t &clipped) {
  auto result = values(pcm);
  auto input = result;
  const auto length = o.last - o.first;
  const double scale = pcm.scale();
  std::vector<uint8_t> selected;
  for (uint8_t c = 0; c < pcm.channels; ++c)
    if (o.channels == SampleChannels::Both || (o.channels == SampleChannels::Left ? c == 0 : c == 1))
      selected.push_back(c);
  double peak = 0;
  for (auto c : selected)
    for (auto i = o.first; i < o.last; ++i)
      peak = std::max(peak, double(std::abs(input[i * pcm.channels + c])));
  for (auto c : selected) {
    double mean = 0;
    for (auto i = o.first; i < o.last; ++i)
      mean += input[i * pcm.channels + c];
    mean /= length;
    for (auto i = o.first; i < o.last; ++i) {
      auto at = i * pcm.channels + c;
      double x = input[at];
      if (o.operation == "reverse")
        x = input[(o.first + o.last - 1 - i) * pcm.channels + c];
      else if (o.operation == "normalize")
        x = peak ? x * (scale - 1) * std::pow(10., o.targetDB / 20) / peak : x;
      else if (o.operation == "gain")
        x *= std::pow(10., o.gainDB / 20);
      else if (o.operation == "silence")
        x = 0;
      else if (o.operation == "invert")
        x = -x;
      else if (o.operation == "remove-dc")
        x -= mean;
      else if (o.operation == "smooth") {
        x = 0;
        for (int j = -int(o.window / 2); j <= int(o.window / 2); ++j)
          x += input[size_t(std::clamp(int64_t(i) + j, int64_t(o.first), int64_t(o.last) - 1)) * pcm.channels + c];
        x /= o.window;
      } else if (o.operation == "swap-channels")
        x = input[i * 2 + 1 - c];
      else if (o.operation == "copy-left")
        x = input[i * 2];
      else if (o.operation == "copy-right")
        x = input[i * 2 + 1];
      else if (o.operation == "stereo-average")
        x = (input[i * 2] + double(input[i * 2 + 1])) / 2;
      else {
        double t = length == 1 ? 0 : double(i - o.first) / (length - 1);
        if (o.operation == "fade-out" && length != 1)
          t = 1 - t;
        if (o.curve == SampleFadeCurve::Smooth)
          t = 3 * t * t - 2 * t * t * t;
        else if (o.curve == SampleFadeCurve::Exponential)
          t = std::pow(t, o.exponent);
        else if (o.curve == SampleFadeCurve::Logarithmic)
          t = 1 - std::pow(1 - t, o.exponent);
        x *= t;
      }
      if (x < -scale || x > scale - 1)
        ++clipped;
      result[at] = int32_t(std::round(std::clamp(x, -scale, scale - 1)));
    }
  }
  return result;
}
static void verifyPlan(SamplePCMView pcm, const SampleProcessOptions &o) {
  const auto original = std::vector<std::byte>(pcm.data.begin(), pcm.data.end());
  auto out = original;
  auto plan = planSampleProcess(pcm, o);
  uint64_t clips = 0;
  const auto expected = reference(pcm, o, clips);
  auto before = values(pcm);
  for (const auto &chunk : plan.chunks) {
    check(chunk.first >= o.first && chunk.first + chunk.frames <= o.last && chunk.frames <= samplePatchFrames,
          "Chunk bounds");
    check(chunk.before != chunk.after, "No unchanged chunks");
    check(std::equal(chunk.before.begin(), chunk.before.end(), original.begin() + chunk.first * pcm.stride()),
          "Exact before bytes");
    std::memcpy(out.data() + chunk.first * pcm.stride(), chunk.after.data(), chunk.after.size());
  }
  check(values(view(out, pcm.bits, pcm.channels)) == expected, "Processor agrees with independent PCM oracle");
  uint64_t samples = 0, frames = 0;
  double peakBefore = 0, peakAfter = 0;
  size_t preview = 0;
  for (uint32_t i = o.first; i < o.last; ++i) {
    bool changed = false;
    for (uint8_t c = 0; c < pcm.channels; ++c) {
      const auto at = i * pcm.channels + c;
      if (o.channels == SampleChannels::Both || (o.channels == SampleChannels::Left ? c == 0 : c == 1)) {
        peakBefore = std::max(peakBefore, std::abs(before[at]) / pcm.scale());
        peakAfter = std::max(peakAfter, std::abs(expected[at]) / pcm.scale());
      }
      if (before[at] != expected[at]) {
        changed = true;
        ++samples;
        if (preview < maximumSamplePreview) {
          auto v = plan.result.preview.at(preview++);
          check(v.frame == i && v.channel == c && v.before == before[at] && v.after == expected[at],
                "Exact bounded preview in frame/channel order");
        }
      }
    }
    if (changed)
      ++frames;
  }
  check(plan.result.changedFrames == frames && plan.result.changedSamples == samples &&
            plan.result.clippedSamples == clips,
        "Accurate change and clipping counts");
  check(plan.result.peakBefore == peakBefore && plan.result.peakAfter == peakAfter &&
            plan.result.preview.size() == preview,
        "Accurate peaks and preview length");
  check(std::equal(pcm.data.begin(), pcm.data.end(), original.begin()), "Planning never changes PCM");
}
static std::vector<float> waveformReference(SamplePCMView pcm, uint32_t first, uint32_t last, size_t bins,
                                            SampleChannels channels) {
  std::vector<float> out(bins * 2);
  if (first == last || !bins)
    return out;
  for (size_t b = 0; b < bins; ++b) {
    auto a = first + uint32_t(b * uint64_t(last - first) / bins),
         z = first + uint32_t((b + 1) * uint64_t(last - first) / bins);
    z = std::min(last, std::max(a + 1, z));
    int lo = INT_MAX, hi = INT_MIN;
    for (auto f = a; f < z; ++f)
      for (uint8_t c = 0; c < pcm.channels; ++c)
        if (channels == SampleChannels::Both || (channels == SampleChannels::Left ? c == 0 : c == 1)) {
          lo = std::min(lo, pcm.value(f, c));
          hi = std::max(hi, pcm.value(f, c));
        }
    out[b * 2] = float(lo / pcm.scale());
    out[b * 2 + 1] = float(hi / pcm.scale());
  }
  return out;
}
static SamplePCMView sample(Document &doc) {
  auto &s = doc.song().GetSample(1);
  return {{s.sampleb(), size_t(s.nLength) * s.GetBytesPerSample()},
          s.nLength,
          uint8_t(s.uFlags[CHN_16BIT] ? 16 : 8),
          s.GetNumChannels()};
}
static void install(Document &doc, const std::vector<std::byte> &data, uint8_t bits, uint8_t channels) {
  auto &s = doc.song().GetSample(1);
  s.FreeSample();
  s.Initialize(doc.song().GetType());
  s.uFlags.set(CHN_16BIT, bits == 16);
  s.uFlags.set(CHN_STEREO, channels == 2);
  s.nLength = uint32_t(data.size() / (bits / 8 * channels));
  s.nC5Speed = 22050;
  s.nVolume = 128;
  s.nLoopStart = 8;
  s.nLoopEnd = s.nLength - 8;
  s.uFlags.set(CHN_LOOP);
  check(s.AllocateSample() != 0, "Allocate fixture");
  std::memcpy(s.sampleb(), data.data(), data.size());
  s.PrecomputeLoops(doc.song(), false);
  doc.song().m_nSamples = 1;
}
int main() {
  try {
    std::mt19937 random(72961);
    size_t cases = 0;
    for (uint8_t bits : {8, 16})
      for (uint8_t channels : {1, 2}) {
        std::vector<int32_t> input(1041 * channels);
        for (auto &v : input)
          v = int32_t(random() % (1u << bits)) - (1 << (bits - 1));
        input[0] = -(1 << (bits - 1));
        input[1] = (1 << (bits - 1)) - 1;
        auto data = bytes(input, bits);
        auto pcm = view(data, bits, channels);
        for (const auto &op : {"reverse", "normalize", "silence", "invert", "gain", "remove-dc", "smooth", "fade-in",
                               "fade-out", "swap-channels", "copy-left", "copy-right", "stereo-average"}) {
          const bool stereo = std::string(op) == "swap-channels" || std::string(op) == "copy-left" ||
                              std::string(op) == "copy-right" || std::string(op) == "stereo-average";
          if (stereo && channels == 1)
            continue;
          for (auto choice : {SampleChannels::Both, SampleChannels::Left, SampleChannels::Right}) {
            if ((choice == SampleChannels::Right && channels == 1) || (stereo && choice != SampleChannels::Both))
              continue;
            for (auto range : {std::pair{0u, 1041u}, {0u, 1u}, {7u, 8u}, {250u, 769u}})
              for (auto curve : {SampleFadeCurve::Linear, SampleFadeCurve::Smooth, SampleFadeCurve::Exponential,
                                 SampleFadeCurve::Logarithmic}) {
                SampleProcessOptions o;
                o.operation = op;
                o.first = range.first;
                o.last = range.second;
                o.channels = choice;
                o.curve = curve;
                o.exponent = 2.6;
                o.gainDB = 6;
                o.window = 255;
                o.targetDB = -1.3;
                verifyPlan(pcm, o);
                ++cases;
              }
          }
        }
        SampleWaveform cache;
        for (int i = 0; i < 600; ++i) {
          uint32_t a = random() % (pcm.frames + 1), z = random() % (pcm.frames + 1);
          if (a > z)
            std::swap(a, z);
          size_t bins = i % 7 ? random() % 173 : 16384;
          auto c = channels == 2 ? SampleChannels(random() % 3) : SampleChannels::Both;
          check(cache.query(pcm, a, z, bins, c) == waveformReference(pcm, a, z, bins, c),
                "Waveform cache agrees with direct frame/channel scan");
        }
        auto bad = SampleProcessOptions{"gain", 0, pcm.frames};
        bad.gainDB = NAN;
        rejects([&] { planSampleProcess(pcm, bad); });
        bad.gainDB = 0;
        bad.last = pcm.frames + 1;
        rejects([&] { planSampleProcess(pcm, bad); });
        bad.last = 0;
        rejects([&] { planSampleProcess(pcm, bad); });
        rejects([&] { cache.query(pcm, 0, pcm.frames, 16385); });
      }
    std::cout << "PASS " << cases
              << " independent 8/16-bit mono/stereo processing cases; exact range/channel waveform oracle\n";
    for (auto type : {MOD_TYPE_MOD, MOD_TYPE_S3M, MOD_TYPE_XM, MOD_TYPE_IT, MOD_TYPE_MPT}) {
      Document doc(type);
      const uint8_t bits = type == MOD_TYPE_MOD ? 8 : 16, channels = type == MOD_TYPE_MOD ? 1 : 2;
      std::vector<int32_t> pcm(4096 * channels);
      for (size_t i = 0; i < pcm.size(); ++i)
        pcm[i] = int32_t(i % 91) - 40;
      auto data = bytes(pcm, bits);
      install(doc, data, bits, channels);
      // Roundtrip first so format-specific header normalization precedes history.
      Document d(doc.serialize());
      const auto original = values(sample(d));
      auto metadata = d.native();
      const auto loopStart = d.song().GetSample(1).nLoopStart, loopEnd = d.song().GetSample(1).nLoopEnd;
      SampleProcessOptions o{"invert", 251, 257};
      auto revision = d.revision;
      auto history = d.historyBytes();
      auto preview = d.processSample(1, o, true);
      check(d.revision == revision && d.historyBytes() == history && values(sample(d)) == original,
            "Preview has no history, revision or PCM side effects");
      auto plan = d.prepareSampleProcess(1, o);
      auto applied = d.applySampleProcess(std::move(plan));
      check(applied.changedSamples == preview.changedSamples && d.revision == revision + 1 &&
                d.historyBytes() - history < 8192,
            "Small PCM edit has small history, not a whole-song snapshot");
      auto changed = values(sample(d));
      check(d.song().GetSample(1).nLoopStart == loopStart && d.song().GetSample(1).nLoopEnd == loopEnd &&
                d.native() == metadata,
            "Fixed-length edit preserves metadata and loops");
      d.edit({Edit{0, 1, 0, {}, Cell{61, 1, 0, 0, 0, 0}}});
      d.transaction([](CSoundFile &s) { s.m_songName = "After structural edit"; });
      Document reopened(d.serialize());
      check(values(sample(reopened)) == changed, "Edited PCM persists in each native module format");
      d.undo();
      d.undo();
      d.undo();
      check(values(sample(d)) == original, "Mixed structural, pattern and sample undo restores PCM");
      const auto rev = d.revision;
      const auto h = d.historyBytes();
      o.operation = "gain";
      d.processSample(1, o);
      check(d.revision == rev && d.historyBytes() == h && d.canRedo(), "No-op retains revision and redo");
      o.operation = "invert";
      auto stale = d.prepareSampleProcess(1, o);
      d.redo();
      d.redo();
      d.redo();
      check(values(sample(d)) == changed, "Mixed redo replays PCM exactly");
      rejects([&] { d.applySampleProcess(std::move(stale)); });
      check(values(sample(d)) == changed, "Stale preparation is atomic");
      auto foreign = d.prepareSampleProcess(1, o);
      rejects([&] { doc.applySampleProcess(std::move(foreign)); });
    }
    std::cout
        << "PASS prepared edit identity/revision checks, exact mixed history and persistence in five module formats\n";
    Document large;
    std::vector<int32_t> input(2 * 1024 * 1024);
    for (size_t i = 0; i < input.size(); ++i)
      input[i] = int32_t(i % 14001) - 7000;
    auto data = bytes(input, 16);
    install(large, data, 16, 2);
    const auto initial = large.waveform(1, 0, 1024 * 1024, 1024, SampleChannels::Both);
    const auto reads = large.waveformReadFrames();
    check(reads == 1024 * 1024, "First full aligned waveform reads sample once");
    check(large.waveform(1, 0, 1024 * 1024, 1024, SampleChannels::Both) == initial &&
              large.waveformReadFrames() == reads,
          "Repeated aligned waveform uses cached tiles");
    large.edit({Edit{0, 0, 0, {}, Cell{61, 1, 0, 0, 0, 0}}});
    large.waveform(1, 0, 1024 * 1024, 1024, SampleChannels::Both);
    check(large.waveformReadFrames() == reads, "Pattern edits do not invalidate sample waveform");
    auto history = large.historyBytes();
    SampleProcessOptions o{"silence", 300, 301};
    auto changed = large.processSample(1, o);
    check(changed.changedFrames == 1 && large.historyBytes() - history < 8192,
          "Million-frame sample single-frame edit is bounded");
    check(large.waveformReadFrames() == reads + 256, "One-frame edit refreshes one waveform tile");
    check(large.waveform(1, 298, 303, 5, SampleChannels::Both) ==
              waveformReference(sample(large), 298, 303, 5, SampleChannels::Both),
          "Cached partial waveform updates exactly");
    large.undo();
    check(values(sample(large)) == input, "Single-frame silence undo is exact");
    large.redo();
    auto h = large.historyBytes();
    auto r = large.revision;
    large.processSample(1, o);
    check(large.revision == r && large.historyBytes() == h, "Already silent sample no-op");
    auto prepared = large.prepareSampleProcess(1, SampleProcessOptions{"invert", 400, 401});
    auto &s = large.song().GetSample(1);
    s.sample16()[800]++;
    auto altered = values(sample(large));
    rejects([&] { large.applySampleProcess(std::move(prepared)); });
    check(values(sample(large)) == altered && large.revision == r && large.historyBytes() == h,
          "Out-of-band PCM changes reject prepared patch atomically");
    std::cout << "PASS million-frame cache reuse/local invalidation, one-frame exact silence and bounded history\n";
    for (uint8_t bits : {8, 16}) {
      const int limit = 1 << (bits - 1);
      auto extrema = bytes({-limit, limit - 1, 0}, bits);
      SampleProcessOptions normal{"normalize", 0, 3};
      auto plan = planSampleProcess(view(extrema, bits, 1), normal);
      check(plan.result.clippedSamples == 0 && plan.result.preview.front().after == -(limit - 1),
            "Default normalize handles most-negative input without overflow");
      auto zeros = bytes({0, 0, 0}, bits);
      check(planSampleProcess(view(zeros, bits, 1), normal).chunks.empty(), "Silence normalization is a no-op");
      SampleProcessOptions fade{"fade-in", 0, 3};
      for (auto curve : {SampleFadeCurve::Linear, SampleFadeCurve::Smooth, SampleFadeCurve::Exponential,
                         SampleFadeCurve::Logarithmic}) {
        fade.curve = curve;
        fade.exponent = .1;
        verifyPlan(view(extrema, bits, 1), fade);
        fade.exponent = 8;
        verifyPlan(view(extrema, bits, 1), fade);
      }
    }
    // Playback integration compares edited audio with separately calculated PCM.
    for (const auto &operation : {"silence", "remove-dc", "smooth", "fade-out"}) {
      auto edited = Document::demo();
      auto originalBytes = edited->serialize();
      Document expected(originalBytes);
      SampleProcessOptions options{operation, 17, 199};
      options.curve = SampleFadeCurve::Exponential;
      options.exponent = 2.6;
      uint64_t clips = 0;
      auto oracle = bytes(reference(sample(expected), options, clips), 16);
      auto &target = expected.song().GetSample(1);
      std::memcpy(target.sampleb(), oracle.data(), oracle.size());
      target.PrecomputeLoops(expected.song(), false);
      edited->processSample(1, options);
      auto actualBytes = edited->serialize();
      auto expectedBytes = expected.serialize();
      Renderer actual(actualBytes, 48000), referenceRenderer(expectedBytes, 48000),
          originalRenderer(originalBytes, 48000);
      std::array<float, 8192> a{}, b{}, c{};
      double difference = 0, energy = 0;
      for (int n = 0; n < 200; ++n) {
        const uint32_t frames = n % 3 == 0 ? 17 : n % 3 == 1 ? 128 : 4096;
        actual.render(a.data(), frames);
        referenceRenderer.render(b.data(), frames);
        originalRenderer.render(c.data(), frames);
        for (size_t i = 0; i < frames * 2; ++i) {
          check(a[i] == b[i] && std::isfinite(a[i]), "Edited playback matches independently processed PCM exactly");
          difference += std::abs(a[i] - c[i]);
          energy += a[i] * a[i];
        }
      }
      check(difference > 0.1 && energy > 0.1, "Fixture detects audible processing and remains non-silent");
      edited->undo();
      Renderer restored(edited->serialize(), 48000), original(originalBytes, 48000);
      restored.render(a.data(), 4096);
      original.render(b.data(), 4096);
      check(a == b, "Sample undo restores exact rendered audio");
    }
    std::cout << "PASS full-scale/silent normalization, fade exponent boundaries and exact rendered audio against "
                 "independent processed PCM\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL " << e.what() << '\n';
    return 1;
  }
}
