#include "SongTiming.hpp"
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace Tracker {
using namespace OpenMPT;
namespace {
void require(bool value, const char *message) { if (!value) throw std::invalid_argument(message); }
void validate(const SongTiming &t) {
  require(t.mode < TempoMode::NumModes, "Unknown timing mode");
  require(t.rowsPerBeat <= MAX_ROWS_PER_BEAT && (t.rowsPerBeat || t.mode != TempoMode::Modern) &&
    t.rowsPerMeasure >= t.rowsPerBeat && t.rowsPerMeasure <= MAX_ROWS_PER_BEAT, "Invalid song beat/bar length");
  require(!t.sequences.empty() && t.sequences.size() <= 256, "Invalid timing sequence inventory");
  for (const auto &s : t.sequences)
    require(s.tempo >= TEMPO::fractFact && s.tempo <= 65535u * TEMPO::fractFact && s.speed > 0 && s.speed <= 65535,
      "Invalid sequence tempo or speed");
  require(t.groove.size() <= MAX_ROWS_PER_BEAT, "Groove is too long");
  uint64_t total = 0;
  for (auto factor : t.groove) {
    require(factor > 0 && factor <= 16u * TempoSwing::Unity, "Invalid groove duration"); total += factor;
  }
  require(t.groove.empty() || (t.groove.size() == t.rowsPerBeat && total == uint64_t(t.groove.size()) * TempoSwing::Unity),
    "Groove must span a beat with an average row duration of one");
}
}
void validateSongTiming(const SongTiming &timing) { validate(timing); }
SongTiming songTiming(const CSoundFile &song) {
  SongTiming result{song.m_nTempoMode, song.m_nDefaultRowsPerBeat, song.m_nDefaultRowsPerMeasure,
    {song.m_tempoSwing.begin(), song.m_tempoSwing.end()}, {}};
  for (SEQUENCEINDEX i = 0; i < song.Order.GetNumSequences(); ++i)
    result.sequences.push_back({song.Order(i).GetDefaultTempo().GetRaw(), song.Order(i).GetDefaultSpeed()});
  return result;
}
void applySongTiming(CSoundFile &song, const SongTiming &t) {
  validate(t);
  require(t.sequences.size() == song.Order.GetNumSequences(), "Timing sequence inventory changed");
  // Allocate before touching the song so a failed allocation cannot leave partial settings.
  TempoSwing groove; groove.assign(t.groove.begin(), t.groove.end());
  song.m_nTempoMode = t.mode; song.m_nDefaultRowsPerBeat = t.rowsPerBeat; song.m_nDefaultRowsPerMeasure = t.rowsPerMeasure;
  song.m_tempoSwing = std::move(groove);
  for (SEQUENCEINDEX i = 0; i < song.Order.GetNumSequences(); ++i) {
    song.Order(i).SetDefaultTempo(TEMPO{}.SetRaw(t.sequences[i].tempo)); song.Order(i).SetDefaultSpeed(t.sequences[i].speed);
  }
}
std::vector<uint32_t> normalizedGroove(std::span<const double> weights) {
  require(weights.size() <= 32, "Use at most 32 groove durations");
  if (weights.empty()) return {};
  TempoSwing factors;
  for (auto value : weights) {
    require(std::isfinite(value) && value >= .25 && value <= 4, "Groove durations must be between 0.25 and 4");
    factors.push_back(uint32_t(std::llround(value * TempoSwing::Unity)));
  }
  factors.Normalize();
  for (auto factor : factors) require(factor >= TempoSwing::Unity / 4 && factor <= TempoSwing::Unity * 4,
    "Normalized groove durations must remain between 0.25 and 4");
  return {factors.begin(), factors.end()};
}
std::vector<std::byte> encodeTimingArchive(const CSoundFile &source, const CSoundFile &base) {
  const auto t = songTiming(source);
  if (t == songTiming(base)) return {};
  validate(t);
  require(source.Order.GetNumSequences() == base.Order.GetNumSequences(), "Module cannot retain this sequence inventory");
  std::vector<std::byte> bytes;
  auto put = [&](uint32_t value) { for (int shift = 0; shift < 32; shift += 8) bytes.push_back(std::byte(value >> shift)); };
  put(1); put(uint32_t(t.mode)); put(t.rowsPerBeat); put(t.rowsPerMeasure);
  put(uint32_t(t.sequences.size()));
  for (const auto &s : t.sequences) { put(s.tempo); put(s.speed); }
  put(uint32_t(t.groove.size())); for (auto factor : t.groove) put(factor);
  return bytes;
}
void restoreTimingArchive(CSoundFile &song, std::span<const std::byte> bytes) {
  if (bytes.empty()) return;
  size_t position = 0;
  auto take = [&]() {
    require(bytes.size() - position >= 4, "Truncated timing archive"); uint32_t value = 0;
    for (int shift = 0; shift < 32; shift += 8) value |= uint32_t(std::to_integer<uint8_t>(bytes[position++])) << shift;
    return value;
  };
  require(take() == 1, "Unknown timing archive version");
  SongTiming t;
  auto mode = take(); require(mode < uint32_t(TempoMode::NumModes), "Unknown timing mode"); t.mode = TempoMode(mode);
  t.rowsPerBeat = take(); t.rowsPerMeasure = take();
  auto sequences = take(); require(sequences == song.Order.GetNumSequences() && sequences <= 256, "Invalid timing sequence inventory");
  for (uint32_t i = 0; i < sequences; ++i) t.sequences.push_back({take(), take()});
  auto count = take(); require(count <= MAX_ROWS_PER_BEAT && count <= (bytes.size() - position) / 4, "Invalid timing groove length");
  for (uint32_t i = 0; i < count; ++i) t.groove.push_back(take());
  require(position == bytes.size(), "Trailing timing archive data"); applySongTiming(song, t);
}
void validateTimingExport(const CSoundFile &source, const CSoundFile &converted) {
  require(songTiming(source) == songTiming(converted), "Module export would change tempo, groove or timing. Save a .resonance project.");
}
}
