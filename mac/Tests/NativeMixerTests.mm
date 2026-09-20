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
static void enable(Document &doc) {
  doc.annotate([](NativeSong &n) {
    auto master = n.makeEntity().id;
    for (const auto &[channel, track] : n.tracks) n.mixer.buses.push_back({track.id, master, MixerBusKind::Track, "Track"});
    n.mixer.buses.push_back({master, 0, MixerBusKind::Master, "Master"});
  });
}
static std::vector<float> render(Document &doc, const std::vector<PluginState> &plugins, uint32_t rate, uint32_t block,
                                 bool offline = true, uint32_t order = 0) {
  Renderer renderer(doc.serialize(), rate, order);
  PluginChain chain(plugins, rate, offline, {}, uint64_t(double(renderer.telemetry().frames) * 48000 / rate));
  chain.attachInstruments(renderer, &doc.native()); chain.attachMusicalAutomation(renderer, doc.native());
  const uint32_t total = rate * 3;
  std::vector<float> result(total * 2);
  bool ended = false;
  for (uint32_t pos = 0; pos < total; pos += block) {
    const auto count = std::min(block, total - pos);
    uint64_t a, f, l; tracker_audit_begin();
    chain.syncTransport(renderer);
    if (!ended && renderer.render(result.data() + pos * 2, count) < count) { chain.endNotes(); ended = true; }
    bool ok = chain.process(result.data() + pos * 2, count);
    tracker_audit_end(&a, &f, &l);
    check(ok && !renderer.faulted(), "Native mixer rendering succeeds");
    check(a + f + l == 0, "Native mixer performs no callback allocation, free or lock");
  }
  return result;
}
static double error(const std::vector<float> &a, const std::vector<float> &b) {
  double result = 0; check(a.size() == b.size(), "Render length matches");
  for (size_t i = 0; i < a.size(); ++i) result = std::max(result, std::abs(double(a[i]) - b[i]));
  return result;
}
int main(int argc, char **argv) {
  @autoreleasepool { try {
    check(argc == 2, "Fixture bundle path required");
    auto descriptions = NativePlugin::discoverVST3(argv[1]);
    PluginState gain{descriptions.at(0)}, synth{descriptions.at(1)}, delayed{descriptions.at(2)};
    gain.instanceID = "gain"; synth.instanceID = "synth"; delayed.instanceID = "delayed";
    synth.instrument = delayed.instrument = 1;
    for (uint32_t rate : {44100, 48000, 96000}) {
      auto doc = Document::demo();
      doc->transaction([](CSoundFile &song) {
        song.Order().assign(2, 0); song.Patterns[0].Resize(4);
        song.m_nInstruments = 4;
        for (int n = 1; n <= 4; ++n) { song.Instruments[n] = new ModInstrument(SAMPLEINDEX(n)); song.Instruments[n]->nNNA = NewNoteAction::Continue; }
      });
      auto legacy = render(*doc, {}, rate, 128); enable(*doc);
      auto routed = render(*doc, {}, rate, 128);
      std::cout << "Native mixer unity at " << rate << " Hz: " << error(legacy, routed) << '\n';
      check(error(legacy, routed) < 2e-7, "Unity mixer retains sample/NNA playback and song ending");
      for (auto block : {17u, 4096u}) check(error(routed, render(*doc, {}, rate, block)) < 2e-7, "Core graph rendering is callback-size independent");
      doc->annotate([](NativeSong &n) {
        auto group = n.makeEntity().id, master = n.mixer.buses.back().id;
        for (auto &bus : n.mixer.buses) if (bus.kind == MixerBusKind::Track) bus.output = group;
        n.mixer.buses.push_back({group, master, MixerBusKind::Group, "Group"});
      });
      check(error(routed, render(*doc, {}, rate, 17)) < 2e-7, "Group topology preserves sample output even when master is not last in stored order");
      doc->annotate([](NativeSong &n) { n.mixer.buses[0].mute = true; });
      auto muted = render(*doc, {}, rate, 128);
      doc->transaction([](CSoundFile &s) { for (auto &pattern : s.Patterns) if (pattern.IsValid()) for (ROWINDEX row = 0; row < pattern.GetNumRows(); ++row) *pattern.GetpModCommand(row, 0) = {}; });
      check(error(muted, render(*doc, {}, rate, 17)) < 2e-7, "Track mute removes only that track's samples and NNA tails");
      doc->undo(); doc->undo();
      doc->annotate([](NativeSong &n) { n.mixer.buses[0].inserts = {"gain"}; });
      auto effected = render(*doc, {gain}, rate, 128);
      check(error(effected, routed) > 1e-5 && error(effected, render(*doc, {gain}, rate, 17)) < 2e-7, "Track insert changes audio once and remains callback independent");
      doc->annotate([](NativeSong &n) {
        n.mixer.instruments = {{"synth", n.tracks.at(1).id, 0}};
        n.automation.push_back({n.makeEntity().id, n.patterns.at(0).id, "gain", 7, true,
          {{0, .2, AutomationCurve::Linear}, {768, .8, AutomationCurve::Step}}});
      });
      auto instruments = render(*doc, {gain, synth}, rate, 128, false);
      check(error(instruments, render(*doc, {gain, synth}, rate, 17)) < 2e-7 && error(instruments, render(*doc, {gain, synth}, rate, 4096)) < 2e-7,
            "Routed plugin instruments, musical effect automation and tails agree live/offline across callback sizes");
      check(error(render(*doc, {gain, synth}, rate, 17, true, 1), render(*doc, {gain, synth}, rate, 4096, true, 1)) < 2e-7,
            "Order seek initializes mixer and automation on the same timeline");
      doc->annotate([](NativeSong &n) { n.automation.clear(); n.mixer = {}; });
      auto delayedLegacy = render(*doc, {delayed}, rate, 128);
      enable(*doc);
      auto delayedMixer = render(*doc, {delayed}, rate, 17);
      check(error(delayedLegacy, delayedMixer) < 2e-7, "Graph compensates delayed instrument and samples with the same result as the legacy host");
      doc->annotate([](NativeSong &n) { for (auto &bus : n.mixer.buses) if (bus.kind == MixerBusKind::Track) bus.timingMS = 500; });
      doc->transaction([](CSoundFile &s) { s.m_nDefaultGlobalVolume = 0; });
      auto silent = render(*doc, {}, rate, 4096);
      check(std::all_of(silent.begin(), silent.end(), [](float sample) { return sample == 0; }),
            "Delayed audio remains muted by song global volume after the core ends");
    }
    TrackerSession *session = [TrackerSession new]; NSError *problem = nil;
    auto call = [&](NSString *method, NSDictionary *params, bool write = false) -> NSDictionary * {
      auto p = [params mutableCopy]; if (write) p[@"expectedRevision"] = session.automationRevision;
      auto result = [session automationMethod:method params:p error:&problem];
      if (!result) throw std::runtime_error(problem.localizedDescription.UTF8String);
      return result;
    };
    auto rejects = [&](NSString *method, NSDictionary *params) {
      NSString *revision = session.automationRevision;
      auto p = [params mutableCopy]; p[@"expectedRevision"] = revision;
      check(![session automationMethod:method params:p error:&problem], "Invalid mixer edit is rejected");
      check([revision isEqual:session.automationRevision], "Invalid edit preserves the revision");
    };
    check(![call(@"mixer.get", @{})[@"data"][@"active"] boolValue], "Existing projects retain the legacy mixer until enabled");
    check(![call(@"mixer.enable", @{@"dryRun": @YES}, true)[@"changed"] boolValue], "Mixer enable preview is read-only");
    call(@"mixer.enable", @{}, true);
    NSDictionary *initial = call(@"mixer.get", @{})[@"data"];
    NSDictionary *oldMetadata = [NSPropertyListSerialization propertyListWithData:[session serializedData] options:0 format:nil error:&problem][@"native"];
    check([oldMetadata[@"version"] intValue] == 4 && oldMetadata[@"mixer"][@"buses"][0][@"prePan"] == nil &&
      [initial[@"buses"][0][@"prePan"] doubleValue] == 0, "Neutral input balance retains older metadata while API exposes the default");
    NSString *first = initial[@"buses"][0][@"id"], *master = [initial[@"buses"] lastObject][@"id"];
    NSString *revision = session.automationRevision;
    check(![call(@"mixer.enable", @{}, true)[@"changed"] boolValue] && [revision isEqual:session.automationRevision], "Enable is idempotent");
    auto group = call(@"mixer.bus.add", @{@"kind": @"group", @"name": @"Rhythm"}, true)[@"data"][@"bus"];
    auto space = call(@"mixer.bus.add", @{@"kind": @"return", @"name": @"Space"}, true)[@"data"][@"bus"];
    call(@"mixer.bus.set", @{@"bus": first, @"output": group}, true);
    call(@"mixer.sends.set", @{@"bus": first, @"sends": @[@{@"target": space, @"gainDB": @(-6), @"preFader": @YES}]}, true);
    rejects(@"mixer.bus.set", @{@"bus": group, @"output": first});
    rejects(@"mixer.sends.set", @{@"bus": group, @"sends": @[@{@"target": group}]});
    rejects(@"mixer.bus.set", @{@"bus": first, @"pan": @2});
    rejects(@"mixer.bus.set", @{@"bus": first, @"prePan": @(-1.01)});
    rejects(@"mixer.bus.set", @{@"bus": first, @"prePan": @YES});
    rejects(@"mixer.bus.set", @{@"bus": first, @"gainDB": @YES});
    rejects(@"mixer.bus.remove", @{@"bus": master});
    rejects(@"mixer.bus.set", @{@"bus": first, @"inserts": @[@"missing-plugin"]});
    rejects(@"mixer.bus.set", @{@"bus": first, @"output": master, @"preview": @YES});
    revision = session.automationRevision;
    call(@"mixer.bus.set", @{@"bus": first, @"gainDB": @(-12), @"prePan": @(-0.5), @"preview": @YES}, true);
    check([revision isEqual:session.automationRevision] && [call(@"mixer.get", @{})[@"data"][@"buses"][0][@"gainDB"] doubleValue] == 0,
          "Live control preview does not mutate song or consume undo");
    check([call(@"mixer.get", @{})[@"data"][@"buses"][0][@"prePan"] doubleValue] == 0, "Input balance preview does not persist");
    call(@"mixer.bus.set", @{@"bus": first, @"gainDB": @(-12), @"prePan": @(-0.5), @"pan": @0.25, @"width": @0.75, @"mute": @YES}, true);
    call(@"history.undo", @{@"domain": @"document"}, true);
    check([call(@"mixer.get", @{})[@"data"][@"buses"][0][@"gainDB"] doubleValue] == 0, "One undo restores all controls in a gesture");
    check([call(@"mixer.get", @{})[@"data"][@"buses"][0][@"prePan"] doubleValue] == 0, "Input balance shares the gesture's document Undo");
    call(@"history.redo", @{@"domain": @"document"}, true);
    NSString *path = [NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".resonance"]];
    NSDictionary *saved = call(@"mixer.get", @{})[@"data"];
    check([session savePath:path error:&problem], problem.localizedDescription.UTF8String ?: "Mixer project saves");
    NSMutableDictionary *encoded = [NSPropertyListSerialization propertyListWithData:[NSData dataWithContentsOfFile:path] options:NSPropertyListMutableContainers format:nil error:&problem];
    check([encoded[@"native"][@"version"] intValue] == 6 && [encoded[@"native"][@"mixer"][@"buses"][0][@"prePan"] doubleValue] == -.5,
      "Non-neutral input balance requires native metadata v6");
    encoded[@"native"][@"version"] = @5;
    NSString *badPath = [path stringByAppendingString:@".invalid.resonance"];
    [[NSPropertyListSerialization dataWithPropertyList:encoded format:NSPropertyListBinaryFormat_v1_0 options:0 error:&problem] writeToFile:badPath atomically:YES];
    revision = session.automationRevision;
    check(![session openPath:badPath error:&problem] && [revision isEqual:session.automationRevision], "Legacy metadata with input balance is rejected without replacing the song");
    encoded[@"native"][@"version"] = @6; [encoded[@"native"][@"mixer"][@"buses"][0] removeObjectForKey:@"prePan"];
    [[NSPropertyListSerialization dataWithPropertyList:encoded format:NSPropertyListBinaryFormat_v1_0 options:0 error:&problem] writeToFile:badPath atomically:YES];
    check(![session openPath:badPath error:&problem] && [revision isEqual:session.automationRevision], "Version 6 requires an explicit valid input balance on every bus");
    [[NSFileManager defaultManager] removeItemAtPath:badPath error:nil];
    check([session openPath:path error:&problem], "Mixer project reopens");
    auto loaded = call(@"mixer.get", @{})[@"data"];
    check([saved[@"buses"] isEqual:loaded[@"buses"]] && [saved[@"instruments"] isEqual:loaded[@"instruments"]], "Routing and control values persist exactly");
    call(@"mixer.bus.remove", @{@"bus": group}, true);
    check([call(@"mixer.get", @{})[@"data"][@"buses"][0][@"output"] isEqual:master], "Deleting group reconnects its child to its output");
    call(@"history.undo", @{@"domain": @"document"}, true);
    check([call(@"mixer.get", @{})[@"data"][@"buses"] isEqual:loaded[@"buses"]], "Undo restores group identity and routing");
    call(@"mixer.enable", @{@"enabled": @NO}, true);
    NSData *legacyProject = [session serializedData];
    call(@"mixer.enable", @{}, true);
    NSData *plainProject = [session serializedData];
    NSString *newMaster = [call(@"mixer.get", @{})[@"data"][@"buses"] lastObject][@"id"];
    call(@"mixer.bus.set", @{@"bus": newMaster, @"gainDB": @(-6), @"prePan": @(-0.5)}, true);
    NSString *plainPath = [path stringByAppendingString:@".plain.wav"], *mixedPath = [path stringByAppendingString:@".mixed.wav"],
      *legacyPath = [path stringByAppendingString:@".legacy.wav"];
    check([TrackerSession exportData:legacyProject path:legacyPath error:&problem], "Legacy sample-mode project exports");
    check([TrackerSession exportData:plainProject path:plainPath error:&problem], "Baseline project exports");
    check([TrackerSession exportData:[session serializedData] path:mixedPath error:&problem], "Persisted mixer project exports");
    NSData *plainAudio = [NSData dataWithContentsOfFile:plainPath], *mixedAudio = [NSData dataWithContentsOfFile:mixedPath],
      *legacyAudio = [NSData dataWithContentsOfFile:legacyPath];
    check(plainAudio.length == mixedAudio.length && plainAudio.length > 44, "Mixer export preserves song duration");
    check(legacyAudio.length == plainAudio.length, "Native and legacy sample-mode export durations agree");
    double signal = 0, maximum = 0, legacyDifference = 0;
    for (size_t byte = 44; byte + sizeof(float) <= plainAudio.length; byte += sizeof(float)) {
      float a = 0, b = 0, legacy = 0;
      std::memcpy(&a, static_cast<const char *>(plainAudio.bytes) + byte, sizeof(float));
      std::memcpy(&b, static_cast<const char *>(mixedAudio.bytes) + byte, sizeof(float));
      std::memcpy(&legacy, static_cast<const char *>(legacyAudio.bytes) + byte, sizeof(float));
      legacyDifference = std::max(legacyDifference, std::abs(double(a) - legacy));
      const double balance = ((byte - 44) / sizeof(float)) % 2 ? .5 : 1;
      signal += std::abs(a); maximum = std::max(maximum, std::abs(b - a * std::pow(10.0, -.3) * balance));
    }
    std::cout << "Mixer WAV fader check: signal " << signal << ", maximum difference " << maximum << ", legacy sample-mode difference " << legacyDifference << '\n';
    check(signal > 1 && maximum < 2e-7, "Actual WAV export decodes the saved graph and applies independent input balance and master fader");
    // Separately decaying integer channel click-removal offsets cannot be
    // bit-identical to decaying their already-summed integer offset. Retain a
    // strict sub -114 dB bound for that sample-mode routing difference.
    check(legacyDifference < 2e-6, "Sample-mode routing retains the legacy signal within bounded click-removal rounding");
    [[NSFileManager defaultManager] removeItemAtPath:plainPath error:nil];
    [[NSFileManager defaultManager] removeItemAtPath:mixedPath error:nil];
    [[NSFileManager defaultManager] removeItemAtPath:legacyPath error:nil];
    [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
    std::cout << "PASS native mixer: sample/NNA fidelity, groups, track effects, instruments, automation, seeks, PDC, tails, realtime safety, API validation, undo and persistence\n";
    return 0;
  } catch (const std::exception &e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; } }
}
