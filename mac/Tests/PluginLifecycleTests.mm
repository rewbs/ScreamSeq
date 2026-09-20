// A process-local Audio Unit asserts the same main-thread requirement as
// Battery, without requiring a commercial plugin or accessing an audio device.
#include "../Audio/AudioUnitHost.hpp"
#import <AppKit/AppKit.h>
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace Tracker;
namespace {
std::atomic<int> violations{0}, opened{0}, closed{0}, rendered{0}, initialized{0}, restored{0};
std::atomic<bool> failInitialize{false};
void mainThread() {
  if (![NSThread isMainThread])
    ++violations;
}
OSStatus openUnit(void *, AudioComponentInstance) {
  mainThread();
  ++opened;
  return noErr;
}
OSStatus closeUnit(void *self) {
  mainThread();
  ++closed;
  delete static_cast<AudioComponentPlugInInterface *>(self);
  return noErr;
}
OSStatus initialize(void *) {
  mainThread();
  ++initialized;
  return failInitialize ? OSStatus(kAudioUnitErr_FailedInitialization) : OSStatus(noErr);
}
OSStatus uninitialize(void *) {
  mainThread();
  return noErr;
}
OSStatus propertyInfo(void *, AudioUnitPropertyID id, AudioUnitScope, AudioUnitElement, UInt32 *size, Boolean *write) {
  mainThread();
  *write = false;
  if (id != kAudioUnitProperty_ParameterList)
    return kAudioUnitErr_InvalidProperty;
  *size = 0;
  return noErr;
}
OSStatus getProperty(void *, AudioUnitPropertyID id, AudioUnitScope, AudioUnitElement, void *data, UInt32 *size) {
  mainThread();
  if (id == kAudioUnitProperty_ClassInfo && *size >= sizeof(CFPropertyListRef)) {
    *static_cast<CFPropertyListRef *>(data) = (__bridge_retained CFPropertyListRef) @{@"fixture" : @1};
    *size = sizeof(CFPropertyListRef);
  } else if ((id == kAudioUnitProperty_Latency || id == kAudioUnitProperty_TailTime) && *size >= sizeof(double)) {
    *static_cast<double *>(data) = 0;
    *size = sizeof(double);
  } else if (id == kAudioUnitProperty_ElementCount && *size >= sizeof(UInt32)) {
    *static_cast<UInt32 *>(data) = 1; *size = sizeof(UInt32);
  } else if (id == kAudioUnitProperty_StreamFormat && *size >= sizeof(AudioStreamBasicDescription)) {
    auto &format = *static_cast<AudioStreamBasicDescription *>(data);
    format = {}; format.mChannelsPerFrame = 2; *size = sizeof(format);
  } else
    return kAudioUnitErr_InvalidProperty;
  return noErr;
}
OSStatus setProperty(void *, AudioUnitPropertyID id, AudioUnitScope, AudioUnitElement, const void *, UInt32) {
  mainThread();
  if (id == kAudioUnitProperty_ClassInfo)
    ++restored;
  return noErr;
}
OSStatus render(void *, AudioUnitRenderActionFlags *, const AudioTimeStamp *, UInt32, UInt32 frames,
                AudioBufferList *out) {
  if ([NSThread isMainThread])
    ++violations;
  ++rendered;
  for (UInt32 ch = 0; ch < out->mNumberBuffers; ++ch)
    std::fill_n(static_cast<float *>(out->mBuffers[ch].mData), frames, .125f);
  return noErr;
}
AudioComponentMethod lookup(SInt16 selector) {
  switch (selector) {
  case kAudioUnitInitializeSelect:
    return reinterpret_cast<AudioComponentMethod>(initialize);
  case kAudioUnitUninitializeSelect:
    return reinterpret_cast<AudioComponentMethod>(uninitialize);
  case kAudioUnitGetPropertyInfoSelect:
    return reinterpret_cast<AudioComponentMethod>(propertyInfo);
  case kAudioUnitGetPropertySelect:
    return reinterpret_cast<AudioComponentMethod>(getProperty);
  case kAudioUnitSetPropertySelect:
    return reinterpret_cast<AudioComponentMethod>(setProperty);
  case kAudioUnitRenderSelect:
    return reinterpret_cast<AudioComponentMethod>(render);
  default:
    return nullptr;
  }
}
AudioComponentPlugInInterface *factory(const AudioComponentDescription *) {
  mainThread();
  return new AudioComponentPlugInInterface{openUnit, closeUnit, lookup, nullptr};
}
void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
} // namespace
int main() {
  @autoreleasepool {
    NSApplication.sharedApplication.activationPolicy = NSApplicationActivationPolicyProhibited;
    AudioComponentDescription desc{kAudioUnitType_Effect, 'rthr', 'Rsnc', 0, 0};
    if (!AudioComponentRegister(&desc, CFSTR("Resonance thread fixture"), 1, factory))
      return 1;
    std::atomic<bool> done{false};
    std::exception_ptr failure;
    std::thread worker([&] {
      @autoreleasepool {
        try {
          PluginState state{{desc.componentType, desc.componentSubType, desc.componentManufacturer, "Thread fixture"}};
          {
            NativePlugin unit(state, 48000);
            check(unit.parameters().empty(), "Fixture parameter list");
            auto saved = unit.state();
            check(!saved.state.empty(), "Fixture state capture");
            NativePlugin recalled(saved, 48000);
            std::array<float, 512> buffer{};
            check(recalled.process(buffer.data(), 256, 0) && buffer[17] == .125f, "Worker-thread audio render");
          }
          failInitialize = true;
          bool rejected = false;
          try {
            NativePlugin failed(state, 48000);
          } catch (const std::runtime_error &) {
            rejected = true;
          }
          check(rejected, "Initialization failure must reach caller");
          check(opened == 3 && closed == 3 && initialized == 3 && rendered == 1 && restored == 1,
                "Lifecycle counts including failed initialization cleanup");
          check(violations == 0, "AU lifecycle/state/property calls must be on main; render must remain on worker");
        } catch (...) {
          failure = std::current_exception();
        }
      }
      done = true;
    });
    auto deadline = [NSDate dateWithTimeIntervalSinceNow:15];
    while (!done && deadline.timeIntervalSinceNow > 0)
      [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.01]];
    if (!done) {
      std::cerr << "FAIL plugin lifecycle deadlock\n";
      std::_Exit(1);
    }
    worker.join();
    try {
      if (failure)
        std::rethrow_exception(failure);
      std::cout << "PASS AU worker lifecycle: main-thread create/configure/initialize/state/restore/dispose, failure "
                   "cleanup, worker render; no windows or devices\n";
      return 0;
    } catch (const std::exception &e) {
      std::cerr << "FAIL: " << e.what() << '\n';
      return 1;
    }
  }
}
