#include "AudioDevice.hpp"
#import <Foundation/Foundation.h>
#include <algorithm>
#include <cmath>
#include <mach/mach_time.h>
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
  mach_timebase_info_data_t timebase;
  mach_timebase_info(&timebase);
  nanosPerTick_ = double(timebase.numer) / timebase.denom;
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
                       uint32_t sequence, const NativeSong *native, PlaybackRegion region) {
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
  renderer_ = std::make_unique<Renderer>(bytes, uint32_t(sampleRate_), order, preview, sourcePath, sequence, region, preview ? nullptr : native);
  if (native && !preview) {renderer_->applyColumnMutes(*native, renderer_->song());renderer_->preparePreciseNotes(*native);}
  previewing_ = preview;
  plugins_ = pluginStates_.empty() && !(native && (native->mixer.active() || !native->performance.commands.empty()) && !preview)
                 ? nullptr
                 : std::make_unique<PluginChain>(pluginStates_, sampleRate_, false, automation_,
                                                 uint64_t(double(renderer_->telemetry().frames) * 48000 / sampleRate_));
  if (plugins_)
    plugins_->attachInstruments(*renderer_, preview ? nullptr : native);
  if (plugins_ && native && !preview)
    plugins_->attachMusicalAutomation(*renderer_, *native);
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
    throw;
  }
}
void AudioDevice::setPlugins(const std::vector<PluginState> &states, const std::vector<ParameterChange> &automation,
                             bool preserveEditors) {
  pendingEditors_ = preserveEditors && plugins_ ? plugins_->openEditors() : std::vector<size_t>{};
  auto next = states.empty() ? nullptr : std::make_unique<PluginChain>(states, sampleRate_, false, automation);
  stop();
  renderer_.reset();
  plugins_ = std::move(next);
  pluginStates_ = states;
  automation_ = automation;
}
std::vector<PluginState> AudioDevice::pluginStates() {
  stop();
  if (plugins_)
    pluginStates_ = plugins_->states();
  return pluginStates_;
}
void AudioDevice::stop() {
  playing_ = false;
  if (unit_)
    AudioOutputUnitStop(unit_);
}
void AudioDevice::refreshPluginLatencies() {
  const bool resume = active();
  stop(); // Quiesces the callback before touching processor state or delay storage.
  if (plugins_) plugins_->refreshLatencies();
  if (resume && unit_) {
    check(AudioOutputUnitStart(unit_), "Cannot resume playback after plugin latency update");
    playing_ = true;
  }
}
OSStatus AudioDevice::callback(void *ref, AudioUnitRenderActionFlags *, const AudioTimeStamp *timestamp, UInt32, UInt32 frames,
                               AudioBufferList *buffers) {
  auto &self = *static_cast<AudioDevice *>(ref);
  auto start = mach_absolute_time();
  for (UInt32 i = 0; i < buffers->mNumberBuffers; ++i)
    if (buffers->mBuffers[i].mData)
      std::memset(buffers->mBuffers[i].mData, 0, buffers->mBuffers[i].mDataByteSize);
  if (self.playing_.load(std::memory_order_relaxed) && !self.pluginLatencyChanged() && self.renderer_ && buffers->mNumberBuffers == 1 &&
      buffers->mBuffers[0].mData && buffers->mBuffers[0].mDataByteSize >= frames * 8) {
    auto *output = static_cast<float *>(buffers->mBuffers[0].mData);
    if (self.plugins_) {
      self.plugins_->applyPending();
      self.plugins_->syncTransport(*self.renderer_);
    }
    if (!self.renderEnded_) {
      self.renderer_->recordingTime(timestamp && (timestamp->mFlags & kAudioTimeStampHostTimeValid) ? timestamp->mHostTime : 0,
        1e9/(self.sampleRate_*self.nanosPerTick_));
      auto received = self.renderer_->render(output, frames);
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
    if (self.plugins_ && !self.plugins_->process(output, frames))
      self.playing_ = false;
    if (self.renderEnded_ && self.plugins_) {
      const auto budget = uint64_t(std::ceil((self.plugins_->tail() + self.plugins_->latency()) * self.sampleRate_));
      if (budget > self.tailBudgetFrames_) self.tailFrames_ += budget - self.tailBudgetFrames_;
      self.tailBudgetFrames_ = std::max(self.tailBudgetFrames_, budget);
      // A control edit can move stored energy into a slower mode even when it
      // remains inside a precomputed range. Allow decay from that edit too.
      if (self.plugins_->tailRevision() != self.tailRevision_) self.tailFrames_ = std::max(self.tailFrames_, budget);
    }
    if (self.plugins_) self.tailRevision_ = self.plugins_->tailRevision();
    if (self.renderEnded_ && !self.tailFrames_)
      self.playing_ = false;
    float left = 0, right = 0;
    for (uint32_t frame = 0; frame < frames; ++frame) {
      left = std::max(left, std::abs(output[frame * 2]));
      right = std::max(right, std::abs(output[frame * 2 + 1]));
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
