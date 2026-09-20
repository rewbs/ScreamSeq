#import "../Bridge/TrackerSession.h"
#include <cmath>
#include <dlfcn.h>
#include <iostream>
#include <limits>
#include <stdexcept>

static void check(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
int main(int argc, const char **argv) {
  @autoreleasepool {
    try {
      check(argc == 2, "Pass the local VST3 fixture path");
      TrackerSession *session = [TrackerSession new];
      auto call = [&](NSString *method, NSDictionary *params, bool write = false) -> NSDictionary * {
        NSMutableDictionary *input = [params mutableCopy];
        if (write) input[@"expectedRevision"] = session.automationRevision;
        NSError *error = nil;
        NSDictionary *result = [session automationMethod:method params:input error:&error];
        if (!result) throw std::runtime_error(error.localizedDescription.UTF8String);
        check([NSJSONSerialization isValidJSONObject:result], "JSON serializable response");
        return result;
      };
      auto target = [&]() -> NSDictionary * { return call(@"automation.target.get", @{})[@"data"]; };
      auto initial = target();
      check(initial[@"target"] == NSNull.null, "New document has no learned parameter");
      NSDictionary *builtin = [session builtInPlugins][0];
      call(@"plugin.add", @{@"descriptor": builtin}, true);
      check([target() isEqual:initial], "Adding a plugin does not learn its initial values");
      NSString *firstID = [session snapshot:0][@"nativePlugins"][0][@"instanceID"];
      NSError *error = nil;
      check([session pluginParameter:0 identifier:1 value:-12 record:NO error:&error], "Accept native parameter gesture");
      NSDictionary *native = target();
      check([native[@"target"][@"source"] isEqual:@"native-control"] && [native[@"target"][@"plugin"] isEqual:firstID] &&
        [native[@"target"][@"parameter"] intValue] == 1 && [native[@"target"][@"available"] boolValue] &&
        std::abs([native[@"target"][@"value"] doubleValue] + 12) < 1e-6, "Native gesture reports stable identity and current value");
      NSString *revision = session.automationRevision;
      auto before = call(@"document.get", @{});
      check([target() isEqual:native] && [session.automationRevision isEqual:revision] && [before isEqual:call(@"document.get", @{})],
        "Read preserves document/history/revision/transport and token");
      check(![session pluginParameter:-1 identifier:1 value:0 record:NO error:&error] &&
        ![session pluginParameter:0 identifier:1 value:std::numeric_limits<double>::quiet_NaN() record:NO error:&error] &&
        [target() isEqual:native], "Rejected native edits cannot replace target");
      call(@"plugin.add", @{@"descriptor": builtin}, true);
      call(@"plugin.move", @{@"slot": @0, @"direction": @1}, true);
      check([target()[@"target"][@"slot"] intValue] == 1 && [target()[@"target"][@"plugin"] isEqual:firstID] &&
        [target()[@"token"] isEqual:native[@"token"]], "Reordering retains original plugin and touch token");
      call(@"plugin.remove", @{@"slot": @1}, true);
      check(![target()[@"target"][@"available"] boolValue] && target()[@"target"][@"slot"] == NSNull.null,
        "Removal reports unresolved identity instead of targeting another instance");
      call(@"history.undo", @{@"domain": @"plugins"}, true);
      check([target()[@"target"][@"available"] boolValue] && [target()[@"token"] isEqual:native[@"token"]], "Undo restores resolution without a new gesture");
      call(@"plugin.parameters.set", @{@"slot": @1, @"values": @[@{@"id": @1, @"value": @-9}, @{@"id": @3, @"value": @1}]}, true);
      NSDictionary *api = target();
      check([api[@"target"][@"source"] isEqual:@"api"] && [api[@"target"][@"parameter"] intValue] == 3 &&
        ![api[@"token"] isEqual:native[@"token"]], "API batch learns its last accepted parameter");
      NSString *state = call(@"plugin.state.get", @{@"slot": @1})[@"data"][@"data"];
      call(@"plugin.state.set", @{@"slot": @1, @"data": state}, true);
      check([target()[@"token"] isEqual:api[@"token"]], "State loading never learns");
      call(@"automation.pattern.set", @{@"pattern": @0, @"plugin": firstID, @"parameter": @1,
        @"points": @[@{@"position": @0, @"value": @0.5}]}, true);
      check([target()[@"token"] isEqual:api[@"token"]], "Writing envelopes never learns");
      NSString *path = [NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".resonance"]];
      check([session savePath:path error:&error], "Save learned project");
      check([target()[@"token"] isEqual:api[@"token"]], "Saving does not learn");
      check([session openPath:path error:&error] && target()[@"target"] == NSNull.null, "Touch state is session-local, reset on reopening");
      [NSFileManager.defaultManager removeItemAtPath:path error:nil];
      [session newSong:YES];
      NSString *fixture = @(argv[1]);
      call(@"plugin.add", @{@"descriptor": @{@"format": @"VST3", @"type": @0, @"subtype": @0, @"manufacturer": @0,
        @"name": @"Windowless gesture fixture", @"path": fixture, @"classID": @"5245534F4E414E434546464543540001", @"isInstrument": @NO}}, true);
      void *bundle = dlopen([fixture stringByAppendingPathComponent:@"Contents/MacOS/ResonanceFixture"].UTF8String, RTLD_NOW | RTLD_LOCAL);
      check(bundle != nullptr, "Load local fixture entry point");
      auto gesture = reinterpret_cast<int (*)(double)>(dlsym(bundle, "ResonanceFixtureGesture"));
      check(gesture && gesture(0.72) == 1, "Emit real plugin-owned edit without creating a window");
      check([session collectPluginEdits:NO error:&error] == 1, "Drain custom interface edit");
      auto custom = target();
      check([custom[@"target"][@"source"] isEqual:@"plugin-editor"] && [custom[@"target"][@"parameter"] intValue] == 7 &&
        std::abs([custom[@"target"][@"value"] doubleValue] - 0.72) < 1e-6, "VST3 callback learns same current parameter as native controls");
      check([session collectPluginEdits:NO error:&error] == 0 && [custom isEqual:target()], "Empty polls do not relearn");
      [session newSong:YES];
      check(target()[@"target"] == NSNull.null && gesture(0.2) == 0, "New document clears target and unregisters fixture callbacks");
      dlclose(bundle);
      check(!session.playing, "Tests never start a physical audio device");
      std::cout << "PASS automation target: native/API/VST3 gestures, identity across rack/history, read isolation, session reset; no windows or audio output\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
  }
}
