// Extracted from mac/Audio/AudioUnitHost.mm; shared by all platform hosts.
#include "HostedAudio.hpp"
#include "mac/Audio/NativeSignalGraph.hpp"
#include "editor/TrackerDocument.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace Tracker {
void NativePlugin::automate(const std::vector<ParameterChange> &points, size_t slot, double rate, uint64_t start) {
  automation_.clear();
  automationPosition_ = 0;
  renderedThrough_ = start;
  for (auto p : points)
    if (p.slot == slot) {
      p.frame = uint64_t(double(p.frame) * rate / 48000);
      automation_.push_back(p);
      includeParameterRange(p.id, p.value, p.value);
    }
  std::stable_sort(automation_.begin(), automation_.end(), [](auto &a, auto &b) { return a.frame < b.frame; });
  while (automationPosition_ < automation_.size() && automation_[automationPosition_].frame < start) {
    auto p = automation_[automationPosition_++];
    if (!parameter(p.id, p.value))
      throw std::runtime_error("Plugin rejected automation");
  }
}
bool NativePlugin::process(float *buffer, uint32_t frames, uint64_t position, std::span<const PluginAudioInput> inputs) noexcept {
  if (frames > maximumFrames || position > UINT64_MAX - frames) return false;
  inputSources_.fill(nullptr);
  for (const auto &input : inputs) {
    if (!input.bus || input.bus >= 64 || !input.samples || inputSources_[input.bus] ||
        std::find(auxiliaryInputs_.begin(), auxiliaryInputs_.end(), input.bus) == auxiliaryInputs_.end()) return false;
    inputSources_[input.bus] = input.samples;
  }
  if (musicalCount_) std::sort(musicalEvents_->begin(), musicalEvents_->begin() + musicalCount_, [](const auto &a, const auto &b) {
    return a.frame != b.frame ? a.frame < b.frame : a.id != b.id ? a.id < b.id : a.sequence < b.sequence;
  });
  size_t musicalRead = 0;
  if(musicalMIDICount_)std::sort(musicalMIDI_->begin(),musicalMIDI_->begin()+musicalMIDICount_,[](const auto &a,const auto &b){
    return std::tie(a.frame,a.sequence)<std::tie(b.frame,b.sequence);
  });
  size_t midiRead=0;
  uint32_t consumed = 0;
  size_t events = 0;
  while (consumed < frames) {
    while(midiRead<musicalMIDICount_&&(*musicalMIDI_)[midiRead].frame<=position+consumed) {
      const auto &event=(*musicalMIDI_)[midiRead++];
      if(!midi(event.status,event.a,event.b))return false;
    }
    while (automationPosition_ < automation_.size() && automation_[automationPosition_].frame <= position + consumed) {
      auto p = automation_[automationPosition_++];
      if (++events > 256 || !parameter(p.id, p.value))
        return false;
    }
    while (musicalRead < musicalCount_ && (*musicalEvents_)[musicalRead].frame <= position + consumed) {
      const auto &point = (*musicalEvents_)[musicalRead++];
      auto ramp = std::find_if(parameterRamps_.begin(), parameterRamps_.end(), [&](const auto &r) { return r.active && r.id == point.id; });
      if (point.duration) {
        if (ramp == parameterRamps_.end()) ramp = std::find_if(parameterRamps_.begin(), parameterRamps_.end(), [](const auto &r) { return !r.active; });
        if (ramp == parameterRamps_.end()) return false;
        *ramp = {point.id, true, {point.frame, point.duration, point.value, point.target}};
      } else {
        if (ramp != parameterRamps_.end()) ramp->active = false;
        if (!parameter(point.id, float(point.value))) return false;
      }
    }
    uint32_t count = frames - consumed;
    for (auto &ramp : parameterRamps_) if (ramp.active) {
      const auto at = position + consumed;
      const double value=ramp.ramp.value(at);
      if (!(backend_ && backend_->supportsSampleOffsetParameters() ? backend_->parameter(ramp.id,value,0) : parameter(ramp.id,float(value)))) return false;
      if (ramp.ramp.finished(at)) ramp.active = false;
      else if(backend_ && backend_->supportsSampleOffsetParameters()){const auto remaining=ramp.ramp.duration-(at-ramp.ramp.start);if(remaining<count)count=uint32_t(remaining+1);}
      else count = 1;
    }
    if (automationPosition_ < automation_.size())
      count = uint32_t(std::min<uint64_t>(count, automation_[automationPosition_].frame - position - consumed));
    if (musicalRead < musicalCount_)
      count = uint32_t(std::min<uint64_t>(count, (*musicalEvents_)[musicalRead].frame - position - consumed));
    if(midiRead<musicalMIDICount_)count=uint32_t(std::min<uint64_t>(count,(*musicalMIDI_)[midiRead].frame-position-consumed));
    // VST3 queues carry both endpoints of a linear segment inside the audio
    // buffer. Split only at musical events or ramp endings, not every sample.
    if(backend_ && backend_->supportsSampleOffsetParameters() && count>1)for(const auto &ramp:parameterRamps_)if(ramp.active)
      if(!backend_->parameter(ramp.id,ramp.ramp.value(position+consumed+count-1),count-1))return false;
    if (!count || !processBlock(buffer + consumed * 2, count, position + consumed, consumed))
      return false;
    consumed += count;
    if (transport_.playing)
      transport_.beat += count * transport_.tempo / (60 * rate_);
    if (backend_) backend_->transport(transport_);
  }
  if (musicalRead) {
    std::move(musicalEvents_->begin() + musicalRead, musicalEvents_->begin() + musicalCount_, musicalEvents_->begin());
    musicalCount_ -= musicalRead;
  }
  if(midiRead) {
    std::move(musicalMIDI_->begin()+midiRead,musicalMIDI_->begin()+musicalMIDICount_,musicalMIDI_->begin());
    musicalMIDICount_-=midiRead;
  }
  if (!outputDelay_.empty())
    for (uint32_t n = 0; n < frames * 2; ++n) {
      std::swap(buffer[n], outputDelay_[outputDelayPosition_]);
      outputDelayPosition_ = (outputDelayPosition_ + 1) % outputDelay_.size();
    }
  renderedThrough_ = position + frames;
  return true;
}
bool NativePlugin::schedule(uint32_t id, float value, uint64_t frame) noexcept {
  if (!musicalEvents_ || musicalCount_ == musicalEvents_->size() || !std::isfinite(value)) return false;
  (*musicalEvents_)[musicalCount_++] = {id, value, frame, musicalSequence_++};
  return true;
}
bool NativePlugin::scheduleRamp(uint32_t id, double from, double to, uint64_t frame, uint64_t duration) noexcept {
  if (!musicalEvents_ || musicalCount_ == musicalEvents_->size() || !SampleRamp{frame,duration,from,to}.valid()) return false;
  if (!duration || from == to) return schedule(id, float(to), frame);
  (*musicalEvents_)[musicalCount_++] = {id, duration ? from : to, frame, musicalSequence_++, duration, to};
  return true;
}
bool NativePlugin::scheduleMIDI(uint8_t status,uint8_t a,uint8_t b,uint64_t frame) noexcept {
  if(!musicalMIDI_||musicalMIDICount_==musicalMIDI_->size()||status<0x80||status>=0xf0||a>127||b>127)return false;
  (*musicalMIDI_)[musicalMIDICount_++]={frame,musicalSequence_++,status,a,b};return true;
}
} // namespace Tracker
