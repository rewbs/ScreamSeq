#pragma once
#import <Foundation/Foundation.h>
#include <functional>

namespace Tracker {
// Accessed on the serial document queue. Discovery is injected so cache tests
// need neither installed third-party plugins nor scanner processes.
class PluginInventory {
  NSString *path_;
  NSArray<NSDictionary *> *cached_ = nil;

public:
  static NSString *defaultPath();
  explicit PluginInventory(NSString *path = defaultPath()) : path_(path) {}
  NSArray<NSDictionary *> *load(bool rescan, const std::function<NSArray<NSDictionary *> *()> &scan);
};
} // namespace Tracker
