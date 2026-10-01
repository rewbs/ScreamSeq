#include "editor/TrackerDocument.hpp"
#include "soundlib/ModInstrument.h"
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
  check(failed, "Expected copy rejection");
}
static void fixture(Document &d, int bits, int channels, int frames = 1031) {
  auto &s = d.song().GetSample(1);
  s.Initialize(d.song().GetType());
  s.uFlags.set(CHN_16BIT, bits == 16);
  s.uFlags.set(CHN_STEREO, channels == 2);
  s.nLength = frames;
  s.nC5Speed = 22050;
  s.FrequencyToTranspose();
  s.nFineTune = -19;
  s.nPan = 71;
  s.nVolume = 207;
  s.nGlobalVol = 47;
  s.nVibSweep = 9;
  s.nVibRate = 7;
  s.nVibDepth = 13;
  s.nVibType = VIB_RAMP_UP;
  s.rootNote = 49;
  s.filename = "original.wav";
  s.uFlags.set(CHN_LOOP | CHN_PINGPONGLOOP | CHN_SUSTAINLOOP | CHN_PINGPONGSUSTAIN | CHN_PANNING | CHN_REVERSE |
               SMP_NODEFAULTVOLUME);
  s.nLoopStart = 101;
  s.nLoopEnd = 900;
  s.nSustainStart = 3;
  s.nSustainEnd = 303;
  s.cues = {1, 100, 200, 499, 500, 900, 1030, MAX_SAMPLE_LENGTH, 999999};
  check(s.AllocateSample() != 0, "Allocate source");
  for (int f = 0; f < frames; ++f)
    for (int c = 0; c < channels; ++c) {
      if (bits == 8)
        s.sample8()[f * channels + c] = int8_t((f * 11 + c * 19) % 255 - 127);
      else
        s.sample16()[f * channels + c] = int16_t((f * 997 + c * 331) % 65535 - 32767);
    }
  s.PrecomputeLoops(d.song(), false);
  d.song().m_nSamples = 1;
  d.song().m_szNames[1] = "Source";
  auto n = d.native();
  n.reconcile(d.song());
  n.samples.at(1).annotation = "source annotation";
  n.samples.at(1).color = 0xABCDEF;
  d.restoreNative(std::move(n));
}
static std::vector<std::byte> pcm(const ModSample &s) {
  if (!s.nLength)
    return {};
  return {s.sampleb(), s.sampleb() + size_t(s.nLength) * s.GetBytesPerSample()};
}
int main() {
  try {
    size_t cases = 0;
    for (auto type : {MOD_TYPE_MOD, MOD_TYPE_XM, MOD_TYPE_S3M, MOD_TYPE_IT, MOD_TYPE_MPT})
      for (int bits : {8, 16})
        for (int channels : {1, 2})
          for (auto channel : {SampleChannels::Both, SampleChannels::Left, SampleChannels::Right}) {
            if (channels == 1 && channel == SampleChannels::Right)
              continue;
            Document d(type);
            fixture(d, bits, channels);
            const auto original = pcm(d.song().GetSample(1));
            const auto source = d.song().GetSample(1);
            const auto sourceID = d.native().samples.at(1).id;
            auto prepared = d.prepareSampleCopy(1, 100, 500, channel);
            check(d.revision == 0 && !d.canUndo() && d.song().GetNumSamples() == 1, "Prepare is read-only");
            check(prepared.sample() == 2 && !prepared.reusesEmptySlot() && prepared.identity() != sourceID,
                  "Copy gets new slot and distinct identity");
            const auto id = prepared.identity();
            const auto history = prepared.historyBytes();
            check(prepared.settings().nLoopStart == 1 && prepared.settings().nLoopEnd == 400 &&
                      prepared.settings().nSustainStart == 0 && prepared.settings().nSustainEnd == 203,
                  "Copy intersects loops with selected range");
            check(d.applySampleCopy(std::move(prepared)) == 2 && d.revision == 1, "Prepared copy commits once");
            const auto &target = d.song().GetSample(2);
            const int destChannels = channel == SampleChannels::Both ? channels : 1;
            check(target.nLength == 400 && target.GetNumChannels() == destChannels &&
                      target.GetElementarySampleSize() * 8 == bits,
                  "Exact copied layout");
            for (uint32_t f = 0; f < 400; ++f)
              for (int c = 0; c < destChannels; ++c) {
                int sc = channel == SampleChannels::Both ? c : channel == SampleChannels::Left ? 0 : 1;
                check(bits == 8
                          ? target.sample8()[f * destChannels + c] == source.sample8()[(f + 100) * channels + sc]
                          : target.sample16()[f * destChannels + c] == source.sample16()[(f + 100) * channels + sc],
                      "Copy PCM equals independent selected source channel");
              }
            check(target.cues == std::array<uint32_t, 9>{MAX_SAMPLE_LENGTH, 0, 100, 399, MAX_SAMPLE_LENGTH,
                                                         MAX_SAMPLE_LENGTH, MAX_SAMPLE_LENGTH, MAX_SAMPLE_LENGTH,
                                                         MAX_SAMPLE_LENGTH},
                  "Cue inclusion is half-open and rebased");
            check(target.nC5Speed == source.nC5Speed && target.RelativeTone == source.RelativeTone &&
                      target.nFineTune == source.nFineTune &&
                      target.GetSampleRate(type) == source.GetSampleRate(type) && target.nPan == source.nPan &&
                      target.nVolume == source.nVolume && target.nGlobalVol == source.nGlobalVol &&
                      target.nVibType == source.nVibType && target.nVibRate == source.nVibRate &&
                      target.nVibDepth == source.nVibDepth && target.nVibSweep == source.nVibSweep &&
                      target.rootNote == source.rootNote && target.filename.str() == source.filename.str(),
                  "Musical settings and effective tuning retained exactly");
            check(d.native().samples.at(2).id == id && d.native().samples.at(2).annotation == "source annotation" &&
                      d.native().samples.at(1).id == sourceID && pcm(source) == original,
                  "Independent identity and metadata; source unchanged");
            auto copied = pcm(target);
            auto flags = target.uFlags;
            const auto cues = target.cues;
            const auto metadata = d.native();
            d.native().validate(d.song());
            check(d.historyBytes() == history, "Preview history estimate matches retained entry");
            d.undo();
            check(d.song().GetNumSamples() == 1 && !d.native().samples.contains(2) &&
                      pcm(d.song().GetSample(1)) == original,
                  "Undo removes new slot without touching source");
            d.redo();
            check(pcm(d.song().GetSample(2)) == copied && d.native().samples.at(2).id == id,
                  "Redo restores exact copied data and identity");
            d.transaction([](auto &s) { s.m_songName = "generic"; });
            d.undo();
            d.undo();
            d.redo();
            check(pcm(d.song().GetSample(2)) == copied && d.song().GetSample(2).uFlags == flags,
                  "Mixed structural history preserves sample copy");
            Document reopened(d.snapshotData());
            reopened.restoreNative(d.native());
            check(pcm(reopened.song().GetSample(2)) == copied && reopened.native().samples.at(2).id == id &&
                      reopened.song().GetSample(2).cues == cues,
                  "Native copy roundtrip retains exact audio and identity");
            Document reference(type);
            fixture(reference, bits, channels);
            auto &independent = reference.song().GetSample(2);
            independent = reference.song().GetSample(1);
            independent.pData.pSample = nullptr;
            independent.nLength = 400;
            independent.uFlags.set(CHN_STEREO, destChannels == 2);
            independent.nLoopStart = 1;
            independent.nLoopEnd = 400;
            independent.nSustainStart = 0;
            independent.nSustainEnd = 203;
            check(independent.AllocateSample() != 0, "Independent copied-audio fixture allocation");
            const auto &input = reference.song().GetSample(1);
            for (uint32_t f = 0; f < 400; ++f)
              for (int c = 0; c < destChannels; ++c) {
                const int sc = channel == SampleChannels::Both ? c : channel == SampleChannels::Left ? 0 : 1;
                if (bits == 8)
                  independent.sample8()[f * destChannels + c] = input.sample8()[(f + 100) * channels + sc];
                else
                  independent.sample16()[f * destChannels + c] = input.sample16()[(f + 100) * channels + sc];
              }
            independent.PrecomputeLoops(reference.song(), false);
            reference.song().m_nSamples = 2;
            auto referenceNative = reference.native();
            referenceNative.reconcile(reference.song());
            reference.restoreNative(referenceNative);
            for (auto *doc : {&reopened, &reference}) {
              auto &note = *doc->song().Patterns[0].GetpModCommand(0, 0);
              note.note = 61;
              note.instr = 2;
              // The fixture deliberately retains SMP_NODEFAULTVOLUME. Give the
              // test voice an explicit tracker volume so silence cannot pass.
              note.command = CMD_VOLUME;
              note.param = 64;
            }
            Renderer actualAudio(reopened.snapshotData(), 48000), expectedAudio(reference.snapshotData(), 48000);
            std::array<float, 8192> a{}, b{};
            bool signal = false;
            for (size_t block : {17u, 128u, 4096u}) {
              check(actualAudio.render(a.data(), block) == block && expectedAudio.render(b.data(), block) == block,
                    "Copied sample renders through native playback");
              check(std::equal(a.begin(), a.begin() + block * 2, b.begin()),
                    "Copied sample audio matches independent source extraction and loop renderer exactly");
              signal |= std::any_of(a.begin(), a.begin() + block * 2, [](float x) { return x != 0; });
            }
            check(signal, "Copied sample comparison includes audible signal");
            ++cases;
          }
    Document large;
    fixture(large, 16, 2, 1000000);
    auto tiny = large.prepareSampleCopy(1, 950, 951, SampleChannels::Right, "One frame");
    check(tiny.historyBytes() < 16384, "One-frame copy does not retain million-frame source PCM");
    large.applySampleCopy(std::move(tiny));
    check(large.song().m_szNames[2].str() == "One frame" &&
              !large.song().GetSample(2).uFlags[CHN_LOOP | CHN_SUSTAINLOOP],
          "Copy name and collapsed loops");
    auto stale = large.prepareSampleCopy(1, 0, 1, SampleChannels::Both);
    large.annotate([](auto &n) { n.samples.at(1).annotation = "change"; });
    rejects([&] { large.applySampleCopy(std::move(stale)); });
    auto other = large.prepareSampleCopy(1, 0, 1, SampleChannels::Both);
    Document foreign;
    rejects([&] { foreign.applySampleCopy(std::move(other)); });
    auto changed = large.prepareSampleCopy(1, 0, 1, SampleChannels::Both);
    large.song().GetSample(1).sample16()[0] ^= 1;
    auto rev = large.revision;
    rejects([&] { large.applySampleCopy(std::move(changed)); });
    check(large.revision == rev, "Tampered preparation rejected atomically");
    rejects([&] { large.prepareSampleCopy(1, 1, 1, SampleChannels::Both); });
    rejects([&] { large.prepareSampleCopy(1, 0, 1000001, SampleChannels::Both); });
    // Imported MOD inventories are full-sized. Never turn an existing silent
    // pattern reference or instrument mapping into sound through automatic reuse.
    Document full(MOD_TYPE_MOD);
    fixture(full, 8, 1);
    full.song().m_nSamples = 31;
    for (int i = 2; i <= 31; ++i)
      full.song().m_szNames[i] = "Reserved";
    full.song().m_szNames[25] = "";
    full.song().GetSample(25).nVolume = 13;
    auto n = full.native();
    n.reconcile(full.song());
    full.restoreNative(n);
    const auto oldID = full.native().samples.at(25).id;
    auto reuse = full.prepareSampleCopy(1, 0, 17, SampleChannels::Both);
    check(reuse.sample() == 25 && reuse.reusesEmptySlot(), "Full format uses an unnamed unreferenced empty slot");
    const auto newID = reuse.identity();
    full.applySampleCopy(std::move(reuse));
    full.undo();
    check(full.song().GetNumSamples() == 31 && full.song().GetSample(25).nVolume == 13 &&
              full.native().samples.at(25).id == oldID,
          "Undo restores empty slot's settings and identity");
    full.redo();
    check(full.native().samples.at(25).id == newID, "Redo restores replacement identity");
    full.undo();
    check(full.prepareSampleCopy(1, 0, 17, SampleChannels::Both).identity() != newID,
          "A branched copy never reuses the identity of the undone sample");
    full.song().Patterns[0].GetpModCommand(0, 0)->instr = 25;
    rejects([&] { full.prepareSampleCopy(1, 0, 17, SampleChannels::Both); });
    full.song().Patterns[0].GetpModCommand(0, 0)->instr = 0;
    full.song().Instruments[1] = new ModInstrument(25);
    full.song().m_nInstruments = 1;
    n = full.native();
    n.reconcile(full.song());
    full.restoreNative(n);
    rejects([&] { full.prepareSampleCopy(1, 0, 17, SampleChannels::Both); });
    std::cout << "PASS " << cases
              << " independent copy-to-new layout/range/settings/audio cases, loops/cues, native identity/persistence, "
                 "compact history, stale/foreign/tampered rejection and safe full-inventory reuse\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL " << e.what() << '\n';
    return 1;
  }
}
