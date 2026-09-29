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
  // The last completed scan, from memory or the cache file. Never starts a
  // scanner; returns nil when no valid inventory has been stored yet.
  NSArray<NSDictionary *> *cached();
};
// Decides whether a VST3 bundle may be loaded into this process. A bundle path
// stored in a project, recovery file or graph recipe is untrusted input: it is
// only a hint for finding an installed plugin, never permission to execute it.
// A location is trusted when its canonical path (symbolic links and ".."
// resolved) lies inside a standard VST3 folder, names a bundle of the scanned
// inventory, or was trusted by an explicit user/agent action in this process
// or, through the private store, in an earlier launch. Nothing read from a
// project, preset or the environment adds trust.
class PluginTrust {
public:
  // Existing file or folder with symbolic links resolved; nil when absent.
  static NSString *canonical(NSString *path);
  // In-process only. Pass one .vst3 bundle, or a folder whose bundles are
  // accepted. Returns false when the location does not exist.
  static bool trust(NSString *location);
  // Explicit user/agent choice of one validated bundle: trusted now and, when
  // a store is available, in later launches. The store is a private per-user
  // file in Application Support. It holds canonical bundle paths only and
  // is never written from, or read out of, a project, recovery or preset file.
  // Returns false when the bundle does not exist.
  static bool persist(NSString *bundle);
  // Nil when nothing is persisted: command-line hosts such as the test
  // executables have no bundle identifier and keep trust in-process only.
  static NSString *storePath();
  static NSString *defaultStorePath();
  // Tests only: use this file instead (nil restores the default).
  static void setStorePath(NSString *path);
  // Tests only: forget in-process trust, as a new launch would.
  static void resetProcessTrust();
  static void revoke(NSString *location); // Withdraws in-process trust only.
  static bool trusted(NSString *bundle, NSArray<NSDictionary *> *inventory);
  // The single check made by the VST3 loader immediately before it loads code:
  // the canonical location of the bundle when that location is trusted (the
  // stored inventory is consulted), otherwise nil. Load the returned path.
  static NSString *loadable(NSString *bundle);
  // Canonical loadable bundle for a stored hint: the hint's real location when
  // trusted, otherwise the inventory's bundle for the same class ID. Nil means
  // the plugin is missing. Keep the stored string separately for saving.
  static NSString *resolve(NSString *hint, NSString *classID, NSArray<NSDictionary *> *inventory);
};
} // namespace Tracker
