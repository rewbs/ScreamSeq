// Extracted from mac/Audio/AudioUnitHost.mm; shared by all platform hosts.
#include "HostedAudio.hpp"
#include "mac/Audio/NativeSignalGraph.hpp"
#include "editor/TrackerDocument.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace Tracker {
void NativePlugin::automate(const std::vector<ParameterChange> &points, size_t slot, double rate, uint64_t start) {
  activeAutomation_=&automation_;
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
    if (!appliedParameter(p.id,p.value,p.frame,{ParameterOrigin::Recorded}))
      throw std::runtime_error("Plugin rejected automation");
  }
}
bool NativePlugin::process(float *buffer, uint32_t frames, uint64_t position, std::span<const PluginAudioInput> inputs,const PluginSongModulation *modulation,std::span<const PluginParameterSamples> sampled) noexcept {
  if (frames > maximumFrames || position > UINT64_MAX - frames || sampled.size()>64) return false;
  const auto &automation=*activeAutomation_;
  for(size_t i=0;i<sampled.size();++i){const auto &p=sampled[i];if(p.values.size()!=frames||frames>32||!std::isfinite(p.minimum)||!std::isfinite(p.maximum)||!std::isfinite(p.maximum-p.minimum)||p.maximum<=p.minimum)return false;for(const auto value:p.values)if(!std::isfinite(value)||value<0||value>1)return false;for(size_t j=0;j<i;++j)if(sampled[j].parameter==p.parameter)return false;}
  processingModulation_=modulation;
  struct Clear {const PluginSongModulation *&pointer;~Clear(){pointer=nullptr;}} clear{processingModulation_};
  for(auto &p:baselines_){const bool active=modulationParameter(p.id);
    if(p.overlaid && !active && !effectiveParameter(p.id,baselineAt(p.id,position),position,p.source))return false;
    p.overlaid=active;
  }
  bypassControl_.begin(buffer,frames);
  inputSources_.fill(nullptr);autoDetectorSource_=nullptr;
  for (const auto &input : inputs) {
    if (!input.bus || input.bus >= 64 || !input.samples || inputSources_[input.bus] ||
        !(preparedInputs_&(uint64_t(1)<<input.bus))) return false;
    if(input.autoFallback){if(input.bus!=1||!autoDetectorBuffer_)return false;autoDetectorSource_=input.autoFallback;}
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
  while (consumed < frames) {
    while(midiRead<musicalMIDICount_&&(*musicalMIDI_)[midiRead].frame<=position+consumed) {
      const auto &event=(*musicalMIDI_)[midiRead++];
      if(!midi(event.status,event.a,event.b))return false;
    }
    while (automationPosition_ < automation.size() && automation[automationPosition_].frame <= position + consumed) {
      // No per-call event limit: it made dense automation fail, and fail
      // differently for each callback size. The prepared list bounds the work.
      auto p = automation[automationPosition_++];
      if (!appliedParameter(p.id,p.value,p.frame,{ParameterOrigin::Recorded}))
        return false;
    }
    while (musicalRead < musicalCount_ && (*musicalEvents_)[musicalRead].frame <= position + consumed) {
      const auto &point = (*musicalEvents_)[musicalRead++];
      auto ramp = std::find_if(parameterRamps_.begin(), parameterRamps_.end(), [&](const auto &r) { return r.active && r.id == point.id; });
      if (point.duration) {
        if (ramp == parameterRamps_.end()) ramp = std::find_if(parameterRamps_.begin(), parameterRamps_.end(), [](const auto &r) { return !r.active; });
        // Every ramp slot is busy: jump to the target instead of failing. The
        // choice depends on the event timeline only, never on the partition.
        if (ramp == parameterRamps_.end()) { if (!appliedParameter(point.id, float(point.target),point.frame,point.source)) return false; continue; }
        *ramp = {point.id, true, {point.frame, point.duration, point.value, point.target},point.source};
      } else {
        if (ramp != parameterRamps_.end()) ramp->active = false;
        if (!appliedParameter(point.id,point.value,position+consumed,point.source)) return false;
      }
    }
    uint32_t count = frames - consumed;
    for (auto &ramp : parameterRamps_) if (ramp.active) {
      const auto at = position + consumed;
      const double value=ramp.ramp.value(at);
      if (!appliedParameter(ramp.id,value,at,ramp.source)) return false;
      if (ramp.ramp.finished(at)) ramp.active = false;
      else if(backend_ && backend_->supportsSampleOffsetParameters()){const auto remaining=ramp.ramp.duration-(at-ramp.ramp.start);if(remaining<count)count=uint32_t(remaining+1);}
      else count = 1;
    }
    if (automationPosition_ < automation.size())
      count = uint32_t(std::min<uint64_t>(count, automation[automationPosition_].frame - position - consumed));
    if (musicalRead < musicalCount_)
      count = uint32_t(std::min<uint64_t>(count, (*musicalEvents_)[musicalRead].frame - position - consumed));
    if(midiRead<musicalMIDICount_)count=uint32_t(std::min<uint64_t>(count,(*musicalMIDI_)[midiRead].frame-position-consumed));
    // Stream dense source values into bounded per-parameter queues. VST3 gets
    // up to sixteen sample-offset points per audio call, never a scheduled
    // buffer's worth of future points or a one-sample audio-call workaround.
    bool denseModulation=false;
    if(modulation && !modulation->targets.empty()){
      auto clock=modulation->clock;const auto elapsed=position+consumed-modulation->frame;
      clock.beat+=clock.playing?elapsed*clock.tempo/(60*rate_):0;clock.position+=elapsed*clock.unitsPerFrame;
      uint32_t linear=count;
      for(const auto &target:modulation->targets){const auto segment=modulation->runtime->rampFrames(target.index,position+consumed,count,clock);if(!segment)return false;if(segment==1)denseModulation=true;else linear=std::min(linear,segment);}
      count=std::min(count,backend_&&backend_->supportsSampleOffsetParameters()?(denseModulation?16u:linear):1u);
      // Clamping a linear sum creates corners. Sample those segments densely
      // instead of interpolating through a saturation boundary.
      if(!denseModulation && count>1)for(const auto &target:modulation->targets)for(auto frame:{position+consumed,position+consumed+count-1}){
        double raw=(baselineAt(target.parameter,frame)-target.minimum)/(target.maximum-target.minimum);
        for(const auto &source:modulation->runtime->targets()[target.index].contributions){double value;if(!modulation->runtime->scaledContribution(source,frame,value))return false;raw+=value;}
        if(raw<0 || raw>1)denseModulation=true;
      }
      if(denseModulation)count=std::min(count,16u);
    }
    if(!sampled.empty()){
      if(backend_&&backend_->supportsSampleOffsetParameters())count=std::min(count,16u);
      else for(const auto &target:sampled){const auto first=target.values[consumed];for(uint32_t i=1;i<count;++i)if(target.values[consumed+i]!=first){count=i;break;}}
    }
    // VST3 queues carry both endpoints of a linear segment inside the audio
    // buffer. Split only at musical events or ramp endings, not every sample.
    if(backend_ && backend_->supportsSampleOffsetParameters() && count>1)for(const auto &ramp:parameterRamps_)if(ramp.active)
      if(!appliedParameter(ramp.id,ramp.ramp.value(position+consumed+count-1),position+consumed+count-1,ramp.source,count-1))return false;
    // Discrete graph targets provide the already summed/clamped/quantized
    // value at each sample. Dense VST3 points live inside ordinary sub-blocks;
    // AU/builtins split only at an actual discrete value change.
    for(const auto &target:sampled){const bool offsets=backend_&&backend_->supportsSampleOffsetParameters();for(uint32_t i=0;i<(offsets?count:1u);++i){const auto value=target.values[consumed+i];if(!std::isfinite(value)||value<0||value>1||!appliedParameter(target.parameter,target.minimum+(target.maximum-target.minimum)*value,position+consumed+i,target.source,i))return false;}}
    if(modulation)for(const auto &target:modulation->targets){
      for(uint32_t offset=0;offset<count;offset=denseModulation?offset+1:offset==0&&count>1?count-1:count){const auto frame=position+consumed+offset;
        const auto base=(baselineAt(target.parameter,frame)-target.minimum)/(target.maximum-target.minimum);double value;
        if(!modulation->runtime->overlay(target.index,frame,base,value) || !effectiveParameter(target.parameter,target.minimum+(target.maximum-target.minimum)*value,frame,{ParameterOrigin::Graph},offset))return false;
      }
      const auto frame=position+consumed+count-1;
      contribution(target.parameter,0,(baselineAt(target.parameter,frame)-target.minimum)/(target.maximum-target.minimum),frame);
      for(const auto &source:modulation->runtime->targets()[target.index].contributions){double value;if(modulation->runtime->scaledContribution(source,frame,value))contribution(target.parameter,modulation->runtime->source(source.source).node.id,value,frame);}
    }
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
  bypassControl_.finish(buffer,frames);
  for(uint32_t bus=1;bus<64;++bus)if(auxiliaryOutputBuffers_[bus])bypassControl_.finish(auxiliaryOutputBuffers_[bus]->interleaved.data(),frames,true);
  if (!outputDelay_.empty())
    for (uint32_t n = 0; n < frames * 2; ++n) {
      std::swap(buffer[n], outputDelay_[outputDelayPosition_]);
      outputDelayPosition_ = (outputDelayPosition_ + 1) % outputDelay_.size();
    }
  renderedThrough_ = position + frames;
  if(activity_)activity_->held(activityProcessor_,renderedThrough_-1,activityAudible_&&!bypassed());
  return true;
}
void NativePlugin::cancelScheduledParameter(uint32_t id) noexcept {
  if(musicalEvents_) {
    auto end=std::remove_if(musicalEvents_->begin(),musicalEvents_->begin()+musicalCount_,[&](const auto &p){return p.id==id;});
    musicalCount_=size_t(end-musicalEvents_->begin());
  }
  for(auto &r:parameterRamps_)if(r.id==id)r.active=false;
}
bool NativePlugin::schedule(uint32_t id, float value, uint64_t frame, ParameterSource source) noexcept {
  if(!musicalActive_.load(std::memory_order_acquire))return true;
  if (!musicalEvents_ || musicalCount_ == musicalEvents_->size() || !std::isfinite(value)) return false;
  (*musicalEvents_)[musicalCount_++] = {id, value, frame, musicalSequence_++,0,0,source};
  return true;
}
bool NativePlugin::scheduleRamp(uint32_t id, double from, double to, uint64_t frame, uint64_t duration, ParameterSource source) noexcept {
  if(!musicalActive_.load(std::memory_order_acquire))return true;
  if (!musicalEvents_ || musicalCount_ == musicalEvents_->size() || !SampleRamp{frame,duration,from,to}.valid()) return false;
  if (!duration || from == to) return schedule(id, float(to), frame, source);
  (*musicalEvents_)[musicalCount_++] = {id, duration ? from : to, frame, musicalSequence_++, duration, to,source};
  return true;
}
bool NativePlugin::scheduleMIDI(uint8_t status,uint8_t a,uint8_t b,uint64_t frame) noexcept {
  if(!musicalMIDI_||musicalMIDICount_==musicalMIDI_->size()||status<0x80||status>=0xf0||a>127||b>127)return false;
  (*musicalMIDI_)[musicalMIDICount_++]={frame,musicalSequence_++,status,a,b};return true;
}
} // namespace Tracker
