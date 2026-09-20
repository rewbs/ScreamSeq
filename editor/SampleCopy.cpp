#include "TrackerDocument.hpp"
#include "soundlib/ModInstrument.h"
#include "soundlib/mod_specifications.h"
#include <algorithm>
#include <cstring>
#include <tuple>

namespace Tracker {
namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::invalid_argument(message);
}
bool sameHeader(const ModSample &a, const ModSample &b) {
  auto fields = [](const ModSample &s) {
    return std::tuple(s.nLength, s.nLoopStart, s.nLoopEnd, s.nSustainStart, s.nSustainEnd, s.nC5Speed, s.nPan,
                      s.nVolume, s.nGlobalVol, s.uFlags.GetRaw(), s.RelativeTone, s.nFineTune, s.nVibType, s.nVibSweep,
                      s.nVibDepth, s.nVibRate, s.rootNote, s.cues, s.nativeReverseLoops);
  };
  return fields(a) == fields(b) && std::memcmp(a.filename.buf, b.filename.buf, MAX_SAMPLEFILENAME) == 0;
}
} // namespace
Document::PreparedSampleCopy Document::prepareSampleCopy(int source, uint32_t first, uint32_t last,
                                                         SampleChannels channels,
                                                         const std::optional<std::string> &name) const {
  require(editable(), "This document is read-only");
  native_.validate(*song_);
  require(!name || (name->size() <= 800 && name->find('\0') == std::string::npos), "Invalid sample name");
  auto clip = copySample(source, first, last, channels);
  const auto count = song_->GetNumSamples(),
             maximum = std::min<uint16_t>(song_->GetModSpecifications().samplesMax, MAX_SAMPLES - 1);
  uint16_t slot = count < maximum ? count + 1 : 0;
  if (!slot) {
    std::vector<bool> referenced(MAX_SAMPLES, false);
    if (song_->GetNumInstruments()) {
      for (INSTRUMENTINDEX i = 1; i <= song_->GetNumInstruments(); ++i)
        if (song_->Instruments[i])
          for (auto s : song_->Instruments[i]->Keyboard)
            if (s < referenced.size())
              referenced[s] = true;
    } else {
      for (PATTERNINDEX i = 0; i < song_->Patterns.Size(); ++i)
        if (song_->Patterns.IsValidPat(i))
          for (const auto &cell : song_->Patterns[i])
            if (cell.instr < referenced.size())
              referenced[cell.instr] = true;
    }
    for (uint16_t i = 1; i <= count && i <= maximum; ++i) {
      const auto &s = song_->GetSample(i);
      const auto &entity = native_.samples.at(i);
      if (i != source && !referenced[i] && !s.nLength && !s.samplev() && !s.uFlags[CHN_ADLIB] &&
          !song_->SampleHasPath(i) && song_->m_szNames[i].str().empty() && entity.name.empty() &&
          entity.annotation.empty() && !entity.color) {
        slot = i;
        break;
      }
    }
  }
  require(slot != 0, "Sample slots are full; no unnamed, unreferenced empty slot is available");
  PreparedSampleCopy p;
  p.owner_ = this;
  p.revision_ = revision;
  p.source_ = source;
  p.first_ = first;
  p.last_ = last;
  p.channels_ = channels;
  p.sourceHeader_ = song_->GetSample(source);
  p.sourceHeader_.pData.pSample = nullptr;
  SampleSlotUndo edit;
  edit.sample = slot;
  edit.countBefore = count;
  edit.countAfter = std::max(count, slot);
  edit.before = song_->GetSample(slot);
  edit.before.pData.pSample = nullptr;
  edit.nameBefore = song_->m_szNames[slot];
  edit.after = p.sourceHeader_;
  edit.after.nLength = last - first;
  edit.after.uFlags.set(CHN_STEREO, clip.channels == 2);
  edit.after.uFlags.reset(SMP_KEEPONDISK | SMP_MODIFIED);
  auto bound = [&](uint32_t value) { return std::clamp(value, first, last) - first; };
  edit.after.nLoopStart = bound(edit.after.nLoopStart);
  edit.after.nLoopEnd = bound(edit.after.nLoopEnd);
  edit.after.nSustainStart = bound(edit.after.nSustainStart);
  edit.after.nSustainEnd = bound(edit.after.nSustainEnd);
  if (edit.after.nLoopStart >= edit.after.nLoopEnd) {
    edit.after.nativeReverseLoops &= ~uint8_t(1);
    edit.after.nLoopStart = edit.after.nLoopEnd = 0;
    edit.after.uFlags.reset(CHN_LOOP | CHN_PINGPONGLOOP);
  }
  if (edit.after.nSustainStart >= edit.after.nSustainEnd) {
    edit.after.nativeReverseLoops &= ~uint8_t(2);
    edit.after.nSustainStart = edit.after.nSustainEnd = 0;
    edit.after.uFlags.reset(CHN_SUSTAINLOOP | CHN_PINGPONGSUSTAIN);
  }
  for (auto &cue : edit.after.cues)
    cue = cue >= first && cue < last ? cue - first : MAX_SAMPLE_LENGTH;
  edit.nameAfter = name ? ::OpenMPT::mpt::ToCharset(song_->GetCharsetInternal(), ::OpenMPT::mpt::Charset::UTF8, *name)
                        : song_->m_szNames[source].str();
  edit.data = std::move(clip.data);
  p.entry_.nativeBefore = native_;
  p.entry_.nativeAfter = native_;
  auto entity = native_.samples.at(source);
  entity.id = p.entry_.nativeAfter->makeEntity().id;
  p.entry_.nativeAfter->samples[slot] = std::move(entity);
  p.entry_.slot = std::move(edit);
  p.allocation_ = prepareSampleSlot(*p.entry_.slot, true);
  return p;
}
void Document::validateSampleSlot(const SampleSlotUndo &edit, bool redo) const {
  const auto count = redo ? edit.countBefore : edit.countAfter;
  const auto &header = redo ? edit.before : edit.after;
  const auto &name = redo ? edit.nameBefore : edit.nameAfter;
  const auto &current = song_->GetSample(edit.sample);
  require(song_->GetNumSamples() == count && sameHeader(current, header) &&
              std::memcmp(song_->m_szNames[edit.sample].buf, name.buf, MAX_SAMPLENAME) == 0 &&
              !song_->SampleHasPath(edit.sample),
          "Sample slot changed outside its edit history");
  require(!edit.before.nLength && !edit.before.samplev() && !edit.before.uFlags[CHN_ADLIB] && edit.after.nLength &&
              !edit.after.samplev() && !edit.after.uFlags[CHN_ADLIB] &&
              edit.data.size() == size_t(edit.after.nLength) * edit.after.GetBytesPerSample(),
          "Invalid prepared sample copy");
  require(redo ? !current.samplev()
               : current.samplev() && std::memcmp(current.samplev(), edit.data.data(), edit.data.size()) == 0,
          "Sample audio changed outside its edit history");
}
Document::SampleAllocation Document::prepareSampleSlot(const SampleSlotUndo &edit, bool redo) const {
  validateSampleSlot(edit, redo);
  if (!redo)
    return {};
  SampleAllocation memory(ModSample::AllocateSample(edit.after.nLength, edit.after.GetBytesPerSample()));
  if (!memory)
    throw std::bad_alloc();
  std::memcpy(memory.get(), edit.data.data(), edit.data.size());
  return memory;
}
void Document::applySampleSlot(const SampleSlotUndo &edit, bool redo, SampleAllocation memory) noexcept {
  auto &sample = song_->GetSample(edit.sample);
  sample.FreeSample();
  sample = redo ? edit.after : edit.before;
  sample.pData.pSample = memory.release();
  song_->m_szNames[edit.sample] = redo ? edit.nameAfter : edit.nameBefore;
  song_->m_nSamples = redo ? edit.countAfter : edit.countBefore;
  sample.PrecomputeLoops(*song_, false);
  // Guard preparation may sanitize inactive bounds. Keep the saved header exact.
  void *data = sample.samplev();
  sample = redo ? edit.after : edit.before;
  sample.pData.pSample = data;
  if (waveformSample_ == edit.sample)
    waveformCache_.invalidate();
}
int Document::applySampleCopy(PreparedSampleCopy prepared) {
  require(prepared.owner_ == this && prepared.revision_ == revision, "Song changed; prepare the sample copy again");
  require(sameHeader(song_->GetSample(prepared.source_), prepared.sourceHeader_),
          "Source sample settings changed after preparation");
  const auto source = samplePCM(prepared.source_);
  const auto &edit = *prepared.entry_.slot;
  SamplePCMView expected{edit.data, edit.after.nLength, uint8_t(edit.after.GetElementarySampleSize() * 8),
                         edit.after.GetNumChannels()};
  for (uint32_t f = 0; f < expected.frames; ++f)
    for (uint8_t c = 0; c < expected.channels; ++c) {
      const auto channel = prepared.channels_ == SampleChannels::Both    ? c
                           : prepared.channels_ == SampleChannels::Right ? 1
                                                                         : 0;
      require(source.value(prepared.first_ + f, channel) == expected.value(f, c),
              "Source sample audio changed after preparation");
    }
  validateSampleSlot(edit, true);
  auto metadata = *prepared.entry_.nativeAfter;
  undo_.reserve(undo_.size() + 1);
  const int result = edit.sample;
  applySampleSlot(edit, true, std::move(prepared.allocation_));
  native_ = std::move(metadata);
  undo_.push_back(std::move(prepared.entry_));
  redo_.clear();
  ++revision;
  trimHistory();
  return result;
}
} // namespace Tracker
