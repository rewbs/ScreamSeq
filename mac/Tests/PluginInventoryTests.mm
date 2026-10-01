#include "../Plugins/PluginInventory.hpp"
#include <iostream>
#include <stdexcept>
using namespace Tracker;
static void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
int main() {
  @autoreleasepool {
    NSString *directory = [NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
    NSString *path = [directory stringByAppendingPathComponent:@"plugins.json"];
    try {
      NSArray *first = @[ @{
        @"type" : @1635085685,
        @"subtype" : @1,
        @"manufacturer" : @2,
        @"name" : @"Test AU",
        @"format" : @"AU",
        @"isInstrument" : @YES
      } ];
      NSArray *next = [first arrayByAddingObject:@{
        @"type" : @0,
        @"subtype" : @0,
        @"manufacturer" : @0,
        @"name" : @"Test VST3",
        @"format" : @"VST3",
        @"isInstrument" : @NO,
        @"path" : @"/test/plugin.vst3",
        @"classID" : @"0123456789ABCDEF0123456789ABCDEF"
      }];
      int scans = 0;
      auto scan = [&]() -> NSArray<NSDictionary *> * {
        ++scans;
        return scans == 1 ? first : next;
      };
      PluginInventory inventory(path);
      check([inventory.load(false, scan) isEqual:first] && scans == 1, "Cold cache must scan");
      check([inventory.load(false, scan) isEqual:first] && scans == 1, "Memory cache must not scan");
      PluginInventory restarted(path);
      check([restarted.load(false, scan) isEqual:first] && scans == 1, "Persistent cache must survive restart");
      check([restarted.load(true, scan) isEqual:next] && scans == 2, "Rescan must replace inventory");
      NSData *good = [NSData dataWithContentsOfFile:path];
      try {
        restarted.load(true, []() -> NSArray<NSDictionary *> * { throw std::runtime_error("scanner failed"); });
        throw std::logic_error("Failed scan accepted");
      } catch (const std::runtime_error &) {
      }
      check([restarted.load(false, scan) isEqual:next] && scans == 2, "Failed rescan must retain memory cache");
      check([[NSData dataWithContentsOfFile:path] isEqual:good], "Failed rescan must retain disk cache");
      NSMutableDictionary *root = [[NSJSONSerialization JSONObjectWithData:good options:0 error:nil] mutableCopy];
      for (NSString *key in @[ @"version", @"architecture", @"plugins" ]) {
        NSMutableDictionary *bad = [root mutableCopy];
        bad[key] = @"bad";
        [[NSJSONSerialization dataWithJSONObject:bad options:0 error:nil] writeToFile:path atomically:YES];
        PluginInventory invalid(path);
        auto before = scans;
        check([invalid.load(false, scan) isEqual:next] && scans == before + 1, "Invalid cache must rescan");
      }
      [@"{incomplete" writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:nil];
      PluginInventory broken(path);
      auto before = scans;
      check([broken.load(false, scan) isEqual:next] && scans == before + 1, "Truncated cache must rescan");
      try {
        broken.load(true, []() -> NSArray<NSDictionary *> * { return @[ @{@"name" : @"malformed"} ]; });
        throw std::logic_error("Malformed scan accepted");
      } catch (const std::runtime_error &) {
      }
      check([broken.load(false, scan) isEqual:next], "Malformed scan must not replace good cache");
      // Even empty inventories are valid cached results.
      PluginInventory empty([directory stringByAppendingPathComponent:@"empty.json"]);
      auto noPlugins = [&]() -> NSArray<NSDictionary *> * {
        ++scans;
        return @[];
      };
      empty.load(false, noPlugins);
      before = scans;
      check(empty.load(false, noPlugins).count == 0 && scans == before, "Empty inventory must cache");
      PluginInventory unwritable([path stringByAppendingPathComponent:@"not-a-directory.json"]);
      check([unwritable.load(false, scan) isEqual:next], "Cache write failure must not block discovery");
      before = scans;
      unwritable.load(false, scan);
      check(scans == before, "Cache write failure must retain memory cache");
      [[NSFileManager defaultManager] removeItemAtPath:directory error:nil];
      std::cout << "PASS plugin cache: cold/warm/restart, explicit rescan, empty inventory, invalid/truncated cache, "
                   "failed rescan preservation, unwritable cache\n";
      return 0;
    } catch (const std::exception &e) {
      [[NSFileManager defaultManager] removeItemAtPath:directory error:nil];
      std::cerr << "FAIL: " << e.what() << '\n';
      return 1;
    }
  }
}
