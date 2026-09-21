#include "../Audio/AudioDevice.hpp"
#include "soundlib/ModInstrument.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <thread>

namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
void check(OSStatus value, const char *message) {
  if (value)
    throw std::runtime_error(std::string(message) + " (" + std::to_string(value) + ")");
}
template <typename T>
T property(AudioDeviceID device, AudioObjectPropertySelector selector,
           AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal) {
  T value{};
  UInt32 size = sizeof(value);
  AudioObjectPropertyAddress address{selector, scope, kAudioObjectPropertyElementMain};
  check(AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &value), "Read loopback device property");
  return value;
}
struct Capture {
  AudioDeviceID device;
  AudioDeviceIOProcID proc = nullptr;
  std::vector<float> samples;
  size_t count = 0;
  bool invalid = false;
  uint64_t discontinuities = 0;
  double nextSampleTime = -1;
  explicit Capture(AudioDeviceID id, double rate, double seconds=10) : device(id), samples(size_t(rate * seconds) * 2) {
    const auto format =
        property<AudioStreamBasicDescription>(id, kAudioDevicePropertyStreamFormat, kAudioObjectPropertyScopeInput);
    require(format.mFormatID == kAudioFormatLinearPCM && (format.mFormatFlags & kAudioFormatFlagIsFloat) &&
                !(format.mFormatFlags & kAudioFormatFlagIsNonInterleaved) && format.mBitsPerChannel == 32 &&
                format.mChannelsPerFrame == 2 && format.mSampleRate == rate,
            "Loopback requires interleaved stereo float32 input at the output device rate");
    check(AudioDeviceCreateIOProcID(device, input, this, &proc), "Create virtual-device capture");
  }
  ~Capture() {
    if (proc) {
      AudioDeviceStop(device, proc);
      AudioDeviceDestroyIOProcID(device, proc);
    }
  }
  void start() { check(AudioDeviceStart(device, proc), "Start virtual-device capture"); }
  void stop() { check(AudioDeviceStop(device, proc), "Stop virtual-device capture"); }
  // This callback owns the preallocated capture buffer until AudioDeviceStop.
  // It records the device's PCM, without allocation, logging, or locks.
  static OSStatus input(AudioObjectID, const AudioTimeStamp *, const AudioBufferList *buffers,
                        const AudioTimeStamp *timestamp, AudioBufferList *, const AudioTimeStamp *, void *context) {
    auto &self = *static_cast<Capture *>(context);
    if (!buffers || buffers->mNumberBuffers != 1 || !buffers->mBuffers[0].mData ||
        buffers->mBuffers[0].mNumberChannels != 2 || buffers->mBuffers[0].mDataByteSize % 8) {
      self.invalid = true;
      return noErr;
    }
    const size_t count = buffers->mBuffers[0].mDataByteSize / sizeof(float);
    if (timestamp->mFlags & kAudioTimeStampSampleTimeValid) {
      if (self.nextSampleTime >= 0 && timestamp->mSampleTime != self.nextSampleTime)
        ++self.discontinuities;
      self.nextSampleTime = timestamp->mSampleTime + count / 2;
    }
    if (self.count + count > self.samples.size()) {
      self.invalid = true;
      return noErr;
    }
    std::memcpy(self.samples.data() + self.count, buffers->mBuffers[0].mData, count * sizeof(float));
    self.count += count;
    return noErr;
  }
};
size_t onset(const std::vector<float> &samples, size_t count) {
  for (size_t n = 0; n + 1 < count; n += 2)
    if (std::abs(samples[n]) > 1e-6f || std::abs(samples[n + 1]) > 1e-6f)
      return n / 2;
  throw std::runtime_error("No signal received through the virtual loopback device");
}
} // namespace

int main(int argc, char **argv) {
  try {
    // Explicitly select a named virtual device. Never record the default input,
    // microphone, or speakers, and never change the system's default route.
    require(argc >= 2 && argc <= 4, "usage: loopback-tests 'BlackHole 2ch' [VST3 fixture] [graph soak seconds]");
    const int graphSeconds=argc==4?std::stoi(argv[3]):2;
    require(graphSeconds>=2&&graphSeconds<=300,"Graph capture duration must be 2..300 seconds");
    const std::string name = argv[1];
    require(name == "BlackHole 2ch", "Only the explicitly supported BlackHole 2ch virtual route is accepted");
    AudioDeviceID id = 0;
    for (const auto &device : Tracker::AudioDevice::devices())
      if (device.name == name)
        id = device.id;
    require(id != 0, "Requested virtual loopback device is not installed");
    const auto priorFrames = property<UInt32>(id, kAudioDevicePropertyBufferFrameSize);
    // Preserve this device's existing buffer configuration as well as the
    // system default. The test runs at the device's existing sample rate.
    Tracker::AudioDevice output;
    output.configure(id, priorFrames);
    const auto rate = uint32_t(output.sampleRate());
    const auto frames = output.bufferSize();
    auto document = Tracker::Document::demo();
    if (argc >= 3) {
      document->transaction([&](Tracker::CSoundFile &song){
        song.m_nInstruments = 4;
        for (int i = 1; i <= 4; ++i)song.Instruments[i] = new OpenMPT::ModInstrument(OpenMPT::SAMPLEINDEX(i));
        if(argc==4)song.Order().assign(64,0);
      });
    }
    const auto bytes = document->serialize();
    for (int test = 0; test < (argc >= 3 ? 6 : 2); ++test) {
      std::vector<Tracker::PluginState> effects;
      std::optional<Tracker::NativeSong> native;
      if (test == 1)
        effects.push_back(
            {{kAudioUnitType_Effect, kAudioUnitSubType_LowPassFilter, kAudioUnitManufacturer_Apple, "Apple AULowpass"},
             {},
             false});
      std::vector<Tracker::ParameterChange> automation;
      if (test == 2 || test == 3) {
        auto available = Tracker::NativePlugin::discoverVST3(argv[2]);
        Tracker::PluginState plugin{available[test == 2 ? 0 : 1]};
        if (test == 3)
          plugin.instrument = 1;
        effects.push_back(plugin);
        automation = {{0, 7, .25f, 1237}, {0, 7, .7f, 17821}, {0, 7, .5f, 33007}};
      }
      if (test == 4) {
        Tracker::PluginState plugin{
            {kAudioUnitType_MusicDevice, kAudioUnitSubType_DLSSynth, kAudioUnitManufacturer_Apple, "Apple DLS"}};
        plugin.instrument = 1;
        effects.push_back(plugin);
      }
      if(test==5){
        native=document->native();auto &n=*native;const auto master=n.makeEntity().id;
        for(const auto &[channel,track]:n.tracks)n.mixer.buses.push_back({track.id,master,Tracker::MixerBusKind::Track,"Track"});
        n.mixer.buses.push_back({master,0,Tracker::MixerBusKind::Master,"Master"});
        auto descriptor=Tracker::NativePlugin::discoverVST3(argv[2]).at(0);
        Tracker::SignalDefinition d;d.id=n.makeEntity().id;d.number=1;d.name="Live graph fixture";
        const auto in=n.makeEntity().id,plugin=n.makeEntity().id,out=n.makeEntity().id,amount=n.makeEntity().id;
        d.nodes={{in,Tracker::SignalNodeKind::Input,"Input"},{plugin,Tracker::SignalNodeKind::Plugin,"Gain"},{out,Tracker::SignalNodeKind::Output,"Output"},{amount,Tracker::SignalNodeKind::Amount,"Amount"}};
        d.nodes[1].plugin={descriptor.format,descriptor.name,descriptor.path,descriptor.classID};d.audio={{in,plugin},{plugin,out}};d.modulation={{amount,plugin,7}};
        const auto drawn=n.makeEntity().id;Tracker::SignalNode envelope;envelope.id=drawn;envelope.kind=Tracker::SignalNodeKind::Automation;envelope.name="Drawn motion";envelope.envelopes={{n.patterns.at(0).id,true,{{0,.1,Tracker::AutomationCurve::Smooth},{8192,.9,Tracker::AutomationCurve::Linear}}}};d.nodes.push_back(envelope);d.modulation[0].maximum=.8;d.modulation.push_back({drawn,plugin,7,0,.1,0});
        n.signal.library={d};n.signal.assignments={{master,d.id,.8,1}};n.signal.instrumentAssignments={{n.instruments.at(1).id,d.id,.9,1}};
        const auto track=n.tracks.at(0).id,pattern=n.patterns.at(0).id;n.signal.lanes[track]=1;
        n.signal.commands={{pattern,track,d.id,0,0,Tracker::SignalCommandKind::Start,.4,1},{pattern,track,d.id,4*65536+32768,0,Tracker::SignalCommandKind::Amount,.7,1},{pattern,track,d.id,8*65536,0,Tracker::SignalCommandKind::Stop},{pattern,track,d.id,12*65536,0,Tracker::SignalCommandKind::Row,.2,1}};
        n.validate(document->song());
      }
      output.setPlugins(effects, automation);
      Tracker::PluginChain chain(effects, rate, false, automation);
      Tracker::Renderer reference(bytes, rate);
      chain.attachInstruments(reference,native ? &*native : nullptr);
      if(native)chain.attachMusicalAutomation(reference,*native);
      const size_t compareFrames = size_t(rate) * (test==5?graphSeconds:2);
      std::vector<float> expected(((compareFrames + frames - 1) / frames) * frames * 2);
      for (size_t n = 0; n < expected.size() / 2; n += frames) {
        chain.syncTransport(reference);
        require(reference.render(expected.data() + n * 2, frames) == frames, "Reference unexpectedly ended");
        require(chain.process(expected.data() + n * 2, frames), "Reference AU failed");
      }
      Capture capture(id, rate,test==5?graphSeconds+2:10);
      capture.start();
      std::this_thread::sleep_for(std::chrono::milliseconds(150));
      output.play(bytes,0,false,{},0,native ? &*native : nullptr);
      std::this_thread::sleep_for(std::chrono::milliseconds(test==5?graphSeconds*1000+500:2500));
      output.stop();
      std::this_thread::sleep_for(std::chrono::milliseconds(150));
      capture.stop();
      require(!capture.invalid, "Invalid or overflowing virtual-device input");
      require(!capture.discontinuities, "Virtual-device capture timestamps were discontinuous");
      const auto receivedOnset = onset(capture.samples, capture.count);
      const auto expectedOnset = onset(expected, expected.size());
      require(receivedOnset >= expectedOnset, "Captured signal began before reference alignment");
      const auto offset = receivedOnset - expectedOnset;
      require((offset + compareFrames) * 2 <= capture.count, "Captured signal was truncated");
      double squaredError = 0;
      float maximumError = 0;
      for (size_t n = 0; n < compareFrames * 2; ++n) {
        const auto actual = capture.samples[offset * 2 + n];
        require(std::isfinite(actual), "Captured non-finite PCM");
        const auto error = std::abs(actual - expected[n]);
        maximumError = std::max(maximumError, error);
        squaredError += double(error) * error;
      }
      const auto telemetry = output.telemetry();
      std::cout << std::array<const char *, 6>{"Dry", "AU effect", "VST3 effect + automation",
                                               "VST3 instrument + automation", "AU instrument", "Graph copies + fractional pattern commands"}[test]
                << " virtual loopback: " << compareFrames << " stereo frames, " << rate << " Hz / " << frames
                << " frames, max error " << maximumError << ", RMS error "
                << std::sqrt(squaredError / (compareFrames * 2)) << ", alignment " << offset
                << " capture frames, callback overruns " << telemetry.overruns << '\n';
      require(maximumError < 2e-6f, "Virtual-device PCM differs from the reference or contains a dropout");
      require(!telemetry.overruns && !output.pluginFailed(), "Live output failed during loopback");
    }
    std::cout
        << "PASS actual Core Audio output/capture continuity and PCM fidelity, all requested plugin configurations\n";
    std::cout << "Virtual routing does not measure physical DAC latency or speaker sound.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
