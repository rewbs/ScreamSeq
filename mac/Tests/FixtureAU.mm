// Process-local AU fixture. It is never registered system-wide or installed.
// Unlike the VST3 fixture it requires all enabled output pulls to use one
// timestamp and frame count, and rejects duplicate processing of a bus.
#include "../Audio/AudioUnitHost.hpp"
#import <Foundation/Foundation.h>
#include <algorithm>
#include <cstring>
static std::atomic<bool> fixtureAUChannelWeights{false};
static bool fixtureAUPitchMode=false;
void setFixtureAUPitchMode(bool enabled){fixtureAUPitchMode=enabled;}
void setFixtureAUChannelWeights(bool enabled) { fixtureAUChannelWeights.store(enabled); }
namespace {
struct FixtureAU {
  AudioComponentPlugInInterface interface{}; // Must be first for the AU C interface.
  bool instrument = false;
  std::array<uint16_t, 16 * 128> notes{};
  float gain = .5f;
  std::array<uint16_t,16> pitchWheels{};
  std::array<AURenderCallbackStruct, 2> callbacks{};
  std::array<AudioStreamBasicDescription, 2> inputs{};
  std::array<AudioStreamBasicDescription, 32> outputs{};
  double lastTime = -1;
  UInt32 lastFrames = 0;
  uint64_t rendered = 0;
  explicit FixtureAU(bool synth) : instrument(synth) {
    pitchWheels.fill(8192);
    interface.Open = [](void *, AudioComponentInstance) -> OSStatus { return noErr; };
    interface.Close = [](void *self) -> OSStatus { delete static_cast<FixtureAU *>(self); return noErr; };
    interface.Lookup = lookup;
    for (size_t i = 0; i < inputs.size(); ++i) inputs[i].mChannelsPerFrame = i ? 1 : 2;
    for (size_t i = 0; i < outputs.size(); ++i) outputs[i].mChannelsPerFrame = i % 2 ? 1 : 2;
  }
  static OSStatus initialize(void *) { return noErr; }
  static OSStatus info(void *, AudioUnitPropertyID id, AudioUnitScope, AudioUnitElement, UInt32 *size, Boolean *writable) {
    UInt32 bytes = 0;
    switch (id) {
      case kAudioUnitProperty_ElementCount: case kAudioUnitProperty_ParameterList: bytes = sizeof(UInt32); break;
      case kAudioUnitProperty_StreamFormat: bytes = sizeof(AudioStreamBasicDescription); break;
      case kAudioUnitProperty_Latency: case kAudioUnitProperty_TailTime: bytes = sizeof(double); break;
      case kAudioUnitProperty_FactoryPresets: case kAudioUnitProperty_ClassInfo: case kAudioUnitProperty_ElementName: bytes = sizeof(CFTypeRef); break;
      case kAudioUnitProperty_ParameterInfo: bytes = sizeof(AudioUnitParameterInfo); break;
      default: return kAudioUnitErr_InvalidProperty;
    }
    if (size) *size = bytes; if (writable) *writable = id == kAudioUnitProperty_StreamFormat || id == kAudioUnitProperty_ClassInfo;
    return noErr;
  }
  static OSStatus get(void *self, AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement bus, void *out, UInt32 *size) {
    auto &s = *static_cast<FixtureAU *>(self);
    UInt32 required = 0; if (info(self, id, scope, bus, &required, nullptr) || !size || *size < required) return kAudioUnitErr_InvalidProperty;
    *size = required;
    switch (id) {
      case kAudioUnitProperty_ElementCount: *static_cast<UInt32 *>(out) = scope == kAudioUnitScope_Input ? (s.instrument ? 0 : 2) : (s.instrument ? 32 : 1); break;
      case kAudioUnitProperty_StreamFormat:
        if ((scope == kAudioUnitScope_Input && bus >= 2) || bus >= 32) return kAudioUnitErr_InvalidElement;
        *static_cast<AudioStreamBasicDescription *>(out) = scope == kAudioUnitScope_Input ? s.inputs[bus] : s.outputs[bus]; break;
      case kAudioUnitProperty_ElementName:
        *static_cast<CFStringRef *>(out) = CFStringCreateWithFormat(nullptr, nullptr, CFSTR("Fixture bus %u"), unsigned(bus + 1)); break;
      case kAudioUnitProperty_Latency: case kAudioUnitProperty_TailTime: *static_cast<double *>(out) = 0; break;
      case kAudioUnitProperty_ParameterList: *static_cast<AudioUnitParameterID *>(out) = 7; break;
      case kAudioUnitProperty_ParameterInfo: {
        if (bus != 7) return kAudioUnitErr_InvalidParameter;
        auto &p = *static_cast<AudioUnitParameterInfo *>(out); p = {};
        std::strcpy(p.name, "Gain"); p.minValue = 0; p.maxValue = 1; p.defaultValue = .5;
        p.flags = kAudioUnitParameterFlag_IsWritable | kAudioUnitParameterFlag_IsReadable; break;
      }
      case kAudioUnitProperty_FactoryPresets: {
        static const AUPreset presets[]={{2,CFSTR("Quiet")},{7,CFSTR("Medium")},{16,CFSTR("Full")}};
        const void *items[]={&presets[0],&presets[1],&presets[2]};
        *static_cast<CFArrayRef *>(out)=CFArrayCreate(nullptr,items,3,nullptr);break;
      }
      case kAudioUnitProperty_ClassInfo: {
        NSDictionary *value = @{@"gain": @(s.gain)};
        *static_cast<CFPropertyListRef *>(out) = CFBridgingRetain(value); break;
      }
      default: return kAudioUnitErr_InvalidProperty;
    }
    return noErr;
  }
  static OSStatus set(void *self, AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement bus, const void *data, UInt32 size) {
    auto &s = *static_cast<FixtureAU *>(self);
    switch (id) {
      case kAudioUnitProperty_PresentPreset: {
        if(size!=sizeof(AUPreset)) return kAudioUnitErr_InvalidPropertyValue;
        const auto number=static_cast<const AUPreset *>(data)->presetNumber;
        if(number!=2&&number!=7&&number!=16)return kAudioUnitErr_InvalidPropertyValue;
        s.gain=number==2?.25f:number==7?.5f:1.f;return noErr;
      }
      case kAudioUnitProperty_StreamFormat: {
        if (size != sizeof(AudioStreamBasicDescription) || (scope == kAudioUnitScope_Input && bus >= 2) || bus >= 32) return kAudioUnitErr_InvalidPropertyValue;
        auto value = *static_cast<const AudioStreamBasicDescription *>(data);
        if (value.mChannelsPerFrame != (bus % 2 ? 1 : 2) || value.mFormatID != kAudioFormatLinearPCM || value.mBitsPerChannel != 32 || !(value.mFormatFlags & kAudioFormatFlagIsNonInterleaved)) return kAudioUnitErr_FormatNotSupported;
        if (scope == kAudioUnitScope_Input) s.inputs[bus] = value; else s.outputs[bus] = value;
        break;
      }
      case kAudioUnitProperty_SetRenderCallback:
        if (bus >= 2 || size != sizeof(AURenderCallbackStruct)) return kAudioUnitErr_InvalidElement;
        s.callbacks[bus] = *static_cast<const AURenderCallbackStruct *>(data); break;
      case kAudioUnitProperty_ClassInfo: {
        if (size != sizeof(CFPropertyListRef)) return kAudioUnitErr_InvalidPropertyValue;
        NSDictionary *value = (__bridge NSDictionary *)*static_cast<const CFPropertyListRef *>(data);
        if (![value isKindOfClass:NSDictionary.class] || ![value[@"gain"] isKindOfClass:NSNumber.class]) return kAudioUnitErr_InvalidPropertyValue;
        s.gain = [value[@"gain"] floatValue]; break;
      }
      case kAudioUnitProperty_MaximumFramesPerSlice: case kAudioUnitProperty_OfflineRender: case kAudioUnitProperty_HostCallbacks: break;
      default: return kAudioUnitErr_InvalidProperty;
    }
    return noErr;
  }
  static OSStatus getParameter(void *self, AudioUnitParameterID id, AudioUnitScope, AudioUnitElement, Float32 *value) {
    if (id != 7) return kAudioUnitErr_InvalidParameter; *value = static_cast<FixtureAU *>(self)->gain; return noErr;
  }
  static OSStatus setParameter(void *self, AudioUnitParameterID id, AudioUnitScope, AudioUnitElement, Float32 value, UInt32 offset) {
    if (id != 7 || offset) return kAudioUnitErr_InvalidParameter; static_cast<FixtureAU *>(self)->gain = value; return noErr;
  }
  static OSStatus midi(void *self, UInt32 status, UInt32 note, UInt32 velocity, UInt32) {
    auto &s = *static_cast<FixtureAU *>(self);
    if((status&0xf0)==0xe0)s.pitchWheels[status&15]=(note&127)+((velocity&127)<<7);
    auto &count = s.notes[(status & 15) * 128 + (note & 127)];
    if ((status & 0xf0) == 0x90 && velocity) ++count;
    if ((status & 0xf0) == 0x80 || ((status & 0xf0) == 0x90 && !velocity)) { if(count) --count; }
    if ((status & 0xf0) == 0xb0 && (note == 120 || note == 123)) std::fill_n(s.notes.begin() + (status & 15) * 128, 128, 0);
    return noErr;
  }
  static OSStatus render(void *self, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *time, UInt32 bus, UInt32 frames, AudioBufferList *out) {
    auto &s = *static_cast<FixtureAU *>(self);
    if (frames > 4096 || bus >= (s.instrument ? 32u : 1u) || out->mNumberBuffers != s.outputs[bus].mChannelsPerFrame) return kAudioUnitErr_InvalidPropertyValue;
    if (bus == 0) {
      if (time->mSampleTime < s.lastTime + s.lastFrames) return kAudioUnitErr_CannotDoInCurrentContext;
      s.lastTime = time->mSampleTime; s.lastFrames = frames; s.rendered = 1;
    } else {
      if (s.lastTime != time->mSampleTime || s.lastFrames != frames || (s.rendered & (uint64_t(1) << bus))) return kAudioUnitErr_CannotDoInCurrentContext;
      s.rendered |= uint64_t(1) << bus;
    }
    struct Buffers { UInt32 count; AudioBuffer data[2]; } input{2, {{1, frames * 4, nullptr}, {1, frames * 4, nullptr}}}, side{1, {{1, frames * 4, nullptr}, {}}};
    if (!s.instrument) {
      if (!s.callbacks[0].inputProc || !s.callbacks[1].inputProc) return kAudioUnitErr_NoConnection;
      auto error = s.callbacks[0].inputProc(s.callbacks[0].inputProcRefCon, flags, time, 0, frames, reinterpret_cast<AudioBufferList *>(&input));
      if (error) return error;
      error = s.callbacks[1].inputProc(s.callbacks[1].inputProcRefCon, flags, time, 1, frames, reinterpret_cast<AudioBufferList *>(&side));
      if (error) return error;
    }
    bool any = false; float activeChannels = 0;
    for (size_t channel = 0; channel < 16; ++channel) {
      bool active = false; for (size_t note = 0; note < 128; ++note) active |= s.notes[channel * 128 + note] != 0;
      any |= active; if(active) activeChannels += float(channel + 1) / 16;
    }
    const float weight = (fixtureAUChannelWeights.load(std::memory_order_relaxed) ? activeChannels : 1) * (fixtureAUPitchMode ? 1+(int(s.pitchWheels[0])-8192)/8192.f : 1);
    for (UInt32 channel = 0; channel < out->mNumberBuffers; ++channel) {
      auto &dest = out->mBuffers[channel]; if (!dest.mData || dest.mDataByteSize < frames * 4) return kAudioUnitErr_TooManyFramesToProcess;
      for (UInt32 i = 0; i < frames; ++i) {
        float value = s.instrument ? (any ? .2f * s.gain * weight : 0) : static_cast<float *>(input.data[channel].mData)[i] * s.gain * (1 + static_cast<float *>(side.data[0].mData)[i]);
        if (bus) value *= float(bus + 1) * (channel ? -.5f : 1);
        static_cast<float *>(dest.mData)[i] = value;
      }
    }
    return noErr;
  }
  static AudioComponentMethod lookup(SInt16 selector) {
    switch (selector) {
      case kAudioUnitInitializeSelect: case kAudioUnitUninitializeSelect: return reinterpret_cast<AudioComponentMethod>(initialize);
      case kAudioUnitGetPropertyInfoSelect: return reinterpret_cast<AudioComponentMethod>(info);
      case kAudioUnitGetPropertySelect: return reinterpret_cast<AudioComponentMethod>(get);
      case kAudioUnitSetPropertySelect: return reinterpret_cast<AudioComponentMethod>(set);
      case kAudioUnitGetParameterSelect: return reinterpret_cast<AudioComponentMethod>(getParameter);
      case kAudioUnitSetParameterSelect: return reinterpret_cast<AudioComponentMethod>(setParameter);
      case kAudioUnitRenderSelect: return reinterpret_cast<AudioComponentMethod>(render);
      case kMusicDeviceMIDIEventSelect: return reinterpret_cast<AudioComponentMethod>(midi);
      default: return nullptr;
    }
  }
};
}
std::vector<Tracker::PluginDescriptor> registerFixtureAUs() {
  std::vector<Tracker::PluginDescriptor> result;
  for (bool instrument : {false, true}) {
    AudioComponentDescription d{instrument ? kAudioUnitType_MusicDevice : kAudioUnitType_Effect, OSType(instrument ? 'raux' : 'rscf'), 'RsnT', 0, 0};
    auto component = AudioComponentRegister(&d, CFSTR("Resonance: Multi-bus Test"), 1, [](const AudioComponentDescription *d) -> AudioComponentPlugInInterface * {
      return &(new FixtureAU(d->componentType == kAudioUnitType_MusicDevice))->interface;
    });
    if (!component) throw std::runtime_error("Cannot register process-local AU fixture");
    result.push_back({d.componentType, d.componentSubType, d.componentManufacturer, "Resonance Multi-bus Test", "AU", {}, {}, instrument});
  }
  return result;
}
