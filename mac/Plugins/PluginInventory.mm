#include "PluginInventory.hpp"
#include <cmath>
#include <stdexcept>
#include <sys/utsname.h>

namespace Tracker {
namespace {
NSString *architecture() {
#if defined(__arm64__)
  return @"arm64";
#elif defined(__x86_64__)
  return @"x86_64";
#else
  struct utsname info{};
  uname(&info);
  return @(info.machine);
#endif
}
bool integer(id value) {
  if (![value isKindOfClass:NSNumber.class] || CFGetTypeID((__bridge CFTypeRef)value) == CFBooleanGetTypeID())
    return false;
  auto number = [value doubleValue];
  return std::isfinite(number) && number >= 0 && number <= UINT32_MAX && std::floor(number) == number;
}
bool string(id value, NSUInteger maximum) {
  return [value isKindOfClass:NSString.class] && [value length] <= maximum;
}
NSArray<NSDictionary *> *validated(id value) {
  if (![value isKindOfClass:NSArray.class] || [value count] > 4096)
    return nil;
  NSMutableArray *result = [NSMutableArray array];
  for (id item in value) {
    if (![item isKindOfClass:NSDictionary.class] || !integer(item[@"type"]) || !integer(item[@"subtype"]) ||
        !integer(item[@"manufacturer"]) || !string(item[@"name"], 1024) ||
        ![item[@"isInstrument"] isKindOfClass:NSNumber.class] ||
        CFGetTypeID((__bridge CFTypeRef)item[@"isInstrument"]) != CFBooleanGetTypeID())
      return nil;
    NSString *format = item[@"format"];
    if (![format isEqual:@"AU"] && ![format isEqual:@"VST3"])
      return nil;
    if ([format isEqual:@"VST3"]) {
      NSString *path = item[@"path"], *classID = item[@"classID"];
      if (!string(path, 8192) || !path.isAbsolutePath || ![path.pathExtension.lowercaseString isEqual:@"vst3"] ||
          !string(classID, 32) || classID.length != 32 ||
          [classID
              rangeOfCharacterFromSet:[[NSCharacterSet characterSetWithCharactersInString:@"0123456789abcdefABCDEF"]
                                          invertedSet]]
                  .location != NSNotFound)
        return nil;
    }
    [result addObject:[item copy]];
  }
  return [result copy];
}
} // namespace
NSString *PluginInventory::defaultPath() {
  NSString *root = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES).firstObject;
  return [[root stringByAppendingPathComponent:@"org.resonance"]
      stringByAppendingPathComponent:[NSString stringWithFormat:@"plugins-v1-%@.json", architecture()]];
}
NSArray<NSDictionary *> *PluginInventory::load(bool rescan, const std::function<NSArray<NSDictionary *> *()> &scan) {
  if (!rescan && cached_)
    return cached_;
  if (!rescan) {
    auto attributes = [[NSFileManager defaultManager] attributesOfItemAtPath:path_ error:nil];
    if (attributes && attributes.fileSize <= 8 * 1024 * 1024) {
      NSData *data = [NSData dataWithContentsOfFile:path_];
      id root = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
      if ([root isKindOfClass:NSDictionary.class] && integer(root[@"version"]) && [root[@"version"] isEqual:@1] &&
          [root[@"architecture"] isEqual:architecture()])
        cached_ = validated(root[@"plugins"]);
      if (cached_)
        return cached_;
    }
  }
  // Commit only a completed, well-formed scan. Failure preserves the last good
  // list both in memory and on disk, so Cancel + Add Plugin still works.
  NSArray *next = validated(scan());
  if (!next)
    throw std::runtime_error("The plugin scanner returned an invalid inventory");
  NSData *data = [NSJSONSerialization dataWithJSONObject:@{
    @"version" : @1,
    @"architecture" : architecture(),
    @"plugins" : next
  }
                                                 options:0
                                                   error:nil];
  if (!data || data.length > 8 * 1024 * 1024)
    throw std::runtime_error("The plugin inventory exceeds its size limit");
  cached_ = next;
  // A read-only/full cache directory must not prevent adding a plugin. Keep
  // the in-memory cache and retry persistence after the next explicit scan.
  [[NSFileManager defaultManager] createDirectoryAtPath:path_.stringByDeletingLastPathComponent
                            withIntermediateDirectories:YES
                                             attributes:nil
                                                  error:nil];
  [data writeToFile:path_ options:NSDataWritingAtomic error:nil];
  return cached_;
}
} // namespace Tracker
