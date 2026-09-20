#include "../Audio/AudioUnitHost.hpp"
#include "editor/TrackerDocument.hpp"
#include "soundlib/ModInstrument.h"
#import "../Bridge/TrackerSession.h"
#include <iostream>
using namespace Tracker;
using namespace OpenMPT;
#ifdef TRACKER_SANITIZER
static void tracker_audit_begin() {}
static void tracker_audit_end(uint64_t *a, uint64_t *f, uint64_t *l) { *a = *f = *l = 0; }
#else
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *, uint64_t *, uint64_t *);
#endif
std::vector<PluginDescriptor> registerFixtureAUs();
static void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
static void auditEnd(bool ok) {
  uint64_t a, f, l; tracker_audit_end(&a, &f, &l);
  check(ok, "Multi-bus audio callback succeeds"); check(a + f + l == 0, "No multi-bus callback allocations, frees or locks");
}
static NSDictionary *dictionary(const PluginDescriptor &d) {
  return @{@"type": @(d.type), @"subtype": @(d.subtype), @"manufacturer": @(d.manufacturer), @"name": @(d.name.c_str()),
    @"format": @(d.format.c_str()), @"path": @(d.path.c_str()), @"classID": @(d.classID.c_str()), @"isInstrument": @(d.instrument)};
}
static void hostOutputs(PluginState source, uint32_t rate, uint32_t block) {
  source.auxiliaryOutputs = {1, 2, 31};
  NativePlugin plugin(source, rate, true);
  check(plugin.buses().size() == 32, "All 32 instrument buses are described without renumbering");
  check(plugin.buses()[1].channels == 1 && plugin.buses()[2].channels == 2 && !plugin.buses()[3].active, "Mono/stereo and disabled bus metadata");
  check(plugin.state().auxiliaryOutputs == source.auxiliaryOutputs, "Opaque plugin state capture retains bus configuration");
  plugin.prepareMusicalAutomation(); plugin.schedule(7, .25f, 37); plugin.schedule(7, .8f, 113); plugin.schedule(7, .1f, 4097);
  plugin.midi(0x90, 60, 127);
  std::array<float, 8192> main{};
  for (uint32_t pos = 0; pos < 8192; pos += block) {
    const auto frames = std::min(block, 8192 - pos);
    tracker_audit_begin(); const bool ok = plugin.process(main.data(), frames, pos); auditEnd(ok);
    for (uint32_t i = 0; i < frames; ++i) {
      const uint32_t at = pos + i; const float gain = at < 37 ? .5f : at < 113 ? .25f : at < 4097 ? .8f : .1f;
      check(std::abs(main[i * 2] - .2f * gain) < 1e-7, "Main bus parameter boundary");
      for (auto bus : {1u, 2u, 31u}) {
        const auto *output = plugin.auxiliaryOutput(bus);
        const float left = .2f * gain * (bus + 1), right = bus % 2 ? left : left * -.5f;
        check(output && std::abs(output[i * 2] - left) < 1e-6 && std::abs(output[i * 2 + 1] - right) < 1e-6,
              "Each auxiliary preserves identity, mono duplication and automation sub-block offsets");
      }
    }
  }
  check(!plugin.auxiliaryOutput(3) && !plugin.auxiliaryOutput(64), "Disabled and invalid outputs have no audio buffer");
}
static void hostInputs(PluginState effect, uint32_t rate, uint32_t block) {
  effect.auxiliaryInputs = {1}; NativePlugin plugin(effect, rate, true);
  plugin.prepareMusicalAutomation(); plugin.schedule(7, .25f, 37); plugin.schedule(7, .8f, 113);
  std::array<float, 8192> main{}, side{};
  const PluginAudioInput input{1, side.data()};
  for (uint32_t pos = 0; pos < 8192; pos += block) {
    const auto frames = std::min(block, 8192 - pos);
    for (uint32_t i = 0; i < frames; ++i) {
      main[i * 2] = .2; main[i * 2 + 1] = -.1;
      side[i * 2] = float((pos + i) % 97) / 97; side[i * 2 + 1] = .1;
    }
    tracker_audit_begin(); const bool ok = plugin.process(main.data(), frames, pos, {&input, 1}); auditEnd(ok);
    for (uint32_t i = 0; i < frames; ++i) {
      const auto at = pos + i; const float gain = at < 37 ? .5f : at < 113 ? .25f : .8f;
      const float sideValue = (side[i * 2] + side[i * 2 + 1]) * .5f;
      check(std::abs(main[i * 2] - .2f * gain * (1 + sideValue)) < 1e-7 && std::abs(main[i * 2 + 1] + .1f * gain * (1 + sideValue)) < 1e-7,
            "Mono auxiliary input uses the correct source offsets after parameter splits");
    }
  }
  main.fill(.2f); check(plugin.process(main.data(), 128, 8192), "Unconnected auxiliary gets silence");
  check(std::abs(main[0] - .16f) < 1e-7, "Auxiliary input never retains an old callback pointer");
  auto invalid = input; invalid.bus = 2; check(!plugin.process(main.data(), 128, 8320, {&invalid, 1}), "Reject unavailable auxiliary input");
}
static std::vector<float> graphRender(Document &doc, PluginState synth, uint32_t rate, uint32_t block, bool offline) {
  Renderer renderer(doc.serialize(), rate); PluginChain chain({synth}, rate, offline);
  chain.attachInstruments(renderer, &doc.native()); chain.attachMusicalAutomation(renderer, doc.native());
  const uint32_t total = rate; std::vector<float> out(total * 2);
  for (uint32_t pos = 0; pos < total; pos += block) {
    const auto frames = std::min(block, total - pos);
    tracker_audit_begin(); chain.syncTransport(renderer); renderer.render(out.data() + pos * 2, frames);
    const bool ok = chain.process(out.data() + pos * 2, frames); auditEnd(ok);
  }
  return out;
}
static void graphOutputs(PluginState synth, uint32_t rate) {
  auto doc = Document::demo();
  doc->transaction([](CSoundFile &s) {
    for (auto &p : s.Patterns) if (p.IsValid()) for (auto &cell : p) cell = {};
    s.Order().assign(1, 0); s.m_nInstruments = 1; s.Instruments[1] = new ModInstrument(0);
    auto &on = *s.Patterns[0].GetpModCommand(0, 0); on.note = 61; on.instr = 1;
  });
  doc->annotate([](NativeSong &n) {
    const auto master = n.makeEntity().id;
    for (const auto &[index, track] : n.tracks) n.mixer.buses.push_back({track.id, master, MixerBusKind::Track, "Track"});
    n.mixer.buses.push_back({master, 0, MixerBusKind::Master, "Master"});
    n.mixer.instruments = {{"synth", n.tracks.at(0).id, 0}, {"synth", n.tracks.at(1).id, 1}, {"synth", n.tracks.at(2).id, 2}};
    n.mixer.buses[0].mute = true; // Listen to auxiliary ports alone.
    n.automation.push_back({n.makeEntity().id, n.patterns.at(0).id, "synth", 7, true, {{0, .2, AutomationCurve::Linear}, {768, .8, AutomationCurve::Step}}});
  });
  synth.instanceID = "synth"; synth.instrument = 1; synth.auxiliaryOutputs = {1, 2};
  auto reference = graphRender(*doc, synth, rate, 128, false);
  for (auto block : {17u, 512u, 4096u}) {
    auto rendered = graphRender(*doc, synth, rate, block, true);
    check(reference == rendered, "Multi-output native graph and musical automation are exact across live/offline callback sizes");
  }
  double l = 0, r = 0;
  for (size_t i = 0; i < reference.size(); i += 2) { l += reference[i]; r += reference[i + 1]; }
  std::cout << "Aux graph sums " << rate << ": " << l << " / " << r << " ratio " << l/r << " sum residual " << l-r*10 << std::endl;
  check(l > 1 && std::abs(l / r - 10) < 1e-5, "Mono output 1 and stereo output 2 reach distinct buses; main mute leaves auxiliaries sounding");
  doc->annotate([](NativeSong &n) { n.mixer.buses[1].mute = true; n.mixer.buses[2].mute = true; });
  auto silent = graphRender(*doc, synth, rate, 17, true);
  check(std::all_of(silent.begin(), silent.end(), [](float value) { return value == 0; }), "Output faders and mute are independent");
}
static void graphSidechains(PluginState effect, PluginState synth, uint32_t rate) {
  auto doc = Document::demo();
  doc->transaction([](CSoundFile &s) {
    for (auto &p : s.Patterns) if (p.IsValid()) for (auto &cell : p) cell = {};
    s.Order().assign(1, 0); s.m_nInstruments = 1; s.Instruments[1] = new ModInstrument(0);
    auto &on = *s.Patterns[0].GetpModCommand(0, 0); on.note = 61; on.instr = 1;
  });
  doc->annotate([](NativeSong &n) {
    const auto master = n.makeEntity().id;
    for (const auto &[index, track] : n.tracks) n.mixer.buses.push_back({track.id, master, MixerBusKind::Track, "Track"});
    n.mixer.buses.push_back({master, 0, MixerBusKind::Master, "Master"});
    n.mixer.instruments = {{"synth", n.tracks.at(0).id, 0}, {"synth", n.tracks.at(1).id, 1}};
    n.mixer.buses[0].inserts = {"effect"}; n.mixer.buses[1].gainDB = -96;
    n.mixer.sidechains = {{n.tracks.at(1).id, "effect", 1, 0, true, true}};
    n.automation.push_back({n.makeEntity().id, n.patterns.at(0).id, "effect", 7, true,
      {{0, .2, AutomationCurve::Linear}, {768, .8, AutomationCurve::Step}}});
  });
  effect.instanceID = "effect"; effect.auxiliaryInputs = {1};
  synth.instanceID = "synth"; synth.instrument = 1; synth.auxiliaryOutputs = {1};
  auto render = [&](uint32_t block, bool offline) {
    Renderer renderer(doc->serialize(), rate); PluginChain chain({effect, synth}, rate, offline);
    chain.attachInstruments(renderer, &doc->native()); chain.attachMusicalAutomation(renderer, doc->native());
    std::vector<float> out(rate * 2);
    for (uint32_t pos = 0; pos < rate; pos += block) {
      const auto frames = std::min(block, rate - pos);
      tracker_audit_begin(); chain.syncTransport(renderer); renderer.render(out.data() + pos * 2, frames);
      const bool ok = chain.process(out.data() + pos * 2, frames); auditEnd(ok);
    }
    return out;
  };
  auto actual = render(128, false);
  for (auto block : {17u, 512u, 4096u}) check(actual == render(block, true), "AU/VST3 sidechain and parameter envelopes are callback-independent live/offline");
  doc->annotate([](NativeSong &n) {
    n.mixer.sidechains.clear(); for (auto &point : n.automation[0].points) point.value *= 1.2;
  });
  auto expected = render(512, true); double maximum = 0, energy = 0;
  for (size_t i = 0; i < actual.size(); ++i) { maximum = std::max(maximum, std::abs(double(actual[i]) - expected[i])); energy += std::abs(actual[i]); }
  check(energy > 1 && maximum < 2e-7, "Actual hosted sidechain gain matches an independently scaled automation envelope");
  std::cout << "Hosted " << effect.descriptor.format << " sidechain at " << rate << " Hz, reference difference " << maximum << '\n';
}
int main(int argc, char **argv) {
  @autoreleasepool { try {
    check(argc == 2, "Fixture bundle path required");
    auto descriptors = NativePlugin::discoverVST3(argv[1]);
    for (uint32_t rate : {44100, 48000, 96000}) {
      for (auto block : {17u, 128u, 512u, 4096u}) { hostOutputs({descriptors[1]}, rate, block); hostInputs({descriptors[0]}, rate, block); }
      graphOutputs({descriptors[1]}, rate);
      graphSidechains({descriptors[0]}, {descriptors[1]}, rate);
    }
    auto au = registerFixtureAUs();
    for (uint32_t rate : {44100, 48000, 96000}) {
      for (auto block : {17u, 128u, 512u, 4096u}) { hostOutputs({au[1]}, rate, block); hostInputs({au[0]}, rate, block); }
      graphOutputs({au[1]}, rate);
      graphSidechains({au[0]}, {au[1]}, rate);
    }
    TrackerSession *session = [TrackerSession new]; NSError *problem = nil;
    auto call = [&](NSString *method, NSDictionary *params, bool write = false) -> NSDictionary * {
      auto p = [params mutableCopy]; if (write) p[@"expectedRevision"] = session.automationRevision;
      auto reply = [session automationMethod:method params:p error:&problem];
      if (!reply) throw std::runtime_error(problem.localizedDescription.UTF8String);
      return reply;
    };
    auto rejects = [&](NSString *method, NSDictionary *params) {
      auto revision = session.automationRevision; auto p = [params mutableCopy]; p[@"expectedRevision"] = revision;
      check(![session automationMethod:method params:p error:&problem], "Invalid bus edit rejected");
      check([revision isEqual:session.automationRevision], "Invalid bus edit is atomic");
    };
    call(@"plugin.add", @{@"descriptor": dictionary(descriptors[1])}, true);
    auto info = call(@"plugin.buses.get", @{@"slot": @0})[@"data"];
    check([info[@"buses"] count] == 32, "API exposes all instrument outputs");
    auto id = info[@"plugin"];
    rejects(@"plugin.buses.set", @{@"slot": @0, @"outputs": @[@0]});
    rejects(@"plugin.buses.set", @{@"slot": @0, @"outputs": @[@32]});
    rejects(@"plugin.buses.set", @{@"slot": @0, @"outputs": @[@1, @1]});
    rejects(@"plugin.buses.set", @{@"slot": @0, @"inputs": @[@1]});
    auto revision = session.automationRevision;
    check(![call(@"plugin.buses.set", @{@"slot": @0, @"outputs": @[@1, @31], @"dryRun": @YES}, true)[@"changed"] boolValue], "Bus dry run read-only");
    check([revision isEqual:session.automationRevision], "Dry run does not consume revision");
    call(@"plugin.buses.set", @{@"slot": @0, @"outputs": @[@1, @31]}, true);
    call(@"history.undo", @{@"domain": @"plugins"}, true);
    check(![call(@"plugin.buses.get", @{@"slot": @0})[@"data"][@"buses"][1][@"active"] boolValue], "Plugin history restores activation");
    call(@"history.redo", @{@"domain": @"plugins"}, true);
    call(@"mixer.enable", @{}, true);
    auto graph = call(@"mixer.get", @{})[@"data"]; auto bus = graph[@"buses"][1][@"id"];
    rejects(@"mixer.instrument.route", @{@"plugin": id, @"target": bus, @"output": @2});
    call(@"mixer.instrument.route", @{@"plugin": id, @"target": bus, @"output": @31}, true);
    NSData *project = [session serializedData];
    NSString *path = [NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".resonance"]];
    check([project writeToFile:path atomically:YES], "Save multi-output project");
    TrackerSession *reopened = [TrackerSession new]; check([reopened openPath:path error:&problem], "Reopen multi-output project");
    auto reply = [reopened automationMethod:@"plugin.buses.get" params:@{@"slot": @0} error:&problem];
    check([reply[@"data"][@"buses"][31][@"active"] boolValue], "Auxiliary activation persists");
    reply = [reopened automationMethod:@"mixer.get" params:@{} error:&problem];
    check([reply[@"data"][@"instruments"][0][@"output"] intValue] == 31, "Native output identity persists without renumbering");
    call(@"plugin.buses.set", @{@"slot": @0, @"outputs": @[]}, true);
    check([call(@"mixer.get", @{})[@"data"][@"instruments"] count] == 1, "Disabling retains a dormant route for reactivation");
    call(@"mixer.instrument.route", @{@"plugin": id, @"target": NSNull.null, @"output": @31}, true);
    check([call(@"mixer.get", @{})[@"data"][@"instruments"] count] == 0, "API can remove an auxiliary route");
    call(@"plugin.add", @{@"descriptor": dictionary(descriptors[0])}, true);
    NSDictionary *mixer = call(@"mixer.get", @{})[@"data"];
    NSString *effectID = mixer[@"plugins"][1][@"id"], *programBus = mixer[@"buses"][0][@"id"];
    call(@"mixer.bus.set", @{@"bus": programBus, @"inserts": @[effectID]}, true);
    NSDictionary *side = @{@"plugin": effectID, @"input": @1, @"sources": @[@{@"source": bus, @"preFader": @YES}]};
    rejects(@"mixer.sidechains.set", side);
    call(@"plugin.buses.set", @{@"slot": @1, @"inputs": @[@1]}, true);
    rejects(@"mixer.sidechains.set", @{@"plugin": effectID, @"input": @0, @"sources": @[]});
    rejects(@"mixer.sidechains.set", @{@"plugin": effectID, @"input": @1, @"sources": @[@{@"source": programBus}]});
    rejects(@"mixer.sidechains.set", @{@"plugin": effectID, @"input": @1, @"sources": @[@{@"source": bus}, @{@"source": bus}]});
    NSMutableDictionary *preview = [side mutableCopy]; preview[@"dryRun"] = @YES;
    check(![call(@"mixer.sidechains.set", preview, true)[@"changed"] boolValue], "Sidechain dry run does not mutate");
    call(@"mixer.sidechains.set", side, true);
    call(@"history.undo", @{@"domain": @"document"}, true);
    check([call(@"mixer.get", @{})[@"data"][@"sidechains"] count] == 0, "Sidechain has one document Undo");
    call(@"history.redo", @{@"domain": @"document"}, true);
    check([[session serializedData] writeToFile:path atomically:YES] && [reopened openPath:path error:&problem], "Reopen sidechain project");
    reply = [reopened automationMethod:@"mixer.get" params:@{} error:&problem];
    check([reply[@"data"][@"sidechains"][0][@"source"] isEqual:bus], "Sidechain source identity persists");
    reply = [reopened automationMethod:@"plugin.buses.get" params:@{@"slot": @1} error:&problem];
    bool activeInput = false;
    for (NSDictionary *port in reply[@"data"][@"buses"]) if ([port[@"direction"] isEqual:@"input"] && [port[@"index"] intValue] == 1) activeInput = [port[@"active"] boolValue];
    check(activeInput, "Sidechain input activation persists");
    call(@"plugin.buses.set", @{@"slot": @1, @"inputs": @[]}, true);
    check([call(@"mixer.get", @{})[@"data"][@"sidechains"] count] == 1, "Disabling sidechain input retains routing");
    call(@"mixer.sidechains.set", @{@"plugin": effectID, @"input": @1, @"sources": @[]}, true);
    check([call(@"mixer.get", @{})[@"data"][@"sidechains"] count] == 0, "Dormant sidechain can be removed");
    [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
    std::cout << "PASS multi-bus host and native graph: mono/stereo outputs through port 31, side input buffers, timed parameters, musical automation, independent faders, 44.1/48/96 kHz, 17/128/512/4096 frames, RT audit, API validation, history and persistence\n";
    return 0;
  } catch (const std::exception &e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; } }
}
