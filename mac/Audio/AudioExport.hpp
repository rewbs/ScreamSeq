#pragma once
#include "AudioUnitHost.hpp"
namespace Tracker {
void exportProjectAudio(const std::vector<std::byte> &, const std::vector<PluginState> &,
                        const std::vector<ParameterChange> &, const std::string &path, uint32_t sequence = UINT32_MAX,
                        const NativeSong *native = nullptr);
}
