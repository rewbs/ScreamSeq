#pragma once
#include "../Plugins/PluginInventory.hpp"
#include <string_view>
// The VST3 loader refuses bundles outside the trusted plugin locations. A test
// loads the fixture bundle named on its own command line, and says so here.
// Trust is in-process only; nothing is written to the user's trust store.
inline void trustFixtureArguments(int argc, const char *const *argv) {
  for (int index = 1; index < argc; ++index)
    if (std::string_view(argv[index]).ends_with(".vst3"))
      Tracker::PluginTrust::trust([NSString stringWithUTF8String:argv[index]]);
}
