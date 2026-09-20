#pragma once
#import <Foundation/Foundation.h>
namespace Tracker {
// Small per-user browser preferences, independent of song/plugin processing.
// Each operation reloads under a cross-process lock, so multiple documents can
// share a library without overwriting one another's edits.
class PluginLibrary {
  NSString *path_;
public:
  static NSString *defaultPath();
  static NSString *identifier(NSDictionary *descriptor);
  explicit PluginLibrary(NSString *path = defaultPath()) : path_(path) {}
  NSDictionary *read();
  NSDictionary *set(NSString *expectedRevision, NSString *identifier, NSDictionary *patch, bool dryRun = false);
  static NSArray<NSDictionary *> *decorate(NSArray<NSDictionary *> *plugins, NSDictionary *library);
};
}
