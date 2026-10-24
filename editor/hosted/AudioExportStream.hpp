#pragma once
#include "PluginTypes.hpp"
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <vector>

namespace Tracker {
struct NativeSong;
inline constexpr uint32_t audioExportRate=48000;
// Offline worker only. Own private native processors and render the complete
// selected sequence to stereo float32 WAV. The stream must start at offset zero
// and support seek; its owner supplies staging, durability and atomic publication.
// No document, device, live processor, save baseline or history is changed.
// Returns written stereo frames, excluding compensated latency, including tails.
uint64_t writeProjectAudio(const std::vector<std::byte> &module,
                          const std::vector<PluginState> &plugins,
                          const std::vector<ParameterChange> &automation,
                          std::ostream &output,uint32_t sequence=UINT32_MAX,
                          const NativeSong *native=nullptr,uint32_t blockFrames=512);
}
