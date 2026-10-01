#include "../Plugins/PluginPreset.hpp"
#include "../Bridge/AutomationValidation.hpp"
#include <fcntl.h>
#include <functional>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
using namespace Tracker;
static void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
static void rejects(const std::function<void()> &run, const char *message) {
  try { run(); } catch (const std::exception &) { return; }
  throw std::runtime_error(message);
}
int main() {
  @autoreleasepool {
    NSString *folder = [NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
    try {
      check([NSFileManager.defaultManager createDirectoryAtPath:folder withIntermediateDirectories:NO attributes:nil error:nil], "Create private test directory");
      NSString *path = [folder stringByAppendingPathComponent:@"Warm pad.screamseq-preset"];
      NSDictionary *plugin = @{@"format": @"VST3", @"name": @"Test synth", @"type": @0, @"subtype": @0, @"manufacturer": @0,
        @"path": @"/original/Test.vst3", @"classID": @"0123456789ABCDEF0123456789ABCDEF", @"isInstrument": @YES};
      NSData *state = [NSData dataWithBytes:"\0\1\2\377" length:4];
      auto preview = PluginPreset::write(path, plugin, state, @"Warm 🎹", false, true);
      check(![preview[@"written"] boolValue] && ![NSFileManager.defaultManager fileExistsAtPath:path], "Preview creates no preset file");
      auto saved = PluginPreset::write(path, plugin, state, @"Warm 🎹", false, false);
      auto loaded = PluginPreset::read(path);
      check([loaded[@"state"] isEqual:state] && [loaded[@"name"] isEqual:@"Warm 🎹"] && [loaded[@"presetRevision"] isEqual:saved[@"presetRevision"]], "Preset bytes, Unicode name and content revision roundtrip");
      check(PluginPreset::summary(loaded)[@"state"] == nil && [PluginPreset::summary(loaded)[@"stateBytes"] intValue] == 4, "Summary never exposes opaque plugin bytes");
      struct stat info{}; check(stat(path.fileSystemRepresentation, &info) == 0 && (info.st_mode & 0777) == 0600, "Published preset is private to the account");
      NSData *original = [NSData dataWithContentsOfFile:path];
      NSString *legacyPath = [folder stringByAppendingPathComponent:@"Legacy.resonance-preset"];
      [original writeToFile:legacyPath atomically:YES];
      check([PluginPreset::read(legacyPath)[@"state"] isEqual:state], "Legacy preset extension remains compatible with ScreamSeq");
      rejects([&] { PluginPreset::write(path, plugin, [NSData data], @"Changed", false, false); }, "Refuse implicit replacement");
      rejects([&] { PluginPreset::write(path, plugin, [NSData data], @"Changed", false, true); }, "Preview enforces replacement policy too");
      check([[NSData dataWithContentsOfFile:path] isEqual:original], "Rejected replacement preserves the complete destination");
      PluginPreset::write(path, plugin, [NSData data], @"Changed", true, false);
      check(![PluginPreset::read(path)[@"presetRevision"] isEqual:loaded[@"presetRevision"]], "Changed file gets a different content revision");
      NSMutableDictionary *relocated = [plugin mutableCopy]; relocated[@"path"] = @"/new/Test.vst3"; relocated[@"name"] = @"Renamed synth";
      relocated[@"classID"] = [plugin[@"classID"] lowercaseString];
      check(PluginPreset::matches(plugin, relocated), "VST3 presets follow class identity across installation paths and names");
      relocated[@"classID"] = @"1123456789ABCDEF0123456789ABCDEF";
      check(!PluginPreset::matches(plugin, relocated), "Different VST3 class cannot receive a preset");
      relocated = [plugin mutableCopy]; relocated[@"isInstrument"] = @NO;
      check(!PluginPreset::matches(plugin, relocated), "Instrument/effect identity remains distinct");
      NSMutableDictionary *au = [plugin mutableCopy]; au[@"format"] = @"AU"; au[@"type"] = @1; au[@"subtype"] = @2; au[@"manufacturer"] = @3;
      auto auOther = [au mutableCopy]; auOther[@"name"] = @"New name";
      check(PluginPreset::matches(au, auOther) && !PluginPreset::matches(plugin, au), "AU identity is format/component based");
      for (NSString *key in @[@"type", @"subtype", @"manufacturer"]) {
        auOther = [au mutableCopy]; auOther[key] = @99;
        check(!PluginPreset::matches(au, auOther), "All AU component identifiers must match");
      }
      auto builtin = [plugin mutableCopy]; builtin[@"format"] = @"Built-in"; builtin[@"classID"] = @"resonance.gain";
      builtin[@"isInstrument"] = @NO; builtin[@"path"] = @"";
      auto otherBuiltin = [builtin mutableCopy]; otherBuiltin[@"classID"] = @"resonance.filter";
      check(PluginPreset::matches(builtin, builtin) && !PluginPreset::matches(builtin, otherBuiltin), "Built-in presets match stable effect IDs");
      auto malformed = [NSPropertyListSerialization propertyListWithData:original options:NSPropertyListMutableContainers format:nil error:nil];
      auto writeBad = [&](NSDictionary *value) {
        [[NSPropertyListSerialization dataWithPropertyList:value format:NSPropertyListBinaryFormat_v1_0 options:0 error:nil] writeToFile:path atomically:YES];
        rejects([&] { PluginPreset::read(path); }, "Malformed preset is rejected before any plugin can load it");
      };
      auto bad = [malformed mutableCopy]; bad[@"version"] = @2; writeBad(bad);
      bad = [malformed mutableCopy]; bad[@"version"] = @YES; writeBad(bad);
      bad = [malformed mutableCopy]; bad[@"state"] = @"not data"; writeBad(bad);
      bad = [malformed mutableCopy]; bad[@"routing"] = @{}; writeBad(bad);
      bad = [malformed mutableCopy]; auto badPlugin = [plugin mutableCopy]; badPlugin[@"classID"] = @"bad"; bad[@"plugin"] = badPlugin; writeBad(bad);
      rejects([&] { PluginPreset::read(@"relative.resonance-preset"); }, "Reject relative paths");
      rejects([&] { PluginPreset::write([folder stringByAppendingPathComponent:@"wrong.wav"], plugin, state, @"", false, true); }, "Reject wrong extension");
      rejects([&] { PluginPreset::write(path, plugin, [NSMutableData dataWithLength:PluginPreset::maximumStateBytes + 1], @"", true, true); }, "Reject oversized saved state");
      int fd = open(path.fileSystemRepresentation, O_WRONLY | O_TRUNC); check(fd >= 0, "Open sparse oversized fixture");
      check(ftruncate(fd, PluginPreset::maximumStateBytes + 65537) == 0, "Make oversized fixture"); close(fd);
      rejects([&] { PluginPreset::read(path); }, "File size is bounded before allocation");
      [NSFileManager.defaultManager removeItemAtPath:path error:nil];
      check(mkfifo(path.fileSystemRepresentation, 0600) == 0, "Create non-regular fixture");
      rejects([&] { PluginPreset::read(path); }, "Non-regular read rejects without waiting for a writer");
      rejects([&] { PluginPreset::write(path, plugin, state, @"", true, true); }, "Refuse non-regular overwrite destination");
      [NSFileManager.defaultManager removeItemAtPath:path error:nil];
      check(symlink("missing", path.fileSystemRepresentation) == 0, "Create broken symlink fixture");
      rejects([&] { PluginPreset::write(path, plugin, state, @"", false, false); }, "Broken symlink cannot bypass no-overwrite");
      [NSFileManager.defaultManager removeItemAtPath:folder error:nil];
      std::cout << "PASS plugin preset files: lossless binary state, portable identity, strict validation, revisions, dry-run/atomic overwrite and bounded regular-file I/O\n";
      return 0;
    } catch (const std::exception &error) {
      [NSFileManager.defaultManager removeItemAtPath:folder error:nil];
      std::cerr << "FAIL " << error.what() << '\n'; return 1;
    }
  }
}
