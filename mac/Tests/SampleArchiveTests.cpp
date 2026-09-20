#include "editor/SampleArchive.hpp"
#include "editor/TrackerDocument.hpp"
#include "soundlib/ModInstrument.h"
#include <cstring>
#include <iostream>
using namespace Tracker;
using namespace OpenMPT;
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
  check(failed, "Malformed snapshot must fail");
}
static void install(Document &d, int index, uint32_t length, int bits, int channels, int seed = 0) {
  auto &song = d.song();
  auto &s = song.GetSample(index);
  s.FreeSample();
  s.Initialize(song.GetType());
  s.uFlags = SampleFlags{};
  s.uFlags.set(CHN_16BIT, bits == 16);
  s.uFlags.set(CHN_STEREO, channels == 2);
  s.nLength = length;
  s.nVolume = 172;
  s.nGlobalVol = 51;
  s.nPan = 115;
  s.nC5Speed = 23879;
  s.RelativeTone = -3;
  s.nFineTune = -49;
  s.rootNote = 64;
  s.nVibType = VIB_SQUARE;
  s.nVibSweep = 17;
  s.nVibRate = 23;
  s.nVibDepth = 11;
  s.filename = "Original.wav";
  song.m_szNames[index] = "Original sample";
  for (size_t i = 0; i < s.cues.size(); ++i)
    s.cues[i] = uint32_t(i * 3);
  if (length) {
    check(s.AllocateSample() != 0, "Allocate PCM");
    for (size_t i = 0; i < size_t(length) * channels; ++i) {
      if (bits == 16)
        s.sample16()[i] = int16_t((i * 913 + seed * 7919 + 718) % 64001 - 32000);
      else
        s.sample8()[i] = int8_t((i * 71 + seed * 37 + 31) % 251 - 125);
    }
  }
  song.m_nSamples = std::max<SAMPLEINDEX>(song.GetNumSamples(), index);
  s.PrecomputeLoops(song, false);
}
static void equalSamples(const CSoundFile &a, const CSoundFile &b) {
  check(a.GetType() == b.GetType() && a.GetNumSamples() == b.GetNumSamples(), "Exact sample inventory/type");
  check(a.GetCharsetInternal() == b.GetCharsetInternal(), "Exact sample charset");
  for (SAMPLEINDEX i = 1; i <= a.GetNumSamples(); ++i) {
    const auto &x = a.GetSample(i), &y = b.GetSample(i);
    check(x.nLength == y.nLength && x.GetBytesPerSample() == y.GetBytesPerSample(), "Exact PCM layout");
    check((x.uFlags.GetRaw() & ~uint16_t(SMP_KEEPONDISK)) == y.uFlags.GetRaw(), "Exact sample flags");
    check(x.nLoopStart == y.nLoopStart && x.nLoopEnd == y.nLoopEnd && x.nSustainStart == y.nSustainStart &&
              x.nSustainEnd == y.nSustainEnd,
          "Exact loop positions");
    check(x.nC5Speed == y.nC5Speed && x.RelativeTone == y.RelativeTone && x.nFineTune == y.nFineTune &&
              x.nPan == y.nPan && x.nVolume == y.nVolume && x.nGlobalVol == y.nGlobalVol && x.rootNote == y.rootNote,
          "Exact sample tuning and levels");
    check(x.nVibType == y.nVibType && x.nVibSweep == y.nVibSweep && x.nVibDepth == y.nVibDepth &&
              x.nVibRate == y.nVibRate,
          "Exact sample vibrato");
    check(std::memcmp(x.filename.buf, y.filename.buf, MAX_SAMPLEFILENAME) == 0 &&
              std::memcmp(a.m_szNames[i].buf, b.m_szNames[i].buf, MAX_SAMPLENAME) == 0,
          "Exact name/filename bytes");
    check(x.uFlags[CHN_ADLIB] ? x.adlib == y.adlib : x.cues == y.cues, "Exact cues/OPL patch");
    check(!x.nLength || std::memcmp(x.sampleb(), y.sampleb(), size_t(x.nLength) * x.GetBytesPerSample()) == 0,
          "Exact PCM bytes");
  }
  check(a.GetNumInstruments() == b.GetNumInstruments(), "Exact instrument inventory");
  for (INSTRUMENTINDEX i = 1; i <= a.GetNumInstruments(); ++i) {
    const auto *x = a.Instruments[i], *y = b.Instruments[i];
    check(bool(x) == bool(y), "Exact instrument presence");
    if (x)
      check(x->Keyboard == y->Keyboard && x->NoteMap == y->NoteMap, "Exact sample/note mapping");
  }
}
static std::vector<float> render(std::vector<std::byte> bytes, uint32_t block) {
  Renderer renderer(bytes, 48000);
  std::vector<float> result(24000 * 2);
  for (uint32_t f = 0; f < 24000;) {
    auto count = std::min(block, 24000 - f);
    renderer.render(result.data() + f * 2, count);
    f += count;
  }
  return result;
}
static void put32(std::vector<std::byte> &b, size_t at, uint32_t v) {
  for (int i = 0; i < 4; ++i)
    b.at(at + i) = std::byte(v >> (8 * i));
}
int main() {
  try {
    auto defaultSong = Document::demo();
    Document defaultExport(defaultSong->serialize());
    validateSampleExport(defaultSong->song(), defaultExport.song());
    size_t cases = 0;
    for (auto type : {MOD_TYPE_MOD, MOD_TYPE_XM, MOD_TYPE_S3M, MOD_TYPE_IT, MOD_TYPE_MPT})
      for (int bits : {8, 16})
        for (int channels : {1, 2}) {
          Document d(type);
          install(d, 1, 17, bits, channels);
          install(d, 2, 521, bits, channels, 1);
          install(d, 3, 0, bits, channels);
          auto &s = d.song().GetSample(2);
          s.nLoopStart = 3;
          s.nLoopEnd = 519;
          s.nSustainStart = 5;
          s.nSustainEnd = 17;
          s.uFlags.set(CHN_LOOP | CHN_SUSTAINLOOP | CHN_PINGPONGLOOP | CHN_PINGPONGSUSTAIN | CHN_PANNING |
                       SMP_MODIFIED);
          s.PrecomputeLoops(d.song(), false);
          auto data = d.snapshotData();
          Document restored(data);
          equalSamples(d.song(), restored.song());
          // A snapshot can itself be snapshotted without accumulating containers or changing audio.
          Document second(restored.snapshotData());
          equalSamples(d.song(), second.song());
          auto &a = d.song().GetSample(1);
          const auto oldLength = a.nLength;
          d.processSample(1, "trim", 3, 14);
          check(d.song().GetSample(1).nLength == 11, "Odd trim remains odd");
          Document trimmed(d.snapshotData());
          d.undo();
          equalSamples(restored.song(), d.song());
          check(d.song().GetSample(1).nLength == oldLength, "Undo trim length");
          d.redo();
          equalSamples(trimmed.song(), d.song());
          SampleProcessOptions o;
          o.operation = "invert";
          o.first = 0;
          o.last = 2;
          d.processSample(1, o);
          Document processed(d.snapshotData());
          d.transaction([](CSoundFile &song) { song.m_songName = "A structural history entry"; });
          d.undo();
          equalSamples(processed.song(), d.song());
          d.undo();
          equalSamples(trimmed.song(), d.song());
          d.undo();
          equalSamples(restored.song(), d.song());
          d.redo();
          d.redo();
          d.redo();
          equalSamples(processed.song(), d.song());
          auto rev = d.revision;
          rejects([&] {
            d.transaction([](CSoundFile &song) {
              song.GetSample(1).sampleb()[0] ^= std::byte{127};
              throw std::runtime_error("Rollback");
            });
          });
          check(d.revision == rev, "Rollback revision");
          equalSamples(processed.song(), d.song());
          ++cases;
        }
    // Independently demonstrate the legacy format losses this archive corrects.
    Document mod(MOD_TYPE_MOD);
    install(mod, 1, 17, 8, 1);
    Document converted(mod.serialize());
    check(converted.song().GetSample(1).nLength == 18 && converted.song().GetSample(1).sample8()[0] == 0 &&
              converted.song().GetSample(1).sample8()[1] == 0,
          "MOD pads odd lengths and zeroes one-shot guard bytes");
    auto &loop = mod.song().GetSample(1);
    loop.uFlags.set(CHN_LOOP);
    loop.nLoopStart = 3;
    loop.nLoopEnd = 15;
    loop.PrecomputeLoops(mod.song(), false);
    Document quantized(mod.serialize());
    check(quantized.song().GetSample(1).nLoopStart != 3 || quantized.song().GetSample(1).nLoopEnd != 15,
          "MOD loop format cannot preserve individual frames");
    Document exact(mod.snapshotData());
    equalSamples(mod.song(), exact.song());

    // XM reorders/duplicates samples by instrument; unused early slots would disappear.
    Document xm(MOD_TYPE_XM);
    for (int i = 1; i <= 4; ++i)
      install(xm, i, 513 + i, 16, 2, i);
    xm.song().m_nInstruments = 3;
    for (int i = 1; i <= 3; ++i)
      xm.song().Instruments[i] = new ModInstrument();
    for (int n = 0; n < 128; ++n) {
      xm.song().Instruments[1]->Keyboard[n] = n & 1 ? 4 : 2;
      xm.song().Instruments[2]->Keyboard[n] = n & 1 ? 2 : 4;
    }
    xm.song().Instruments[1]->NoteMap[60] = 49;
    Document xmBase(xm.serialize());
    check(xmBase.song().GetNumSamples() != xm.song().GetNumSamples() ||
              xmBase.song().Instruments[1]->Keyboard != xm.song().Instruments[1]->Keyboard,
          "XM fixture requires correction");
    Document xmExact(xm.snapshotData());
    equalSamples(xm.song(), xmExact.song());
    auto mappings = encodeSampleArchive(xm.song(), xm.song());
    const size_t firstInstrument = 12 + 4 * 134; // No PCM patches when compared with itself.
    for (auto offset : {firstInstrument, firstInstrument + 1, firstInstrument + 257}) {
      auto corrupt = mappings;
      corrupt.at(offset) = std::byte{255};
      if (offset == firstInstrument + 1)
        corrupt.at(offset + 1) = std::byte{255};
      Document target(xm.snapshotData());
      rejects([&] { restoreSampleArchive(target.song(), corrupt); });
    }

    Document opl(MOD_TYPE_S3M);
    install(opl, 1, 0, 8, 1);
    auto &synth = opl.song().GetSample(1);
    synth.uFlags.set(CHN_ADLIB);
    for (size_t i = 0; i < synth.adlib.size(); ++i)
      synth.adlib[i] = uint8_t(i * 7 + 1);
    Document oplExact(opl.snapshotData());
    equalSamples(opl.song(), oplExact.song());

    // Correction storage is sparse even when the embedded sample is large.
    Document big;
    install(big, 1, 1000000, 16, 2);
    Document baseline(big.snapshotData());
    baseline.song().GetSample(1).sample16()[543211] ^= 73;
    auto patch = encodeSampleArchive(big.song(), baseline.song());
    check(patch.size() < 1300, "One changed 256-frame tile does not duplicate million-frame sample");
    restoreSampleArchive(baseline.song(), patch);
    equalSamples(big.song(), baseline.song());

    // A canonical module's audio does not change when wrapped; all callbacks see the same PCM.
    auto demo = Document::demo();
    Document canonical(demo->serialize());
    auto raw = canonical.serialize(), native = canonical.snapshotData();
    check(render(raw, 128) == render(native, 17) && render(native, 4096) == render(native, 128),
          "Exact canonical audio across native loading and render partitions");
    canonical.processSample(1, "trim", 3, 204);
    auto playback = canonical.playbackData();
    Document audible(playback);
    equalSamples(canonical.song(), audible.song());
    check(render(playback, 17) == render(canonical.snapshotData(), 4096), "Edited native audio is block independent");

    Document small(MOD_TYPE_MOD);
    install(small, 1, 17, 8, 1);
    auto bytes = small.snapshotData();
    const auto parts = splitSongSnapshot(bytes);
    std::vector<std::byte> module(parts.module.begin(), parts.module.end()),
        archive(parts.samples.begin(), parts.samples.end());
    for (size_t n = 0; n < archive.size(); ++n) {
      auto truncated = std::span<const std::byte>(archive).first(n);
      if (n)
        rejects([&] { Document broken(packSongSnapshot(module, truncated)); });
    }
    auto malformed = [&](size_t at, uint32_t value, bool word = false) {
      auto changed = archive;
      if (word) {
        changed.at(at) = std::byte(value);
        changed.at(at + 1) = std::byte(value >> 8);
      } else
        put32(changed, at, value);
      rejects([&] { Document broken(packSongSnapshot(module, changed)); });
    };
    malformed(0, uint32_t(MOD_TYPE_IT));
    malformed(4, MAX_SAMPLES, true);
    malformed(6, MAX_INSTRUMENTS, true);
    malformed(8, 100, true);
    malformed(10, 65535, true);
    malformed(12, MAX_SAMPLE_LENGTH + 1);
    malformed(12 + 24, 257, true);
    malformed(12 + 30, 0x4000, true);
    malformed(12 + 130, 65536);
    malformed(12 + 134, 1);
    malformed(12 + 138, 0);
    for (auto [first, count] : {std::pair{12 + 39, 22}, std::pair{12 + 97, 32}}) {
      auto text = archive;
      std::fill(text.begin() + first, text.begin() + first + count, std::byte{'x'});
      rejects([&] { Document broken(packSongSnapshot(module, text)); });
    }
    auto badMode = archive;
    badMode.at(12 + 129) = std::byte{2};
    rejects([&] { Document broken(packSongSnapshot(module, badMode)); });
    badMode.at(12 + 129) = std::byte{0};
    rejects([&] { Document broken(packSongSnapshot(module, badMode)); });
    archive.push_back(std::byte{0});
    rejects([&] { Document broken(packSongSnapshot(module, archive)); });
    rejects([&] { Document broken(packSongSnapshot(bytes, parts.samples)); });
    auto trailing = bytes;
    trailing.push_back(std::byte{0});
    rejects([&] { Document broken(trailing); });
    put32(bytes, 8, UINT32_MAX);
    rejects([&] { Document broken(bytes); });
    std::cout << "PASS exact native samples across " << cases
              << " format/layout cases, trim and mixed history, XM maps, OPL, sparse storage, audio and malformed "
                 "snapshots\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL " << e.what() << '\n';
    return 1;
  }
}
