#include "TrackerDocument.hpp"
#include <algorithm>
#include <cstring>

namespace Tracker {
namespace {
void require(bool condition, const char *message) {
  if (!condition)
    throw std::invalid_argument(message);
}
} // namespace
Document::SampleGeometry Document::sampleGeometry(const ModSample &s) {
  return {s.nLength, s.nLoopStart, s.nLoopEnd, s.nSustainStart, s.nSustainEnd, s.uFlags.GetRaw(), s.cues, s.nativeReverseLoops};
}
SampleClipboard Document::copySample(int index, uint32_t first, uint32_t last, SampleChannels channels) const {
  const auto pcm = samplePCM(index);
  const auto &s = song_->GetSample(index);
  require(!s.uFlags[CHN_ADLIB], "An OPL instrument has no PCM clipboard data");
  return copySamplePCM(pcm, s.GetSampleRate(song_->GetType()), first, last, channels,
                       ::OpenMPT::mpt::ToCharset(::OpenMPT::mpt::Charset::UTF8, song_->GetCharsetInternal(),
                                                 song_->m_szNames[index].str()));
}
Document::PreparedSampleEdit Document::prepareSamplePaste(int sample, const SampleClipboard &clipboard,
                                                          const SamplePasteOptions &options) const {
  const auto pcm = samplePCM(sample);
  require(!song_->GetSample(sample).uFlags[CHN_ADLIB], "Cannot paste PCM into an OPL instrument");
  return prepareSampleSplice(
      sample, planSamplePaste(pcm, song_->GetSample(sample).GetSampleRate(song_->GetType()), clipboard, options));
}
Document::PreparedSampleEdit Document::prepareSampleErase(int sample, uint32_t first, uint32_t last) const {
  const auto pcm = samplePCM(sample);
  require(!song_->GetSample(sample).uFlags[CHN_ADLIB], "Cannot delete PCM from an OPL instrument");
  return prepareSampleSplice(sample, planSampleErase(pcm, first, last));
}
Document::PreparedSampleEdit Document::prepareSampleSplice(int sample, SampleSplicePlan plan) const {
  require(editable(), "This document is read-only");
  PreparedSampleEdit prepared;
  prepared.owner_ = this;
  prepared.revision_ = revision;
  prepared.result_ = std::move(plan.result);
  prepared.result_.before = prepared.result_.after = sampleGeometry(song_->GetSample(sample));
  if (!plan.changesLength() && !prepared.result_.changedSamples) {
    prepared.result_.historyBytes = 0;
    return prepared;
  }
  if (!plan.changesLength()) {
    // The result still contains scalar geometry after its preview was moved.
    auto patches = sampleSplicePatches(plan);
    prepared.entry_.sample =
        SampleUndo{uint16_t(sample), patches.totalFrames, patches.bits, patches.channels, std::move(patches.chunks)};
    validateSampleUndo(*prepared.entry_.sample, true);
  } else {
    const auto before = sampleGeometry(song_->GetSample(sample));
    auto after = before;
    const auto &result = prepared.result_;
    after.frames = result.resultFrames;
    const auto first = result.first, last = first + result.removedFrames, inserted = result.insertedFrames;
    auto point = [&](uint32_t p, bool end) {
      if (p > before.frames)
        return p;
      if (!result.removedFrames)
        return p < first || (end && p == first) ? p : p + inserted;
      if (p <= first)
        return p;
      if (p < last)
        return first;
      return p - result.removedFrames + inserted;
    };
    after.loopStart = std::min(after.frames, point(before.loopStart, false));
    after.loopEnd = std::min(after.frames, point(before.loopEnd, true));
    after.sustainStart = std::min(after.frames, point(before.sustainStart, false));
    after.sustainEnd = std::min(after.frames, point(before.sustainEnd, true));
    if (after.loopStart >= after.loopEnd) {
      after.reverseLoops &= ~uint8_t(1);
      after.flags &= ~uint16_t((CHN_LOOP | CHN_PINGPONGLOOP).as_bits());
      after.loopStart = after.loopEnd = 0;
    }
    if (after.sustainStart >= after.sustainEnd) {
      after.reverseLoops &= ~uint8_t(2);
      after.flags &= ~uint16_t((CHN_SUSTAINLOOP | CHN_PINGPONGSUSTAIN).as_bits());
      after.sustainStart = after.sustainEnd = 0;
    }
    for (size_t i = 0; i < after.cues.size(); ++i)
      after.cues[i] = before.cues[i] < before.frames ? point(before.cues[i], false) : MAX_SAMPLE_LENGTH;
    prepared.entry_.splice =
        SampleSpliceUndo{uint16_t(sample), first, before, after, std::move(plan.removed), std::move(plan.inserted)};
    prepared.result_.after = after;
    prepared.replacement_ = prepareSpliceUndo(*prepared.entry_.splice, true);
  }
  prepared.result_.historyBytes = prepared.entry_.bytes();
  return prepared;
}
void Document::validateSpliceUndo(const SampleSpliceUndo &edit, bool redo) const {
  const auto pcm = samplePCM(edit.sample);
  pcm.validate();
  const auto &before = redo ? edit.before : edit.after, &after = redo ? edit.after : edit.before;
  const auto &removed = redo ? edit.removed : edit.inserted, &inserted = redo ? edit.inserted : edit.removed;
  require(sampleGeometry(song_->GetSample(edit.sample)) == before, "Sample geometry changed outside its edit history");
  require(removed.size() % pcm.stride() == 0 && inserted.size() % pcm.stride() == 0 && edit.first <= before.frames &&
              removed.size() / pcm.stride() <= before.frames - edit.first &&
              uint64_t(before.frames) - removed.size() / pcm.stride() + inserted.size() / pcm.stride() ==
                  after.frames &&
              after.frames <= MAX_SAMPLE_LENGTH && uint64_t(after.frames) * pcm.stride() <= 512 * 1024 * 1024,
          "Invalid sample splice geometry");
  require(removed.empty() ||
              std::memcmp(pcm.data.data() + size_t(edit.first) * pcm.stride(), removed.data(), removed.size()) == 0,
          "Sample audio changed outside its edit history");
}
Document::SampleAllocation Document::prepareSpliceUndo(const SampleSpliceUndo &edit, bool redo) const {
  validateSpliceUndo(edit, redo);
  const auto pcm = samplePCM(edit.sample);
  const auto &after = redo ? edit.after : edit.before;
  const auto &removed = redo ? edit.removed : edit.inserted, &inserted = redo ? edit.inserted : edit.removed;
  SampleAllocation allocation;
  if (!after.frames)
    return allocation;
  allocation.reset(ModSample::AllocateSample(after.frames, pcm.stride()));
  if (!allocation)
    throw std::bad_alloc();
  auto *out = static_cast<std::byte *>(allocation.get());
  const size_t prefix = size_t(edit.first) * pcm.stride(), suffix = pcm.data.size() - prefix - removed.size();
  if (prefix)
    std::memcpy(out, pcm.data.data(), prefix);
  if (!inserted.empty())
    std::memcpy(out + prefix, inserted.data(), inserted.size());
  if (suffix)
    std::memcpy(out + prefix + inserted.size(), pcm.data.data() + prefix + removed.size(), suffix);
  return allocation;
}
void Document::applySpliceUndo(const SampleSpliceUndo &edit, bool redo, SampleAllocation allocation) noexcept {
  const auto &state = redo ? edit.after : edit.before;
  auto &s = song_->GetSample(edit.sample);
  s.FreeSample();
  s.pData.pSample = allocation.release();
  s.nLength = state.frames;
  s.nLoopStart = state.loopStart;
  s.nLoopEnd = state.loopEnd;
  s.nSustainStart = state.sustainStart;
  s.nSustainEnd = state.sustainEnd;
  s.uFlags = SampleFlags(static_cast<ChannelFlags>(state.flags));
  s.cues = state.cues;
  s.nativeReverseLoops = state.reverseLoops;
  s.PrecomputeLoops(*song_, false);
  // Restore even inactive loop metadata exactly after guard-buffer preparation.
  s.nLoopStart = state.loopStart;
  s.nLoopEnd = state.loopEnd;
  s.nSustainStart = state.sustainStart;
  s.nSustainEnd = state.sustainEnd;
  s.uFlags = SampleFlags(static_cast<ChannelFlags>(state.flags));
  if (waveformSample_ == edit.sample)
    waveformCache_.invalidate();
}
SampleSpliceResult Document::applySampleEdit(PreparedSampleEdit prepared) {
  require(prepared.owner_ == this && prepared.revision_ == revision, "Song changed; prepare the sample edit again");
  if (!prepared.hasChanges())
    return std::move(prepared.result_);
  undo_.reserve(undo_.size() + 1);
  auto &entry = prepared.entry_;
  if (entry.sample)
    validateSampleUndo(*entry.sample, true);
  if (entry.splice) {
    const auto &e = *entry.splice;
    validateSpliceUndo(e, true);
    const auto pcm = samplePCM(e.sample);
    const auto *copy = static_cast<const std::byte *>(prepared.replacement_.get());
    const size_t prefix = size_t(e.first) * pcm.stride(), suffix = pcm.data.size() - prefix - e.removed.size();
    require((!prefix || std::memcmp(copy, pcm.data.data(), prefix) == 0) &&
                (!suffix || std::memcmp(copy + prefix + e.inserted.size(), pcm.data.data() + prefix + e.removed.size(),
                                        suffix) == 0),
            "Sample changed after the splice was prepared");
  }
  if (entry.sample)
    applySampleUndo(*entry.sample, true);
  if (entry.splice)
    applySpliceUndo(*entry.splice, true, std::move(prepared.replacement_));
  undo_.push_back(std::move(entry));
  redo_.clear();
  ++revision;
  trimHistory();
  return std::move(prepared.result_);
}
} // namespace Tracker
