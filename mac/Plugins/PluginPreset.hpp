#pragma once
#import <Foundation/Foundation.h>
namespace Tracker {
// Portable host presets contain only plugin identity and opaque saved state.
// They never contain song routing, instance identity or instrument assignment.
class PluginPreset {
public:
  static constexpr NSUInteger maximumStateBytes = 16 * 1024 * 1024;
  static NSDictionary *read(NSString *path);
  static NSDictionary *summary(NSDictionary *preset);
  static bool matches(NSDictionary *a, NSDictionary *b);
  static NSDictionary *write(NSString *path, NSDictionary *descriptor, NSData *state,
                            NSString *name, bool overwrite, bool dryRun);
};
}
