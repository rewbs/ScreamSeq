#pragma once
#include "editor/hosted/PluginBackend.hpp"
#include <chrono>
#include <stdexcept>
namespace Tracker::WindowsVST3 {
// The private UI owner could not accept a call right now (its message queue is
// full). Transient: retry later; the plugin and its editor are unaffected.
struct UiOwnerBusy final:std::runtime_error {using std::runtime_error::runtime_error;};
// Control owner only. Never call discovery/rescan/configuration from rendering.
// Configure before first use; cache can be absent, but never silently rescanned.
void configure(std::string scannerExecutable, std::string cacheFile);
std::vector<PluginDescriptor> rescan(const std::string &path, uint32_t timeoutMs = 5000);
std::vector<std::string> defaultSearchRoots();
// Explicit recursive filesystem enumeration; does not execute modules.
std::vector<std::string> candidates(const std::vector<std::string> &roots);
bool validClassID(const std::string &) noexcept;
struct ScannedPlugin { PluginDescriptor descriptor;std::string sha256; };
// Cached identity only: does not instantiate or execute a module.
std::vector<ScannedPlugin> scannedPlugins(const std::string &classID,bool instrument);
// Verify the current binary against a captured scanner hash without loading it.
// Returns the canonical native path. Missing/stale/foreign modules fail closed.
// Empty SHA-256 is for read-only inspection against the current scan record.
std::string verifyScannedPlugin(const std::string &path,const std::string &classID,bool instrument,const std::string &sha256);
}
