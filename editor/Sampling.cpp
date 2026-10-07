#include "Sampling.hpp"
#include "TrackLayout.hpp"
#include "soundlib/ModInstrument.h"
#include "soundlib/mod_specifications.h"
#include <cmath>
#include "mpt/string_transcode/transcode.hpp"

namespace Tracker {
SamplingImportResult validateRecordedAudioImport(const Document &document, uint32_t frames, uint32_t rate,
                                                 uint32_t channels, const std::string &name, bool instrument) {
  if(!document.editable()) throw std::invalid_argument("This document is read-only");
  if(!frames || frames > maximumRecordedSampleFrames) throw std::invalid_argument("A recorded sample needs 1 to 16777216 frames");
  if(rate < 8000 || rate > 384000 || channels < 1 || channels > 2) throw std::invalid_argument("Recorded audio requires mono/stereo at 8000 to 384000 Hz");
  if(name.empty() || name.size() > 128 || name.find('\0') != std::string::npos || !::mpt::is_utf8(name)) throw std::invalid_argument("Use a sample name of 1 to 128 UTF-8 bytes");
  const auto &song = document.song(); const auto &spec = song.GetModSpecifications();
  const int sample = int(song.GetNumSamples()) + 1;
  const int slot = (song.GetNumInstruments() ? song.GetNumInstruments() : song.GetNumSamples()) + 1;
  if(sample > std::min<int>(spec.samplesMax, MAX_SAMPLES - 1)) throw std::invalid_argument("No free sample slots in this song format");
  if(instrument && (!spec.instrumentsMax || slot > std::min<int>(spec.instrumentsMax, MAX_INSTRUMENTS - 1)))
    throw std::invalid_argument("No free instrument slots; import the recording as a sample");
  return {sample, instrument ? slot : 0, frames, rate, channels, 0, instrument && !song.GetNumInstruments()};
}
SamplingImportResult importRecordedAudio(Document &document, std::span<const float> pcm, uint32_t rate,
                                         uint32_t channels, const std::string &name, bool instrument, bool dryRun) {
  if(channels < 1 || channels > 2 || pcm.size() % channels || pcm.size() / channels > maximumRecordedSampleFrames)
    throw std::invalid_argument("Recorded audio has invalid channels or frame count");
  auto result = validateRecordedAudioImport(document, uint32_t(pcm.size()/channels), rate, channels, name, instrument);
  for(const float value : pcm) {
    if(!std::isfinite(value)) throw std::invalid_argument("Recorded audio contains non-finite samples");
    if(value < -1 || value > 1) ++result.clippedValues;
  }
  if(dryRun) return result;
  document.transaction([&](CSoundFile &song, NativeSong &native) {
    const auto oldSamples = song.GetNumSamples();
    if(result.convertsToInstruments) for(SAMPLEINDEX i = 1; i <= oldSamples; ++i) {
      song.Instruments[i] = new ModInstrument(i);
      song.Instruments[i]->name = song.GetSampleName(i); song.m_nInstruments = i;
    }
    const auto index = SAMPLEINDEX(result.sample); auto &sample = song.GetSample(index);
    sample.Initialize(song.GetType()); sample.nLength = result.frames; sample.nC5Speed = rate;
    if(song.GetType() & (MOD_TYPE_MOD | MOD_TYPE_XM)) sample.FrequencyToTranspose();
    sample.uFlags.set(CHN_16BIT); sample.uFlags.set(CHN_STEREO, channels == 2);
    if(!sample.AllocateSample()) throw std::runtime_error("Cannot allocate recorded sample");
    for(size_t i = 0; i < pcm.size(); ++i)
      sample.sample16()[i] = int16_t(std::clamp<long>(std::lround(double(std::clamp(pcm[i], -1.f, 1.f))*32768.0), -32768, 32767));
    song.m_nSamples = index;
    song.m_szNames[index] = ::OpenMPT::mpt::ToCharset(song.GetCharsetInternal(), ::OpenMPT::mpt::Charset::UTF8, name);
    sample.PrecomputeLoops(song, false);
    if(instrument) {
      const auto target = INSTRUMENTINDEX(result.instrument);
      song.Instruments[target] = new ModInstrument(index);
      song.Instruments[target]->name = song.GetSampleName(index); song.m_nInstruments = target;
    }
    native.reconcile(song);
    native.samples.at(index).name = name;
    if(instrument) native.instruments.at(uint16_t(result.instrument)).name = name;
  });
  return result;
}
namespace {
bool transportCommand(uint8_t command, uint8_t parameter) {
  return command == CMD_POSITIONJUMP || command == CMD_PATTERNBREAK
    || (command == CMD_MODCMDEX && (parameter & 0xf0) == 0x60)
    || (command == CMD_S3MCMDEX && (parameter & 0xf0) == 0xb0);
}
}
void validateSamplingSelection(const Document &document, const SamplingSelection &selection) {
  const auto &song = document.song();
  if(!song.Patterns.IsValidPat(selection.pattern) || selection.firstRow > selection.lastRow
     || selection.lastRow >= song.Patterns[selection.pattern].GetNumRows()
     || selection.firstChannel > selection.lastChannel || selection.lastChannel >= song.GetNumChannels())
    throw std::invalid_argument("Choose rows and channels inside one existing pattern");
}
PreparedSamplingSelection prepareSamplingSelection(Document &document, const SamplingSelection &selection) {
  validateSamplingSelection(document, selection);
  PreparedSamplingSelection result; result.selection = selection; result.native = document.native();
  Document copy(document.snapshotData()); auto &song = copy.song();
  result.sequence = document.song().Order.GetCurrentSequenceIndex(); song.Order.SetSequence(SEQUENCEINDEX(result.sequence));
  for(size_t order = 0; order < song.Order().size(); ++order) if(song.Order()[order] == selection.pattern) {
    result.order = uint32_t(order); result.arranged = true; break;
  }
  // The bounded region owns navigation, not pattern jump/loop effects. Keep all
  // tempo, fine delay and row-length effects, including ones on muted channels.
  for(auto &cell : song.Patterns[selection.pattern]) if(transportCommand(cell.command, cell.param)) { cell.command = CMD_NONE; cell.param = 0; }
  const auto pattern = result.native.patterns.at(selection.pattern).id;
  auto &commands = result.native.performance.commands;
  if(song.m_playBehaviour[kST3NoMutedChannels]) {
    // The render adapter disables ST3's effect-skip when it mutes additional
    // sources. Preserve already-muted channels' original skip semantics first.
    for(uint16_t channel=0;channel<song.GetNumChannels();++channel) if(effectiveColumnMute(result.native,song,channel)) {
      for(ROWINDEX row=0;row<song.Patterns[selection.pattern].GetNumRows();++row) song.Patterns[selection.pattern].GetpModCommand(row,channel)->Clear();
      const auto track=result.native.tracks.at(channel).id;
      std::erase_if(commands,[&](const auto &c){return c.pattern==pattern&&c.track==track&&c.kind==PatternCommandKind::TrackerEffect;});
    }
  }
  std::erase_if(commands, [&](const auto &c) { return c.pattern == pattern && c.kind == PatternCommandKind::TrackerEffect && transportCommand(c.effect, c.parameter); });
  for(auto &note : result.native.preciseNotes) if(note.pattern == pattern && transportCommand(note.effect, note.parameter)) { note.effect = CMD_NONE; note.parameter = 0; }
  result.module = copy.snapshotData();
  return result;
}
SamplingRenderWindow::SamplingRenderWindow(Renderer &renderer, uint32_t firstRow)
  : song_(renderer.song()), rowContext_(song_.nativeTransportContext), mixContext_(song_.nativePrepareContext),
    row_(song_.nativeTransportRow), mix_(song_.nativePrepareMix), firstRow_(firstRow) {
  song_.nativeTransportContext = this; song_.nativeTransportRow = &row;
  song_.nativePrepareContext = this; song_.nativePrepareMix = &mix;
}
SamplingRenderWindow::~SamplingRenderWindow() {
  song_.nativeTransportContext = rowContext_; song_.nativeTransportRow = row_;
  song_.nativePrepareContext = mixContext_; song_.nativePrepareMix = mix_;
}
bool SamplingRenderWindow::row(void *context) noexcept {
  auto &self = *static_cast<SamplingRenderWindow *>(context);
  if(self.row_ && !self.row_(self.rowContext_)) return false;
  if(self.firstFrame_ == UINT64_MAX && self.song_.m_PlayState.m_nRow == self.firstRow_) self.firstFrame_ = self.frames_;
  return true;
}
uint32_t SamplingRenderWindow::mix(void *context, uint32_t count) noexcept {
  auto &self = *static_cast<SamplingRenderWindow *>(context);
  if(self.mix_) count = self.mix_(self.mixContext_, count);
  self.frames_ += count; return count;
}
}
