#import "../Bridge/TrackerSession.h"
#include "../Audio/AudioUnitHost.hpp"
#include <cmath>
#include <iostream>
using namespace Tracker;
static void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
static NSDictionary *describe(const PluginDescriptor &d) {
  return @{@"format": @(d.format.c_str()), @"name": @(d.name.c_str()), @"type": @(d.type), @"subtype": @(d.subtype),
    @"manufacturer": @(d.manufacturer), @"path": @(d.path.c_str()), @"classID": @(d.classID.c_str()), @"isInstrument": @(d.instrument)};
}
int main(int argc, const char **argv) {
  @autoreleasepool {
    NSString *folder = [NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
    try {
      check(argc == 2, "Pass local VST3 fixture path");
      check([NSFileManager.defaultManager createDirectoryAtPath:folder withIntermediateDirectories:NO attributes:nil error:nil], "Create test directory");
      TrackerSession *session = [TrackerSession new]; NSError *error = nil;
      auto call = [&](NSString *method, NSDictionary *params, bool write = false) -> NSDictionary * {
        NSMutableDictionary *p = [params mutableCopy]; if (write) p[@"expectedRevision"] = session.automationRevision;
        auto reply = [session automationMethod:method params:p error:&error];
        if (!reply) throw std::runtime_error(error.localizedDescription.UTF8String);
        check([NSJSONSerialization isValidJSONObject:reply], "Preset reply is valid JSON"); return reply;
      };
      auto reject = [&](NSString *method, NSDictionary *params, int code) {
        NSString *revision = session.automationRevision;
        NSMutableDictionary *p = [params mutableCopy]; if (!p[@"expectedRevision"]) p[@"expectedRevision"] = revision;
        check(![session automationMethod:method params:p error:&error] && error.code == code && [revision isEqual:session.automationRevision], "Rejected preset edit preserves song and returns expected error");
      };
      const auto fixture = NativePlugin::discoverVST3(argv[1]);
      NSArray *descriptors = @[[session builtInPlugins][0], describe(fixture.at(0)), describe(fixture.at(1)),
        @{@"format": @"AU", @"name": @"Apple low pass", @"type": @(kAudioUnitType_Effect), @"subtype": @(kAudioUnitSubType_LowPassFilter),
          @"manufacturer": @(kAudioUnitManufacturer_Apple), @"path": @"", @"classID": @"", @"isInstrument": @NO}];
      for (NSUInteger index = 0; index < descriptors.count; ++index) {
        [session newSong:YES]; call(@"plugin.add", @{@"descriptor": descriptors[index]}, true);
        NSString *identity = [session snapshot:0][@"nativePlugins"][0][@"instanceID"];
        const int parameter = index == 0 ? 1 : index == 3 ? 0 : 7;
        const double desired = index == 0 ? -7 : index == 3 ? 777 : .37;
        const double other = index == 0 ? -21 : index == 3 ? 2345 : .81;
        auto value = [&]() {
          for (NSDictionary *p in call(@"plugin.parameters.get", @{@"slot": @0})[@"data"])
            if ([p[@"id"] intValue] == parameter) return [p[@"value"] doubleValue];
          throw std::runtime_error("Missing fixture parameter");
        };
        check([session pluginParameter:0 identifier:parameter value:desired record:NO error:&error], "Set pending native manual parameter");
        NSString *path = [folder stringByAppendingPathComponent:[NSString stringWithFormat:@"preset-%lu.resonance-preset", (unsigned long)index]];
        NSString *revision = session.automationRevision;
        NSDictionary *save = @{@"plugin": identity, @"path": path, @"name": @"Saved tone"};
        NSMutableDictionary *drySave = [save mutableCopy]; drySave[@"dryRun"] = @YES;
        check(![call(@"plugin.preset.save", drySave, true)[@"data"][@"written"] boolValue] && ![NSFileManager.defaultManager fileExistsAtPath:path], "Save preview writes no file");
        auto saved = call(@"plugin.preset.save", save, true);
        check(![saved[@"changed"] boolValue] && [revision isEqual:session.automationRevision] && !session.playing, "Saving manual baseline preserves revision and transport");
        auto inspected = call(@"plugin.preset.inspect", @{@"path": path})[@"data"];
        check([inspected[@"presetRevision"] isEqual:saved[@"data"][@"presetRevision"]], "Inspection reads saved content revision");
        reject(@"plugin.preset.save", save, -32602);
        call(@"plugin.parameters.set", @{@"slot": @0, @"values": @[@{@"id": @(parameter), @"value": @(other)}]}, true);
        call(@"plugin.bypass", @{@"slot": @0, @"bypass": @YES}, true);
        if (index == 2) {
          auto instrument = call(@"instrument.create", @{}, true)[@"data"][@"instrument"];
          call(@"plugin.assign", @{@"slot": @0, @"instrument": instrument}, true);
          call(@"plugin.buses.set", @{@"slot": @0, @"outputs": @[@1]}, true);
        }
        call(@"automation.pattern.set", @{@"pattern": @0, @"plugin": identity, @"parameter": @(parameter),
          @"points": @[@{@"position": @0, @"value": @0.4}]}, true);
        auto envelope = call(@"automation.pattern.get", @{@"pattern": @0})[@"data"];
        NSDictionary *beforePlugin = [session snapshot:0][@"nativePlugins"][0];
        auto beforePorts = call(@"plugin.buses.get", @{@"slot": @0})[@"data"];
        auto touched = call(@"automation.target.get", @{})[@"data"][@"token"];
        NSMutableDictionary *load = [@{@"plugin": identity, @"path": path, @"expectedPresetRevision": inspected[@"presetRevision"], @"dryRun": @YES} mutableCopy];
        check(![call(@"plugin.preset.load", load, true)[@"changed"] boolValue] && std::abs(value() - other) < .001, "Load preview leaves live parameter unchanged");
        load[@"dryRun"] = @NO;
        call(@"plugin.preset.load", load, true);
        check(std::abs(value() - desired) < .001 && [call(@"automation.target.get", @{})[@"data"][@"token"] isEqual:touched], "Preset restores manual baseline without learning an automation gesture");
        NSDictionary *afterPlugin = [session snapshot:0][@"nativePlugins"][0];
        for (NSString *key in @[@"instanceID", @"instrument", @"bypass"])
          check([beforePlugin[key] isEqual:afterPlugin[key]], "Preset load preserves identity, instrument assignment and bypass");
        check([beforePorts isEqual:call(@"plugin.buses.get", @{@"slot": @0})[@"data"]] &&
          [envelope isEqual:call(@"automation.pattern.get", @{@"pattern": @0})[@"data"]], "Preset leaves bus activation and pattern automation intact");
        call(@"history.undo", @{@"domain": @"plugins"}, true);
        check(std::abs(value() - other) < .001, "One plugin Undo restores the state before loading");
        call(@"history.redo", @{@"domain": @"plugins"}, true);
        check(std::abs(value() - desired) < .001, "Redo restores loaded preset");
        auto stale = [load mutableCopy]; stale[@"expectedRevision"] = revision;
        reject(@"plugin.preset.load", stale, -32001);
        stale = [load mutableCopy]; stale[@"expectedPresetRevision"] = @"preset:outdated";
        reject(@"plugin.preset.load", stale, -32001);
        // Target remains the original instance after another identical plugin is
        // inserted and the first one moves to a different rack slot.
        call(@"plugin.add", @{@"descriptor": descriptors[index]}, true);
        call(@"plugin.parameters.set", @{@"slot": @0, @"values": @[@{@"id": @(parameter), @"value": @(other)}]}, true);
        call(@"plugin.move", @{@"slot": @0, @"direction": @1}, true);
        auto neighbor = call(@"plugin.parameters.get", @{@"slot": @0})[@"data"];
        call(@"plugin.preset.load", load, true);
        check([[session snapshot:0][@"nativePlugins"][1][@"instanceID"] isEqual:identity], "Preset resolves stable identity after rack reorder");
        bool restored = false;
        for (NSDictionary *p in call(@"plugin.parameters.get", @{@"slot": @1})[@"data"])
          if ([p[@"id"] intValue] == parameter) restored = std::abs([p[@"value"] doubleValue] - desired) < .001;
        check(restored && [neighbor isEqual:call(@"plugin.parameters.get", @{@"slot": @0})[@"data"]], "Moved target receives preset while its same-type neighbor stays unchanged");
        call(@"plugin.remove", @{@"slot": @1}, true);
        reject(@"plugin.preset.load", load, -32602);
        [session newSong:YES]; call(@"plugin.add", @{@"descriptor": [session builtInPlugins][index == 0 ? 1 : 0]}, true);
        load[@"plugin"] = [session snapshot:0][@"nativePlugins"][0][@"instanceID"];
        reject(@"plugin.preset.load", load, -32602);
        check(!session.playing, "Preset tests never start audio output");
      }
      [NSFileManager.defaultManager removeItemAtPath:folder error:nil];
      std::cout << "PASS preset session: built-in/AU/VST3 effects/instruments, manual baseline, dry runs, song/file revisions, stable identity, type rejection, plugin Undo/Redo and no touch learning\n";
      return 0;
    } catch (const std::exception &e) {
      [NSFileManager.defaultManager removeItemAtPath:folder error:nil];
      std::cerr << "FAIL " << e.what() << '\n'; return 1;
    }
  }
}
