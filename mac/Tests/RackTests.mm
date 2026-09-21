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
static void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
template<typename F> static void rejects(F action) { bool rejected = false; try { action(); } catch (const std::exception &) { rejected = true; } check(rejected, "Excess rack or adapter capacity rejects"); }
static std::unique_ptr<Document> fixture() {
  auto doc = Document::demo();
  doc->transaction([](CSoundFile &song) {
    Document::resizeChannels(song, 127); song.m_nInstruments = 11;
    for (int i = 1; i <= 11; ++i) song.Instruments[i] = new ModInstrument(0);
    for (auto &pattern : song.Patterns) if (pattern.IsValid()) for (auto &cell : pattern) cell = {};
    for (int i = 0; i < 10; ++i) { auto &cell = *song.Patterns[0].GetpModCommand(0, i); cell.note = 61; cell.instr = uint8_t(i + 1); }
  });
  doc->annotate([](NativeSong &n) {
    const auto master = n.makeEntity().id;
    for (const auto &[index, track] : n.tracks) n.mixer.buses.push_back({track.id, master, MixerBusKind::Track, "Track"});
    while (n.mixer.buses.size() < 239) n.mixer.buses.push_back({n.makeEntity().id, master, MixerBusKind::Group, "Group"});
    n.mixer.buses.push_back({master, 0, MixerBusKind::Master, "Master"});
    for (int i = 0; i < 54; ++i) n.mixer.buses[i].inserts.push_back("effect-" + std::to_string(i));
  });
  return doc;
}
static std::vector<float> render(Document &doc, const std::vector<PluginState> &states, uint32_t block) {
  Renderer renderer(doc.serialize(), 48000); PluginChain chain(states, 48000, true);
  chain.attachInstruments(renderer, &doc.native());
  size_t populated = 0; for (const auto &slot : renderer.song().m_MixPlugins) populated += slot.pMixPlugin != nullptr;
  check(populated == 250, "Ten compact instrument adapters plus 240 graph buses fit all 250 core slots");
  std::vector<float> out(24000);
  for (uint32_t pos = 0; pos < out.size() / 2; pos += block) {
    auto count = std::min<uint32_t>(block, out.size() / 2 - pos);
    tracker_audit_begin(); chain.syncTransport(renderer); renderer.render(out.data() + pos * 2, count);
    bool ok = chain.process(out.data() + pos * 2, count); uint64_t a, f, l; tracker_audit_end(&a, &f, &l);
    check(ok && !(a + f + l), "Full rack and adapter boundary renders without callback allocation/free/lock");
  }
  check(std::any_of(out.begin(), out.end(), [](float v) { return std::abs(v) > .01; }), "Compact instrument adapters receive notes and produce audio");
  return out;
}
static void api() {
  TrackerSession *session = [TrackerSession new]; NSError *error = nil;
  auto call = [&](NSString *method, NSDictionary *parameters, bool write = false) -> NSDictionary * {
    NSMutableDictionary *p = [parameters mutableCopy]; if (write) p[@"expectedRevision"] = session.automationRevision;
    NSDictionary *reply = [session automationMethod:method params:p error:&error];
    if (!reply) throw std::runtime_error(error.localizedDescription.UTF8String); return reply;
  };
  for (int i = 0; i < 64; ++i) call(@"plugin.add", @{@"descriptor": session.builtInPlugins[0]}, true);
  auto revision = session.automationRevision;
  check(![session automationMethod:@"plugin.add" params:@{@"descriptor": session.builtInPlugins[0], @"expectedRevision": revision} error:&error] && [revision isEqual:session.automationRevision], "Device 65 is rejected atomically");
  call(@"plugin.parameters.set", @{@"slot": @63, @"values": @[@{@"id": @1, @"value": @-12}]}, true);
  call(@"automation.replaceLane", @{@"slot": @63, @"id": @1, @"points": @[@{@"frame": @24000, @"value": @-18}]}, true);
  NSString *path = [NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".resonance"]];
  check([session savePath:path error:&error] && [session openPath:path error:&error], "64-device project and high-slot automation save/reopen");
  check([call(@"plugin.parameters.get", @{@"slot": @63})[@"data"][1][@"value"] floatValue] == -12, "High rack slot baseline persists");
  check([call(@"automation.get", @{})[@"data"][@"points"][0][@"slot"] intValue] == 63, "High rack automation slot persists");
  call(@"plugin.move", @{@"slot": @63, @"direction": @-1}, true);
  check([call(@"automation.get", @{})[@"data"][@"points"][0][@"slot"] intValue] == 62, "Moving high rack slot remaps absolute automation");
  call(@"history.undo", @{@"domain": @"plugins"}, true);
  check([call(@"automation.get", @{})[@"data"][@"points"][0][@"slot"] intValue] == 63, "High-slot move Undo preserves target");
  [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
}
static void capacityAPI(Document &doc, std::vector<PluginState> states) {
  // A legacy project can carry a large rack before the mixer is enabled. Use
  // public edits to hit the joint limit and cross-domain Undo conflict.
  states[0].bypass = false; states[0].instrument = 0;
  NSMutableArray *plugins = [NSMutableArray array];
  for (const auto &s : states) {
    const auto &d = s.descriptor;
    [plugins addObject:@{@"type": @(d.type), @"subtype": @(d.subtype), @"manufacturer": @(d.manufacturer),
      @"name": @(d.name.c_str()), @"format": @(d.format.c_str()), @"path": @(d.path.c_str()), @"classID": @(d.classID.c_str()),
      @"isInstrument": @(d.instrument), @"instrument": @(s.instrument), @"instanceID": @(s.instanceID.c_str()),
      @"instrumentAssignments":s.instrument ? @[@{@"instrument":@(s.instrument),@"channel":@1}] : @[], @"state": [NSData dataWithBytes:s.state.data() length:s.state.size()]}];
  }
  NSString *path = [NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".screamseq"]];
  NSString *modulePath=[path stringByAppendingString:@".mptm"];
  auto bytes=doc.serialize();[[NSData dataWithBytes:bytes.data() length:bytes.size()] writeToFile:modulePath atomically:YES];
  TrackerSession *session=[TrackerSession new];NSError *error=nil;
  check([session openPath:modulePath error:&error],"Import rack fixture module");
  [[NSFileManager defaultManager] removeItemAtPath:modulePath error:nil];
  NSMutableDictionary *root=[[NSPropertyListSerialization propertyListWithData:[session serializedData] options:0 format:nil error:nil] mutableCopy];root[@"plugins"]=plugins;
  NSData *project=[NSPropertyListSerialization dataWithPropertyList:root format:NSPropertyListBinaryFormat_v1_0 options:0 error:nil];
  check([project writeToFile:path atomically:YES]&&[session openPath:path error:&error],"Current project loads expanded instrument rack");
  auto call = [&](NSString *method, NSDictionary *params, bool reject = false) -> NSDictionary * {
    NSMutableDictionary *p = [params mutableCopy]; NSString *before = session.automationRevision; p[@"expectedRevision"] = before;
    auto reply = [session automationMethod:method params:p error:&error];
    if (reject) check(!reply && (error.code == -32602 || error.code == -32003) && [error.localizedDescription containsString:@"250"] && [before isEqual:session.automationRevision], "Joint capacity edit rejects without consuming revision/history");
    else if (!reply) throw std::runtime_error(error.localizedDescription.UTF8String);
    return reply;
  };
  call(@"mixer.enable", @{});
  for (int i = 0; i < 112; ++i) call(@"mixer.bus.add", @{@"kind": @"group"});
  call(@"document.patch", @{@"channels": @126});
  call(@"plugin.assign", @{@"slot": @0, @"instrument": @11});
  check(!session.canUndo, "Native Undo disables a pending graph incompatible with current instrument assignments");
  call(@"history.undo", @{@"domain": @"document"}, true);
  call(@"mixer.bus.add", @{@"kind": @"group"}, true);
  call(@"document.patch", @{@"channels": @127}, true);
  call(@"plugin.assign", @{@"slot": @0, @"instrument": @0});
  check(session.canUndo, "Releasing an instrument restores native Undo availability");
  call(@"history.undo", @{@"domain": @"document"});
  call(@"plugin.assign", @{@"slot": @0, @"instrument": @11}, true);
  const auto aliasRequest=@{@"plugin":@(states[0].instanceID.c_str()),@"assignments":@[@{@"instrument":@11,@"channel":@4}],@"dryRun":@YES};
  call(@"plugin.instruments.set", aliasRequest, true);
  call(@"history.undo", @{@"domain": @"plugins"}, true);
  [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
}
int main(int argc, char **argv) { @autoreleasepool { try {
  check(argc == 2, "Fixture VST3 path required"); auto doc = fixture();
  PluginState source{NativePlugin::discoverVST3(argv[1]).at(1)};
  NativePlugin quiet(source, 48000, true); quiet.parameter(7, .05); source = quiet.state();
  std::vector<PluginState> states, sources;
  for (int i = 0; i < 54; ++i) { PluginState effect{NativePlugin::builtins()[0]}; effect.instanceID = "effect-" + std::to_string(i); states.push_back(effect); }
  for (int i = 0; i < 10; ++i) { auto s = source; s.instanceID = "source-" + std::to_string(i); s.instrument = i + 1; states.push_back(s); sources.push_back(s); }
  validatePluginCapacity(states, 240);
  check(render(*doc, states, 17) == render(*doc, sources, 4096), "64 mixed devices with high-slot instruments match instrument-only reference exactly");
  auto excess = states; excess.push_back(states[0]); rejects([&] { PluginChain chain(excess, 48000); });
  auto over = source; over.instanceID = "source-10"; over.instrument = 11; states[0] = over;
  rejects([&] { validatePluginCapacity(states, 240); });
  Renderer renderer(doc->serialize(), 48000); PluginChain chain(states, 48000);
  rejects([&] { chain.attachInstruments(renderer, &doc->native()); });
  check(std::none_of(renderer.song().m_MixPlugins.begin(), renderer.song().m_MixPlugins.end(), [](const auto &s) { return s.pMixPlugin != nullptr; }), "Over-capacity playback rejects before installing any adapters");
  states[0].bypass = true; rejects([&] { validatePluginCapacity(states, 240); });
  doc->annotate([](NativeSong &n) { n.mixer.buses.erase(n.mixer.buses.end() - 2); });
  validatePluginCapacity(states, doc->native().mixer.buses.size());
  rejects([&] { validatePluginCapacity(states, doc->historyNative(false).mixer.buses.size()); });
  check(doc->native().mixer.buses.size() == 239 && doc->canUndo(), "Capacity preflight reads pending history without consuming it");
  doc->undo(); check(doc->historyNative(true).mixer.buses.size() == 239, "Redo preflight exposes the corresponding native snapshot");
  capacityAPI(*doc, states); api(); std::cout << "PASS 64-device rack: compact high-slot instruments, all 250 adapters, atomic capacity validation including cross-domain Undo, exact audio, realtime audit, high-slot automation/persistence/reorder/history\n";
  return 0;
} catch (const std::exception &e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; } } }
