#include "../Audio/AudioDevice.hpp"
#include "soundlib/ModInstrument.h"
#include <chrono>
#include <iostream>
#include <thread>
using namespace Tracker;
using namespace OpenMPT;
int main(int argc, char **argv) {
  @autoreleasepool { try {
    if (argc < 2) throw std::runtime_error("Provide fixture plugin path, optionally followed by test seconds");
    const int seconds = argc > 2 ? std::stoi(argv[2]) : 60;
    if (seconds < 3 || seconds > 1800) throw std::runtime_error("Duration must be 3 to 1800 seconds");
    auto doc = Document::demo();
    doc->transaction([](CSoundFile &song) {
      Document::resizeChannels(song, 127); song.Patterns[0].Resize(1024); song.Order().assign(32, 0);
      // Exercise all voices/processing without making the user's speakers play.
      song.m_nDefaultGlobalVolume = 0;
      song.m_nInstruments = 5;
      for (int i = 1; i <= 5; ++i) song.Instruments[i] = new ModInstrument(i < 5 ? SAMPLEINDEX(i) : SAMPLEINDEX(0));
      for (ROWINDEX row = 0; row < 1024; row += 4) for (CHANNELINDEX ch = 0; ch < 127; ++ch) {
        auto &cell = *song.Patterns[0].GetpModCommand(row, ch);
        cell.note = uint8_t(37 + ch % 24); cell.instr = ch == 126 ? 5 : 1; cell.volcmd = VOLCMD_VOLUME; cell.vol = 24;
      }
    });
    const auto descriptors = NativePlugin::discoverVST3(argv[1]); auto descriptor = descriptors.at(0);
    std::vector<PluginState> states;
    for (int i = 0; i < 3; ++i) { PluginState p{descriptor}; p.instanceID = "gain-" + std::to_string(i); p.auxiliaryInputs = {1}; states.push_back(std::move(p)); }
    PluginState lowpass{{kAudioUnitType_Effect, kAudioUnitSubType_LowPassFilter, kAudioUnitManufacturer_Apple, "Apple AULowpass"}};
    lowpass.instanceID = "lowpass"; states.push_back(lowpass);
    PluginState synth{descriptors.at(1)}; synth.instanceID = "synth"; synth.instrument = 5; synth.auxiliaryOutputs = {1, 2, 31}; states.push_back(synth);
    for (const auto &descriptor : NativePlugin::builtins()) {
      PluginState effect{descriptor}; effect.instanceID = descriptor.classID;
      if (nativeEffect(descriptor.classID).sidechain) {
        effect.auxiliaryInputs = {1}; NativeEffect settings(descriptor.classID,48000);
        settings.parameter(9,1); settings.parameter(1,-36); effect.state=settings.state();
      }
      states.push_back(std::move(effect));
    }
    auto slotFor = [&](const std::string &id) {
      auto found = std::find_if(states.begin(), states.end(), [&](const auto &s) { return s.instanceID == id; });
      if (found == states.end()) throw std::runtime_error("Missing qualification device");
      return uint32_t(found - states.begin());
    };
    const auto digitalSlot = slotFor("resonance.digital-filter.v1"), eq5Slot = slotFor("resonance.eq5.v1"),
      eq10Slot = slotFor("resonance.eq10.v1"), mixerEQSlot = slotFor("resonance.mixer-eq.v1"), combSlot = slotFor("resonance.comb-filter.v1"),
      distortionSlot = slotFor("resonance.distortion.v1"), lofiSlot = slotFor("resonance.lofimat.v1"), cabinetSlot = slotFor("resonance.cabinet-simulator.v1"), compressorSlot=slotFor("resonance.compressor.v1"), gateSlot=slotFor("resonance.gate.v1"), maximizerSlot=slotFor("resonance.maximizer.v1"), busSlot=slotFor("resonance.bus-compressor.v1");
    doc->annotate([](NativeSong &n) {
      const auto master = n.makeEntity().id;
      std::array<uint64_t, 4> groups; for (auto &id : groups) id = n.makeEntity().id;
      std::array<uint64_t, 2> returns; for (auto &id : returns) id = n.makeEntity().id;
      for (const auto &[ch, track] : n.tracks) {
        n.mixer.buses.push_back({track.id, groups[ch % groups.size()], MixerBusKind::Track, "Track"});
        if (ch % 4 == 0) n.mixer.buses.back().sends.push_back({returns[ch % 2], -12, ch % 8 == 0, true});
      }
      for (auto id : groups) n.mixer.buses.push_back({id, master, MixerBusKind::Group, "Group"});
      for (auto id : returns) n.mixer.buses.push_back({id, master, MixerBusKind::Return, "Return"});
      n.mixer.buses.push_back({master, 0, MixerBusKind::Master, "Master"});
      n.mixer.buses[0].inserts = {"gain-0"}; n.mixer.buses[127].inserts = {"gain-1"};
      n.mixer.buses[131].inserts = {"lowpass"}; n.mixer.buses.back().inserts = {"gain-2"};
      n.mixer.buses[128].inserts = {"resonance.gainer.v1"}; n.mixer.buses[129].inserts = {"resonance.stereo-expander.v1"};
      n.mixer.buses.back().inserts.push_back("resonance.dc-offset.v1");
      n.mixer.buses[3].inserts = {"resonance.digital-filter.v1"};
      n.mixer.buses[4].inserts = {"resonance.distortion.v1"};
      n.mixer.buses[5].inserts = {"resonance.lofimat.v1"};
      n.mixer.buses[6].inserts = {"resonance.cabinet-simulator.v1"};
      n.mixer.buses[7].inserts = {"resonance.compressor.v1"};
      n.mixer.buses[8].inserts = {"resonance.gate.v1"};
      n.mixer.buses[129].inserts.push_back("resonance.bus-compressor.v1");
      n.mixer.buses[130].inserts = {"resonance.eq5.v1", "resonance.comb-filter.v1"};
      n.mixer.buses[132].inserts = {"resonance.eq10.v1"};
      n.mixer.buses.back().inserts.push_back("resonance.mixer-eq.v1");
      n.mixer.buses.back().inserts.push_back("resonance.maximizer.v1");
      n.mixer.instruments = {{"synth", n.tracks.at(126).id, 0}, {"synth", n.tracks.at(125).id, 1},
        {"synth", n.tracks.at(124).id, 2}, {"synth", n.tracks.at(123).id, 31}};
      n.mixer.sidechains = {{n.tracks.at(1).id, "gain-0", 1, -12, true}, {n.tracks.at(2).id, "gain-1", 1, -18, false},
        {groups[1], "gain-2", 1, -24, false},
        {n.tracks.at(1).id,"resonance.compressor.v1",1,-6,true}, {n.tracks.at(2).id,"resonance.gate.v1",1,-6,true}, {n.tracks.at(3).id,"resonance.bus-compressor.v1",1,-6,true}};
    });
    AudioDevice device; device.configure(0, 128); device.setPlugins(states);
    device.play(doc->serialize(), 0, false, {}, 0, &doc->native());
    std::vector<MixerControls> controls(doc->native().mixer.buses.size());
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    uint32_t gestures = 0; size_t meterReads = 0, dynamicsReads = 0, limiterReads = 0, busReads = 0;
    while (std::chrono::steady_clock::now() < end) {
      controls[gestures % 127].gainDB = -double(gestures % 24);
      controls[127].pan = std::sin(gestures * .03) * .8;
      if (!device.mixerControls(controls)) throw std::runtime_error("Live mixer control queue unexpectedly saturated");
      if (!device.pluginParameter(5, 1, -float(gestures % 24)) || !device.pluginParameter(7, 1, float(gestures % 200)))
        throw std::runtime_error("Built-in live parameter queue unexpectedly saturated");
      if (!device.pluginParameter(digitalSlot, 2, 20 + float(gestures % 1000) * 19) ||
          !device.pluginParameter(eq5Slot, 11, float(gestures % 37) - 18) ||
          !device.pluginParameter(eq10Slot, 47, float(gestures % 31) - 15) ||
          !device.pluginParameter(mixerEQSlot, 1, float(gestures % 5)) ||
          !device.pluginParameter(combSlot, 1, float(40 + gestures % 51)) ||
          !device.pluginParameter(combSlot, 3, float(gestures % 161) - 80) ||
          !device.pluginParameter(distortionSlot, 1, float(gestures % 37)) ||
          !device.pluginParameter(distortionSlot, 2, float(gestures % 4)) ||
          !device.pluginParameter(distortionSlot, 3, float(gestures % 201) - 100) ||
          !device.pluginParameter(lofiSlot, 1, 1 + float(gestures % 230) / 10) ||
          !device.pluginParameter(lofiSlot, 2, 20 + float(gestures % 480) * 100) ||
          !device.pluginParameter(lofiSlot, 3, float(gestures % 11)) ||
          !device.pluginParameter(lofiSlot, 4, float(gestures % 2)) ||
          !device.pluginParameter(cabinetSlot, 1, float(gestures % 18)) ||
          !device.pluginParameter(cabinetSlot, 2, float(gestures % 6)) ||
          !device.pluginParameter(cabinetSlot, 3, float(gestures % 37)) ||
          !device.pluginParameter(cabinetSlot, 11, float(gestures % 37) - 18) ||
          !device.pluginParameter(compressorSlot, 1, -12 - float(gestures % 49)) ||
          !device.pluginParameter(compressorSlot, 2, 1 + float(gestures % 20)) ||
          !device.pluginParameter(compressorSlot, 7, float(gestures % 2)) ||
          !device.pluginParameter(gateSlot, 1, -20 - float(gestures % 48)) ||
          !device.pluginParameter(gateSlot, 16, float(gestures % 501)) ||
          !device.pluginParameter(gateSlot, 19, float(gestures % 2)) ||
          !device.pluginParameter(maximizerSlot, 1, float(gestures % 37)) ||
          !device.pluginParameter(maximizerSlot, 2, -float(gestures % 37)) ||
          !device.pluginParameter(maximizerSlot, 5, -float(gestures % 13)) ||
          !device.pluginParameter(busSlot, 1, -float(gestures % 73)) ||
          !device.pluginParameter(busSlot, 2, 1 + float(gestures % 40)) ||
          !device.pluginParameter(busSlot, 7, float(gestures % 3)))
        throw std::runtime_error("Filter/EQ live parameter queue unexpectedly saturated");
      const auto dynamics = device.pluginMeters(compressorSlot);
      if (!dynamics) throw std::runtime_error("Live dynamics meters unavailable");
      if (dynamics->reductionDB[0] > 0 && dynamics->detectorDB[0] > -100) ++dynamicsReads;
      const auto limiter = device.pluginMeters(maximizerSlot);
      if (!limiter) throw std::runtime_error("Live limiter meters unavailable");
      if (limiter->reductionDB[0] > 0 && limiter->detectorDB[0] > -100) ++limiterReads;
      const auto bus = device.pluginMeters(busSlot);
      if (!bus) throw std::runtime_error("Live bus-compressor meters unavailable");
      if (bus->reductionDB[0] > 0 && bus->detectorDB[0] > -100) ++busReads;
      auto meters = device.mixerMeters();
      if (meters.size() != controls.size()) throw std::runtime_error("Live bus meters unavailable");
      if (std::any_of(meters.begin(), meters.end(), [](const auto &meter) { return meter.left > 0 || meter.right > 0; })) ++meterReads;
      std::vector<Edit> edits;
      for (int ch = 0; ch < 127; ++ch) edits.push_back({0, uint16_t((gestures % 255) * 4), uint16_t(ch), {},
        {uint8_t(37 + (gestures + ch) % 24), uint8_t(ch == 126 ? 5 : 1), uint8_t(VOLCMD_VOLUME), 24, 0, 0}});
      if (!device.renderer()->enqueue(edits)) throw std::runtime_error("Concurrent pattern edit queue saturated");
      ++gestures; std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    device.stop(); const auto t = device.telemetry();
    std::cout << seconds << "s silent CoreAudio mixer: " << t.callbacks << " callbacks, " << t.maxMicros << " us maximum, p99.9 <= "
      << t.p999Micros << " us, " << t.overruns << " overruns, " << gestures << " control/edit gestures at " << device.sampleRate()
      << " Hz / " << device.bufferSize() << " frames; " << meterReads << " meter reads received signal, " << dynamicsReads << " dynamics reads received reduction, " << limiterReads << " limiter reads received reduction, " << busReads << " bus-compressor reads received reduction\n";
    if (device.pluginFailed() || device.renderer()->faulted() || t.frames < device.sampleRate() * (seconds - 1) ||
        t.overruns || !meterReads || !dynamicsReads || !limiterReads || !busReads || t.p999Micros >= double(device.bufferSize()) / device.sampleRate() * 500000)
      throw std::runtime_error("Live mixer missed its realtime qualification gate");
    std::cout << "PASS real-device native mixer: 127 tracks, 4 groups, 2 returns, AU/VST3 inserts, 3 built-in utilities, Digital Filter, EQ5/EQ10/Mixer EQ, Comb Filter, 16x Distortion, LofiMat, Cabinet Simulator, Compressor/Bus Compressor/Gate/Maximizer, sends, 4 instrument outputs, 6 sidechains, smoothed controls, concurrent edits and meters\n";
    return 0;
  } catch (const std::exception &e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; } }
}
