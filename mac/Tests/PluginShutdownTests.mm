#import "../Bridge/TrackerSession.h"
#include "../Audio/AudioUnitHost.hpp"
#import <AppKit/AppKit.h>
#include <dlfcn.h>
#include <iostream>

using namespace Tracker;
static void check(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
int main(int argc, char **argv) {
  @autoreleasepool {
    try {
      check(argc >= 2, "Fixture bundle required");
      const bool ui = argc > 2 && std::string(argv[2]) == "--ui";
      NSApplication.sharedApplication.activationPolicy = NSApplicationActivationPolicyProhibited;
      auto path = [@(argv[1]) stringByAppendingPathComponent:@"Contents/MacOS/ResonanceFixture"];
      void *module = dlopen(path.UTF8String, RTLD_NOW | RTLD_LOCAL);
      check(module, "Open test fixture diagnostics");
      auto counts = reinterpret_cast<int (*)(int)>(dlsym(module, "ResonanceFixtureLifecycle"));
      check(counts, "Lifecycle diagnostics export");
      auto descriptor = NativePlugin::discoverVST3(argv[1]).at(0);
      NSDictionary *plugin = @{@"format": @"VST3", @"path": @(descriptor.path.c_str()),
        @"classID": @(descriptor.classID.c_str()), @"name": @(descriptor.name.c_str()),
        @"type": @0, @"subtype": @0, @"manufacturer": @0, @"isInstrument": @NO};
      for (int cycle = 0; cycle < 3; ++cycle) {
        // Deliberately keep the session alive across shutdown, just like the
        // application controller that survives until NSApplication calls exit().
        __attribute__((objc_precise_lifetime)) TrackerSession *session = [TrackerSession new];
        NSError *error = nil;
        for (int slot = 0; slot < 2; ++slot)
          if (![session addPlugin:plugin error:&error]) throw std::runtime_error(error.localizedDescription.UTF8String);
        check(counts(0) == 2 && counts(2) == 2, "Two retained rack instances");
        if (ui) {
          check([session showPluginEditor:0 error:&error], "Open rack editor");
          auto call = [&](NSString *method, NSDictionary *parameters) {
            NSMutableDictionary *p = [parameters mutableCopy]; p[@"expectedRevision"] = session.automationRevision;
            auto result = [session automationMethod:method params:p error:&error];
            if (!result) throw std::runtime_error(error.localizedDescription.UTF8String);
            return result[@"data"];
          };
          NSString *graph = call(@"graph.create", @{@"name": @"Shutdown graph"})[@"graph"];
          NSString *node = call(@"graph.node.add", @{@"graph": graph, @"kind": @"plugin", @"slot": @0})[@"node"];
          call(@"graph.plugin.editor.open", @{@"graph": graph, @"node": node});
          check(counts(0) == 3 && counts(1) == 2, "Rack and graph recipe editors coexist");
        }
        [session stop];
        check(counts(0) >= 2, "Stopping transport intentionally retains processors");
        [session shutdown];
        check(counts(0) == 0 && counts(1) == 0 && counts(2) == 0,
              "Shutdown must release rack, graph recipe editor, views and module entries before session deallocation");
        [session shutdown];
        check(counts(3) == 0, "Balanced, ordered main-thread shutdown, including repeated calls");
      }
      dlclose(module);
      std::cout << "PASS retained-session shutdown: three cycles, two rack processors, balanced VST3 teardown"
                << (ui ? ", open rack and graph editors" : "") << '\n';
      return 0;
    } catch (const std::exception &e) {
      std::cerr << "FAIL " << e.what() << '\n';
      return 1;
    }
  }
}
