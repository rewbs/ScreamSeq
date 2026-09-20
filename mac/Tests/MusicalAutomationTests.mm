#import "../Bridge/TrackerSession.h"
#include "../Audio/AudioUnitHost.hpp"
#include "editor/TrackerDocument.hpp"
#include "editor/ArrangementTools.hpp"
#include "editor/SongTiming.hpp"
#include "soundlib/ModInstrument.h"
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
static void check(bool result, const char *message) { if (!result) throw std::runtime_error(message); }
static NSDictionary *descriptor(const PluginDescriptor &d) {
  return @{@"type": @(d.type), @"subtype": @(d.subtype), @"manufacturer": @(d.manufacturer),
    @"name": @(d.name.c_str()), @"format": @(d.format.c_str()), @"path": @(d.path.c_str()),
    @"classID": @(d.classID.c_str()), @"isInstrument": @(d.instrument)};
}
int main(int argc, char **argv) {
  @autoreleasepool {
    try {
      check(argc == 2, "Fixture path required");
      auto discovered = NativePlugin::discoverVST3(argv[1]);
      PluginState gain{discovered.at(0)}, synth{discovered.at(1)};
      gain.instanceID = "gain"; synth.instanceID = "synth"; synth.instrument = 1;
      Document doc;
      doc.transaction([](CSoundFile &song) {
        check(song.Patterns[0].Resize(4), "Resize musical test pattern");
        song.Order().assign(3, 0);
        song.Order().SetDefaultTempoInt(125); song.Order().SetDefaultSpeed(6);
      });
      auto lane = MusicalAutomationLane{0, doc.native().patterns.at(0).id, gain.instanceID, 7, true,
        {{0, .2, AutomationCurve::Step}, {37, .8, AutomationCurve::Step}, {256, .4, AutomationCurve::Linear},
         {768, .9, AutomationCurve::Smooth}}};
      doc.annotate([&](NativeSong &n) { lane.id = n.makeEntity().id; n.automation.push_back(lane); });
      check(doc.undoChangesAutomation() && !doc.undoChangesStructure(), "Automation uses document undo without rewriting the module");
      auto native = doc.native();
      doc.undo(); check(doc.native().automation.empty(), "Undo removes lane");
      doc.redo(); check(doc.native().automation == native.automation, "Redo restores stable lane identity");
      int duplicate = doc.addPattern(4, true, 0);
      check(doc.native().automation.size() == 2 && doc.native().automation[1].pattern == doc.native().patterns.at(duplicate).id &&
            doc.native().automation[1].id != lane.id, "Pattern duplicate copies automation with new identities");
      doc.undo();
      const auto revision = doc.revision;
      try { doc.annotate([](NativeSong &n) { n.automation[0].points[1].value = 2; }); check(false, "Reject unnormalized point"); }
      catch (const std::invalid_argument &) {}
      check(doc.revision == revision, "Invalid automation edit is atomic");
      std::vector<AutomationPoint> curve{{0, .2, AutomationCurve::Linear}, {256, .8, AutomationCurve::Step}};
      check(std::abs(automationValue(curve, 128) - .5) < 1e-12 && automationValue(curve, -1) == .2 && automationValue(curve, 999) == .8,
            "Interpolation and endpoint chase");
      curve[0].curve = AutomationCurve::Exponential;
      check(automationValue(curve, 128) < .3, "Exponential curve shape");
      curve[0].curve = AutomationCurve::Logarithmic;
      check(automationValue(curve, 128) > .69 && automationValue(curve, 128) < .7, "Logarithmic curve shape");
      for (uint32_t rate : {44100, 48000, 96000}) {
        auto render = [&](uint32_t block, uint32_t order = 0, bool reorder = false) {
          PluginState unused = gain; unused.instanceID = "unrelated"; unused.bypass = true;
          PluginChain chain(reorder ? std::vector<PluginState>{unused, gain} : std::vector<PluginState>{gain}, rate, true);
          Renderer renderer(doc.snapshotData(), rate, order);
          chain.attachInstruments(renderer); chain.attachMusicalAutomation(renderer, doc.native());
          uint32_t total = rate * 9 / 10;
          std::vector<float> out(total * 2);
          std::array<float, 8192> scratch{};
          for (uint32_t pos = 0; pos < total; pos += block) {
            auto count = std::min(block, total - pos);
            std::fill(out.begin() + pos * 2, out.begin() + (pos + count) * 2, .2f);
            uint64_t a, f, l;
            tracker_audit_begin();
            renderer.render(scratch.data(), count);
            bool ok = chain.process(out.data() + pos * 2, count);
            tracker_audit_end(&a, &f, &l);
            check(ok && !renderer.faulted(), "Musical automation renders");
            check(a + f + l == 0, "Musical automation must allocate, free and lock zero times in callback");
          }
          return out;
        };
        auto reference = render(128);
        for (uint32_t block : {17, 512, 4096}) {
          auto compared = render(block);
          auto mismatch = std::mismatch(reference.begin(), reference.end(), compared.begin());
          if (mismatch.first != reference.end()) {
            std::cerr << "Rate " << rate << " block " << block << " first mismatch frame " << (mismatch.first - reference.begin()) / 2
                      << ": " << *mismatch.first << " versus " << *mismatch.second << '\n';
            check(false, "Musical automation independent of callback size");
          }
        }
        check(reference == render(128, 0, true), "Automation follows plugin identity through rack reorder");
        const uint32_t rowFrames = rate * 12 / 100;
        const uint32_t knot = uint32_t(std::ceil(37.0 * rowFrames / 256));
        check(std::abs(reference[0] - .04) < 1e-6 && std::abs(reference[(knot - 1) * 2] - .04) < 1e-6 &&
              std::abs(reference[knot * 2] - .16) < 1e-6, "Subrow step occurs at the exact musical sample");
        check(std::abs(reference[rowFrames * 4 * 2] - .04) < 1e-6, "Repeated pattern restarts its automation");
        doc.annotate([](NativeSong &n) { n.automation[0].points = {{0, .2, AutomationCurve::StepNext}, {256, .8, AutomationCurve::Step}}; });
        const auto earlyStep = render(1);
        check(std::abs(earlyStep[0] - .04) < 1e-6 && std::abs(earlyStep[2] - .16) < 1e-6,
              "Reversed step changes exactly one sample after an aligned opening knot");
        check(earlyStep == render(17) && earlyStep == render(512), "Reversed step timing is independent of callback boundaries");
        doc.undo();
        doc.annotate([](NativeSong &n) { n.automation[0].points = {{0,.2,AutomationCurve::Scripted,CurveFormula("mix(start,end,t)")},{256,.8,AutomationCurve::Scripted,CurveFormula("start*(1-t)^2")}}; });
        const auto scripted=render(1);
        for(uint32_t block:{17u,128u,512u,4096u}) check(scripted==render(block),"Scripted VST ramps are bit-exact across callback sizes");
        check(std::abs(scripted[rowFrames]-0.1f)<1e-6,"Scripted interpolation reaches independent midpoint");
        check(scripted[200]!=scripted[202],"Scripted continuous parameters change per sample between grid evaluations");
        check(scripted[rowFrames*3*2]<scripted[rowFrames*2*2],"Final scripted node keeps running after the final anchor");
        doc.undo();
        auto seek = render(128, 1);
        check(std::abs(seek[0] - .04) < 1e-6 && std::abs(seek[knot * 2] - .16) < 1e-6, "Order seek chases pattern automation");
        doc.transaction([](CSoundFile &song) { auto &cell = *song.Patterns[0].GetpModCommand(1, 0); cell.command = CMD_TEMPO; cell.param = 200; });
        check(render(17) == render(4096), "Tempo-changing automation remains callback-size independent");
        doc.undo();
        doc.transaction([](CSoundFile &song) {
          auto timing = songTiming(song); timing.mode = TempoMode::Modern;
          timing.sequences[0] = {1271250, 7}; timing.rowsPerBeat = 4; timing.rowsPerMeasure = 12;
          timing.groove = normalizedGroove(std::array{1.5, .5, 1.25, .75}); applySongTiming(song, timing);
        });
        const auto grooved = render(1);
        for(auto block : {17u, 4096u}) {
          auto other=render(block);auto first=std::mismatch(grooved.begin(),grooved.end(),other.begin());
          if(first.first!=grooved.end()) std::cerr<<"Groove mismatch rate "<<rate<<" block "<<block<<" frame "<<(first.first-grooved.begin())/2<<" values "<<*first.first<<" / "<<*first.second<<'\n';
          check(grooved == other, "Fractional-tempo grooved envelopes are sample-exact across callback sizes");
        }
        const long double tick = static_cast<long double>(rate) * 60 / 127.125L / 4 / 7 * 1.5L;
        const uint32_t first = uint32_t(std::floor(tick)), second = uint32_t(std::floor(2 * tick)) - first;
        const uint32_t expectedStep = first + uint32_t(std::ceil((37.0L * 7 / 256 - 1) * second));
        check(std::abs(grooved[(expectedStep - 1) * 2] - .04) < 1e-6 && std::abs(grooved[expectedStep * 2] - .16) < 1e-6,
          "Independent uneven-row clock locates the subrow automation step at the exact sample");
        doc.undo();
      }
      {
        Document instrument;
        instrument.transaction([](CSoundFile &song) {
          song.m_nInstruments = 1; song.Instruments[1] = new ModInstrument(0);
          song.Order().SetDefaultTempoInt(125); song.Order().SetDefaultSpeed(6);
          auto &note = *song.Patterns[0].GetpModCommand(0, 0); note.note = 61; note.instr = 1;
        });
        instrument.annotate([&](NativeSong &n) {
          auto copy = lane; copy.id = n.makeEntity().id; copy.pattern = n.patterns.at(0).id; copy.plugin = synth.instanceID;
          n.automation.push_back(copy);
        });
        auto render = [&](uint32_t block, bool offline) {
          PluginChain chain({synth}, 48000, offline);
          Renderer renderer(instrument.serialize(), 48000);
          chain.attachInstruments(renderer); chain.attachMusicalAutomation(renderer, instrument.native());
          std::vector<float> out(48000);
          for (uint32_t pos = 0; pos < 24000; pos += block) {
            uint32_t count = std::min(block, 24000 - pos);
            uint64_t a, f, l; tracker_audit_begin();
            renderer.render(out.data() + pos * 2, count);
            bool ok = chain.process(out.data() + pos * 2, count);
            tracker_audit_end(&a, &f, &l);
            check(ok && a + f + l == 0, "Instrument musical automation is safe on the audio callback");
          }
          return out;
        };
        auto live = render(128, false);
        check(live == render(17, true) && live == render(4096, true), "Plugin instruments use identical musical automation live and offline");
        check(std::abs(live[100] - .04) < 1e-4 && std::abs(live[2000] - .16) < 1e-4, "Instrument envelope actually controls its audio");
      }
      {
        PluginChain conflict({gain}, 48000, true, {{0, 7, .5f, 0}});
        Renderer renderer(doc.serialize(), 48000);
        try { conflict.attachMusicalAutomation(renderer, doc.native()); check(false, "Conflicting automation clocks must be rejected"); }
        catch (const std::invalid_argument &) {}
      }
      TrackerSession *session = [TrackerSession new];
      NSError *error = nil;
      check([session addPlugin:descriptor(gain.descriptor) error:&error], "Add automation test plugin");
      auto call = [&](NSString *method, NSDictionary *params, bool write = false) -> NSDictionary * {
        NSMutableDictionary *p = [params mutableCopy];
        if (write) p[@"expectedRevision"] = session.automationRevision;
        NSDictionary *reply = [session automationMethod:method params:p error:&error];
        if (!reply) throw std::runtime_error(error.localizedDescription.UTF8String);
        return reply;
      };
      NSString *plugin = [session snapshot:0][@"nativePlugins"][0][@"instanceID"];
      NSData *baselineProject = [session serializedData];
      NSDictionary *params = @{@"pattern": @0, @"plugin": plugin, @"parameter": @7,
        @"points": @[@{@"position": @0, @"value": @0.2}, @{@"position": @768, @"value": @0.8, @"curve": @"smooth"}]};
      NSMutableDictionary *preview = [params mutableCopy]; preview[@"dryRun"] = @YES;
      check(![call(@"automation.pattern.set", preview, true)[@"changed"] boolValue], "Lane preview is read-only");
      call(@"automation.pattern.set", params, true);
      NSDictionary *savedLane = call(@"automation.pattern.get", @{@"pattern": @0})[@"data"];
      check([savedLane[@"lanes"] count] == 1 && [savedLane[@"lanes"][0][@"resolved"] boolValue], "API lane targets a resolved parameter");
      check(![call(@"automation.pattern.set", params, true)[@"changed"] boolValue], "No-op lane edit preserves revision");
      NSString *path = [NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".resonance"]];
      NSString *baselineWav = [path stringByAppendingString:@".baseline.wav"];
      NSString *automatedWav = [path stringByAppendingString:@".automated.wav"];
      check([TrackerSession exportData:baselineProject path:baselineWav error:&error], "Baseline project export");
      check([TrackerSession exportData:[session serializedData] path:automatedWav error:&error], "Musical project export");
      NSData *plain = [NSData dataWithContentsOfFile:baselineWav], *automated = [NSData dataWithContentsOfFile:automatedWav];
      check(plain.length == automated.length && plain.length > 44 + 64 * sizeof(float), "Automation export keeps song duration");
      double signal = 0, difference = 0;
      // First 32 samples hold the first grid value, .2 versus the baseline .5.
      for (size_t sample = 0; sample < 64; ++sample) {
        float before = 0, after = 0;
        std::memcpy(&before, static_cast<const char *>(plain.bytes) + 44 + sample * sizeof(float), sizeof(float));
        std::memcpy(&after, static_cast<const char *>(automated.bytes) + 44 + sample * sizeof(float), sizeof(float));
        signal += std::abs(before); difference += std::abs(after - before * .4);
      }
      check(signal > 1e-7 && difference < 1e-6, "Export decodes and applies musical envelopes instead of the baseline plugin value");
      [[NSFileManager defaultManager] removeItemAtPath:baselineWav error:nil];
      [[NSFileManager defaultManager] removeItemAtPath:automatedWav error:nil];
      check([session savePath:path error:&error] && [session openPath:path error:&error], "Musical automation project save/reopen");
      check([savedLane isEqual:call(@"automation.pattern.get", @{@"pattern": @0})[@"data"]], "Musical envelopes persist exactly");
      NSString *laneID = savedLane[@"lanes"][0][@"id"];
      for (NSString *shape in @[@"step", @"exponential", @"logarithmic"]) {
        NSMutableDictionary *shaped = [params mutableCopy];
        shaped[@"points"] = @[@{@"position":@0, @"value":@0.2, @"curve":shape}, @{@"position":@768, @"value":@0.8}];
        call(@"automation.pattern.set", shaped, true);
        auto flipped = call(@"automation.pattern.transform", @{@"lane":laneID, @"operation":@"flip-time"}, true);
        auto actual = call(@"automation.pattern.get", @{@"pattern":@0})[@"data"];
        check([flipped[@"data"][@"after"] isEqual:actual[@"lanes"][0][@"points"]], "Transform response equals saved lane");
        check([session savePath:path error:&error] && [session openPath:path error:&error], "Reversed curve project save/reopen");
        check([actual isEqual:call(@"automation.pattern.get", @{@"pattern":@0})[@"data"]], "Mirrored curves persist exactly");
      }
      call(@"automation.pattern.set", params, true);
      call(@"plugin.remove", @{@"slot": @0}, true);
      check(![call(@"automation.pattern.get", @{@"pattern": @0})[@"data"][@"lanes"][0][@"resolved"] boolValue], "Removed plugin leaves an unresolved retained lane");
      call(@"history.undo", @{@"domain": @"plugins"}, true);
      check([savedLane isEqual:call(@"automation.pattern.get", @{@"pattern": @0})[@"data"]], "Plugin undo reconnects its original lane");
      [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
      std::cout << "PASS pattern automation curves, exact knots, callback-size independence, repeats, seek, stable plugin identity, realtime audit, API preview, no-op history and project persistence\n";
      return 0;
    } catch (const std::exception &e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
  }
}
