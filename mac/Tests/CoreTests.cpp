#include "editor/TrackerDocument.hpp"
#include "soundlib/ModInstrument.h"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace Tracker;
static void require(bool b, const char *m) {
  if (!b)
    throw std::runtime_error(m);
}
static void equalSongs(const CSoundFile &a, const CSoundFile &b) {
  require(a.GetNumChannels() == b.GetNumChannels(), "channel count roundtrip");
  for (int p = 0; p < a.Patterns.Size(); ++p)
    if (a.Patterns.IsValidPat(p)) {
      require(b.Patterns.IsValidPat(p), "pattern retained");
      require(a.Patterns[p].GetNumRows() == b.Patterns[p].GetNumRows(), "row count retained");
      for (int r = 0; r < a.Patterns[p].GetNumRows(); ++r)
        for (int c = 0; c < a.GetNumChannels(); ++c) {
          auto x = *a.Patterns[p].GetpModCommand(r, c), y = *b.Patterns[p].GetpModCommand(r, c);
          require(x == y, "pattern cells roundtrip");
        }
    }
  for (int i = 1; i <= a.GetNumSamples(); ++i) {
    auto &x = a.GetSample(i);
    auto &y = b.GetSample(i);
    if (!x.HasSampleData())
      continue;
    require(x.nLength == y.nLength, "sample length roundtrip");
    require(x.GetBytesPerSample() == y.GetBytesPerSample(), "sample format roundtrip");
    require(std::memcmp(x.samplev(), y.samplev(), x.GetSampleSizeInBytes()) == 0, "sample PCM roundtrip");
    require(x.nLoopStart == y.nLoopStart && x.nLoopEnd == y.nLoopEnd, "loop boundaries roundtrip");
  }
}
int main(int argc, char **argv) {
  try {
    auto doc = Document::demo();
    auto original = doc->serialize();
    auto before = doc->cell(0, 1, 0);
    Edit edit{0, 1, 0, before, {61, 1, 1, 40, 0, 0}};
    doc->edit({edit});
    require(doc->cell(0, 1, 0) == edit.after, "note edit");
    doc->undo();
    require(doc->cell(0, 1, 0) == before, "undo");
    doc->redo();
    require(doc->cell(0, 1, 0) == edit.after, "redo");
    auto bytes = doc->serialize();
    Document reopened(bytes);
    equalSongs(doc->song(), reopened.song());
    auto valid = Edit{0, 2, 0, {}, {65, 1, 1, 40, 0, 0}}, invalid = Edit{0, 999, 0, {}, {67, 1, 1, 40, 0, 0}};
    auto unchanged = doc->cell(0, 2, 0);
    bool rejected = false;
    try {
      doc->edit({valid, invalid});
    } catch (...) {
      rejected = true;
    }
    require(rejected && doc->cell(0, 2, 0) == unchanged, "bulk edit rejects atomically");
    doc->edit({valid, Edit{0, 3, 0, {}, valid.after}});
    doc->undo();
    require(doc->cell(0, 2, 0) == unchanged && doc->cell(0, 3, 0).note == 0, "bulk undo is one transaction");
    auto sample = doc->waveform(1, 256);
    doc->processSample(1, "reverse", 0, 0);
    doc->undo();
    require(doc->waveform(1, 256) == sample, "sample undo restores exact waveform");
    auto oldLength = doc->song().GetSample(1).nLength;
    doc->processSample(1, "trim", 32, 128);
    require(doc->song().GetSample(1).nLength == 96, "trim selection");
    doc->undo();
    require(doc->song().GetSample(1).nLength == oldLength, "trim undo");
    rejected = false;
    try {
      doc->sampleSettings(1, 22050, 16, 64, 200, 100, true, false);
    } catch (...) {
      rejected = true;
    }
    require(rejected && doc->song().GetSample(1).nVolume == 128, "invalid loop rolls back all settings");
    int pattern = doc->addPattern(64, true, 0);
    require(doc->cell(pattern, 0, 0) == doc->cell(0, 0, 0), "pattern duplication");
    require(doc->song().Order().size() == 2, "order appended");
    doc->undo();
    require(doc->song().Order().size() == 1, "structural undo");
    doc->redo();
    require(doc->song().Order().size() == 2, "structural redo");
    doc->transaction([](CSoundFile &song) { Document::resizeChannels(song, 127); });
    require(doc->song().GetNumChannels() == 127 && doc->cell(0, 1, 0) == edit.after,
            "channel expansion preserves row stride");
    doc->edit({Edit{0, 1, 126, {}, {73, 2, 0, 0, 0, 0}}});
    doc->transaction([](CSoundFile &song) { Document::resizeChannels(song, 4); });
    require(doc->song().GetNumChannels() == 4 && doc->cell(0, 1, 0) == edit.after,
            "channel reduction preserves retained cells");
    doc->undo();
    require(doc->song().GetNumChannels() == 127 && doc->cell(0, 1, 126).note == 73,
            "channel reduction undo restores removed notes");
    doc->undo();
    doc->undo();
    require(doc->song().GetNumChannels() == 8, "channel expansion undo");
    doc->sampleSettings(1, 22050, 32, 128, 0, 0, false, false, std::string("Renamed sample"));
    require(std::string(doc->song().GetSampleName(1)) == "Renamed sample", "sample rename");
    doc->undo();
    Renderer a(bytes, 48000), b(bytes, 48000);
    std::array<float, 1024> x{}, y{};
    double energy = 0, maxTime = 0;
    for (int i = 0; i < 1000; ++i) {
      auto start = std::chrono::steady_clock::now();
      a.render(x.data(), 128);
      maxTime = std::max(maxTime,
                         std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count());
      b.render(y.data(), 128);
      for (int j = 0; j < 256; ++j) {
        require(std::isfinite(x[j]), "finite output");
        require(x[j] == y[j], "deterministic render");
        energy += x[j] * x[j];
      }
    }
    require(energy > 0.1, "audible output");
    Renderer preview(original, 48000, 0, true);
    require(preview.preview({61, 1, 100, true}), "audition note on enqueue");
    double previewEnergy = 0;
    for (int block = 0; block < 100; ++block) {
      preview.render(x.data(), 128);
      for (int i = 0; i < 256; ++i)
        previewEnergy += x[i] * x[i];
    }
    require(previewEnergy > 0.1, "keyboard audition audible while transport is stopped");
    preview.panic();
    for (int block = 0; block < 100; ++block)
      preview.render(x.data(), 128);
    double silence = 0;
    for (int i = 0; i < 256; ++i)
      silence += x[i] * x[i];
    require(silence < 1e-8, "panic silences audition voices");
    Renderer queue(bytes, 48000);
    std::vector<Edit> batch(512, valid);
    for (int i = 0; i < 16; ++i)
      require(queue.enqueue(batch), "bounded queue accepts capacity");
    require(!queue.enqueue(batch), "queue saturation explicit");
    queue.render(x.data(), 128);
    require(queue.enqueue(batch), "queue recovers after consume");
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "resonance-tests";
    std::filesystem::create_directories(dir);
    auto path = dir / "demo.mptm";
    doc->save(path.string());
    auto saved = Document::open(path.string());
    equalSongs(doc->song(), saved->song());
    std::filesystem::path occupied = dir / "destination-directory";
    std::filesystem::create_directories(occupied);
    {
      std::ofstream marker(occupied / "keep.txt");
      marker << "original";
    }
    rejected = false;
    try {
      doc->save(occupied.string());
    } catch (...) {
      rejected = true;
    }
    require(rejected && std::filesystem::exists(occupied / "keep.txt"),
            "failed atomic replacement retains destination");
    for (auto &entry : std::filesystem::directory_iterator(dir))
      require(entry.path().filename().string().find("destination-directory.writing.") != 0,
              "failed save cleans temporary file");
    if (argc > 1) {
      auto sampleBeforeReplace = doc->waveform(1, 256);
      require(doc->importSample(std::string(argv[1]) + "/test/test.flac", 1) == 1, "sample replacement retains slot");
      require(doc->waveform(1, 256) != sampleBeforeReplace, "sample replacement changes PCM");
      doc->undo();
      require(doc->waveform(1, 256) == sampleBeforeReplace, "sample replacement undo restores PCM");
    }
    auto it = Document::demo(MOD_TYPE_IT);
    it->save((dir / "demo.it").string());
    auto sequences = Document::demo();
    const auto alternatePattern = sequences->addPattern(64, true, 0);
    sequences->transaction([&](CSoundFile &song) {
      song.Order(0).assign(1, 0);
      require(song.Order.AddSequence() == 1, "add alternate sequence");
      song.Order.SetSequence(1);
      song.Order().assign(1, PATTERNINDEX(alternatePattern));
      song.Order().SetDefaultTempoInt(96);
      for (auto &cell : song.Patterns[alternatePattern])
        if (cell.note > 0 && cell.note <= 108)
          cell.note += 12;
    });
    auto sequenceBytes = sequences->serialize();
    sequences->save((dir / "sequences.mptm").string());
    require(Document::open((dir / "sequences.mptm").string())->song().Order.GetCurrentSequenceIndex() == 1,
            "saved sequence selection survives module reopening");
    Renderer firstSequence(sequenceBytes, 48000), secondSequence(sequenceBytes, 48000, 0, false, {}, 1),
        savedSequence(sequenceBytes, 48000, 0, false, {}, UINT32_MAX);
    std::array<float, 1024> firstAudio{}, secondAudio{}, savedAudio{};
    firstSequence.render(firstAudio.data(), 512);
    secondSequence.render(secondAudio.data(), 512);
    savedSequence.render(savedAudio.data(), 512);
    require(firstAudio != secondAudio && secondAudio == savedAudio,
            "selected sequence reaches the correct audio renderer");
    require(secondSequence.telemetry().pattern == alternatePattern, "sequence-specific playhead pattern");
    bool invalidSequence = false;
    try {
      Renderer invalid(sequenceBytes, 48000, 0, false, {}, 99);
    } catch (...) {
      invalidSequence = true;
    }
    require(invalidSequence, "invalid sequence rejected before playback");
    auto loop = Document::demo();
    loop->transaction([](CSoundFile &s) {
      s.Patterns[0].GetpModCommand(0, 7)->command = CMD_S3MCMDEX;
      s.Patterns[0].GetpModCommand(0, 7)->param = 0xb0;
      s.Patterns[0].GetpModCommand(15, 7)->command = CMD_S3MCMDEX;
      s.Patterns[0].GetpModCommand(15, 7)->param = 0xb3;
      s.m_nInstruments = 4;
      for (int i = 1; i <= 4; ++i) {
        s.Instruments[i] = new ModInstrument(SAMPLEINDEX(i));
        s.Instruments[i]->VolEnv.push_back(0, 64);
        s.Instruments[i]->VolEnv.push_back(24, 0);
        s.Instruments[i]->VolEnv.dwFlags.set(ENV_ENABLED);
        s.Instruments[i]->nNNA = NewNoteAction::Continue;
      }
    });
    loop->save((dir / "loops.mptm").string());
    for (auto extension : {"iti", "xi"}) {
      const auto instrumentPath = dir / (std::string("instrument.") + extension);
      {
        std::ofstream file(instrumentPath, std::ios::binary);
        require(extension == std::string("iti") ? loop->song().SaveITIInstrument(1, file, {}, false, false)
                                                : loop->song().SaveXIInstrument(1, file),
                "write instrument fixture");
      }
      auto target = Document::demo();
      const auto originalCell = target->cell(0, 0, 0);
      const auto imported = target->importInstrument(instrumentPath.string());
      require(imported == 5 && target->song().GetNumInstruments() == 5,
              "import preserves sample-slot instruments before adding new one");
      require(target->cell(0, 0, 0) == originalCell, "instrument import retains existing music");
      const auto sampleIndex = target->song().Instruments[imported]->Keyboard[60];
      require(sampleIndex > 0 && target->song().GetSample(sampleIndex).HasSampleData(), "instrument sample loaded");
      require(target->song().Instruments[imported]->VolEnv == loop->song().Instruments[1]->VolEnv,
              "instrument envelope retained");
      target->undo();
      require(target->song().GetNumInstruments() == 0 && target->song().GetNumSamples() == 4, "instrument import undo");
    }
    for (auto rate : {44100, 48000, 96000}) {
      Renderer r(loop->serialize(), rate);
      uint64_t frames = 0;
      while (auto n = r.render(x.data(), 128)) {
        frames += n;
        require(!r.faulted(), "loop state capacity");
        require(frames < uint64_t(rate) * 60, "finite pattern loops terminate");
      }
      require(frames > uint64_t(rate) * 10, "pattern loop repeats");
    }
    std::cout << "PASS atomic edits, grouped undo, sample processing/rollback, pattern/order undo, exact native "
                 "roundtrip, bounded queue, loop termination at 44.1/48/96 kHz; max callback "
              << maxTime << " us\n";
    if (argc > 1)
      for (auto name : {"test.mod", "test.xm", "test.s3m", "test.mptm"}) {
        auto fixture = Document::open(std::string(argv[1]) + "/test/" + name);
        auto data = fixture->serialize();
        fixture->save((dir / name).string());
        Document loaded(data);
        equalSongs(fixture->song(), loaded.song());
        Renderer r(data, 48000);
        for (int i = 0; i < 1000; ++i)
          r.render(x.data(), 128);
        std::cout << "PASS fixture " << name << "\n";
      }
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
  }
}
