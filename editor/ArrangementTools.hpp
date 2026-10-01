#pragma once
#include "PatternTools.hpp"
namespace Tracker {
struct ArrangementCopy {
  uint16_t sourceOrder{}, targetOrder{}, sourceChannel{}, targetChannel{}, channels = 1;
  std::string mode = "overwrite";
  bool makeUnique = true, clip = false;
};
struct ArrangementCopyPlan {
  uint64_t revision{};
  uint16_t sequence{}, targetOrder{}, originalPattern{}, targetPattern{};
  bool clone = false;
  std::vector<Edit> edits;
};
ArrangementCopyPlan prepareArrangementCopy(const Document &, const ArrangementCopy &);
void applyArrangementCopy(Document &, const ArrangementCopyPlan &);
} // namespace Tracker
