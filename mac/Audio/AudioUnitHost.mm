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
static_assert(audioUnitMusicDeviceType == kAudioUnitType_MusicDevice);
static_assert(kAudioUnitParameterUnit_Generic == 0 && kAudioUnitParameterUnit_Indexed == 1 &&
  kAudioUnitParameterUnit_Boolean == 2 && kAudioUnitParameterUnit_Percent == 3 &&
  kAudioUnitParameterUnit_Hertz == 8 && kAudioUnitParameterUnit_MIDINoteNumber == 11 &&
  kAudioUnitParameterUnit_Decibels == 13 && kAudioUnitParameterUnit_Milliseconds == 24);
// Platform storage stays private to Objective-C++; the facade and scheduler
// are compiled once in TrackerHosted on both Mac and Windows.
class MacPluginBackend final : public PluginBackend {
  static constexpr uint32_t maximumFrames = 4096;
  AudioUnit unit_ = nullptr;
  std::array<float, maximumFrames> inputLeft_{}, inputRight_{}, outputLeft_{}, outputRight_{};
  struct StereoBuffers { UInt32 count; AudioBuffer buffers[2]; };
  static OSStatus input(void *, AudioUnitRenderActionFlags *, const AudioTimeStamp *, UInt32, UInt32, AudioBufferList *);
  double latency_ = 0, tail_ = 0;
  PluginDescriptor descriptor_;
  std::string instanceID_;
  uint32_t assignedInstrument_ = 0, midiChannel_ = 1;
  std::vector<PluginInstrumentAlias> aliases_;
  PluginTransport transport_;
  double rate_ = 48000;
  uint64_t renderPosition_ = 0;
  std::unique_ptr<VST3Plugin> vst_;
  std::vector<PluginAudioBus> buses_;
  std::vector<uint32_t> auxiliaryInputs_, auxiliaryOutputs_;
  std::array<std::unique_ptr<PluginAudioStorage>, 64> auInputs_, auxiliaryOutputBuffers_;
  std::array<const float *, 64> inputSources_{};
  void *editorWindow_ = nullptr, *editorDelegate_ = nullptr, *parameterListener_ = nullptr;
public:
  MacPluginBackend(const PluginState &, double rate, bool offline);
  ~MacPluginBackend() override;
  bool process(float *, uint32_t, uint64_t, const float *const *, uint32_t, const PluginTransport &) noexcept override;
  bool parameter(uint32_t, double, uint32_t) noexcept override;
  bool supportsSampleOffsetParameters() const noexcept override { return bool(vst_); }
  void transport(const PluginTransport &t) noexcept override { transport_ = t; }
  bool midi(uint8_t, uint8_t, uint8_t) noexcept override;
  const std::vector<PluginAudioBus> &buses() const override { return buses_; }
  const float *auxiliaryOutput(uint32_t bus) const noexcept override {
    return bus < auxiliaryOutputBuffers_.size() && auxiliaryOutputBuffers_[bus] ? auxiliaryOutputBuffers_[bus]->interleaved.data() : nullptr;
  }
  std::vector<PluginParameter> parameters() const override;
  std::vector<PluginProgram> programs() const override;
  void loadProgram(const std::string &) override;
  PluginState state() const override;
  double latency() const override { return latency_; }
  double tail() const override { return tail_; }
  void showEditor() override;
  void closeEditor() override;
  bool editorOpen() const override;
  bool popEdit(uint32_t &, float &) noexcept override;
  static std::vector<PluginDescriptor> discover();
};
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
std::vector<PluginDescriptor> MacPluginBackend::discover() {
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
MacPluginBackend::MacPluginBackend(const PluginState &state, double rate, bool offline)
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
        auto &s = *static_cast<MacPluginBackend *>(ref);
        if (beat)
          *beat = s.transport_.beat;
        if (tempo)
          *tempo = s.transport_.tempo;
        return noErr;
      };
      callbacks.musicalTimeLocationProc = [](void *ref, UInt32 *delta, Float32 *numerator, UInt32 *denominator,
                                             Float64 *bar) -> OSStatus {
        auto &s = *static_cast<MacPluginBackend *>(ref);
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
        auto &s = *static_cast<MacPluginBackend *>(ref);
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
MacPluginBackend::~MacPluginBackend() {
  closeEditor();
  if (unit_)
    pluginMainCall([&] {
      AudioUnitUninitialize(unit_);
      AudioComponentInstanceDispose(unit_);
    });
}
OSStatus MacPluginBackend::input(void *reference, AudioUnitRenderActionFlags *, const AudioTimeStamp *, UInt32 bus,
                             UInt32 frames, AudioBufferList *buffers) {
  auto &self = *static_cast<MacPluginBackend *>(reference);
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
bool MacPluginBackend::process(float *buffer, uint32_t frames, uint64_t position, const float *const *inputs, uint32_t offset, const PluginTransport &transport) noexcept {
  transport_ = transport;
  std::copy_n(inputs, inputSources_.size(), inputSources_.begin());
  renderPosition_ = position;
  if (vst_) {
    vst_->transport(transport_);
    if (!vst_->process(buffer, frames, position, inputSources_.data(), offset)) return false;
    for (auto bus : auxiliaryOutputs_)
      std::copy_n(vst_->auxiliaryOutput(bus), frames * 2, auxiliaryOutputBuffers_[bus]->interleaved.data());
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
    auto *destination = bus.index ? auxiliaryOutputBuffers_[bus.index]->interleaved.data() : buffer;
    for (uint32_t i = 0; i < frames; ++i) {
      const float l = flags & kAudioUnitRenderAction_OutputIsSilence ? 0 : left[i];
      const float r = flags & kAudioUnitRenderAction_OutputIsSilence ? 0 : right[i];
      if (!std::isfinite(l) || !std::isfinite(r)) return false;
      destination[i * 2] = l; destination[i * 2 + 1] = r;
    }
  }
  return true;
}
bool MacPluginBackend::parameter(uint32_t id, double value, uint32_t offset) noexcept {
  if (vst_)
    return vst_->parameter(id, value, offset);
  return AudioUnitSetParameter(unit_, id, kAudioUnitScope_Global, 0, value, offset) == noErr;
}
std::vector<PluginProgram> MacPluginBackend::programs() const {
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
void MacPluginBackend::loadProgram(const std::string &id) {
  if(vst_){vst_->loadProgram(id);return;}
  const auto available=programs();const auto found=std::find_if(available.begin(),available.end(),[&](const auto &p){return p.id==id;});
  if(found==available.end()||!found->loadable)throw std::invalid_argument("Factory preset no longer exists or cannot be loaded");
  pluginMainCall([&]{AUPreset preset{int32_t(std::stol(id.substr(3))),(__bridge CFStringRef)@(found->name.c_str())};
    checkAU(AudioUnitSetProperty(unit_,kAudioUnitProperty_PresentPreset,kAudioUnitScope_Global,0,&preset,sizeof(preset)),"Audio Unit rejected factory preset");});
}
std::vector<PluginParameter> MacPluginBackend::parameters() const {
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
PluginState MacPluginBackend::state() const {
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
bool MacPluginBackend::midi(uint8_t status, uint8_t a, uint8_t b) noexcept {
  return vst_ ? vst_->midi(status, a, b) : MusicDeviceMIDIEvent(unit_, status, a, b, 0) == noErr;
}
void MacPluginBackend::showEditor() {
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
bool MacPluginBackend::editorOpen() const {
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
void MacPluginBackend::closeEditor() {
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
bool MacPluginBackend::popEdit(uint32_t &id, float &value) noexcept {
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
namespace {
class MacBackendFactory final : public PluginBackendFactory {
public:
  std::unique_ptr<PluginBackend> create(const PluginState &state, double rate, bool offline) override {
    return std::make_unique<MacPluginBackend>(state, rate, offline);
  }
  std::vector<PluginDescriptor> discover() override { return MacPluginBackend::discover(); }
  std::vector<PluginDescriptor> discoverVST3(const std::string &path) override { return VST3Plugin::discover(path); }
};
}
PluginBackendFactory &platformPluginBackendFactory() { static MacBackendFactory factory; return factory; }
} // namespace Tracker
