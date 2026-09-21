#pragma once
#include "common/stdafx.h"
#include "soundlib/Sndfile.h"
#include <span>
#include <vector>
namespace Tracker {
inline constexpr size_t maximumSongSnapshotBytes = 512 * 1024 * 1024;
// Internal snapshots retain a module base plus exact sample/header/keymap,
// envelope and optional title/pattern/order corrections. The RSCORE1 sample
// archive extension is emitted only when module conversion loses core data.
// Ordinary module export never writes this container.
std::vector<std::byte> encodeSampleArchive(const OpenMPT::CSoundFile &source, const OpenMPT::CSoundFile &base);
void restoreSampleArchive(OpenMPT::CSoundFile &base, std::span<const std::byte> archive);
// Throws if module roundtrip changes title, allocated patterns or order data.
void validateSongStructureExport(const OpenMPT::CSoundFile &source, const OpenMPT::CSoundFile &converted);
// Throws before writing a module if its roundtrip loses samples, maps or envelopes.
void validateSampleExport(const OpenMPT::CSoundFile &source, const OpenMPT::CSoundFile &converted);
bool isSongSnapshot(std::span<const std::byte> bytes);
struct SongSnapshotParts {
  std::span<const std::byte> module, samples, timing;
};
SongSnapshotParts splitSongSnapshot(std::span<const std::byte> bytes);
std::vector<std::byte> packSongSnapshot(std::span<const std::byte> module, std::span<const std::byte> samples,
                                       std::span<const std::byte> timing = {});
} // namespace Tracker
