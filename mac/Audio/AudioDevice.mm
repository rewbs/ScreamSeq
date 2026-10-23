#include "AudioDevice.hpp"
#import <Foundation/Foundation.h>
#include <algorithm>
#include <cmath>
#include <mach/mach_time.h>
#include "editor/hosted/ParameterEdits.hpp"
#include "editor/hosted/PluginBaselineVerification.hpp"
namespace Tracker {
namespace {
void check(OSStatus status, const char *message) {
  if (status)
    throw std::runtime_error(std::string(message) + " (" + std::to_string(status) + ")");
}
} // namespace
std::vector<DeviceInfo> AudioDevice::devices() {
  AudioObjectPropertyAddress address{kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
                                     kAudioObjectPropertyElementMain};
  UInt32 size = 0;
  check(AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, nullptr, &size),
        "Cannot enumerate audio devices");
  std::vector<AudioDeviceID> ids(size / sizeof(AudioDeviceID));
  check(AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size, ids.data()),
        "Cannot read audio devices");
  std::vector<DeviceInfo> result;
  for (auto id : ids) {
    AudioObjectPropertyAddress streams{kAudioDevicePropertyStreams, kAudioObjectPropertyScopeOutput,
                                       kAudioObjectPropertyElementMain};
    UInt32 bytes = 0;
    if (AudioObjectGetPropertyDataSize(id, &streams, 0, nullptr, &bytes) || !bytes)
      continue;
    CFStringRef name = nullptr;
    size = sizeof(name);
    AudioObjectPropertyAddress label{kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal,
                                     kAudioObjectPropertyElementMain};
    if (!AudioObjectGetPropertyData(id, &label, 0, nullptr, &size, &name) && name) {
      result.push_back({id, std::string([(__bridge NSString *)name UTF8String])});
      CFRelease(name);
    }
  }
  return result;
}
AudioDevice::~AudioDevice() {
  removeListeners();
  stop();
  renderer_.reset();
  plugins_.reset();
  if (unit_)
    AudioComponentInstanceDispose(unit_);
}
void AudioDevice::configure(uint32_t deviceID, uint32_t frames) {
  removeListeners();
  deviceChanged_ = false;
  usesDefault_ = deviceID == 0;
  stop();
  if (unit_) {
    AudioComponentInstanceDispose(unit_);
    unit_ = nullptr;
  }
  if (!deviceID) {
    UInt32 size = sizeof(deviceID);
    AudioObjectPropertyAddress p{kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal,
                                 kAudioObjectPropertyElementMain};
    check(AudioObjectGetPropertyData(kAudioObjectSystemObject, &p, 0, nullptr, &size, &deviceID),
          "No default audio device");
  }
  deviceID_ = deviceID;
  AudioObjectPropertyAddress rate{kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal,
                                  kAudioObjectPropertyElementMain};
  UInt32 size = sizeof(sampleRate_);
  check(AudioObjectGetPropertyData(deviceID, &rate, 0, nullptr, &size, &sampleRate_), "Cannot read sample rate");
  AudioObjectPropertyAddress buffer{kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal,
                                    kAudioObjectPropertyElementMain};
  AudioObjectSetPropertyData(deviceID, &buffer, 0, nullptr, sizeof(frames), &frames);
  size = sizeof(bufferSize_);
  check(AudioObjectGetPropertyData(deviceID, &buffer, 0, nullptr, &size, &bufferSize_), "Cannot read buffer size");
  AudioComponentDescription desc{kAudioUnitType_Output, kAudioUnitSubType_HALOutput, kAudioUnitManufacturer_Apple, 0,
                                 0};
  auto component = AudioComponentFindNext(nullptr, &desc);
  if (!component)
    throw std::runtime_error("Core Audio output is unavailable.");
  check(AudioComponentInstanceNew(component, &unit_), "Cannot create audio output");
  try {
    configureUnit(deviceID);
  } catch (...) {
    // Never retain a half-built output: play() reconfigures only without one.
    AudioComponentInstanceDispose(unit_);
    unit_ = nullptr;
    throw;
  }
  mach_timebase_info_data_t timebase;
  mach_timebase_info(&timebase);
  nanosPerTick_ = double(timebase.numer) / timebase.denom;
  addListeners();
}
void AudioDevice::configureUnit(uint32_t deviceID) {
  check(AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &deviceID,
                             sizeof(deviceID)),
        "Cannot select output device");
  AudioStreamBasicDescription format{};
  format.mSampleRate = sampleRate_;
  format.mFormatID = kAudioFormatLinearPCM;
  format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
  format.mBytesPerPacket = 8;
  format.mFramesPerPacket = 1;
  format.mBytesPerFrame = 8;
  format.mChannelsPerFrame = 2;
  format.mBitsPerChannel = 32;
  check(AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &format, sizeof(format)),
        "Cannot set float audio format");
  AURenderCallbackStruct cb{callback, this};
  check(AudioUnitSetProperty(unit_, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof(cb)),
        "Cannot set render callback");
  check(AudioUnitInitialize(unit_), "Cannot initialize output");
}
void AudioDevice::addListeners() {
  for (auto selector : std::array<AudioObjectPropertySelector, 3>{kAudioDevicePropertyNominalSampleRate,
                                                                  kAudioDevicePropertyDeviceIsAlive,
                                                                  kAudioDevicePropertyBufferFrameSize}) {
    AudioObjectPropertyAddress address{selector, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    AudioObjectAddPropertyListener(deviceID_, &address, propertyChanged, this);
  }
  if (usesDefault_) {
    AudioObjectPropertyAddress address{kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal,
                                       kAudioObjectPropertyElementMain};
    AudioObjectAddPropertyListener(kAudioObjectSystemObject, &address, propertyChanged, this);
  }
  listening_ = true;
}
OSStatus AudioDevice::propertyChanged(AudioObjectID, UInt32, const AudioObjectPropertyAddress *, void *ref) {
  static_cast<AudioDevice *>(ref)->deviceChanged_ = true;
  return noErr;
}
void AudioDevice::removeListeners() {
  if (!listening_)
    return;
  for (auto selector : std::array<AudioObjectPropertySelector, 3>{kAudioDevicePropertyNominalSampleRate,
                                                                  kAudioDevicePropertyDeviceIsAlive,
                                                                  kAudioDevicePropertyBufferFrameSize}) {
    AudioObjectPropertyAddress address{selector, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    AudioObjectRemovePropertyListener(deviceID_, &address, propertyChanged, this);
  }
  if (usesDefault_) {
    AudioObjectPropertyAddress address{kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal,
                                       kAudioObjectPropertyElementMain};
    AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &address, propertyChanged, this);
  }
  listening_ = false;
}
void AudioDevice::play(const std::vector<std::byte> &bytes, uint32_t order, bool preview, const std::string &sourcePath,
                       uint32_t sequence, const NativeSong *native, PlaybackRegion region, bool isolatedSample) {
  stop();
  auto editors = plugins_ ? plugins_->openEditors() : std::vector<size_t>{};
  if (!pendingEditors_.empty()) {
    editors = pendingEditors_;
    pendingEditors_.clear();
  }
  if (plugins_)
    pluginStates_ = plugins_->states();
  if (!unit_)
    configure();
  // Prepare the implicit mixer too, so the first live insert has existing
  // channel adapters and never requires rebuilding held instrument sources.
  std::optional<NativeSong> implicit;
  if(native&&!native->mixer.active()&&!isolatedSample){implicit=*native;implicit->ensureMixer();native=&*implicit;}
  renderer_ = std::make_unique<Renderer>(bytes, uint32_t(sampleRate_), order, preview, sourcePath, sequence, region, native);
  frameTimeline_=std::make_shared<RecordingFrameTimeline>();
  ++captureGeneration_;if(!captureGeneration_)++captureGeneration_;
  captureFrames_=!preview&&!isolatedSample;
  if (native && !preview) {renderer_->applyColumnMutes(*native, renderer_->song());renderer_->preparePreciseNotes(*native);}
  previewing_ = preview;
  // The sample inspector's finite Audition button uses a standalone sample
  // document. Keep the saved rack for the next song preparation, without
  // flattening its channel inserts onto this raw preview.
  plugins_ = isolatedSample || (pluginStates_.empty() && !(native && (native->mixer.active() || !native->performance.commands.empty())))
                 ? nullptr
                 : std::make_shared<PluginChain>(pluginStates_, sampleRate_, false, automation_,
                                                 uint64_t(double(renderer_->telemetry().frames) * 48000 / sampleRate_));
  ++pluginEpoch_;
  if (plugins_)
    plugins_->attachInstruments(*renderer_, native);
  if (plugins_ && native)
    plugins_->attachMusicalAutomation(*renderer_, *native);
  restoreParameterActivity();
  callbacks_ = 0;
  overruns_ = 0;
  maxNanos_ = 0;
  for (auto &bucket : callbackHistogram_)
    bucket.store(0, std::memory_order_relaxed);
  renderEnded_ = false;
  tailFrames_ = tailBudgetFrames_ = tailRevision_ = 0;
  playing_ = true;
  try {
    check(AudioOutputUnitStart(unit_), "Cannot start playback");
    if (plugins_)
      for (auto slot : editors) {
        try {
          plugins_->showEditor(slot);
        } catch (...) { /* Playback remains usable if a vendor editor cannot reopen. */
        }
      }
  } catch (...) {
    playing_ = false;
    frameTimeline_->invalidate();
    throw;
  }
}
void AudioDevice::restoreParameterActivity() {
  if(!plugins_||activityTarget_.empty())return;
  auto &activity=plugins_->parameterActivity();
  for(const auto &target:activity.processors)if(target.key==activityTarget_)for(const auto &p:target.parameters)if(p.id==activityParameter_){activity.watch(target.key,p.id,p.value);activity.begin(plugins_->position());return;}
}
void AudioDevice::setPlugins(const std::vector<PluginState> &states, const std::vector<ParameterChange> &automation,
                             bool preserveEditors) {
  pendingEditors_ = preserveEditors && plugins_ ? plugins_->openEditors() : std::vector<size_t>{};
  auto next = states.empty() ? nullptr : std::make_shared<PluginChain>(states, sampleRate_, false, automation);
  stop();
  renderer_.reset();
  plugins_ = std::move(next);
  ++pluginEpoch_;
  restoreParameterActivity();
  pluginStates_ = states;
  automation_ = automation;
}
std::unique_ptr<AudioDevice::LiveRackPlan> AudioDevice::prepareLiveRack(const std::vector<PluginState> &states,const std::vector<ParameterChange> &automation,const NativeSong &native,std::span<const std::string> presets) {
  if(!active()||!plugins_)throw std::runtime_error("Live rack processing is unavailable");
  auto plan=std::make_unique<LiveRackPlan>();plan->states=states;plan->automation=automation;
  auto prepared=native;prepared.ensureMixer();plan->hosted=plugins_->prepareRack(states,prepared,presets);return plan;
}
bool AudioDevice::publishLiveRack(std::unique_ptr<LiveRackPlan> &plan) noexcept {
  if(!plan||!plugins_||!plugins_->publishRack(plan->hosted))return false;
  pluginStates_.swap(plan->states);automation_.swap(plan->automation);return true;
}
std::unique_ptr<AudioDevice::LiveRecordedPlan> AudioDevice::prepareRecordedAutomation(const std::vector<ParameterChange> &points,std::span<const ParameterChange> manualBaselines) {
  if(!plugins_)throw std::runtime_error("Recorded automation processor is unavailable");
  if(joinStoppedProcessing())plugins_->applyPending();
  auto plan=std::make_unique<LiveRecordedPlan>();plan->pluginEpoch=pluginEpoch_;
  plan->points=points;plan->hosted=plugins_->prepareRecordedAutomation(points,manualBaselines);return plan;
}
bool AudioDevice::publishRecordedAutomation(std::unique_ptr<LiveRecordedPlan> plan) {
  // Pointer equality inside the hosted plan is insufficient after a complete
  // rack replacement: allocator address reuse must not accept an old plan.
  if(!plan||!plugins_||plan->pluginEpoch!=pluginEpoch_)return false;
  const bool stopped=joinStoppedProcessing();
  if(!plugins_->publishRecordedAutomation(std::move(plan->hosted)))return false;
  automation_.swap(plan->points);
  if(stopped)plugins_->applyPending();
  return true;
}
std::unique_ptr<MixerTransition::Plan> AudioDevice::prepareMixerRouting(const NativeSong &native) {
  if(!active() || !plugins_)return nullptr;
  auto prepared=native;prepared.ensureMixer();return plugins_->prepareMixerRouting(prepared);
}
std::vector<PluginState> AudioDevice::pluginStates() {
  stop();
  if (plugins_)
    pluginStates_ = plugins_->states();
  return pluginStates_;
}
std::optional<NativePlugin::ProcessingState> AudioDevice::pluginProcessingState(uint64_t expectedEpoch,const PluginStateOwner &owner) {
  const auto retained=plugins_; // A vendor callback may replace the active host.
  const auto validate=[&]()->size_t {
    if(!retained||plugins_!=retained||pluginEpoch_!=expectedEpoch||owner.plugin.empty()||!owner.source.incarnation)
      throw std::runtime_error("Processing-state observation belongs to a replaced native host");
    const auto index=pluginSlot(owner.plugin);
    if(!index||plugins_->editSource(*index)!=owner.source)
      throw std::runtime_error("Processing-state observation belongs to another native provider");
    return *index;
  };
  const auto index=validate();auto observed=retained->processingState(index);
  if(validate()!=index||(observed&&observed->source!=owner.source))
    throw std::runtime_error("Native provider moved during processing-state observation");
  return observed;
}
std::optional<AudioDevice::PluginPreviewCancellation> AudioDevice::cancelPluginParameterPreviewsTracked(
    uint64_t expectedEpoch,std::span<const ParameterPreviewOwner> owners) {
  if(!plugins_||pluginEpoch_!=expectedEpoch||owners.size()>4096)return std::nullopt;
  PluginPreviewCancellation receipt;receipt.owner_=this;receipt.epoch_=expectedEpoch;receipt.chain_=plugins_;
  std::vector<PluginChain::ParameterPreviewTarget> targets;targets.reserve(owners.size());
  for(const auto &owner:owners) {
    const auto index=pluginSlot(owner.plugin);
    if(!index||*index>UINT32_MAX||!owner.provider||receipt.chain_->editSource(*index).incarnation!=owner.provider)
      return std::nullopt;
    targets.push_back({uint32_t(*index),owner.parameter});
  }
  auto hosted=receipt.chain_->cancelParameterPreviewsTracked(targets);
  if(!hosted)return std::nullopt;
  receipt.hosted_=std::move(*hosted);
  return std::optional<PluginPreviewCancellation>{std::move(receipt)};
}
PluginChain::PreviewCancellationState AudioDevice::pluginPreviewCancellationState(const PluginPreviewCancellation &receipt) const noexcept {
  if(receipt.owner_!=this||receipt.epoch_!=pluginEpoch_||!receipt.chain_||receipt.chain_!=plugins_)
    return PluginChain::PreviewCancellationState::Invalid;
  return receipt.chain_->parameterPreviewCancellationState(receipt.hosted_);
}
void AudioDevice::verifyProcessingPluginBaselines(uint64_t expectedEpoch,std::span<const PluginStateOwner> owners,
    std::span<const PluginState> saved) {
  if(owners.empty()||owners.size()>maximumNativePlugins)throw std::invalid_argument("Invalid processing-state verification group");
  const auto retained=plugins_;
  struct Item {PluginStateOwner owner;size_t slot;PluginState accepted;};
  std::vector<Item> items;items.reserve(owners.size());std::set<std::string> seen;
  size_t bytes=0;
  for(const auto &owner:owners) {
    const auto index=pluginSlot(owner.plugin);
    const auto baseline=std::find_if(saved.begin(),saved.end(),[&](const auto &p){return p.instanceID==owner.plugin;});
    if(!index||baseline==saved.end()||!seen.insert(owner.plugin).second)
      throw std::invalid_argument("Processing-state verification needs distinct saved provider identities");
    constexpr size_t budget=256u*1024u*1024u;
    if(baseline->state.size()>budget-bytes)throw std::runtime_error("Saved verification states exceed the bounded group budget");
    bytes+=baseline->state.size();items.push_back({owner,*index,*baseline});
  }
  const auto validate=[&] {
    if(!retained||plugins_!=retained||pluginEpoch_!=expectedEpoch)
      throw std::runtime_error("Processing-state verification belongs to a replaced host; take retained");
    for(const auto &item:items)if(!item.owner.source.incarnation||pluginSlot(item.owner.plugin)!=item.slot||
        retained->editSource(item.slot)!=item.owner.source)
      throw std::runtime_error("Processing-state verification belongs to another provider; take retained");
  };
  validate();
  for(const auto &item:items) {
    const auto observed=pluginProcessingState(expectedEpoch,item.owner);validate();
    if(!observed)throw std::runtime_error("This plugin cannot verify state during playback; stop playback with the take retained");
    verifyPluginParameterBaseline(item.accepted,observed->state,sampleRate_,validate);
    validate();
  }
}
void AudioDevice::restoreStoppedPluginBaselines(uint64_t expectedEpoch,std::span<const PluginStateOwner> owners,
    std::span<const PluginState> saved) {
  const auto validate=[&] {
    if(active()||!plugins_||pluginEpoch_!=expectedEpoch)
      throw std::runtime_error("Stop playback before native plugin state verification; take retained");
    for(const auto &owner:owners) {
      const auto slot=pluginSlot(owner.plugin);
      if(!slot||*slot>UINT32_MAX||!owner.source.incarnation||plugins_->editSource(*slot)!=owner.source)
        throw std::runtime_error("Native provider changed during baseline restoration; take retained");
    }
  };
  if(owners.empty()||owners.size()>maximumNativePlugins)throw std::invalid_argument("Invalid native restoration group");
  validate();
  if(!joinStoppedProcessing())throw std::runtime_error("Plugin processing is still active; take retained");
  plugins_->applyPending();validate();
  struct Restore {PluginStateOwner owner;size_t slot;PluginState expected;std::vector<ParameterChange> values;};
  std::vector<Restore> prepared;prepared.reserve(owners.size());std::set<std::string> seen;
  for(const auto &owner:owners) {
    if(!seen.insert(owner.plugin).second)throw std::invalid_argument("Duplicate native restoration provider");
    const auto baseline=std::find_if(saved.begin(),saved.end(),[&](const auto &p){return p.instanceID==owner.plugin;});
    if(baseline==saved.end())throw std::runtime_error("Saved plugin baseline is missing; take retained");
    const auto slot=*pluginSlot(owner.plugin);
    NativePlugin probe(*baseline,sampleRate_);
    auto expected=probe.state();auto parameters=probe.parameters();
    validate();if(pluginSlot(owner.plugin)!=slot)throw std::runtime_error("Native rack moved during restoration preparation");
    const auto catalog=plugins_->parameters(slot);validate();
    std::vector<std::pair<uint32_t,double>> requested;
    for(const auto &p:parameters)if(p.writable)requested.emplace_back(p.id,p.manualValue.value_or(p.value));
    Restore item{owner,slot,std::move(expected),{}};item.values.reserve(requested.size());
    if(!requested.empty())for(const auto &[id,value]:prepareParameterEdits(catalog,requested))
      item.values.push_back({uint32_t(slot),id,value,0});
    prepared.push_back(std::move(item));
  }
  // Vendor preparation can pump native work. Validate the entire group again
  // before writing its first value. The outer owner checks queue positions too.
  const auto validatePrepared=[&] {
    validate();
    for(const auto &item:prepared)if(pluginSlot(item.owner.plugin)!=item.slot)
      throw std::runtime_error("Native rack moved during restoration publication");
  };
  validatePrepared();
  for(const auto &item:prepared) {
    for(size_t offset=0;offset<item.values.size();offset+=128) {
      validatePrepared();plugins_->applyPending();validatePrepared();
      if(!plugins_->enqueueParameters(std::span(item.values).subspan(offset,std::min<size_t>(128,item.values.size()-offset))))
        throw std::runtime_error("Native baseline parameter queue refused restoration; take retained");
    }
    plugins_->applyPending();validatePrepared();
    const auto restored=plugins_->state(item.slot);validatePrepared();
    if(pluginSlot(item.owner.plugin)!=item.slot||restored.instanceID!=item.owner.plugin||
       restored.state!=item.expected.state||restored.audioLayout!=item.expected.audioLayout)
      throw std::runtime_error("Native hidden state differs from the saved baseline; take retained for explicit restoration");
  }
}
std::unique_ptr<AudioDevice::StoppedPluginRepair> AudioDevice::prepareStoppedPluginRepair(
    uint64_t expectedEpoch,std::span<const PluginStateOwner> owners,std::span<const PluginState> saved) {
  if(owners.empty()||owners.size()>maximumNativePlugins)throw std::invalid_argument("Invalid opaque repair group");
  const auto validate=[&] {
    if(active()||!plugins_||pluginEpoch_!=expectedEpoch)
      throw std::runtime_error("Stopped opaque repair belongs to a replaced or active host");
    const auto open=plugins_->openEditors();
    if(active()||!plugins_||pluginEpoch_!=expectedEpoch)
      throw std::runtime_error("Native host changed during closed-editor observation");
    for(const auto &owner:owners) {
      const auto slot=pluginSlot(owner.plugin);
      if(!slot||!owner.source.incarnation||plugins_->editSource(*slot)!=owner.source||
          std::find(open.begin(),open.end(),*slot)!=open.end())
        throw std::runtime_error("Close the original native editors before opaque repair");
    }
  };
  validate();if(!joinStoppedProcessing())throw std::runtime_error("Native rendering has not stopped");validate();
  auto result=std::make_unique<StoppedPluginRepair>();result->owner=this;result->epoch=expectedEpoch;
  result->providers.assign(owners.begin(),owners.end());
  std::vector<PluginState> group;group.reserve(owners.size());std::set<std::string> seen;
  for(const auto &owner:owners) {
    if(!seen.insert(owner.plugin).second)throw std::invalid_argument("Duplicate opaque repair provider");
    const auto state=std::find_if(saved.begin(),saved.end(),[&](const auto &p){return p.instanceID==owner.plugin;});
    if(state==saved.end())throw std::runtime_error("Opaque repair has no accepted plugin baseline");
    group.push_back(*state);
  }
  result->hosted=plugins_->prepareStoppedRepair(group);
  validate();
  result->originalCuts.reserve(owners.size());
  for(const auto &owner:owners) {
    const auto slot=pluginSlot(owner.plugin);
    if(!slot||active()||pluginEpoch_!=expectedEpoch)throw std::runtime_error("Original repair host changed");
    const auto cut=pluginInputBoundary(*slot,mach_absolute_time());
    if(!cut||cut->source!=owner.source)throw std::runtime_error("Original repair input provider changed");
    result->originalCuts.push_back(*cut);
  }
  result->replacementCuts.reserve(result->hosted->items.size());
  for(const auto &item:result->hosted->items) {
    const auto cut=item.replacement->prepared->editBoundary(mach_absolute_time());
    if(!cut||cut->fence.pending||cut->source.dropped)
      throw std::runtime_error("Replacement native provider has unreviewed input");
    result->replacementCuts.push_back(*cut);
  }
  validate();
  return result;
}
bool AudioDevice::StoppedPluginRepair::releaseRetiredInputBarrier(const std::string &identity,PluginEditorInputBarrier barrier) {
  if(!adopted()||!barrier.incarnation)return false;
  for(auto &item:hosted->items)if(item.requested.instanceID==identity) {
    auto &retired=item.replacement->prepared;
    return retired&&item.replacement->source.incarnation==barrier.incarnation&&
      retired->editSource().incarnation==barrier.incarnation&&retired->endEditorInputBarrier(barrier);
  }
  return false;
}
bool AudioDevice::adoptStoppedPluginRepair(StoppedPluginRepair &plan) {
  if(plan.owner!=this||!plan.hosted||plan.adopted()||plan.providers.empty()||plan.originalCuts.size()!=plan.providers.size()||
      active()||!plugins_||plan.epoch!=pluginEpoch_||plan.hosted->owner!=plugins_.get())return false;
  if(!joinStoppedProcessing())return false;
  // Editor observation can execute native code. Repeat host and provider checks
  // afterwards, then adopt the complete shared group without native callbacks.
  const auto open=plugins_->openEditors();
  if(active()||!plugins_||plan.epoch!=pluginEpoch_||plan.hosted->owner!=plugins_.get())return false;
  for(size_t n=0;n<plan.providers.size();++n) {
    if(active()||!plugins_||plan.epoch!=pluginEpoch_||plan.hosted->owner!=plugins_.get())return false;
    const auto &owner=plan.providers[n];
    const auto slot=pluginSlot(owner.plugin);
    if(!slot||plugins_->editSource(*slot)!=owner.source||std::find(open.begin(),open.end(),*slot)!=open.end())return false;
    const auto cut=pluginInputBoundary(*slot,mach_absolute_time());const auto &before=plan.originalCuts[n];
    if(!cut||cut->source!=before.source||cut->fence.position!=before.fence.position||cut->fence.pending!=before.fence.pending)return false;
  }
  if(active()||!plugins_||plan.epoch!=pluginEpoch_||plan.hosted->owner!=plugins_.get())return false;
  return plugins_->adoptStoppedRepair(*plan.hosted);
}
void AudioDevice::stop() {
  playing_ = false;
  if (unit_)
    AudioOutputUnitStop(unit_);
  if(frameTimeline_)frameTimeline_->close(captureGeneration_,mach_absolute_time());
}
bool AudioDevice::joinStoppedProcessing() {
  if(active())return false;
  // Natural completion/fault changes playing_ inside the callback before its
  // final processor/timing work. Never infer quiescence from that flag alone.
  // Keep this separate from stop(), which closes the clock at the user's cut.
  if(unit_)check(AudioOutputUnitStop(unit_),"Cannot join stopped plugin processing");
  return true;
}
void AudioDevice::refreshPluginLatencies(const NativeSong &native) {
  if(!plugins_)return;
  if(!joinStoppedProcessing()) {
    // Preparation can wait behind an accepted transition. The existing plan
    // remains audible; the pending vendor serial makes the next UI poll retry.
    auto prepared=native;prepared.ensureMixer();plugins_->refreshLatencies(prepared);
  } else plugins_->refreshLatencies();
}
OSStatus AudioDevice::callback(void *ref, AudioUnitRenderActionFlags *, const AudioTimeStamp *timestamp, UInt32, UInt32 frames,
                               AudioBufferList *buffers) {
  auto &self = *static_cast<AudioDevice *>(ref);
  auto start = mach_absolute_time();
  for (UInt32 i = 0; i < buffers->mNumberBuffers; ++i)
    if (buffers->mBuffers[i].mData)
      std::memset(buffers->mBuffers[i].mData, 0, buffers->mBuffers[i].mDataByteSize);
  if (self.playing_.load(std::memory_order_relaxed) && self.renderer_ && buffers->mNumberBuffers == 1 &&
      buffers->mBuffers[0].mData && buffers->mBuffers[0].mDataByteSize >= frames * 8) {
    auto *const callbackOutput = static_cast<float *>(buffers->mBuffers[0].mData);
    const UInt32 callbackFrames = frames;
    const double ticksPerFrame = 1e9 / (self.sampleRate_ * self.nanosPerTick_);
    float left = 0, right = 0;
    // A device may deliver more frames than it negotiated, or more than the
    // 4096 prepared by every plugin, mixer and graph buffer. Render such a
    // callback as consecutive blocks: results do not depend on the partition.
    for (UInt32 done = 0; done < callbackFrames && self.playing_.load(std::memory_order_relaxed);) {
      const UInt32 frames = std::min<UInt32>(callbackFrames - done, maximumBlockFrames);
      auto *const output = callbackOutput + size_t(done) * 2;
      const UInt32 blockStart = done;
      const auto capturedFrame=self.renderer_->telemetry().frames;
      UInt32 capturedFrames=0;
      done += frames;
      if (self.plugins_) {
        self.plugins_->beginRenderBlock();
        self.plugins_->syncTransport(*self.renderer_);
      }
      if (!self.renderEnded_) {
        self.renderer_->recordingTime(timestamp && (timestamp->mFlags & kAudioTimeStampHostTimeValid)
                                          ? timestamp->mHostTime + uint64_t(std::llround(blockStart * ticksPerFrame)) : 0,
          ticksPerFrame);
        auto received = self.renderer_->render(output, frames);
        capturedFrames=received;
        if (received < frames) {
          if (self.plugins_)
            self.plugins_->endNotes();
          self.renderEnded_ = true;
          self.tailFrames_ = self.tailBudgetFrames_ =
              self.plugins_ ? uint64_t(std::ceil((self.plugins_->tail() + self.plugins_->latency()) * self.sampleRate_))
                            : 0;
          auto silence = frames - received;
          self.tailFrames_ = self.tailFrames_ > silence ? self.tailFrames_ - silence : 0;
        }
      } else
        self.tailFrames_ = self.tailFrames_ > frames ? self.tailFrames_ - frames : 0;
      if (self.plugins_ && !self.plugins_->process(output, frames)) {
        self.playing_ = false;
        if(self.frameTimeline_)self.frameTimeline_->invalidate();
      } else if(self.renderer_->faulted()) {
        if(self.frameTimeline_)self.frameTimeline_->invalidate();
      } else if(self.captureFrames_&&capturedFrames&&self.frameTimeline_&&timestamp&&
                (timestamp->mFlags&kAudioTimeStampHostTimeValid)&&timestamp->mHostTime) {
        const long double first=static_cast<long double>(timestamp->mHostTime)+blockStart*ticksPerFrame;
        const long double last=static_cast<long double>(timestamp->mHostTime)+(blockStart+capturedFrames)*ticksPerFrame;
        if(std::isfinite(first)&&std::isfinite(last)&&first>=1&&last<static_cast<long double>(UINT64_MAX))
          self.frameTimeline_->publish(uint64_t(first),uint64_t(last),capturedFrame,capturedFrames,self.sampleRate_,self.captureGeneration_);
      }
      if (self.renderEnded_ && self.plugins_) {
        const auto budget = uint64_t(std::ceil((self.plugins_->tail() + self.plugins_->latency()) * self.sampleRate_));
        if (budget > self.tailBudgetFrames_) self.tailFrames_ += budget - self.tailBudgetFrames_;
        self.tailBudgetFrames_ = std::max(self.tailBudgetFrames_, budget);
        // A control edit can move stored energy into a slower mode even when it
        // remains inside a precomputed range. Allow decay from that edit too.
        if (self.plugins_->tailRevision() != self.tailRevision_) self.tailFrames_ = std::max(self.tailFrames_, budget);
      }
      if (self.plugins_) self.tailRevision_ = self.plugins_->tailRevision();
      if (self.renderEnded_ && !self.tailFrames_) {
        self.playing_ = false;
        // Keep the last musical presentation bound; callback execution can
        // precede that bound, so wall time here must not trim queued audio.
        if(self.frameTimeline_)self.frameTimeline_->close(self.captureGeneration_,0);
      }
      for (uint32_t frame = 0; frame < frames; ++frame) {
        left = std::max(left, std::abs(output[frame * 2]));
        right = std::max(right, std::abs(output[frame * 2 + 1]));
      }
    }
    self.outputLeft_ = left;
    self.outputRight_ = right;
  }
  uint64_t elapsed = uint64_t((mach_absolute_time() - start) * self.nanosPerTick_);
  self.callbackHistogram_[std::min<size_t>(elapsed / 10000, self.callbackHistogram_.size() - 1)].fetch_add(
      1, std::memory_order_relaxed);
  self.callbacks_.fetch_add(1, std::memory_order_relaxed);
  if (elapsed > frames * 1e9 / self.sampleRate_)
    self.overruns_.fetch_add(1, std::memory_order_relaxed);
  auto old = self.maxNanos_.load(std::memory_order_relaxed);
  if (elapsed > old)
    self.maxNanos_.store(elapsed, std::memory_order_relaxed);
  return noErr;
}
Telemetry AudioDevice::telemetry() const {
  auto t = renderer_ ? renderer_->telemetry() : Telemetry{};
  t.callbacks = callbacks_;
  t.overruns = overruns_;
  t.maxMicros = maxNanos_ / 1000.0;
  uint64_t accumulated = 0;
  const auto target = uint64_t(std::ceil(double(t.callbacks) * 0.999));
  for (size_t i = 0; target && i < callbackHistogram_.size(); ++i) {
    accumulated += callbackHistogram_[i].load(std::memory_order_relaxed);
    if (accumulated >= target) {
      t.p999Micros = i + 1 == callbackHistogram_.size() ? t.maxMicros : double(i + 1) * 10;
      break;
    }
  }
  t.left = outputLeft_.load();
  t.right = outputRight_.load();
  return t;
}
} // namespace Tracker
