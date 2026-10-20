#pragma once
#include <cstdint>
#include <optional>
#include <vector>

namespace Tracker {
class Document;
struct PatternTimelinePosition {
  uint32_t row = 0;
  double beat = 0;
  std::optional<double> patternSeconds, songSeconds;
};
struct PatternTimeline {
  uint16_t pattern = 0;
  std::optional<uint32_t> order;
  std::vector<PatternTimelinePosition> positions;
};
inline constexpr const char *patternTimelineSemantics =
  "First visit to each row in this order occurrence; unreachable rows have null times.";

// Document/control owner only, never drawing or audio. The bounded engine walk
// uses its own playback state and prepares only the document's derived FX lookup.
// No document, history, current sequence, renderer or plugin state is edited.
// Omitted order selects the first occurrence; unarranged patterns have null times.
PatternTimeline patternTimeline(Document &, uint16_t pattern,
  std::optional<uint32_t> order = {});
}
