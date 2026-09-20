#include "../Audio/AudioDevice.hpp"
#include "../Audio/AudioExport.hpp"
#include "../Audio/AudioUnitHost.hpp"
#import "../Bridge/TrackerSession.h"
#include "editor/TrackerDocument.hpp"
#include "soundlib/ModInstrument.h"
#import <AppKit/AppKit.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace Tracker;
using namespace OpenMPT;
#ifdef TRACKER_SANITIZER
static void tracker_audit_begin() {}
static void tracker_audit_end(uint64_t *a, uint64_t *f, uint64_t *l) {
  *a = *f = *l = 0;
}
static constexpr const char *auditSummary = "allocation auditing disabled for this sanitizer run";
#else
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *, uint64_t *, uint64_t *);
static constexpr const char *auditSummary = "zero callback allocations/frees/locks";
#endif
static void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
static std::vector<std::byte> module() {
  auto document = Document::demo();
  auto &song = document->song();
  for (auto &pattern : song.Patterns)
    if (pattern.IsValid())
      for (auto &cell : pattern)
        cell = {};
  song.Order().assign(1, 0);
  song.m_nInstruments = 1;
  song.Instruments[1] = new ModInstrument(0);
  song.Instruments[1]->name = "Native synth";
  auto &on = *song.Patterns[0].GetpModCommand(0, 0);
  on.note = 61;
  on.instr = 1;
  auto &off = *song.Patterns[0].GetpModCommand(2, 0);
  off.note = NOTE_KEYOFF;
  return document->serialize();
}
static NSDictionary *dictionary(const PluginDescriptor &d) {
  return @{
    @"type" : @(d.type),
    @"subtype" : @(d.subtype),
    @"manufacturer" : @(d.manufacturer),
    @"name" : @(d.name.c_str()),
    @"format" : @(d.format.c_str()),
    @"path" : @(d.path.c_str()),
    @"classID" : @(d.classID.c_str()),
    @"isInstrument" : @(d.instrument)
  };
}
int main(int argc, char **argv) {
  @autoreleasepool {
    try {
      check(argc >= 2, "fixture bundle argument");
      auto descriptors = NativePlugin::discoverVST3(argv[1]);
      check(descriptors.size() == 4, "VST3 effect and instrument discovery");
      PluginState gain{descriptors[0]}, synth{descriptors[1]};
      gain.instanceID = "test-gain-instance";
      synth.instrument = 1;
      NativePlugin effect(gain, 48000);
      check(effect.parameter(7, .3f), "VST3 parameter write");
      auto saved = effect.state();
      check(saved.instanceID == gain.instanceID, "VST3 host state retains plugin instance identity");
      NativePlugin recalled(saved, 48000);
      check(std::abs(recalled.parameters()[0].value - .3f) < 1e-6, "VST3 processor/controller state recall");
      std::array<float, 1024> buffer;
      buffer.fill(.2f);
      check(recalled.process(buffer.data(), 512, 0), "VST3 process");
      check(std::abs(buffer[3] - .06) < 1e-6, "VST3 effect output");
      std::vector<ParameterChange> automation{{0, 7, .1f, 37}, {0, 7, .8f, 113}, {0, 7, .25f, 519}};
      for (auto rate : {44100, 48000, 96000}) {
        auto render = [&](uint32_t block) {
          PluginChain chain({gain}, rate, true, automation);
          std::vector<float> out(16384, .2f);
          for (uint32_t pos = 0; pos < 8192; pos += block) {
            auto frames = std::min(block, 8192 - pos);
            uint64_t a, f, l;
            tracker_audit_begin();
            auto ok = chain.process(out.data() + pos * 2, frames);
            tracker_audit_end(&a, &f, &l);
            check(ok, "VST3 automation render");
            check(a + f + l == 0, "VST3 realtime allocation or lock");
          }
          return out;
        };
        check(render(128) == render(512), "VST3 automation independent of block size");
      }
      std::cout << "PASS VST3 discovery, gain, processor/controller state, sample-timed automation at 44.1/48/96k, "
                   ""
                << auditSummary << "\n";
      {
        NativePlugin unit(synth, 48000);
        buffer.fill(0);
        check(unit.midi(0x90, 60, 100) && unit.process(buffer.data(), 128, 0), "VST3 instrument note-on");
        check(buffer[0] > .09, "VST3 instrument audible");
        buffer.fill(0);
        check(unit.midi(0x80, 60, 0) && unit.process(buffer.data(), 128, 128), "VST3 instrument note-off");
        check(buffer[0] == 0, "VST3 note-off silence");
        unit.midi(0x90, 60, 100);
        unit.midi(0x90, 60, 100);
        unit.process(buffer.data(), 128, 256);
        unit.midi(0x80, 60, 0);
        unit.process(buffer.data(), 128, 384);
        check(buffer[0] > .09, "overlapping same-pitch note remains active");
        unit.midi(0xb0, 123, 0);
        unit.process(buffer.data(), 128, 512);
        check(buffer[0] == 0, "all-notes-off releases every note instance");
      }
      auto bytes = module();
      auto renderSong = [&](uint32_t block, bool offline) {
        PluginChain chain({synth}, 48000, offline, automation);
        Renderer renderer(bytes, 48000);
        chain.attachInstruments(renderer);
        std::vector<float> out(48000 * 2);
        for (uint32_t pos = 0; pos < 48000; pos += block) {
          auto frames = std::min(block, 48000 - pos);
          uint64_t a, f, l;
          tracker_audit_begin();
          chain.applyPending();
          renderer.render(out.data() + pos * 2, frames);
          auto ok = chain.process(out.data() + pos * 2, frames);
          tracker_audit_end(&a, &f, &l);
          check(ok, "tracker instrument render");
          check(a + f + l == 0, "tracker instrument callback allocation or lock");
        }
        return out;
      };
      auto live = renderSong(128, false), offline = renderSong(512, true);
      check(live == offline, "tracker notes plus automation live/offline parity");
      double first = 0, last = 0;
      for (int i = 0; i < 1000; ++i)
        first += std::abs(live[i]);
      for (size_t i = live.size() - 1000; i < live.size(); ++i)
        last += std::abs(live[i]);
      check(first > 1 && last == 0, "tracker note-on and row note-off reach instrument");
      std::cout << "PASS VST3 tracker instrument notes, row note-off, automation, live/offline parity, " << auditSummary
                << "\n";
      {
        PluginChain chain({synth}, 48000);
        Renderer renderer(bytes, 48000);
        chain.attachInstruments(renderer);
        renderer.render(buffer.data(), 128);
        chain.process(buffer.data(), 128);
        check(buffer[0] > .01, "instrument before mute");
        renderer.mute(0, true);
        renderer.render(buffer.data(), 128);
        chain.process(buffer.data(), 128);
        check(buffer[0] == 0, "muting releases this track's instrument notes");
      }
      {
        PluginChain chain({synth}, 48000);
        Renderer preview(bytes, 48000, 0, true);
        chain.attachInstruments(preview);
        check(preview.preview({61, 1, 100, true}), "enqueue preview on");
        preview.render(buffer.data(), 128);
        chain.process(buffer.data(), 128);
        check(buffer[0] > .01, "instrument keyboard preview on");
        check(preview.preview({61, 1, 0, false}), "enqueue preview off");
        preview.render(buffer.data(), 128);
        chain.process(buffer.data(), 128);
        check(buffer[0] == 0, "instrument keyboard preview off");
      }
      {
        PluginState delayed{descriptors[2]};
        delayed.instrument = 1;
        auto render = [&](PluginState state) {
          PluginChain chain({state}, 48000, true);
          Renderer renderer(bytes, 48000);
          chain.attachInstruments(renderer);
          std::vector<float> audio(8192 * 2);
          for (int n = 0; n < 8192; n += 128) {
            renderer.render(audio.data() + n * 2, 128);
            check(chain.process(audio.data() + n * 2, 128), "latency render");
          }
          return audio;
        };
        auto fast = render(synth), slow = render(delayed);
        for (int i = 0; i < (8192 - 32) * 2; ++i)
          check(fast[i] == slow[i + 64], "instrument latency advertised and preserved");
        auto doc = Document::demo();
        auto &song = doc->song();
        song.m_nInstruments = 4;
        for (int i = 1; i <= 4; ++i)
          song.Instruments[i] = new ModInstrument(SAMPLEINDEX(i));
        auto mixedBytes = doc->serialize();
        auto mixed = [&](PluginState state) {
          PluginChain chain({state}, 48000, true);
          Renderer renderer(mixedBytes, 48000);
          chain.attachInstruments(renderer);
          std::vector<float> audio(8192 * 2);
          for (int n = 0; n < 8192; n += 128) {
            renderer.render(audio.data() + n * 2, 128);
            check(chain.process(audio.data() + n * 2, 128), "mixed latency render");
          }
          return audio;
        };
        fast = mixed(synth);
        slow = mixed(delayed);
        for (int i = 0; i < (8192 - 32) * 2; ++i)
          check(std::abs(fast[i] - slow[i + 64]) < 1e-6, "samples align with latent instrument");
        std::cout << "PASS instrument latency alignment with sample voices\n";
      }
      std::cout << "PASS VST3 keyboard preview and release\n";
      PluginState au{{kAudioUnitType_MusicDevice, kAudioUnitSubType_DLSSynth, kAudioUnitManufacturer_Apple,
                      "Apple DLSMusicDevice"}};
      au.instrument = 1;
      {
        NativePlugin unit(au, 48000);
        check(unit.midi(0x90, 60, 100), "AU MIDI note");
        double energy = 0;
        for (int n = 0; n < 100; ++n) {
          buffer.fill(0);
          check(unit.process(buffer.data(), 512, n * 512), "AU instrument process");
          for (auto v : buffer)
            energy += v * v;
        }
        check(energy > .01, "AU instrument audible");
        check(unit.midi(0x80, 60, 0), "AU instrument note-off");
        NativePlugin restored(unit.state(), 48000);
      }
      std::cout << "PASS Apple AU instrument note-on, output, note-off and state recall\n";
      NSString *path = [NSTemporaryDirectory() stringByAppendingPathComponent:@"resonance-native-plugins.resonance"];
      TrackerSession *session = [TrackerSession new];
      NSError *error = nil;
      check([session addInstrument:1 error:&error] > 0, "create tracker instrument");
      check([session addPlugin:dictionary(synth.descriptor) error:&error], "VST3 isolated validation and add");
      check([session assignPlugin:0 instrument:1 error:&error], "VST3 instrument assignment");
      check([session pluginParameter:0 identifier:7 value:.27 record:NO error:&error], "project plugin parameter");
      check([session savePath:path error:&error], "save VST3 project");
      TrackerSession *reopened = [TrackerSession new];
      check([reopened openPath:path error:&error], "reopen VST3 project");
      NSDictionary *state = [reopened snapshot:0][@"nativePlugins"][0];
      check([state[@"instrument"] intValue] == 1, "instrument assignment persisted");
      check(std::abs([[reopened pluginParameters:0][0][@"value"] doubleValue] - .27) < 1e-5,
            "VST3 project state persisted");
      std::string wavePath = "/tmp/resonance-native-instrument.wav";
      exportProjectAudio(bytes, {synth}, automation, wavePath);
      std::ifstream wav(wavePath, std::ios::binary);
      wav.seekg(44);
      std::vector<float> exported(live.size());
      wav.read(reinterpret_cast<char *>(exported.data()), exported.size() * 4);
      check(exported == live, "WAV export instrument and automation fidelity");
      std::cout << "PASS VST3 project save/reopen, instrument assignment, parameter persistence and WAV fidelity\n";
      if (argc > 2 && std::string(argv[2]) == "--ui") {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp activateIgnoringOtherApps:YES];
        NativePlugin plugin(gain, 48000);
        plugin.showEditor();
        NSWindow *window = NSApp.windows.firstObject;
        check(window != nil, "plugin editor window");
        NSSlider *slider = nil;
        for (NSView *child in window.contentView.subviews)
          for (NSView *item in child.subviews)
            if ([item isKindOfClass:NSSlider.class])
              slider = (NSSlider *)item;
        check(slider != nil, "plugin-owned slider attached");
        slider.doubleValue = .72;
        [slider sendAction:slider.action to:slider.target];
        uint32_t id;
        float value;
        check(plugin.popEdit(id, value) && id == 7 && std::abs(value - .72) < 1e-5, "custom editor parameter callback");
        // The controller gesture must reach audio without a main-window poll or forwarding call.
        buffer.fill(.2);
        check(plugin.process(buffer.data(), 128, 0) && std::abs(buffer[0] - .144) < 1e-5,
              "custom editor changes audio");
        [window close];
        plugin.showEditor();
        plugin.closeEditor();
        PluginState auView{
            {kAudioUnitType_Effect, kAudioUnitSubType_LowPassFilter, kAudioUnitManufacturer_Apple, "Apple AULowpass"}};
        NativePlugin apple(auView, 48000);
        apple.showEditor();
        apple.closeEditor();
        apple.showEditor();
        apple.closeEditor();
        std::cout << "PASS native AU/VST3 editor attachment, close/reopen, and VST3 custom control-to-audio callback\n";
        if (argc > 3 && std::string(argv[3]) == "--loopback-record") {
          uint32_t virtualDevice = 0;
          for (auto &d : Tracker::AudioDevice::devices())
            if (d.name == "BlackHole 2ch")
              virtualDevice = d.id;
          check(virtualDevice != 0, "BlackHole virtual device for custom-interface recording test");
          UInt32 frames = 0, size = sizeof(frames);
          AudioObjectPropertyAddress address{kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal,
                                             kAudioObjectPropertyElementMain};
          check(AudioObjectGetPropertyData(virtualDevice, &address, 0, nullptr, &size, &frames) == noErr,
                "virtual device buffer size");
          check([reopened configureDevice:virtualDevice buffer:frames error:&error], "configure virtual output");
          check([reopened showPluginEditor:0 error:&error], "open instrument interface before playback");
          check([reopened playOrder:0 error:&error], "start instrument with interface open");
          NSWindow *instrumentWindow = nil;
          for (NSWindow *candidate in NSApp.windows)
            if (candidate.isVisible && [candidate.title isEqual:@"Resonance Test Instrument"])
              instrumentWindow = candidate;
          check(instrumentWindow != nil, "custom interface survives playback restart");
          NSSlider *instrumentSlider = nil;
          for (NSView *child in instrumentWindow.contentView.subviews)
            for (NSView *item in child.subviews)
              if ([item isKindOfClass:NSSlider.class])
                instrumentSlider = (NSSlider *)item;
          check(instrumentSlider != nil, "instrument custom slider");
          instrumentSlider.doubleValue = .61;
          [instrumentSlider sendAction:instrumentSlider.action to:instrumentSlider.target];
          check([reopened collectPluginEdits:YES error:&error] == 1, "record custom-interface parameter gesture");
          [reopened stop];
          check([reopened savePath:path error:&error], "save custom-interface automation");
          NSDictionary *project = [NSPropertyListSerialization propertyListWithData:[NSData dataWithContentsOfFile:path]
                                                                            options:0
                                                                             format:nil
                                                                              error:nil];
          check([project[@"version"] intValue] == 2 && [project[@"automation"] count] == 1,
                "versioned automation project persisted");
          check(std::abs([project[@"automation"][0][2] doubleValue] - .61) < 1e-5,
                "custom-interface automation value retained");
          check([reopened openPath:path error:&error], "reopen custom-interface automation");
          check(std::abs([[reopened pluginParameters:0][0][@"value"] doubleValue] - .27) < 1e-5,
                "recording preserves initial plugin state");
          std::cout << "PASS custom-interface automation recording, initial-state preservation, restart/reopen and "
                       "project persistence via virtual output\n";
        }
      }
      return 0;
    } catch (const std::exception &e) {
      std::cerr << "FAIL " << e.what() << '\n';
      return 1;
    }
  }
}
