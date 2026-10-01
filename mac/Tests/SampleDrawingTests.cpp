#include "editor/TrackerDocument.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
using namespace Tracker;
static void check(bool ok, const char *why) {
  if (!ok)
    throw std::runtime_error(why);
}
template <class F> static void rejects(F f) {
  bool failed = false;
  try {
    f();
  } catch (const std::exception &) {
    failed = true;
  }
  check(failed, "Expected draw rejection");
}
static std::vector<std::byte> data(int bits, int channels, uint32_t frames) {
  std::vector<std::byte> r(size_t(frames) * channels * bits / 8);
  for (uint32_t f = 0; f < frames; ++f)
    for (int c = 0; c < channels; ++c) {
      if (bits == 8) {
        const auto v = int8_t((f * 13 + c * 41) % 255 - 127);
        std::memcpy(r.data() + f * channels + c, &v, 1);
      } else {
        const auto v = int16_t((f * 701 + c * 123) % 65535 - 32767);
        std::memcpy(r.data() + (f * channels + c) * 2, &v, 2);
      }
    }
  return r;
}
static void install(Document &d, int bits, int channels, uint32_t frames, const std::vector<std::byte> &bytes) {
  auto &s = d.song().GetSample(1);
  s.Initialize(d.song().GetType());
  s.nLength = frames;
  s.uFlags.set(CHN_16BIT, bits == 16);
  s.uFlags.set(CHN_STEREO, channels == 2);
  s.nC5Speed = 22050;
  s.FrequencyToTranspose();
  s.nLoopStart = 7;
  s.nLoopEnd = frames;
  s.uFlags.set(CHN_LOOP);
  s.cues[0] = 23;
  check(s.AllocateSample() != 0, "Allocate drawing fixture");
  std::memcpy(s.samplev(), bytes.data(), bytes.size());
  s.PrecomputeLoops(d.song(), false);
  d.song().m_nSamples = 1;
  auto n = d.native();
  n.reconcile(d.song());
  d.restoreNative(n);
}
static std::vector<std::byte> pcm(Document &d) {
  const auto &s = d.song().GetSample(1);
  return {s.sampleb(), s.sampleb() + s.GetSampleSizeInBytes()};
}
int main() {
  try {
    size_t cases = 0;
    for (int bits : {8, 16})
      for (int channels : {1, 2})
        for (auto choice : {SampleChannels::Both, SampleChannels::Left, SampleChannels::Right}) {
          if (channels == 1 && choice == SampleChannels::Right)
            continue;
          const auto original = data(bits, channels, 1031);
          SamplePCMView input{original, 1031, uint8_t(bits), uint8_t(channels)};
          for (auto mode : {SampleDrawInterpolation::Linear, SampleDrawInterpolation::Step})
            for (auto points : std::vector<std::vector<SampleDrawPoint>>{
                     {{0, -1}, {256, 1}}, {{255, .5}}, {{250, .75}, {506, -.25}, {762, .5}}, {{1028, 1}, {1030, -1}}}) {
              SampleDrawOptions o{points, choice, mode};
              const auto plan = planSampleDraw(input, o);
              auto actual = original;
              for (const auto &chunk : plan.chunks)
                std::memcpy(actual.data() + size_t(chunk.first) * input.stride(), chunk.after.data(),
                            chunk.after.size());
              SamplePCMView output{actual, 1031, uint8_t(bits), uint8_t(channels)};
              uint64_t changes = 0, clips = 0;
              uint32_t frames = 0;
              size_t preview = 0;
              double peakBefore = 0, peakAfter = 0;
              for (uint32_t f = 0; f < 1031; ++f) {
                bool changed = false;
                for (int c = 0; c < channels; ++c) {
                  auto expected = input.value(f, c);
                  bool selected = (choice == SampleChannels::Both || c == (choice == SampleChannels::Left ? 0 : 1));
                  if (f >= points.front().frame && f <= points.back().frame && selected) {
                    // Independent weighted-endpoint reference; fixtures use exact binary
                    // amplitudes and power-of-two segment lengths, including rounding ties.
                    auto right = std::find_if(points.begin(), points.end(), [&](auto p) { return p.frame > f; });
                    auto left = std::prev(right);
                    double value = left->value;
                    if (mode == SampleDrawInterpolation::Linear && right != points.end())
                      value = (left->value * double(right->frame - f) + right->value * double(f - left->frame)) /
                              double(right->frame - left->frame);
                    value *= input.scale();
                    clips += value < -input.scale() || value > input.scale() - 1;
                    expected = int32_t(std::round(std::max(-input.scale(), std::min(input.scale() - 1, value))));
                    peakBefore = std::max(peakBefore, std::abs(double(input.value(f, c))) / input.scale());
                    peakAfter = std::max(peakAfter, std::abs(double(expected)) / input.scale());
                  }
                  check(output.value(f, c) == expected,
                        "Drawing agrees with independent interpolation, exact PCM rounding and channel isolation");
                  if (expected != input.value(f, c)) {
                    changed = true;
                    ++changes;
                    if (preview < maximumSamplePreview) {
                      const auto &v = plan.result.preview.at(preview++);
                      check(v.frame == f && v.channel == c && v.before == input.value(f, c) && v.after == expected,
                            "Independent exact bounded drawing preview");
                    }
                  }
                }
                frames += changed;
              }
              check(plan.result.changedSamples == changes && plan.result.changedFrames == frames &&
                        plan.result.clippedSamples == clips && plan.result.peakBefore == peakBefore &&
                        plan.result.peakAfter == peakAfter,
                    "Exact drawing counts, clipping and peaks");
              check(plan.result.first == points.front().frame && plan.result.last == points.back().frame + 1,
                    "Inclusive drawing points become exclusive region end");
              ++cases;
            }
        }
    for (auto type : {MOD_TYPE_MOD, MOD_TYPE_XM, MOD_TYPE_S3M, MOD_TYPE_IT, MOD_TYPE_MPT}) {
      Document d(type), reference(type);
      auto original = data(16, 2, 1031);
      install(d, 16, 2, 1031, original);
      install(reference, 16, 2, 1031, original);
      SampleDrawOptions o{{{250, -.5}, {506, .5}}, SampleChannels::Right};
      auto prepared = d.prepareSampleDraw(1, o);
      check(d.revision == 0 && !d.canUndo(), "Drawing preparation is read-only");
      d.applySampleProcess(std::move(prepared));
      auto &expected = reference.song().GetSample(1);
      for (int f = 250; f <= 506; ++f)
        expected.sample16()[f * 2 + 1] = int16_t((f - 250) * 128 - 16384);
      expected.PrecomputeLoops(reference.song(), false);
      check(pcm(d) == pcm(reference), "Independent document drawing PCM");
      check(d.song().GetSample(1).nLoopStart == 7 && d.song().GetSample(1).nLoopEnd == 1031 &&
                d.song().GetSample(1).cues[0] == 23,
            "Drawing preserves loop and cue metadata");
      auto copy = pcm(d);
      d.undo();
      check(pcm(d) == original, "Drawing undo exact");
      d.redo();
      check(pcm(d) == copy, "Drawing redo exact");
      d.transaction([](auto &s) { s.m_songName = "Other edit"; });
      d.undo();
      d.undo();
      check(pcm(d) == original, "Drawing and generic history compose");
      d.redo();
      Document reopened(d.snapshotData());
      check(pcm(reopened) == copy, "Native save preserves drawing exactly across all formats");
      for (auto *doc : {&reopened, &reference}) {
        auto &cell = *doc->song().Patterns[0].GetpModCommand(0, 0);
        cell.note = 61;
        cell.instr = 1;
      }
      Renderer a(reopened.snapshotData(), 48000), b(reference.snapshotData(), 48000);
      std::array<float, 8192> x{}, y{};
      bool signal = false;
      for (size_t count : {17u, 128u, 4096u}) {
        check(a.render(x.data(), count) == count && b.render(y.data(), count) == count, "Render drawn sample");
        check(std::equal(x.begin(), x.begin() + 2 * count, y.begin()),
              "Drawn audio matches independent PCM renderer exactly");
        signal |= std::any_of(x.begin(), x.begin() + 2 * count, [](float f) { return f != 0; });
      }
      check(signal, "Drawing audio fixture is nonzero");
    }
    Document large;
    auto original = data(16, 2, 2097152);
    install(large, 16, 2, 2097152, original);
    large.waveform(1, 2048);
    const auto reads = large.waveformReadFrames();
    SampleDrawOptions one{{{900003, 0}}, SampleChannels::Left};
    large.applySampleProcess(large.prepareSampleDraw(1, one));
    large.waveform(1, 2048);
    check(large.waveformReadFrames() == reads + 256 && large.historyBytes() < 8192,
          "One-frame drawing updates one waveform tile and retains bounded history");
    auto revision = large.revision;
    large.undo();
    large.redo();
    revision = large.revision;
    large.applySampleProcess(large.prepareSampleDraw(1, one));
    check(large.revision == revision, "Identical drawing is a no-op");
    auto stale = large.prepareSampleDraw(1, one);
    large.annotate([](auto &n) { n.samples.at(1).annotation = "x"; });
    rejects([&] { large.applySampleProcess(std::move(stale)); });
    one.points[0].value = .5;
    auto foreign = large.prepareSampleDraw(1, one);
    Document other;
    rejects([&] { other.applySampleProcess(std::move(foreign)); });
    auto tampered = large.prepareSampleDraw(1, one);
    large.song().GetSample(1).sample16()[900003 * 2] ^= 1;
    rejects([&] { large.applySampleProcess(std::move(tampered)); });
    SamplePCMView view{original, 2097152, 16, 2};
    for (auto points : std::vector<std::vector<SampleDrawPoint>>{{},
                                                                 {{2097152, 0}},
                                                                 {{1, 0}, {1, 1}},
                                                                 {{2, 0}, {1, 1}},
                                                                 {{0, NAN}},
                                                                 {{0, INFINITY}},
                                                                 {{0, 1.01}},
                                                                 {{0, -1.01}},
                                                                 {{0, 0}, {1048576, 1}}})
      rejects([&] { planSampleDraw(view, {points}); });
    auto many = std::vector<SampleDrawPoint>(4097);
    for (size_t i = 0; i < many.size(); ++i)
      many[i] = {uint32_t(i), 0};
    rejects([&] { planSampleDraw(view, {many}); });
    auto bad = one;
    bad.channels = SampleChannels(42);
    rejects([&] { planSampleDraw(view, bad); });
    bad = one;
    bad.interpolation = SampleDrawInterpolation(42);
    rejects([&] { planSampleDraw(view, bad); });
    std::cout << "PASS " << cases
              << " independent drawing interpolation/layout/channel cases, exact previews/clipping, history/native "
                 "persistence/audio, bounded waveform updates and invalid/stale/foreign/tampered rejection\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL " << e.what() << '\n';
    return 1;
  }
}
