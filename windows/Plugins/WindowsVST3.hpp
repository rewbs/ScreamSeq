#pragma once
#include "editor/hosted/PluginBackend.hpp"
#include <chrono>
namespace Tracker::WindowsVST3 {
// Control owner only. Never call discovery/rescan/configuration from rendering.
// Configure before first use; cache can be absent, but never silently rescanned.
void configure(std::string scannerExecutable, std::string cacheFile);
std::vector<PluginDescriptor> rescan(const std::string &path, uint32_t timeoutMs = 5000);
std::vector<std::string> defaultSearchRoots();
// Explicit recursive filesystem enumeration; does not execute modules.
std::vector<std::string> candidates(const std::vector<std::string> &roots);
bool validClassID(const std::string &) noexcept;
}
