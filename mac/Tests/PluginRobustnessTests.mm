// Misbehaving-plugin and boundary conditions which must never stop rendering:
#include "FixtureTrust.hpp"
// editor reports, setup-time latency notifications, note-release bursts, dense
// automation, malformed saved state and events for processors nobody renders.
#include "../Audio/AudioExport.hpp"
#include "../Audio/AudioUnitHost.hpp"
#include "../Audio/VST3Host.hpp"
#include "editor/TrackerDocument.hpp"
#include "soundlib/ModInstrument.h"
#import <AppKit/AppKit.h>
#include <AudioToolbox/AudioToolbox.h>
#include <cmath>
#include <dlfcn.h>
#include <filesystem>
#include <iostream>
#include <limits>
#include "GraphRealtimeAudit.hpp"
using namespace Tracker;
using namespace OpenMPT;
static void check(bool b, const char *why) { if (!b) throw std::runtime_error(why); }
template<class T> static T hook(void *module, const char *name) {
  auto *symbol = dlsym(module, name);
  check(symbol, name);
  return reinterpret_cast<T>(symbol);
}
int main(int argc, char **argv) { trustFixtureArguments(argc, argv); @autoreleasepool { try {
  check(argc == 2, "Pass fixture bundle");
  const auto descriptors = NativePlugin::discoverVST3(argv[1]);
  check(descriptors.size() == 4, "Fixture classes");
  void *module = dlopen((std::string(argv[1]) + "/Contents/MacOS/ResonanceFixture").c_str(), RTLD_NOW | RTLD_LOCAL);
  check(module, "Fixture module");
  const auto latency = hook<int (*)(uint32_t)>(module, "ResonanceFixtureLatency");
  const auto edit = hook<int (*)(uint32_t, double)>(module, "ResonanceFixtureEdit");
  const auto announce = hook<void (*)(bool)>(module, "ResonanceFixtureAnnounceLatency");
  const auto unterminated = hook<void (*)(bool)>(module, "ResonanceFixtureUnterminated");
  const auto connections = hook<int (*)()>(module, "ResonanceFixtureConnections");
  const auto singleVoice = hook<void (*)(bool)>(module, "ResonanceFixtureSingleVoice");
  const auto lifecycle = hook<int (*)(int)>(module, "ResonanceFixtureLifecycle");
  PluginState effect{descriptors.at(0)}, synth{descriptors.at(1)};
  effect.instanceID = "robust-effect"; synth.instanceID = "robust-synth";
  uint64_t a = 0, f = 0, l = 0;

  { // Factory strings filled to the last byte have no terminator.
    unterminated(true);
    const auto found = NativePlugin::discoverVST3(argv[1]);
    unterminated(false);
    check(found.size() == 4, "Unterminated factory strings still enumerate");
    for (const auto &d : found) check(d.name == std::string(64, 'N'), "Class name is bounded to its 64-byte field");
    check(!found[0].instrument && found[1].instrument && found[2].instrument && !found[3].instrument,
          "Subcategories are searched inside their field only");
  }
  { // Latency announced while being created or reactivated is not a change.
    latency(0); announce(true);
    NativePlugin p(effect, 48000);
    check(!p.latencyChangePending(), "Setup-time latency notification is consumed by creation");
    check(connections() == 0, "A single-component plugin is not connected to itself");
    check(latency(5) == 1 && p.latencyChangePending(), "A later notification is still a change");
    p.refreshLatency();
    check(!p.latencyChangePending() && std::llround(p.latency() * 48000) == 5, "Reactivation does not re-arm the request");
    latency(0); p.refreshLatency();
  }
  { // Export used to abort on the first block for such a plugin.
    const auto directory = std::filesystem::temp_directory_path() / "resonance-tests";
    std::filesystem::create_directories(directory);
    const auto path = directory / "robust-latency.wav";
    std::filesystem::remove(path);
    auto document = Document::demo();
    exportProjectAudio(document->serialize(), {effect}, {}, path.string());
    check(std::filesystem::exists(path) && std::filesystem::file_size(path) > 44, "Export completes with a setup-time latency notification");
    std::filesystem::remove(path);
    announce(false);
  }
  { // Editor reports: clamp, ignore, coalesce. None stops rendering.
    VST3Plugin p(effect, 48000, false);
    check(edit(7, 1.5) == 1 && p.droppedEdits() == 0 && p.parameters().at(0).value == 1.f, "Out-of-range editor value is clamped");
    check(edit(7, -3) == 1 && p.parameters().at(0).value == 0.f, "Negative editor value is clamped");
    check(edit(9999, .5) == 0 && p.droppedEdits() == 1, "Unknown parameter is ignored and counted");
    check(edit(7, std::numeric_limits<double>::quiet_NaN()) == 0 && p.droppedEdits() == 2, "Non-finite value is ignored and counted");
    // A long gesture with the transport stopped: nothing drains meanwhile.
    for (int i = 0; i < 40000; ++i) check(edit(7, double(i % 1000) / 1000) == 1, "Gesture accepted while stopped");
    check(edit(7, .25) == 1, "Final gesture value");
    std::array<float, 512> audio; audio.fill(.4f);
    tracker_audit_begin(); const bool ok = p.process(audio.data(), 256, 0); tracker_audit_end(&a, &f, &l);
    check(ok && a + f + l == 0, "Rendering survives the gesture without allocation, free or lock");
    check(std::abs(audio[0] - .1f) < 1e-6 && std::abs(audio[511] - .1f) < 1e-6, "Only the newest editor value is audible");
    check(p.droppedEdits() == 2 + 40003 - 8192, "Only surplus UI notifications were dropped");
    uint32_t id = 0; float value = -1; size_t popped = 0;
    while (p.popEdit(id, value)) { check(id == 7 && value >= 0 && value <= 1, "UI notification is normalized"); ++popped; }
    check(popped == 8192, "UI queue stays bounded");
    check(edit(7, .75) == 1 && !p.state().state.empty() && p.parameters().at(0).value == .75f, "State capture flushes the pending value");
    audio.fill(.4f); check(p.process(audio.data(), 256, 256) && std::abs(audio[0] - .3f) < 1e-6, "Flushed value reached the processor");
  }
  { // All-notes-off for more held notes than one event list can carry.
    NativePlugin p(synth, 48000);
    std::array<float, 256> audio{};
    uint64_t position = 0;
    auto render = [&] {
      audio.fill(0);
      tracker_audit_begin(); const bool ok = p.process(audio.data(), 128, position); tracker_audit_end(&a, &f, &l);
      check(ok && a + f + l == 0, "Note burst renders without allocation, free or lock");
      position += 128;
    };
    for (int ch = 0; ch < 16; ++ch) {
      for (int pitch = 0; pitch < 128; ++pitch) check(p.midi(uint8_t(0x90 | ch), uint8_t(pitch), 100), "Note-on");
      render();
    }
    check(audio[0] > .09f, "All notes are sounding");
    for (int ch = 0; ch < 16; ++ch)
      check(p.midi(uint8_t(0xb0 | ch), 123, 0), "All-notes-off never fails, whatever the number of held notes");
    render();
    check(audio[0] > .09f, "One block cannot carry 2048 releases");
    for (int block = 0; block < 8; ++block) render();
    check(audio[0] == 0, "The remainder is released by the following blocks");
    check(p.midi(0x90, 60, 100), "Note-on after the burst"); render();
    check(audio[0] > .09f, "A later note is not swallowed by carried releases");
    check(p.midi(0x80, 60, 0), "Note-off after the burst"); render();
    check(audio[0] == 0, "Note counts stayed balanced");
  }
  { // A note retriggered while its release is still deferred. Deferred
    // note-offs have no note identity, so they must reach the plugin before
    // the new note-on, never after it.
    singleVoice(true);
    VST3Plugin p(synth, 48000, false);
    std::array<float, 256> audio{};
    uint64_t position = 0;
    auto render = [&] {
      audio.fill(0);
      tracker_audit_begin(); const bool ok = p.process(audio.data(), 128, position); tracker_audit_end(&a, &f, &l);
      check(ok && a + f + l == 0, "Retrigger renders without allocation, free or lock");
      position += 128;
    };
    auto holdAll = [&] {
      for (int ch = 0; ch < 16; ++ch) {
        for (int pitch = 0; pitch < 128; ++pitch) check(p.midi(uint8_t(0x90 | ch), uint8_t(pitch), 100), "Note-on");
        render();
      }
      for (int ch = 0; ch < 16; ++ch) check(p.midi(uint8_t(0xb0 | ch), 123, 0), "All-notes-off");
    };
    holdAll();
    // Channel 16's releases cannot be in this block: 384 were taken by channels 1-3.
    check(p.midi(0x9f, 127, 100), "Retrigger in the block of the all-notes-off");
    for (int block = 0; block < 10; ++block) render();
    check(audio[0] > .09f, "A deferred release does not end the retriggered note");
    check(p.midi(0x8f, 127, 0), "Release of the retriggered note"); render();
    check(audio[0] == 0 && p.droppedNotes() == 0, "Only the retriggered note remained");
    holdAll(); render();
    check(audio[0] > .09f, "Releases are still deferred");
    check(p.midi(0x9f, 127, 100), "Retrigger in a later block");
    for (int block = 0; block < 10; ++block) render();
    check(audio[0] > .09f, "A carried release does not end a note retriggered later");
    check(p.midi(0x8f, 127, 0), "Release"); render();
    check(audio[0] == 0 && p.droppedNotes() == 0, "All voices ended");
    // Repeated instances of one pitch owe more releases than a block can carry
    // ahead of a new note: the note is discarded, the releases are not.
    for (int n = 0; n < 600; ++n) { check(p.midi(0x90, 60, 100), "Stacked note-on"); if (n % 300 == 299) render(); }
    check(p.midi(0xb0, 123, 0) && p.midi(0x90, 60, 100) && p.droppedNotes() == 1, "Unplaceable retrigger is discarded and counted, not fatal");
    for (int block = 0; block < 4; ++block) render();
    check(audio[0] == 0, "No voice hangs after the discarded retrigger");
    check(p.midi(0x90, 60, 100), "Note-on once the releases are delivered"); render();
    check(audio[0] > .09f && p.droppedNotes() == 1, "Pitch is playable again");
    check(p.midi(0x80, 60, 0), "Final release"); render();
    check(audio[0] == 0, "Silent");
    singleVoice(false);
  }
  { // Saved Audio Unit state is project data: it may be any property list.
    PluginState bad{{kAudioUnitType_Effect, kAudioUnitSubType_LowPassFilter, kAudioUnitManufacturer_Apple, "Malformed state"}};
    NSData *data = [NSPropertyListSerialization dataWithPropertyList:@[@1, @"two"] format:NSPropertyListBinaryFormat_v1_0 options:0 error:nil];
    const auto *bytes = static_cast<const std::byte *>(data.bytes);
    bad.state.assign(bytes, bytes + data.length);
    std::string message;
    try { NativePlugin p(bad, 48000); } catch (const std::exception &e) { message = e.what(); }
    check(message == "Invalid Audio Unit state", "Non-dictionary Audio Unit state is rejected before reaching the plugin");
    bad.state.clear();
    NativePlugin good(bad, 48000);
    auto saved = good.state();
    check(!saved.state.empty(), "Audio Unit state capture");
    NativePlugin restored(saved, 48000);
  }
  { // Dense automation: more than 256 events inside one block, any partition.
    std::vector<ParameterChange> automation;
    for (uint32_t i = 0; i < 1500; ++i) automation.push_back({0, 7, float(i % 100) / 100, 40 + i});
    auto render = [&](uint32_t block) {
      PluginChain chain({effect}, 48000, true, automation);
      std::vector<float> out(8192, .2f);
      for (uint32_t at = 0; at < 4096; at += block) {
        tracker_audit_begin(); const bool ok = chain.process(out.data() + at * 2, block); tracker_audit_end(&a, &f, &l);
        check(ok && !chain.failed() && a + f + l == 0, "Dense automation renders without failure or allocation");
      }
      return out;
    };
    const auto reference = render(64);
    check(reference == render(4096) && reference == render(1024) && reference == render(128), "Dense automation is independent of block size");
    check(std::abs(reference[2 * 4000] - .2f * .99f) < 1e-6, "Every event was applied");
  }
  { // With a mixer, an unassigned instrument is rendered by nobody. Its events
    // must not accumulate until the bounded store fills and playback stops.
    Document doc;
    doc.transaction([](CSoundFile &song) {
      check(song.Patterns[0].GetNumRows() == 64 || song.Patterns[0].Resize(64), "Resize pattern");
      song.Order().assign(10, 0);
      song.Order().SetDefaultTempoInt(125); song.Order().SetDefaultSpeed(6);
    });
    doc.annotate([&](NativeSong &n) {
      const auto master = n.makeEntity().id;
      for (const auto &[channel, track] : n.tracks) n.mixer.buses.push_back({track.id, master, MixerBusKind::Track, "Track"});
      n.mixer.buses.push_back({master, 0, MixerBusKind::Master, "Master"});
      MusicalAutomationLane lane{n.makeEntity().id, n.patterns.at(0).id, synth.instanceID, 7, true,
        {{0, .2, AutomationCurve::Linear}, {8192, .8, AutomationCurve::Linear}}};
      n.automation.push_back(lane);
    });
    PluginChain chain({synth}, 48000, true);
    Renderer renderer(doc.snapshotData(), 48000);
    chain.attachInstruments(renderer, &doc.native());
    chain.attachMusicalAutomation(renderer, doc.native());
    std::array<float, 8192> audio{};
    uint64_t rendered = 0;
    for (; rendered < uint64_t(48000) * 60; rendered += 4096) {
      chain.beginRenderBlock();
      tracker_audit_begin();
      const auto received = renderer.render(audio.data(), 4096);
      const bool ok = chain.process(audio.data(), 4096);
      tracker_audit_end(&a, &f, &l);
      check(ok && !chain.failed(), "Automation for an unrendered instrument never stops playback");
      check(a + f + l == 0, "Unrendered instrument automation stays realtime safe");
      if (received < 4096) break;
    }
    check(rendered >= uint64_t(48000) * 50, "Rendered beyond the capacity of the event store");
  }
  latency(0);
  check(lifecycle(0) == 0 && lifecycle(3) == 0, "Balanced plugin lifecycle");
  dlclose(module);
  std::cout << "PASS plugin robustness: bounded factory strings, setup-time latency, export, editor clamp/ignore/coalesce, "
               "carried all-notes-off, retrigger ordering, malformed AU state, dense automation partition invariance, unrendered instrument automation\n";
  return 0;
} catch (const std::exception &e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; } } }
