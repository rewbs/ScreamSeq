#pragma once
#include "NativeSong.hpp"
#include <optional>
#include <span>
namespace Tracker {
void ensureNativeMixer(NativeSong &native);
uint64_t groupNoteColumns(NativeSong &native, std::span<const uint16_t> channels,
                          const std::string &name, std::optional<uint64_t> output = {});
bool effectiveColumnMute(const NativeSong &native, const OpenMPT::CSoundFile &song, uint16_t channel);
}
