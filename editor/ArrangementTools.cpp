#include "ArrangementTools.hpp"
#include "soundlib/mod_specifications.h"
#include <stdexcept>
namespace Tracker {
ArrangementCopyPlan prepareArrangementCopy(const Document &doc, const ArrangementCopy &copy) {
  const auto &s = doc.song();
  const auto &orders = s.Order();
  if (copy.sourceOrder >= orders.size() || copy.targetOrder >= orders.size())
    throw std::invalid_argument("Choose source and destination orders in the current sequence");
  const auto source = orders[copy.sourceOrder], destination = orders[copy.targetOrder];
  if (!s.Patterns.IsValidPat(source) || !s.Patterns.IsValidPat(destination))
    throw std::invalid_argument("End and skip orders do not contain pattern blocks");
  if (!copy.channels || size_t(copy.sourceChannel) + copy.channels > s.GetNumChannels() ||
      size_t(copy.targetChannel) + copy.channels > s.GetNumChannels())
    throw std::invalid_argument("Block channels are outside the song");
  const auto rows = s.Patterns[source].GetNumRows(), targetRows = s.Patterns[destination].GetNumRows();
  if (rows != targetRows && !copy.clip)
    throw std::invalid_argument("Pattern lengths differ; enable clipping to copy their overlapping rows");
  const auto count = std::min(rows, targetRows);
  if (size_t(count) * copy.channels > maximumPatternToolCells)
    throw std::invalid_argument("Block copy exceeds the pattern tool cell limit");
  std::vector<Cell> cells;
  cells.reserve(size_t(count) * copy.channels);
  for (uint16_t r = 0; r < count; ++r)
    for (uint16_t c = 0; c < copy.channels; ++c) cells.push_back(doc.cell(source, r, copy.sourceChannel + c));
  ArrangementCopyPlan plan{doc.revision, s.Order.GetCurrentSequenceIndex(), copy.targetOrder, destination, destination};
  plan.edits = preparePatternPaste(doc, destination, 0, copy.targetChannel, count, copy.channels,
                                  cells, PatternAll, copy.mode, false);
  size_t uses = 0;
  for (OpenMPT::SEQUENCEINDEX seq = 0; seq < s.Order.GetNumSequences(); ++seq)
    for (auto pattern : s.Order(seq)) if (pattern == destination) ++uses;
  if (!plan.edits.empty() && copy.makeUnique && uses > 1) {
    size_t unused = 0;
    while (s.Patterns.IsValidPat(OpenMPT::PATTERNINDEX(unused))) ++unused;
    if (unused >= s.GetModSpecifications().patternsMax)
      throw std::invalid_argument("No free pattern slot for an independent copy");
    plan.clone = true;
    plan.targetPattern = uint16_t(unused);
  }
  return plan;
}
void applyArrangementCopy(Document &doc, const ArrangementCopyPlan &plan) {
  if (doc.revision != plan.revision || doc.song().Order.GetCurrentSequenceIndex() != plan.sequence)
    throw std::invalid_argument("Song changed since the block copy was prepared");
  if (plan.edits.empty()) return;
  doc.transaction([&](OpenMPT::CSoundFile &s, NativeSong &native) {
    if (plan.clone) {
      auto rows = s.Patterns[plan.originalPattern].GetNumRows();
      if (!s.Patterns.Insert(plan.targetPattern, rows)) throw std::runtime_error("Could not allocate independent pattern");
      for (uint16_t r = 0; r < rows; ++r)
        for (uint16_t c = 0; c < s.GetNumChannels(); ++c)
          *s.Patterns[plan.targetPattern].GetpModCommand(r, c) = *s.Patterns[plan.originalPattern].GetpModCommand(r, c);
      s.Order()[plan.targetOrder] = plan.targetPattern;
      auto entity = native.patterns.at(plan.originalPattern);
      entity.id = native.makeEntity().id;
      native.clonePatternAutomation(native.patterns.at(plan.originalPattern).id, entity.id);
      native.patterns[plan.targetPattern] = std::move(entity);
    }
    for (auto edit : plan.edits) { edit.pattern = plan.targetPattern; Document::put(s, edit); }
  });
}
} // namespace Tracker
