#include "AudioUnitHost.hpp"
#include "NativeSignalGraph.hpp"
#include "PluginMainThread.hpp"
#include "PluginWindow.hpp"
#include "VST3Host.hpp"
#include "editor/TrackerDocument.hpp"
#import <AppKit/AppKit.h>
#import <AudioUnit/AUCocoaUIView.h>
#import <Foundation/Foundation.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <set>
@implementation RSPluginWindowDelegate
- (void)windowWillClose:(NSNotification *)notification {
  if (self.onClose)
    self.onClose();
}
@end
namespace Tracker {
void validatePluginCapacity(const std::vector<PluginState> &states, size_t mixerBuses) {
  validatePluginAssignments(states);
  static_assert(maximumNativeAdapters == OpenMPT::MAX_MIXPLUGINS);
  if (states.size() > maximumNativePlugins) throw std::invalid_argument("Use at most 64 native devices.");
  const auto assigned = std::count_if(states.begin(), states.end(), [](const auto &state) { return state.instrument != 0; });
  if (mixerBuses > maximumNativeAdapters || size_t(assigned) > maximumNativeAdapters - mixerBuses)
    throw std::invalid_argument("Mixer buses and assigned plugin instruments together exceed 250. Remove a bus or unassign an instrument.");
}
namespace {
struct AUEditObserver {
  AUEventListenerRef listener = nullptr;
  struct Edit {
    uint32_t id;
    float value;
  };
  std::array<Edit, 1024> events{};
  uint32_t read = 0, write = 0;
  static void callback(void *ref, void *, const AudioUnitEvent *event, UInt64, Float32 value) {
    auto &s = *static_cast<AUEditObserver *>(ref);
    if (event->mEventType == kAudioUnitEvent_ParameterValueChange && s.write - s.read < s.events.size())
      s.events[s.write++ % s.events.size()] = {event->mArgument.mParameter.mParameterID, value};
  }
  ~AUEditObserver() {
    if (listener)
      AUListenerDispose(listener);
  }
};
void checkAU(OSStatus result, const char *context) {
  if (result)
    throw std::runtime_error(std::string(context) + " (" + std::to_string(result) + ")");
}
std::string string(CFStringRef value) {
  if (!value)
    return {};
  return [(__bridge NSString *)value UTF8String];
}
} // namespace
std::vector<PluginDescriptor> NativePlugin::discover() {
  std::vector<PluginDescriptor> result;
  for (auto type : {kAudioUnitType_Effect, kAudioUnitType_MusicDevice}) {
    AudioComponentDescription filter{type, 0, 0, 0, 0};
    AudioComponent component = nullptr;
    while ((component = AudioComponentFindNext(component, &filter))) {
      AudioComponentDescription description{};
      AudioComponentGetDescription(component, &description);
      CFStringRef name = nullptr;
      AudioComponentCopyName(component, &name);
      result.push_back(
          {description.componentType, description.componentSubType, description.componentManufacturer, string(name)});
      result.back().instrument = type == kAudioUnitType_MusicDevice;
      if (name)
        CFRelease(name);
    }
  }
  return result;
}
std::vector<PluginDescriptor> NativePlugin::builtins() {
  std::vector<PluginDescriptor> result;
  for (const auto &effect : nativeEffects()) {
    PluginDescriptor descriptor;
    descriptor.format = "Built-in"; descriptor.name = effect.name; descriptor.classID = effect.identifier;
    result.push_back(std::move(descriptor));
  }
  return result;
}
NativePlugin::NativePlugin(const PluginState &state, double rate, bool offline)
    : descriptor_(state.descriptor), instanceID_(state.instanceID), assignedInstrument_(state.instrument),
      midiChannel_(state.midiChannel), aliases_(state.aliases), rate_(rate) {
  validatePluginAssignments({&state, 1});
  auto validateBuses = [](const std::vector<uint32_t> &values) {
    std::array<bool, 64> seen{};
    for (auto bus : values) {
      if (!bus || bus >= seen.size() || seen[bus]) throw std::invalid_argument("Invalid or duplicate auxiliary bus index");
      seen[bus] = true;
    }
  };
  validateBuses(state.auxiliaryInputs); validateBuses(state.auxiliaryOutputs);
  auxiliaryInputs_ = state.auxiliaryInputs; auxiliaryOutputs_ = state.auxiliaryOutputs;
  if (descriptor_.format == "Built-in") {
    if (descriptor_.type || descriptor_.subtype || descriptor_.manufacturer || descriptor_.instrument ||
        state.instrument || !descriptor_.path.empty() || !auxiliaryOutputs_.empty())
      throw std::invalid_argument("Invalid built-in effect configuration");
    builtin_ = std::make_unique<NativeEffect>(descriptor_.classID, rate, state.state);
    const bool sidechain = builtin_->definition().sidechain;
    if (!auxiliaryInputs_.empty() && (!sidechain || auxiliaryInputs_ != std::vector<uint32_t>{1}))
      throw std::invalid_argument("Built-in auxiliary input is unavailable");
    descriptor_.name = builtin_->definition().name;
    latency_ = builtin_->latency();
    tail_ = builtin_->tail();
    buses_ = {{0, 2, "Stereo input", true, true, true}, {0, 2, "Stereo output", false, true, true}};
    if (sidechain) buses_.push_back({1,2,"Detector sidechain",true,!auxiliaryInputs_.empty(),true});
    return;
  }
  if (descriptor_.format == "VST3") {
    vst_ = std::make_unique<VST3Plugin>(state, rate, offline);
    latency_ = vst_->latency();
    tail_ = vst_->tail();
    buses_ = vst_->buses();
    for (auto bus : auxiliaryOutputs_) auxiliaryOutputBuffers_[bus] = std::make_unique<PluginAudioStorage>();
    return;
  }
  pluginMainCall([&] {
    AudioComponentDescription description{descriptor_.type, descriptor_.subtype, descriptor_.manufacturer, 0, 0};
    if (description.componentType != kAudioUnitType_Effect && description.componentType != kAudioUnitType_MusicDevice)
      throw std::runtime_error("Select a stereo Audio Unit effect.");
    auto component = AudioComponentFindNext(nullptr, &description);
    if (!component)
      throw std::runtime_error("Audio Unit is missing: " + descriptor_.name);
    checkAU(AudioComponentInstanceNew(component, &unit_), "Cannot create Audio Unit");
    try {
      if (!state.state.empty()) {
        CFDataRef data =
            CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(state.state.data()), state.state.size());
        CFPropertyListRef value =
            CFPropertyListCreateWithData(kCFAllocatorDefault, data, kCFPropertyListImmutable, nullptr, nullptr);
        CFRelease(data);
        if (!value)
          throw std::runtime_error("Invalid Audio Unit state");
        auto result =
            AudioUnitSetProperty(unit_, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &value, sizeof(value));
        CFRelease(value);
        checkAU(result, "Cannot restore Audio Unit state");
      }
      AudioStreamBasicDescription format{};
      format.mSampleRate = rate;
      format.mFormatID = kAudioFormatLinearPCM;
      format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
      format.mBytesPerPacket = 4;
      format.mFramesPerPacket = 1;
      format.mBytesPerFrame = 4;
      format.mChannelsPerFrame = 2;
      format.mBitsPerChannel = 32;
      AURenderCallbackStruct callback{input, this};
      for (bool isInput : {true, false}) {
        const auto scope = isInput ? kAudioUnitScope_Input : kAudioUnitScope_Output;
        const auto &enabled = isInput ? auxiliaryInputs_ : auxiliaryOutputs_;
        UInt32 count = 0, size = sizeof(count);
        checkAU(AudioUnitGetProperty(unit_, kAudioUnitProperty_ElementCount, scope, 0, &count, &size), "Cannot read Audio Unit buses");
        if (count > 64 || (!isInput && !count)) throw std::runtime_error("Unsupported Audio Unit bus count");
        for (auto index : enabled) if (index >= count) throw std::runtime_error("Audio Unit auxiliary bus does not exist");
        for (UInt32 bus = 0; bus < count; ++bus) {
          AudioStreamBasicDescription existing{}; size = sizeof(existing);
          checkAU(AudioUnitGetProperty(unit_, kAudioUnitProperty_StreamFormat, scope, bus, &existing, &size), "Cannot read Audio Unit bus format");
          // Main stereo behavior stays compatible with existing projects. Auxiliaries
          // keep their advertised mono/stereo layouts; surround is listed but unavailable.
          const auto channels = bus == 0 ? 2u : existing.mChannelsPerFrame;
          const bool supported = channels >= 1 && channels <= 2;
          const bool active = bus == 0 || std::find(enabled.begin(), enabled.end(), bus) != enabled.end();
          if (active && !supported) throw std::runtime_error("Audio Unit auxiliary bus requires an unsupported layout");
          std::string name = (isInput ? "Input " : "Output ") + std::to_string(bus + 1);
          CFStringRef label = nullptr; size = sizeof(label);
          if (!AudioUnitGetProperty(unit_, kAudioUnitProperty_ElementName, scope, bus, &label, &size) && label) {
            name = string(label); CFRelease(label);
          }
          buses_.push_back({bus, channels, std::move(name), isInput, active, supported});
          // AU has no general activateBus contract. Configure all supported inputs
          // and feed inactive ones silence, while rendering only enabled outputs.
          if (supported && (isInput || active)) {
            format.mChannelsPerFrame = channels;
            checkAU(AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat, scope, bus, &format, sizeof(format)), "Audio Unit rejects bus format");
            if (isInput) {
              if (bus) auInputs_[bus] = std::make_unique<PluginAudioStorage>();
              checkAU(AudioUnitSetProperty(unit_, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, bus, &callback, sizeof(callback)), "Cannot connect Audio Unit input");
            } else if (bus) auxiliaryOutputBuffers_[bus] = std::make_unique<PluginAudioStorage>();
          }
        }
      }
      UInt32 maximum = maximumFrames;
      checkAU(AudioUnitSetProperty(unit_, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maximum, sizeof(maximum)), "Cannot set Audio Unit buffer limit");
      UInt32 isOffline = offline;
      AudioUnitSetProperty(unit_, kAudioUnitProperty_OfflineRender, kAudioUnitScope_Global, 0, &isOffline, sizeof(isOffline));
      HostCallbackInfo callbacks{};
      callbacks.hostUserData = this;
      callbacks.beatAndTempoProc = [](void *ref, Float64 *beat, Float64 *tempo) -> OSStatus {
        auto &s = *static_cast<NativePlugin *>(ref);
        if (beat)
          *beat = s.transport_.beat;
        if (tempo)
          *tempo = s.transport_.tempo;
        return noErr;
      };
      callbacks.musicalTimeLocationProc = [](void *ref, UInt32 *delta, Float32 *numerator, UInt32 *denominator,
                                             Float64 *bar) -> OSStatus {
        auto &s = *static_cast<NativePlugin *>(ref);
        if (delta)
          *delta =
              UInt32(std::ceil((std::ceil(s.transport_.beat) - s.transport_.beat) * 60 * s.rate_ / s.transport_.tempo));
        if (numerator)
          *numerator = s.transport_.numerator;
        if (denominator)
          *denominator = 4;
        if (bar)
          *bar = s.transport_.bar;
        return noErr;
      };
      callbacks.transportStateProc = [](void *ref, Boolean *playing, Boolean *changed, Float64 *frame, Boolean *cycle,
                                        Float64 *start, Float64 *end) -> OSStatus {
        auto &s = *static_cast<NativePlugin *>(ref);
        if (playing)
          *playing = s.transport_.playing;
        if (changed)
          *changed = s.renderPosition_ == 0;
        if (frame)
          *frame = s.renderPosition_;
        if (cycle)
          *cycle = false;
        if (start)
          *start = 0;
        if (end)
          *end = 0;
        return noErr;
      };
      AudioUnitSetProperty(unit_, kAudioUnitProperty_HostCallbacks, kAudioUnitScope_Global, 0, &callbacks,
                           sizeof(callbacks));
      checkAU(AudioUnitInitialize(unit_), "Audio Unit initialization failed");
      UInt32 size = sizeof(double);
      AudioUnitGetProperty(unit_, kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0, &latency_, &size);
      size = sizeof(double);
      AudioUnitGetProperty(unit_, kAudioUnitProperty_TailTime, kAudioUnitScope_Global, 0, &tail_, &size);
      if (!std::isfinite(latency_) || latency_ < 0 || latency_ > 2)
        throw std::runtime_error("Audio Unit latency exceeds the two-second host limit.");
      if (!std::isfinite(tail_) || tail_ < 0)
        tail_ = 0;
      tail_ = std::min(30.0, tail_);
    } catch (...) {
      AudioComponentInstanceDispose(unit_);
      unit_ = nullptr;
      throw;
    }
  });
}
NativePlugin::~NativePlugin() {
  closeEditor();
  if (unit_)
    pluginMainCall([&] {
      AudioUnitUninitialize(unit_);
      AudioComponentInstanceDispose(unit_);
    });
}
OSStatus NativePlugin::input(void *reference, AudioUnitRenderActionFlags *, const AudioTimeStamp *, UInt32 bus,
                             UInt32 frames, AudioBufferList *buffers) {
  auto &self = *static_cast<NativePlugin *>(reference);
  if (frames > maximumFrames || bus >= 64 || (bus && !self.auInputs_[bus]) || buffers->mNumberBuffers < 1 || buffers->mNumberBuffers > 2)
    return kAudioUnitErr_FormatNotSupported;
  for (UInt32 channel = 0; channel < buffers->mNumberBuffers; ++channel) {
    auto *source = bus ? (channel ? self.auInputs_[bus]->right.data() : self.auInputs_[bus]->left.data())
                       : (channel ? self.inputRight_.data() : self.inputLeft_.data());
    auto &buffer = buffers->mBuffers[channel];
    if (buffer.mData) {
      if (buffer.mDataByteSize < frames * sizeof(float))
        return kAudioUnitErr_TooManyFramesToProcess;
      std::memcpy(buffer.mData, source, frames * sizeof(float));
    } else
      buffer.mData = source;
    buffer.mDataByteSize = frames * sizeof(float);
    buffer.mNumberChannels = 1;
  }
  return noErr;
}
bool NativePlugin::processBlock(float *buffer, uint32_t frames, uint64_t position, uint32_t offset) noexcept {
  renderPosition_ = position;
  if (builtin_) return builtin_->process(buffer, frames, inputSources_[1] ? inputSources_[1] + offset*2 : nullptr);
  if (vst_) {
    vst_->transport(transport_);
    if (!vst_->process(buffer, frames, position, inputSources_.data(), offset)) return false;
    for (auto bus : auxiliaryOutputs_)
      std::copy_n(vst_->auxiliaryOutput(bus), frames * 2, auxiliaryOutputBuffers_[bus]->interleaved.data() + offset * 2);
    return true;
  }
  if (frames > maximumFrames)
    return false;
  for (uint32_t i = 0; i < frames; ++i) {
    inputLeft_[i] = buffer[i * 2];
    inputRight_[i] = buffer[i * 2 + 1];
  }
  for (const auto &bus : buses_) if (bus.input && bus.index && auInputs_[bus.index]) {
    auto &audio = *auInputs_[bus.index]; const float *source = inputSources_[bus.index];
    for (uint32_t i = 0; i < frames; ++i) {
      const float l = source ? source[(offset + i) * 2] : 0, r = source ? source[(offset + i) * 2 + 1] : 0;
      audio.left[i] = bus.channels == 1 ? (l + r) * .5f : l; audio.right[i] = r;
    }
  }
  AudioTimeStamp time{};
  time.mSampleTime = double(position); time.mFlags = kAudioTimeStampSampleTimeValid;
  for (const auto &bus : buses_) if (!bus.input && bus.active) {
    auto *left = bus.index ? auxiliaryOutputBuffers_[bus.index]->left.data() : outputLeft_.data();
    auto *right = bus.index ? auxiliaryOutputBuffers_[bus.index]->right.data() : outputRight_.data();
    std::fill_n(left, frames, 0); std::fill_n(right, frames, 0);
    StereoBuffers output{bus.channels, {{1, frames * 4, left}, {1, frames * 4, right}}};
    AudioUnitRenderActionFlags flags = 0;
    if (AudioUnitRender(unit_, &flags, &time, bus.index, frames, reinterpret_cast<AudioBufferList *>(&output))) return false;
    // An AU may return its own buffers even when supplied host storage.
    if (output.count != bus.channels) return false;
    for (uint32_t ch = 0; ch < bus.channels; ++ch)
      if (!output.buffers[ch].mData || output.buffers[ch].mDataByteSize < frames * sizeof(float)) return false;
    left = static_cast<float *>(output.buffers[0].mData);
    right = static_cast<float *>(output.buffers[bus.channels == 1 ? 0 : 1].mData);
    auto *destination = bus.index ? auxiliaryOutputBuffers_[bus.index]->interleaved.data() + offset * 2 : buffer;
    for (uint32_t i = 0; i < frames; ++i) {
      const float l = flags & kAudioUnitRenderAction_OutputIsSilence ? 0 : left[i];
      const float r = flags & kAudioUnitRenderAction_OutputIsSilence ? 0 : right[i];
      if (!std::isfinite(l) || !std::isfinite(r)) return false;
      destination[i * 2] = l; destination[i * 2 + 1] = r;
    }
  }
  return true;
}
bool NativePlugin::parameter(uint32_t id, float value, uint32_t offset) noexcept {
  if (builtin_) return !offset && builtin_->parameter(id, value);
  if (vst_)
    return vst_->parameter(id, value, offset);
  return AudioUnitSetParameter(unit_, id, kAudioUnitScope_Global, 0, value, offset) == noErr;
}
std::vector<PluginProgram> NativePlugin::programs() const {
  if (builtin_) return {};
  if (vst_) return vst_->programs();
  std::vector<PluginProgram> result;
  pluginMainCall([&] {
    CFArrayRef raw=nullptr;UInt32 size=sizeof(raw);
    const auto status=AudioUnitGetProperty(unit_,kAudioUnitProperty_FactoryPresets,kAudioUnitScope_Global,0,&raw,&size);
    if (status==kAudioUnitErr_InvalidProperty) return;
    checkAU(status,"Cannot read Audio Unit factory presets");
    if (!raw) return;
    struct Release { CFArrayRef value; ~Release(){CFRelease(value);} } release{raw};
    if (size!=sizeof(raw)||CFGetTypeID(raw)!=CFArrayGetTypeID()||CFArrayGetCount(raw)>4096)
      throw std::runtime_error("Invalid Audio Unit factory preset list");
    std::set<int32_t> seen;
    for(CFIndex i=0;i<CFArrayGetCount(raw);++i){
      const auto *preset=static_cast<const AUPreset *>(CFArrayGetValueAtIndex(raw,i));
      if(!preset||preset->presetNumber<0||!seen.insert(preset->presetNumber).second||!preset->presetName||
        CFGetTypeID(preset->presetName)!=CFStringGetTypeID()||CFStringGetLength(preset->presetName)>1024)
        throw std::runtime_error("Invalid Audio Unit factory preset entry");
      result.push_back({"au:"+std::to_string(preset->presetNumber),string(preset->presetName),"Factory presets",true});
    }
  });return result;
}
void NativePlugin::loadProgram(const std::string &id) {
  if(vst_){vst_->loadProgram(id);return;}
  const auto available=programs();const auto found=std::find_if(available.begin(),available.end(),[&](const auto &p){return p.id==id;});
  if(found==available.end()||!found->loadable)throw std::invalid_argument("Factory preset no longer exists or cannot be loaded");
  pluginMainCall([&]{AUPreset preset{int32_t(std::stol(id.substr(3))),(__bridge CFStringRef)@(found->name.c_str())};
    checkAU(AudioUnitSetProperty(unit_,kAudioUnitProperty_PresentPreset,kAudioUnitScope_Global,0,&preset,sizeof(preset)),"Audio Unit rejected factory preset");});
}
std::vector<PluginParameter> NativePlugin::parameters() const {
  if (builtin_) {
    std::vector<PluginParameter> result;
    for (const auto &p : builtin_->definition().parameters) {
      PluginParameter value{p.id, std::string(p.name), p.minimum, p.maximum, builtin_->value(p.id), kAudioUnitParameterUnit_Generic};
      value.step = p.step;
      value.continuous = p.step == 0 && p.choices.empty();
      if (p.unit == EffectUnit::Bits) value.unitLabel = "bits";
      else if (p.unit == EffectUnit::Decibels) { value.unit = kAudioUnitParameterUnit_Decibels; value.unitLabel = "dB"; }
      else if (p.unit == EffectUnit::Percent) { value.unit = kAudioUnitParameterUnit_Percent; value.unitLabel = "%"; }
      else if (p.unit == EffectUnit::Hertz) { value.unit = kAudioUnitParameterUnit_Hertz; value.unitLabel = "Hz"; value.logarithmic = true; }
      else if (p.unit == EffectUnit::Q) { value.unitLabel = "Q"; value.logarithmic = true; }
      else if (p.unit == EffectUnit::MidiNote) { value.unit = kAudioUnitParameterUnit_MIDINoteNumber; value.unitLabel = "MIDI"; value.step = 1; }
      else if (p.unit == EffectUnit::Semitones) value.unitLabel = "st";
      else if (p.unit == EffectUnit::Milliseconds) { value.unit = kAudioUnitParameterUnit_Milliseconds; value.unitLabel = "ms"; value.logarithmic = p.minimum > 0; }
      else if (p.unit == EffectUnit::Boolean) value.unit = kAudioUnitParameterUnit_Boolean;
      else if (p.unit == EffectUnit::Choice) value.unit = kAudioUnitParameterUnit_Indexed;
      for (auto choice : p.choices) value.choices.emplace_back(choice);
      value.continuous = value.step == 0 && value.choices.empty() && value.unit != kAudioUnitParameterUnit_Boolean && value.unit != kAudioUnitParameterUnit_Indexed;
      result.push_back(std::move(value));
    }
    return result;
  }
  if (vst_)
    return vst_->parameters();
  std::vector<PluginParameter> result;
  pluginMainCall([&] {
    UInt32 size = 0;
    Boolean writable = false;
    checkAU(
        AudioUnitGetPropertyInfo(unit_, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &size, &writable),
        "Cannot list Audio Unit parameters");
    if (size > 4096 * sizeof(AudioUnitParameterID))
      throw std::runtime_error("Audio Unit has too many parameters");
    std::vector<AudioUnitParameterID> ids(size / sizeof(AudioUnitParameterID));
    if (size)
      checkAU(
          AudioUnitGetProperty(unit_, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, ids.data(), &size),
          "Cannot read Audio Unit parameters");
    for (auto id : ids) {
      AudioUnitParameterInfo info{};
      size = sizeof(info);
      if (AudioUnitGetProperty(unit_, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id, &info, &size))
        continue;
      if (!(info.flags & kAudioUnitParameterFlag_IsWritable))
        continue;
      Float32 value = info.defaultValue;
      AudioUnitGetParameter(unit_, id, kAudioUnitScope_Global, 0, &value);
      auto name =
          (info.flags & kAudioUnitParameterFlag_HasCFNameString) ? string(info.cfNameString) : std::string(info.name);
      result.push_back({id, name, info.minValue, info.maxValue, value, info.unit});
      result.back().continuous = info.unit != kAudioUnitParameterUnit_Indexed && info.unit != kAudioUnitParameterUnit_Boolean && info.unit != kAudioUnitParameterUnit_MIDINoteNumber;
      if ((info.flags & kAudioUnitParameterFlag_CFNameRelease) && info.cfNameString)
        CFRelease(info.cfNameString);
    }
  });
  return result;
}
PluginState NativePlugin::state() const {
  if (vst_) {
    auto state = vst_->state();
    state.instanceID = instanceID_;
    state.instrument = assignedInstrument_; state.midiChannel = midiChannel_; state.aliases = aliases_;
    state.auxiliaryInputs = auxiliaryInputs_; state.auxiliaryOutputs = auxiliaryOutputs_;
    return state;
  }
  PluginState state{descriptor_};
  state.instanceID = instanceID_;
  state.instrument = assignedInstrument_; state.midiChannel = midiChannel_; state.aliases = aliases_;
  state.auxiliaryInputs = auxiliaryInputs_; state.auxiliaryOutputs = auxiliaryOutputs_;
  if (builtin_) { state.state = builtin_->state(); return state; }
  pluginMainCall([&] {
    CFPropertyListRef value = nullptr;
    UInt32 size = sizeof(value);
    checkAU(AudioUnitGetProperty(unit_, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &value, &size),
            "Cannot save Audio Unit state");
    if (!value)
      throw std::runtime_error("Audio Unit returned an empty state");
    CFDataRef data = CFPropertyListCreateData(kCFAllocatorDefault, value, kCFPropertyListBinaryFormat_v1_0, 0, nullptr);
    if (value)
      CFRelease(value);
    if (!data)
      throw std::runtime_error("Audio Unit state is not a property list");
    if (CFDataGetLength(data) > 16 * 1024 * 1024) {
      CFRelease(data);
      throw std::runtime_error("Audio Unit state exceeds the 16 MB project limit");
    }
    auto begin = reinterpret_cast<const std::byte *>(CFDataGetBytePtr(data));
    state.state.assign(begin, begin + CFDataGetLength(data));
    CFRelease(data);
  });
  return state;
}
std::vector<PluginInstrumentAlias> NativePlugin::assignments() const {
  std::vector<PluginInstrumentAlias> result;
  if (assignedInstrument_) result.push_back({assignedInstrument_, midiChannel_});
  result.insert(result.end(), aliases_.begin(), aliases_.end());
  return result;
}
PluginChain::PluginChain(const std::vector<PluginState> &states, double rate, bool offline,
                         const std::vector<ParameterChange> &automation, uint64_t startFrame)
    : sampleRate_(rate), offline_(offline), automation_(automation) {
  validatePluginCapacity(states);
  for (auto &state : states) {
    auto plugin = std::make_shared<NativePlugin>(state, rate, offline);
    plugin->automate(automation, plugins_.size(), rate, uint64_t(double(startFrame) * rate / 48000));
    if (!state.bypass && !plugin->isInstrument()) {
      latency_ += plugin->latency();
      tail_ += plugin->tail();
    }

    plugins_.push_back(std::move(plugin));
    instances_.push_back(state.instanceID);
    instruments_.push_back(state.instrument);
    bypass_.push_back(state.bypass);
  }
  double instrumentLatency = 0, instrumentTail = 0;
  for (size_t i = 0; i < plugins_.size(); ++i)
    if (plugins_[i]->isInstrument() && !bypass_[i] && instruments_[i]) {
      instrumentLatency = std::max(instrumentLatency, plugins_[i]->latency());
      instrumentTail = std::max(instrumentTail, std::max(2.0, plugins_[i]->tail()));
    }
  latency_ += instrumentLatency;
  tail_ += instrumentTail;
  dryDelay_.assign(size_t(std::llround(instrumentLatency * rate)) * 2, 0);
  for (auto &plugin : plugins_)
    if (plugin->isInstrument())
      plugin->compensateLatency(uint32_t(std::max(0.0, std::round((instrumentLatency - plugin->latency()) * rate))));
  for (auto &point : automation_) {
    if (point.slot >= states.size() || !std::isfinite(point.value) || point.frame > uint64_t(48000) * 604800)
      throw std::runtime_error("Invalid Audio Unit automation point");
    point.frame = uint64_t(double(point.frame) * rate / 48000);
  }
  position_ = uint64_t(double(startFrame) * rate / 48000);
  dryThrough_ = position_;
  captureTails();
}
bool PluginChain::parameter(uint32_t slot, uint32_t id, float value) noexcept {
  auto w = write_.load(std::memory_order_relaxed), r = read_.load(std::memory_order_acquire);
  if (w - r >= queue_.size() || slot >= plugins_.size() || !std::isfinite(value))
    return false;
  queue_[w % queue_.size()] = {slot, id, value, 0};
  write_.store(w + 1, std::memory_order_release);
  return true;
}
bool PluginChain::process(float *buffer, uint32_t frames) noexcept {
  applyPending();
  if (frames > 4096) {
    failed_ = true;
    std::fill(buffer, buffer + frames * 2, 0);
    return false;
  }
  if (mixer_) return finishMixer(buffer, frames);
  if (!dryDelay_.empty() && dryThrough_ < position_ + frames) {
    uint32_t offset = uint32_t(std::min<uint64_t>(frames, dryThrough_ > position_ ? dryThrough_ - position_ : 0));
    for (uint32_t n = offset * 2; n < frames * 2; ++n) {
      buffer[n] += dryDelay_[dryDelayPosition_];
      dryDelay_[dryDelayPosition_] = 0;
      dryDelayPosition_ = (dryDelayPosition_ + 1) % dryDelay_.size();
    }
    dryThrough_ = position_ + frames;
  }
  for (size_t i = 0; i < plugins_.size(); ++i) {
    auto &plugin = *plugins_[i];
    if (plugin.isInstrument()) {
      // The engine renders instrument blocks. Complete the final partial block
      // and release tails after the song ends, without rendering a block twice.
      auto rendered = plugin.renderedThrough();
      if (rendered < position_ + frames) {
        uint32_t offset = uint32_t(std::min<uint64_t>(frames, rendered > position_ ? rendered - position_ : 0));
        auto count = frames - offset;
        tailBuffer_.fill(0);
        if (!plugin.process(tailBuffer_.data(), count, position_ + offset))
          failed_ = true;
        if (!bypass_[i] && instruments_[i])
          for (uint32_t n = 0; n < count * 2; ++n)
            buffer[offset * 2 + n] += tailBuffer_[n];
      }
    }
  }
  for (size_t i = 0; i < plugins_.size(); ++i)
    if (!bypass_[i] && !plugins_[i]->isInstrument() && !plugins_[i]->process(buffer, frames, position_))
      failed_ = true;
  position_ += frames;
  if (failed_) {
    std::fill(buffer, buffer + frames * 2, 0);
    return false;
  }
  return true;
}
void PluginChain::applyPending() noexcept {
  auto r = read_.load(std::memory_order_relaxed), w = write_.load(std::memory_order_acquire);
  for (int n = 0; r != w && n < 128; ++n, ++r) {
    auto change = queue_[r % queue_.size()];
    if (!plugins_[change.slot]->parameter(change.id, change.value))
      failed_ = true;
  }
  read_.store(r, std::memory_order_release);
}
std::vector<PluginState> PluginChain::states() {
  while (read_.load() != write_.load())
    applyPending();
  std::vector<PluginState> out;
  for (size_t i = 0; i < plugins_.size(); ++i) {
    auto state = plugins_[i]->state();
    state.bypass = bypass_[i];
    state.instrument = instruments_[i];
    out.push_back(std::move(state));
  }
  return out;
}
std::vector<PluginParameter> PluginChain::parameters(size_t slot) const {
  return slot < plugins_.size() ? plugins_[slot]->parameters() : std::vector<PluginParameter>{};
}

std::vector<PluginDescriptor> NativePlugin::discoverVST3(const std::string &path) {
  return VST3Plugin::discover(path);
}
double NativePlugin::tail() const { return builtin_ ? builtin_->tail() : tail_; }
uint64_t NativePlugin::tailRevision() const noexcept { return builtin_ ? builtin_->tailRevision() : 0; }
void NativePlugin::includeParameterRange(uint32_t id, float minimum, float maximum) noexcept {
  if (builtin_) builtin_->includeParameterRange(id, minimum, maximum);
}
void PluginChain::captureTails() {
  compiledTails_.resize(plugins_.size());
  for (size_t i = 0; i < plugins_.size(); ++i) compiledTails_[i] = plugins_[i]->tail();
}
uint64_t PluginChain::tailRevision() const noexcept {
  uint64_t revision = 0;
  for (size_t i = 0; i < plugins_.size(); ++i)
    if (!bypass_[i] && (!plugins_[i]->isInstrument() || instruments_[i])) revision += plugins_[i]->tailRevision();
  return revision;
}
double PluginChain::tail() const {
  double result = tail_;
  for (size_t i = 0; i < plugins_.size(); ++i)
    if (!bypass_[i] && (!plugins_[i]->isInstrument() || instruments_[i]))
      result += std::max(0., plugins_[i]->tail() - compiledTails_[i]);
  return result;
}
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
      if (!(vst_ ? vst_->parameter(ramp.id,value,0) : parameter(ramp.id,float(value)))) return false;
      if (ramp.ramp.finished(at)) ramp.active = false;
      else if(vst_){const auto remaining=ramp.ramp.duration-(at-ramp.ramp.start);if(remaining<count)count=uint32_t(remaining+1);}
      else count = 1;
    }
    if (automationPosition_ < automation_.size())
      count = uint32_t(std::min<uint64_t>(count, automation_[automationPosition_].frame - position - consumed));
    if (musicalRead < musicalCount_)
      count = uint32_t(std::min<uint64_t>(count, (*musicalEvents_)[musicalRead].frame - position - consumed));
    if(midiRead<musicalMIDICount_)count=uint32_t(std::min<uint64_t>(count,(*musicalMIDI_)[midiRead].frame-position-consumed));
    // VST3 queues carry both endpoints of a linear segment inside the audio
    // buffer. Split only at musical events or ramp endings, not every sample.
    if(vst_&&count>1)for(const auto &ramp:parameterRamps_)if(ramp.active)
      if(!vst_->parameter(ramp.id,ramp.ramp.value(position+consumed+count-1),count-1))return false;
    if (!count || !processBlock(buffer + consumed * 2, count, position + consumed, consumed))
      return false;
    consumed += count;
    if (transport_.playing)
      transport_.beat += count * transport_.tempo / (60 * rate_);
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
bool NativePlugin::midi(uint8_t status, uint8_t a, uint8_t b) noexcept {
  if (builtin_) return false;
  return vst_ ? vst_->midi(status, a, b) : MusicDeviceMIDIEvent(unit_, status, a, b, 0) == noErr;
}
void NativePlugin::showEditor() {
  if (builtin_) throw std::runtime_error("This built-in effect uses the parameter controls in the Plugins panel.");
  if (![NSThread isMainThread]) {
    __block std::exception_ptr error;
    dispatch_sync(dispatch_get_main_queue(), ^{
      try {
        showEditor();
      } catch (...) {
        error = std::current_exception();
      }
    });
    if (error)
      std::rethrow_exception(error);
    return;
  }
  if (vst_) {
    vst_->showEditor();
    return;
  }
  if (editorWindow_) {
    [(__bridge NSWindow *)editorWindow_ makeKeyAndOrderFront:nil];
    return;
  }
  UInt32 size = 0;
  Boolean writable = false;
  checkAU(AudioUnitGetPropertyInfo(unit_, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, &size, &writable),
          "This Audio Unit has no custom interface. Use the parameter controls");
  if (size < sizeof(AudioUnitCocoaViewInfo) || size > 65536)
    throw std::runtime_error("Invalid Audio Unit interface");
  std::vector<std::byte> storage(size);
  auto *info = reinterpret_cast<AudioUnitCocoaViewInfo *>(storage.data());
  checkAU(AudioUnitGetProperty(unit_, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, info, &size),
          "Cannot load Audio Unit interface");
  NSBundle *bundle = [NSBundle bundleWithURL:(__bridge NSURL *)info->mCocoaAUViewBundleLocation];
  [bundle load];
  Class factory = NSClassFromString((__bridge NSString *)info->mCocoaAUViewClass[0]);
  NSView *view = nil;
  if ([factory conformsToProtocol:@protocol(AUCocoaUIBase)]) {
    id<AUCocoaUIBase> object = [[factory alloc] init];
    view = [object uiViewForAudioUnit:unit_ withSize:NSMakeSize(640, 480)];
  }
  CFRelease(info->mCocoaAUViewBundleLocation);
  auto classes = (size - sizeof(CFURLRef)) / sizeof(CFStringRef);
  for (size_t i = 0; i < classes; ++i)
    if (info->mCocoaAUViewClass[i])
      CFRelease(info->mCocoaAUViewClass[i]);
  if (!view)
    throw std::runtime_error("Audio Unit custom interface could not be created");
  NSWindow *window = [[NSWindow alloc] initWithContentRect:view.bounds
                                                 styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                                   backing:NSBackingStoreBuffered
                                                     defer:NO];
  window.releasedWhenClosed = NO;
  window.title = @(descriptor_.name.c_str());
  window.contentView = view;
  auto observer = std::make_unique<AUEditObserver>();
  checkAU(AUEventListenerCreate(AUEditObserver::callback, observer.get(), CFRunLoopGetMain(), kCFRunLoopCommonModes,
                                0.01, 0.01, &observer->listener),
          "Cannot observe Audio Unit edits");
  for (auto &p : parameters()) {
    AudioUnitEvent event{};
    event.mEventType = kAudioUnitEvent_ParameterValueChange;
    event.mArgument.mParameter = {unit_, p.id, kAudioUnitScope_Global, 0};
    AUEventListenerAddEventType(observer->listener, nullptr, &event);
  }
  RSPluginWindowDelegate *delegate = [RSPluginWindowDelegate new];
  delegate.onClose = ^{
    closeEditor();
  };
  window.delegate = delegate;
  editorDelegate_ = (__bridge_retained void *)delegate;
  parameterListener_ = observer.release();
  editorWindow_ = (__bridge_retained void *)window;
  [window center];
  [window makeKeyAndOrderFront:nil];
}
bool NativePlugin::editorOpen() const {
  if (builtin_) return false;
  if (vst_)
    return vst_->editorOpen();
  __block bool result = false;
  auto check = ^{
    result = editorWindow_ != nullptr;
  };
  if ([NSThread isMainThread])
    check();
  else
    dispatch_sync(dispatch_get_main_queue(), check);
  return result;
}
std::vector<size_t> PluginChain::openEditors() const {
  std::vector<size_t> slots;
  for (size_t i = 0; i < plugins_.size(); ++i)
    if (plugins_[i]->editorOpen())
      slots.push_back(i);
  return slots;
}
void NativePlugin::closeEditor() {
  if (builtin_) return;
  if (vst_) {
    vst_->closeEditor();
    return;
  }
  auto close = ^{
    if (parameterListener_) {
      delete static_cast<AUEditObserver *>(parameterListener_);
      parameterListener_ = nullptr;
    }
    if (editorWindow_) {
      NSWindow *window = (__bridge_transfer NSWindow *)editorWindow_;
      editorWindow_ = nullptr;
      window.delegate = nil;
      [window orderOut:nil];
      [window close];
    }
    if (editorDelegate_) {
      RSPluginWindowDelegate *delegate = (__bridge_transfer RSPluginWindowDelegate *)editorDelegate_;
      editorDelegate_ = nullptr;
      delegate.onClose = nil;
    }
  };
  if ([NSThread isMainThread])
    close();
  else
    dispatch_sync(dispatch_get_main_queue(), close);
}
void PluginChain::delayDry(float *left, float *right, uint32_t frames, uint64_t position) noexcept {
  if (!dryDelay_.empty())
    for (uint32_t n = 0; n < frames; ++n) {
      std::swap(left[n], dryDelay_[dryDelayPosition_]);
      std::swap(right[n], dryDelay_[dryDelayPosition_ + 1]);
      dryDelayPosition_ = (dryDelayPosition_ + 2) % dryDelay_.size();
    }
  dryThrough_ = position + frames;
}
bool NativePlugin::popEdit(uint32_t &id, float &value) noexcept {
  if (builtin_) return false;
  if (vst_)
    return vst_->popEdit(id, value);
  auto *observer = static_cast<AUEditObserver *>(parameterListener_);
  if (!observer || observer->read == observer->write)
    return false;
  auto event = observer->events[observer->read++ % observer->events.size()];
  id = event.id;
  value = event.value;
  return true;
}
void PluginChain::endNotes() noexcept {
  for (auto &plugin : plugins_)
    if (plugin->isInstrument())
      for (uint8_t ch = 0; ch < 16; ++ch)
        plugin->midi(0xb0 | ch, 123, 0);
}
void PluginChain::showEditor(size_t slot) {
  if (slot >= plugins_.size())
    throw std::runtime_error("Select a plugin");
  plugins_[slot]->showEditor();
}
bool PluginChain::popEdit(size_t slot, uint32_t &id, float &value) noexcept {
  return slot < plugins_.size() && plugins_[slot]->popEdit(id, value);
}

bool PluginChain::graphController(uint8_t cc,uint8_t value) noexcept {if(!signalGraph_)return false;signalGraph_->controller(cc,value);return true;}
std::vector<SignalActivity> PluginChain::graphActivity() const {return signalGraph_?signalGraph_->activity():std::vector<SignalActivity>{};}
void PluginChain::beginMixer(uint32_t frames) noexcept {
  if (!mixer_) return;
  applyPending(); mixer_->begin(frames, mixer_->through());
}
void PluginChain::routeInstrument(size_t processor, const float *buffer) noexcept {
  if (mixer_) {
    mixer_->instrument(processor, 0, buffer);
    for (const auto &bus : plugins_[processor]->buses()) if (!bus.input && bus.index && bus.active)
      mixer_->instrument(processor, bus.index, plugins_[processor]->auxiliaryOutput(bus.index));
  }
}
const float *PluginChain::processMixerBus(size_t bus, const float *left, const float *right) noexcept {
  if (!mixer_) return nullptr;
  auto process = [](void *context, size_t processor, float *buffer, uint32_t frames, uint64_t position) noexcept {
    auto &chain = *static_cast<PluginChain *>(context);
    if(processor >= chain.plugins_.size()) {
      const auto index=processor-chain.plugins_.size();
      if(!chain.signalGraph_||!chain.signalGraph_->process(index,buffer,frames,position,chain.mixer_->inputs(processor)))return false;
      for(auto port:chain.signalGraph_->outputs(index))chain.mixer_->instrument(processor,port,chain.signalGraph_->output(index,port));
      return true;
    }
    const bool okay=chain.plugins_[processor]->process(buffer, frames, position, chain.mixer_->inputs(processor));
    if(okay)for(const auto &bus:chain.plugins_[processor]->buses())if(!bus.input&&bus.index&&bus.active)chain.mixer_->instrument(processor,bus.index,chain.plugins_[processor]->auxiliaryOutput(bus.index));
    return okay;
  };
  const auto *result = mixer_->process(bus, left, right, process, this);
  if (bus == mixer_->plan().master) mixer_->complete();
  if (mixer_->failed()) failed_ = true;
  return result;
}
bool PluginChain::finishMixer(float *buffer, uint32_t frames) noexcept {
  const uint64_t end = position_ + frames;
  if (mixer_->through() < end) {
    const uint32_t offset = uint32_t(mixer_->through() > position_ ? mixer_->through() - position_ : 0);
    const auto count = frames - offset;
    mixer_->begin(count, position_ + offset);
    if(signalGraph_)signalGraph_->tail();
    for (size_t i = 0; i < plugins_.size(); ++i) if (plugins_[i]->isInstrument() && !bypass_[i] && instruments_[i]) {
      tailBuffer_.fill(0);
      if (!plugins_[i]->process(tailBuffer_.data(), count, position_ + offset)) failed_ = true;
      routeInstrument(i, tailBuffer_.data());
    }
    for (auto bus : mixer_->plan().order) {
      auto result = processMixerBus(bus, nullptr, nullptr);
      if (bus == mixer_->plan().master && result)
        std::copy_n(result, count * 2, buffer + offset * 2);
    }
    if (mixerRenderer_) mixerRenderer_->processNativeTail(buffer + offset * 2, count);
  }
  position_ = end;
  if (failed_) { std::fill_n(buffer, frames * 2, 0); return false; }
  return true;
}
} // namespace Tracker
