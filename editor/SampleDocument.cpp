#include "TrackerDocument.hpp"
#include <algorithm>
#include <cstring>

namespace Tracker {
std::vector<SampleSnapResult> Document::snapSample(int sample, std::span<const uint32_t> positions,
                                                const SampleSnapOptions &options) const {
  return snapSampleBoundaries(samplePCM(sample), positions, options);
}
SamplePCMView Document::samplePCM(int index) const {
  if (index < 1 || index > song_->GetNumSamples())
    throw std::invalid_argument("Select a sample first.");
  const auto &s = song_->GetSample(SAMPLEINDEX(index));
  if (s.nLength && !s.HasSampleData())
    throw std::runtime_error("Sample data is missing");
  return {{s.sampleb(), size_t(s.nLength) * s.GetBytesPerSample()},
          s.nLength,
          uint8_t(s.uFlags[CHN_16BIT] ? 16 : 8),
          s.GetNumChannels()};
}
size_t Document::UndoEntry::bytes() const {
  size_t total = sizeof(UndoEntry) + before.capacity() + after.capacity() + cells.capacity() * sizeof(Edit) +
                 (nativeBefore ? nativeBefore->bytes() : 0) + (nativeAfter ? nativeAfter->bytes() : 0);
  if (sample) {
    total += sample->chunks.capacity() * sizeof(SamplePCMChunk);
    for (const auto &chunk : sample->chunks)
      total += chunk.before.capacity() + chunk.after.capacity();
  }
  if (splice)
    total += splice->removed.capacity() + splice->inserted.capacity();
  if (slot)
    total += slot->data.capacity();
  return total;
}
size_t Document::historyBytes() const {
  size_t total = 0;
  for (const auto &e : undo_)
    total += e.bytes();
  for (const auto &e : redo_)
    total += e.bytes();
  return total;
}
void Document::validateSampleUndo(const SampleUndo &edit, bool redo) const {
  const auto pcm = samplePCM(edit.sample);
  pcm.validate();
  if (pcm.frames != edit.frames || pcm.bits != edit.bits || pcm.channels != edit.channels)
    throw std::runtime_error("Sample undo layout does not match the document");
  if (edit.geometry && (edit.geometry->first.frames != edit.frames || edit.geometry->second.frames != edit.frames ||
      sampleGeometry(song_->GetSample(edit.sample)) != (redo ? edit.geometry->first : edit.geometry->second)))
    throw std::runtime_error("Sample loop geometry changed outside its edit history");
  uint32_t previousEnd = 0;
  for (const auto &chunk : edit.chunks) {
    if (!chunk.frames || chunk.first < previousEnd || chunk.first > pcm.frames ||
        chunk.frames > pcm.frames - chunk.first || chunk.before.size() != size_t(chunk.frames) * pcm.stride() ||
        chunk.after.size() != chunk.before.size())
      throw std::runtime_error("Invalid sample undo range");
    const auto &expected = redo ? chunk.before : chunk.after;
    if (std::memcmp(pcm.data.data() + size_t(chunk.first) * pcm.stride(), expected.data(), expected.size()))
      throw std::runtime_error("Sample changed outside its edit history");
    previousEnd = chunk.first + chunk.frames;
  }
}
void Document::applySampleUndo(const SampleUndo &edit, bool redo) noexcept {
  auto &s = song_->GetSample(edit.sample);
  const SamplePCMView pcm{
      {s.sampleb(), size_t(s.nLength) * s.GetBytesPerSample()}, s.nLength, edit.bits, edit.channels};
  for (const auto &chunk : edit.chunks) {
    const auto &data = redo ? chunk.after : chunk.before;
    std::memcpy(s.sampleb() + size_t(chunk.first) * pcm.stride(), data.data(), data.size());
    if (waveformSample_ == edit.sample)
      waveformCache_.changed(pcm, chunk.first, chunk.first + chunk.frames);
  }
  auto geometry = [&] {
    if (!edit.geometry) return;
    const auto &g = redo ? edit.geometry->second : edit.geometry->first;
    s.nLoopStart = g.loopStart; s.nLoopEnd = g.loopEnd;
    s.nSustainStart = g.sustainStart; s.nSustainEnd = g.sustainEnd;
    s.uFlags = SampleFlags(static_cast<ChannelFlags>(g.flags)); s.cues = g.cues;
    s.nativeReverseLoops = g.reverseLoops;
  };
  geometry();
  s.PrecomputeLoops(*song_, false);
  geometry(); // Retain exact inactive metadata, matching structural sample history.
}
Document::PreparedSampleProcess Document::prepareSampleProcess(int index, const SampleProcessOptions &options) const {
  if (!editable())
    throw std::runtime_error("This document is read-only.");
  PreparedSampleProcess prepared;
  prepared.owner_ = this;
  prepared.revision_ = revision;
  prepared.sample_ = index;
  prepared.plan_ = planSampleProcess(samplePCM(index), options);
  return prepared;
}
SampleProcessResult Document::processSample(int index, const SampleProcessOptions &options, bool dryRun) {
  auto prepared = prepareSampleProcess(index, options);
  if (dryRun)
    return std::move(prepared.plan_.result);
  return applySampleProcess(std::move(prepared));
}
Document::PreparedSampleProcess Document::prepareSampleDraw(int index, const SampleDrawOptions &options) const {
  if (!editable())
    throw std::invalid_argument("This document is read-only");
  PreparedSampleProcess p;
  p.owner_ = this;
  p.revision_ = revision;
  p.sample_ = index;
  p.plan_ = planSampleDraw(samplePCM(index), options);
  return p;
}
Document::PreparedSampleProcess Document::prepareSampleCrossfade(int index, const SampleCrossfadeOptions &options) const {
  if (!editable()) throw std::invalid_argument("This document is read-only");
  const auto pcm = samplePCM(index);
  const auto &sample = song_->GetSample(index);
  const auto [start, end] = options.sustain ? sample.GetSustainLoop() : sample.GetLoop();
  if (!(options.sustain ? sample.HasSustainLoop() : sample.HasLoop()) ||
      (options.sustain ? sample.HasPingPongSustainLoop() : sample.HasPingPongLoop()) ||
      (sample.nativeReverseLoops & (options.sustain ? 2 : 1)))
    throw std::invalid_argument("Crossfade needs an enabled forward loop; reverse and ping-pong loops are not supported");
  auto plan = planSampleCrossfade(pcm, start, end, options);
  PreparedSampleProcess p;
  p.owner_ = this; p.revision_ = revision; p.sample_ = index;
  auto before = sampleGeometry(sample), after = before;
  if (options.sustain) after.sustainStart = plan.loopStart;
  else after.loopStart = plan.loopStart;
  p.geometry_ = std::make_pair(before, after);
  for (const auto first : {plan.sourceStart, plan.pcm.result.first}) {
    SamplePCMChunk guard;
    guard.first = first; guard.frames = options.frames;
    guard.before.assign(pcm.data.begin() + size_t(first) * pcm.stride(), pcm.data.begin() + size_t(first + options.frames) * pcm.stride());
    p.guards_.push_back(std::move(guard));
  }
  p.plan_ = std::move(plan.pcm);
  if (p.hasChanges()) p.plan_.result.historyBytes += sizeof(*p.geometry_);
  return p;
}
Document::PreparedSampleProcess Document::prepareSampleLoops(int index,
    const std::optional<SampleLoopSettings> &normal, const std::optional<SampleLoopSettings> &sustain) const {
  if (!editable()) throw std::invalid_argument("This document is read-only");
  const auto pcm = samplePCM(index);
  if (song_->GetSample(index).uFlags[CHN_ADLIB])
    throw std::invalid_argument("An OPL instrument has no sample loops");
  if (!normal && !sustain) throw std::invalid_argument("Supply a normal or sustain loop");
  auto before = sampleGeometry(song_->GetSample(index)), after = before;
  auto set = [&](const SampleLoopSettings &loop, bool held) {
    if (loop.start > pcm.frames || loop.end > pcm.frames || loop.start > loop.end ||
        (loop.enabled && loop.start == loop.end))
      throw std::invalid_argument("Loop boundaries must be inside the sample; enabled loops must be nonempty");
    if (loop.pingpong && !loop.enabled)
      throw std::invalid_argument("Ping-pong requires an enabled loop");
    if (loop.reverse && (!loop.enabled || loop.pingpong))
      throw std::invalid_argument("Reverse requires an enabled loop without ping-pong");
    (held ? after.sustainStart : after.loopStart) = loop.start;
    (held ? after.sustainEnd : after.loopEnd) = loop.end;
    const auto enabled = held ? CHN_SUSTAINLOOP : CHN_LOOP;
    const auto pingpong = held ? CHN_PINGPONGSUSTAIN : CHN_PINGPONGLOOP;
    after.flags = (after.flags & ~uint16_t((enabled | pingpong).as_bits())) |
                  (loop.enabled ? uint16_t(enabled) : 0) | (loop.pingpong ? uint16_t(pingpong) : 0);
    const uint8_t bit = held ? 2 : 1;
    after.reverseLoops = (after.reverseLoops & ~bit) | (loop.reverse ? bit : 0);
  };
  if (normal) set(*normal, false);
  if (sustain) set(*sustain, true);
  PreparedSampleProcess p;
  p.owner_ = this; p.revision_ = revision; p.sample_ = index;
  p.plan_.totalFrames = pcm.frames; p.plan_.bits = pcm.bits; p.plan_.channels = pcm.channels;
  p.geometry_ = std::make_pair(before, after);
  if (p.hasChanges()) p.plan_.result.historyBytes = sizeof(SampleUndo);
  return p;
}
SampleProcessResult Document::applySampleProcess(PreparedSampleProcess prepared) {
  if (prepared.owner_ != this || prepared.revision_ != revision)
    throw std::runtime_error("Song changed; prepare the sample edit again");
  if (!editable())
    throw std::runtime_error("This document is read-only.");
  auto &plan = prepared.plan_;
  auto result = std::move(plan.result);
  if (prepared.geometry_) {
    const auto pcm = samplePCM(prepared.sample_);
    if (pcm.frames != plan.totalFrames || pcm.bits != plan.bits || pcm.channels != plan.channels ||
        sampleGeometry(song_->GetSample(prepared.sample_)) != prepared.geometry_->first)
      throw std::runtime_error("Sample loop geometry changed after preparation");
    for (const auto &guard : prepared.guards_) {
      if (guard.first > pcm.frames || guard.frames > pcm.frames - guard.first ||
          guard.before.size() != size_t(guard.frames) * pcm.stride() ||
          std::memcmp(pcm.data.data() + size_t(guard.first) * pcm.stride(), guard.before.data(), guard.before.size()))
        throw std::runtime_error("Crossfade source audio changed after preparation");
    }
  }
  if (!prepared.hasChanges())
    return result;
  UndoEntry entry;
  entry.sample =
      SampleUndo{uint16_t(prepared.sample_), plan.totalFrames, plan.bits, plan.channels, std::move(plan.chunks), std::move(prepared.geometry_)};
  undo_.reserve(undo_.size() + 1);
  validateSampleUndo(*entry.sample, true);
  applySampleUndo(*entry.sample, true); // Everything that can allocate/throw precedes mutation.
  undo_.push_back(std::move(entry));
  redo_.clear();
  ++revision;
  trimHistory();
  return result;
}
std::vector<float> Document::waveform(int index, size_t bins) const {
  if (index < 1 || index > song_->GetNumSamples())
    return std::vector<float>(bins * 2, 0);
  return waveform(index, 0, song_->GetSample(SAMPLEINDEX(index)).nLength, bins, SampleChannels::Both);
}
std::vector<float> Document::waveform(int index, uint32_t first, uint32_t last, size_t bins,
                                      SampleChannels channels) const {
  const auto pcm = samplePCM(index);
  if (waveformSample_ != index) {
    waveformCache_.invalidate();
    waveformSample_ = index;
  }
  return waveformCache_.query(pcm, first, last, bins, channels);
}
} // namespace Tracker
