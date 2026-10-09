#include "SampleRecorder.hpp"
#include "CaptureEndMonitor.hpp"
#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <thread>

namespace Tracker {
static_assert(std::atomic<uint32_t>::is_always_lock_free && std::atomic<uint64_t>::is_always_lock_free &&
              std::atomic<float>::is_always_lock_free && std::atomic<int32_t>::is_always_lock_free);

void SampleCaptureBuffer::prepare(uint32_t rate, uint32_t inputs, uint32_t first, uint32_t channels, double seconds) {
  if(reading().capturing) throw std::logic_error("Stop sample capture before preparing another take");
  if(rate < 8000 || rate > 384000 || inputs == 0 || inputs > maximumInputChannels ||
     channels < 1 || channels > 2 || first >= inputs || channels > inputs - first ||
     !std::isfinite(seconds) || seconds <= 0 || seconds > 300)
    throw std::invalid_argument("Invalid sample recording rate, channels or duration");
  const auto capacity = uint32_t(std::min(double(maximumFrames), std::max(1., std::floor(seconds * rate))));
  // Value initialization touches the entire bounded allocation before input is
  // started. No lazy grow, circular overwrite or allocation is needed in ingest.
  std::vector<float> storage(size_t(capacity) * channels, 0);
  storage_ = std::move(storage);
  sampleRate_ = rate; inputChannels_ = inputs; firstChannel_ = first; channels_ = channels; capacity_ = capacity;
  frames_.store(0, std::memory_order_relaxed); peak_.store(0, std::memory_order_relaxed);
  clipped_.store(0, std::memory_order_relaxed); systemError_.store(0, std::memory_order_relaxed);
  end_.store(uint32_t(End::None), std::memory_order_release);
}

void SampleCaptureBuffer::finish(End reason, int32_t status) noexcept {
  if(reason == End::None) return;
  uint32_t expected = uint32_t(End::None);
  if(end_.compare_exchange_strong(expected, uint32_t(reason), std::memory_order_acq_rel))
    systemError_.store(status, std::memory_order_release);
}

void SampleCaptureBuffer::ingest(const float *input, uint32_t count) noexcept {
  if(end_.load(std::memory_order_acquire) != uint32_t(End::None) || !count) return;
  if(count > maximumBlockFrames) { finish(End::OversizedBlock); return; }
  if(!input) { finish(End::InvalidInput); return; }
  const auto offset = frames_.load(std::memory_order_relaxed);
  const auto copy = std::min(count, capacity_ - offset);
  // Reject the entire malformed block rather than retaining a partly written
  // block. Other unselected hardware inputs do not contaminate the take.
  for(uint32_t f = 0; f < copy; ++f)
    for(uint32_t c = 0; c < channels_; ++c)
      if(!std::isfinite(input[size_t(f) * inputChannels_ + firstChannel_ + c])) {
        finish(End::InvalidInput); return;
      }
  float peak = 0; uint64_t clipped = 0;
  for(uint32_t f = 0; f < copy; ++f) {
    for(uint32_t c = 0; c < channels_; ++c) {
      const float value = input[size_t(f) * inputChannels_ + firstChannel_ + c];
      storage_[size_t(offset + f) * channels_ + c] = value;
      peak = std::max(peak, std::abs(value)); clipped += std::abs(value) > 1.f;
    }
  }
  peak_.store(peak, std::memory_order_relaxed);
  clipped_.fetch_add(clipped, std::memory_order_relaxed);
  frames_.store(offset + copy, std::memory_order_release);
  if(offset + copy == capacity_) finish(End::Limit);
}

SampleCaptureBuffer::Reading SampleCaptureBuffer::reading() const noexcept {
  Reading result;
  result.end = End(end_.load(std::memory_order_acquire)); result.capturing = result.end == End::None;
  result.frames = frames_.load(std::memory_order_acquire); result.sampleRate = sampleRate_;
  result.channels = channels_; result.capacityFrames = capacity_;
  result.peak = peak_.load(std::memory_order_relaxed); result.clipped = clipped_.load(std::memory_order_relaxed);
  result.systemError = systemError_.load(std::memory_order_acquire);
  return result;
}

std::span<const float> SampleCaptureBuffer::pcm() const {
  if(reading().capturing) throw std::logic_error("Stop recording before accessing its audio");
  return {storage_.data(), size_t(frames_.load(std::memory_order_acquire)) * channels_};
}

bool sampleRecordingOwnsDocument(const std::string &captured, const std::string &current) noexcept {
  return !captured.empty() && captured == current;
}

namespace {
void captureCheck(OSStatus status, const char *message) {
  if(status) throw std::runtime_error(std::string(message) + " (" + std::to_string(status) + ")");
}
template<typename T> T deviceProperty(AudioObjectID object, AudioObjectPropertySelector selector,
                                    AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal) {
  T result{}; UInt32 bytes = sizeof(result);
  AudioObjectPropertyAddress address{selector, scope, kAudioObjectPropertyElementMain};
  captureCheck(AudioObjectGetPropertyData(object, &address, 0, nullptr, &bytes, &result), "Cannot read input device property");
  return result;
}
std::string deviceString(AudioObjectID object, AudioObjectPropertySelector selector) {
  CFStringRef text = deviceProperty<CFStringRef>(object, selector);
  if(!text) return {};
  const char *utf8 = [(__bridge NSString *)text UTF8String];
  std::string result = utf8 ? utf8 : "";
  CFRelease(text); return result;
}
uint32_t inputChannelCount(AudioDeviceID device) {
  AudioObjectPropertyAddress address{kAudioDevicePropertyStreamConfiguration, kAudioObjectPropertyScopeInput,
                                     kAudioObjectPropertyElementMain};
  UInt32 bytes = 0;
  if(AudioObjectGetPropertyDataSize(device, &address, 0, nullptr, &bytes) || bytes < sizeof(AudioBufferList)) return 0;
  std::vector<std::byte> storage(bytes);
  auto *list = reinterpret_cast<AudioBufferList *>(storage.data());
  if(AudioObjectGetPropertyData(device, &address, 0, nullptr, &bytes, list)) return 0;
  if(bytes < offsetof(AudioBufferList, mBuffers) ||
     list->mNumberBuffers > (bytes - offsetof(AudioBufferList, mBuffers)) / sizeof(AudioBuffer)) return 0;
  uint64_t channels = 0;
  for(UInt32 i = 0; i < list->mNumberBuffers; ++i) channels += list->mBuffers[i].mNumberChannels;
  return channels <= SampleCaptureBuffer::maximumInputChannels ? uint32_t(channels) : 0;
}
struct ResolvedInput { AudioDeviceID id; SampleRecorder::Device info; };
std::vector<ResolvedInput> inputDevices() {
  AudioDeviceID defaultInput = 0;
  try { defaultInput = deviceProperty<AudioDeviceID>(kAudioObjectSystemObject, kAudioHardwarePropertyDefaultInputDevice); } catch(...) {}
  AudioObjectPropertyAddress address{kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
                                     kAudioObjectPropertyElementMain};
  UInt32 bytes = 0;
  captureCheck(AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, nullptr, &bytes), "Cannot enumerate input devices");
  std::vector<AudioDeviceID> ids(bytes / sizeof(AudioDeviceID));
  captureCheck(AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &bytes, ids.data()), "Cannot read input devices");
  std::vector<ResolvedInput> result;
  for(const auto id : ids) {
    try {
      const auto channels = inputChannelCount(id);
      if(!channels || channels > SampleCaptureBuffer::maximumInputChannels ||
         !deviceProperty<UInt32>(id, kAudioDevicePropertyDeviceIsAlive)) continue;
      auto uid = deviceString(id, kAudioDevicePropertyDeviceUID);
      if(uid.empty()) continue;
      result.push_back({id, {std::move(uid), deviceString(id, kAudioObjectPropertyName), channels, id == defaultInput}});
    } catch(...) { /* A device may disappear during enumeration. */ }
  }
  return result;
}
}

struct SampleRecorder::Impl {
  SampleCaptureBuffer capture;
  AudioUnit unit = nullptr;
  AudioDeviceID runtimeDevice = 0;
  Device selected;
  uint32_t first = 0;
  std::vector<float> input;
  AudioBufferList inputList{};
  CaptureEndMonitor monitor;
  bool started = false;
  size_t listenerCount = 0;
  static constexpr std::array<AudioObjectPropertyAddress, 3> properties{{
    {kAudioDevicePropertyDeviceIsAlive, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain},
    {kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain},
    {kAudioDevicePropertyStreamConfiguration, kAudioObjectPropertyScopeInput, kAudioObjectPropertyElementMain}
  }};
  static OSStatus changed(AudioObjectID, UInt32, const AudioObjectPropertyAddress *, void *context) {
    static_cast<Impl *>(context)->capture.finish(SampleCaptureBuffer::End::DeviceChanged);
    return noErr;
  }
  static OSStatus callback(void *context, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *time,
                           UInt32, UInt32 frames, AudioBufferList *) noexcept {
    auto &self = *static_cast<Impl *>(context);
    if(!self.capture.reading().capturing) return noErr;
    if(!frames) return noErr;
    if(frames > SampleCaptureBuffer::maximumBlockFrames) {
      self.capture.finish(SampleCaptureBuffer::End::OversizedBlock); return noErr;
    }
    self.inputList.mBuffers[0].mData = self.input.data();
    self.inputList.mBuffers[0].mDataByteSize = frames * self.selected.channels * sizeof(float);
    const auto status = AudioUnitRender(self.unit, flags, time, 1, frames, &self.inputList);
    if(status) self.capture.finish(SampleCaptureBuffer::End::RenderError, status);
    else if(self.inputList.mNumberBuffers != 1 || self.inputList.mBuffers[0].mNumberChannels != self.selected.channels ||
            self.inputList.mBuffers[0].mDataByteSize < frames * self.selected.channels * sizeof(float))
      self.capture.finish(SampleCaptureBuffer::End::InvalidInput);
    else self.capture.ingest(static_cast<const float *>(self.inputList.mBuffers[0].mData), frames);
    return noErr;
  }
  void close() noexcept {
    capture.finish(SampleCaptureBuffer::End::Stopped);
    // Only this off-callback owner disposes. Joining prevents the autonomous
    // limit/disconnect stop from racing AudioUnitUninitialize/Dispose.
    monitor.stop();
    if(unit && started) AudioOutputUnitStop(unit);
    started = false;
    for(size_t i = 0; i < listenerCount; ++i)
      AudioObjectRemovePropertyListener(runtimeDevice, &properties[i], changed, this);
    listenerCount = 0;
    if(unit) { AudioUnitUninitialize(unit); AudioComponentInstanceDispose(unit); unit = nullptr; }
  }
  ~Impl() { close(); }
};

SampleRecorder::SampleRecorder() : impl_(std::make_unique<Impl>()) {}
SampleRecorder::~SampleRecorder() = default;
const SampleRecorder::Device &SampleRecorder::device() const noexcept { return impl_->selected; }
uint32_t SampleRecorder::firstChannel() const noexcept { return impl_->first; }
std::vector<SampleRecorder::Device> SampleRecorder::devices() {
  std::vector<Device> result; for(auto &device : inputDevices()) result.push_back(std::move(device.info)); return result;
}
const char *SampleRecorder::permission() {
  switch([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]) {
    case AVAuthorizationStatusAuthorized: return "authorized";
    case AVAuthorizationStatusDenied: return "denied";
    case AVAuthorizationStatusRestricted: return "restricted";
    case AVAuthorizationStatusNotDetermined: return "notDetermined";
  }
  return "restricted";
}
const char *SampleRecorder::endMessage(SampleCaptureBuffer::End reason) {
  switch(reason) {
    case SampleCaptureBuffer::End::DeviceChanged: return "Input device disconnected or changed format. Captured audio has been retained.";
    case SampleCaptureBuffer::End::RenderError: return "The input device stopped delivering audio. Captured audio has been retained.";
    case SampleCaptureBuffer::End::OversizedBlock: return "The input device exceeded the prepared buffer size. Captured audio has been retained.";
    case SampleCaptureBuffer::End::InvalidInput: return "The input device returned invalid audio. Captured audio has been retained.";
    default: return "";
  }
}
void SampleRecorder::start(const Options &options) {
  if(impl_->capture.reading().capturing || impl_->capture.reading().capacityFrames)
    throw std::logic_error("Discard or commit the existing sample take before recording again");
  if(std::string(permission()) != "authorized")
    throw std::runtime_error("Microphone access is required. Use Record to grant access, or enable it in System Settings > Privacy & Security > Microphone.");
  const auto devices = inputDevices();
  const auto found = std::find_if(devices.begin(), devices.end(), [&](const auto &d) {
    return options.device.empty() ? d.info.isDefault : d.info.id == options.device;
  });
  if(found == devices.end()) throw std::invalid_argument("The selected input device is unavailable");
  auto &self = *impl_;
  self.runtimeDevice = found->id; self.selected = found->info; self.first = options.firstChannel;
  const auto rate = deviceProperty<Float64>(self.runtimeDevice, kAudioDevicePropertyNominalSampleRate);
  if(!std::isfinite(rate) || rate < 8000 || rate > 384000 || std::abs(rate - std::round(rate)) > .001)
    throw std::invalid_argument("Input device sample rate is unsupported; use an integer rate from 8000 to 384000 Hz");
  self.capture.prepare(uint32_t(std::round(rate)), self.selected.channels, self.first, options.channels, options.maxSeconds);
  try {
    AudioComponentDescription description{kAudioUnitType_Output, kAudioUnitSubType_HALOutput, kAudioUnitManufacturer_Apple, 0, 0};
    auto component = AudioComponentFindNext(nullptr, &description);
    if(!component) throw std::runtime_error("Core Audio input is unavailable");
    captureCheck(AudioComponentInstanceNew(component, &self.unit), "Cannot create Core Audio input");
    UInt32 enabled = 1, disabled = 0;
    captureCheck(AudioUnitSetProperty(self.unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &enabled, sizeof(enabled)), "Cannot enable audio input");
    captureCheck(AudioUnitSetProperty(self.unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &disabled, sizeof(disabled)), "Cannot disable recording-device output");
    captureCheck(AudioUnitSetProperty(self.unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &self.runtimeDevice, sizeof(self.runtimeDevice)), "Cannot select input device");
    AudioStreamBasicDescription format{};
    format.mSampleRate = rate; format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    format.mBytesPerPacket = format.mBytesPerFrame = self.selected.channels * sizeof(float);
    format.mFramesPerPacket = 1; format.mChannelsPerFrame = self.selected.channels; format.mBitsPerChannel = 32;
    captureCheck(AudioUnitSetProperty(self.unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &format, sizeof(format)), "Cannot set input PCM format");
    UInt32 capacity = SampleCaptureBuffer::maximumBlockFrames;
    captureCheck(AudioUnitSetProperty(self.unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &capacity, sizeof(capacity)), "Cannot prepare bounded input buffers");
    captureCheck(AudioUnitSetProperty(self.unit, kAudioUnitProperty_ShouldAllocateBuffer, kAudioUnitScope_Output, 1, &disabled, sizeof(disabled)), "Cannot select preallocated input storage");
    self.input.assign(size_t(capacity) * self.selected.channels, 0);
    self.inputList.mNumberBuffers = 1;
    self.inputList.mBuffers[0] = {self.selected.channels, UInt32(self.input.size() * sizeof(float)), self.input.data()};
    AURenderCallbackStruct callback{Impl::callback, &self};
    captureCheck(AudioUnitSetProperty(self.unit, kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Global, 0, &callback, sizeof(callback)), "Cannot install input callback");
    captureCheck(AudioUnitInitialize(self.unit), "Cannot initialize audio input");
    for(const auto &property : Impl::properties) {
      captureCheck(AudioObjectAddPropertyListener(self.runtimeDevice, &property, Impl::changed, &self), "Cannot observe input-device changes");
      ++self.listenerCount;
    }
    captureCheck(AudioOutputUnitStart(self.unit), "Cannot start audio input"); self.started = true;
    // Stop the actual microphone promptly at the limit/disconnection even if
    // no UI/API client polls. This thread never reads or frees PCM storage.
    self.monitor.start([&self] { return self.capture.reading().capturing; },
                       [&self] { AudioOutputUnitStop(self.unit); });
  } catch(...) { self.close(); throw; }
}
void SampleRecorder::stop() noexcept { impl_->close(); }
SampleCaptureBuffer::Reading SampleRecorder::reading() {
  const auto state = impl_->capture.reading();
  if(!state.capturing && impl_->unit) impl_->close();
  return impl_->capture.reading();
}
std::span<const float> SampleRecorder::pcm() const {
  if(impl_->unit) throw std::logic_error("Stop the input device before accessing its take");
  return impl_->capture.pcm();
}
} // namespace Tracker
