#pragma once
#include "common/stdafx.h"
#include "soundlib/Sndfile.h"
#include <span>
#include <vector>

namespace Tracker {
struct SequenceTiming {
  uint32_t tempo = 1250000, speed = 6; // BPM in ten-thousandths.
  bool operator==(const SequenceTiming &) const = default;
};
struct SongTiming {
  OpenMPT::TempoMode mode = OpenMPT::TempoMode::Classic;
  uint32_t rowsPerBeat = 4, rowsPerMeasure = 16;
  std::vector<uint32_t> groove; // Fixed-point row durations; mean is TempoSwing::Unity.
  std::vector<SequenceTiming> sequences;
  bool operator==(const SongTiming &) const = default;
};
SongTiming songTiming(const OpenMPT::CSoundFile &song);
void validateSongTiming(const SongTiming &timing);
void applySongTiming(OpenMPT::CSoundFile &song, const SongTiming &timing);
std::vector<uint32_t> normalizedGroove(std::span<const double> weights);
// Empty when the ordinary module already preserves every timing value.
std::vector<std::byte> encodeTimingArchive(const OpenMPT::CSoundFile &source, const OpenMPT::CSoundFile &base);
void restoreTimingArchive(OpenMPT::CSoundFile &song, std::span<const std::byte> bytes);
void validateTimingExport(const OpenMPT::CSoundFile &source, const OpenMPT::CSoundFile &converted);
}
