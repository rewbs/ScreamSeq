#include "PatternTimeline.hpp"
#include "TrackerDocument.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Tracker {
PatternTimeline patternTimeline(Document &document, uint16_t pattern,
  std::optional<uint32_t> order) {
  using namespace OpenMPT;
  auto &song = document.song();
  if (!song.Patterns.IsValidPat(pattern)) throw std::invalid_argument("Pattern does not exist");
  if (order) {
    if (*order >= song.Order().size() || song.Order()[ORDERINDEX(*order)] != pattern)
      throw std::invalid_argument("Order does not contain this pattern");
  } else {
    for (uint32_t i = 0; i < song.Order().size(); ++i)
      if (song.Order()[ORDERINDEX(i)] == pattern) { order = i; break; }
  }
  const auto &source = song.Patterns[pattern];
  const auto rows = source.GetNumRows();
  const auto beat = std::max(1u, uint32_t(source.GetOverrideSignature()
    ? source.GetRowsPerBeat() : song.m_nDefaultRowsPerBeat));
  PatternTimeline result{pattern, order, {}};
  result.positions.reserve(rows);
  for (uint32_t row = 0; row < rows; ++row)
    result.positions.push_back({row, double(row) / beat, {}, {}});
  if (order) {
    std::optional<double> origin;
    GetLengthTarget target;
    target.onRow = [&](ORDERINDEX occurrence, ROWINDEX row, double seconds) {
      if (occurrence != *order || row >= rows || !std::isfinite(seconds) || seconds < 0) return;
      auto &entry = result.positions[row];
      if (entry.songSeconds) return;
      if (!origin) origin = seconds;
      entry.songSeconds = seconds;
      entry.patternSeconds = seconds - *origin;
    };
    document.native().prepareEffects(song);
    song.GetLength(eNoAdjust, target);
  }
  return result;
}
}
