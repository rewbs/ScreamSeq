#pragma once
#include "PatternTools.hpp"
#include <array>
namespace Tracker {
struct ArrangementMatrixRange {
  uint32_t startOrder=0,orderCount=64,startChannel=0;
  // Omitted means min(16, remaining tracks). Explicit zero is invalid.
  std::optional<uint32_t> channelCount;
};
struct ArrangementBlockDensity {
  uint32_t channel=0;
  uint64_t trackID=0;
  // One event per occupied tracker cell, precise on/off, or native FX record.
  // Stored layers are counted independently, including coexisting FX 1 data.
  // Notes counts pitched tracker cells and precise onsets, never offs/cuts.
  // Native bins use full sub-row position; tracker bins retain row precision.
  uint32_t events=0,notes=0,trackerEvents=0,preciseEvents=0,nativeFxEvents=0;
  std::array<uint32_t,16> bins{};
};
struct ArrangementPatternDensity {
  uint32_t pattern=0,rows=0;
  uint64_t patternID=0;
  std::vector<ArrangementBlockDensity> blocks;
};
inline constexpr uint32_t noArrangementPatternSummary=UINT32_MAX;
struct ArrangementOrderDensity {
  uint32_t order=0,pattern=0,summaryIndex=noArrangementPatternSummary;
};
struct ArrangementMatrixSummary {
  uint32_t sequence=0,totalOrders=0,totalChannels=0,startOrder=0,startChannel=0,channelCount=0;
  std::vector<ArrangementOrderDensity> orders;
  // Each valid visible pattern occurs once; repeated orders share its index.
  // End/Skip orders have no summary. Metadata strings stay with the adapters.
  std::vector<ArrangementPatternDensity> patterns;
};
// Control/worker-thread query; no retained pointers or document mutations.
// At most 128 orders by 32 tracks. Scans each distinct pattern's tracker cells
// once and each native event vector once, independent of repeated order count.
ArrangementMatrixSummary summarizeArrangement(const Document &,const ArrangementMatrixRange & = {});
struct ArrangementCopy {
  // Tracker fields use normal paste modes. Precise events are independent of
  // those fields: overwrite replaces the overlapping span, merge replaces an
  // equal (position,on/off) key, and mix keeps an occupied key. FX commands use
  // (row,column) keys; tracker FX 1 and native column zero share one slot.
  // Clip excludes onsets outside overlapping rows and clips copied slide ends.
  // Copying an identical physical pattern/channel span is always a no-op after
  // validation, including aliased orders and imported legacy/native FX 1 pairs.
  uint16_t sourceOrder{}, targetOrder{}, sourceChannel{}, targetChannel{}, channels = 1;
  std::string mode = "overwrite";
  bool makeUnique = true, clip = false;
};
struct ArrangementCopyPlan {
  uint64_t revision{};
  uint16_t sequence{}, targetOrder{}, originalPattern{}, targetPattern{};
  bool clone = false;
  std::vector<Edit> edits;
  std::optional<NativeSong> native;
  const Document *owner = nullptr;
  uint64_t orderID = 0, originalPatternID = 0;
  bool changed() const { return native.has_value(); }
};
// Exact snapshot validation is control-thread work. Preparation never consumes
// live IDs/history; changed() also reports native-only copies with zero cells.
// Optional host admission sees the complete changed candidate, including clone
// metadata. It may throw, but must not mutate the live document.
ArrangementCopyPlan prepareArrangementCopy(Document &, const ArrangementCopy &,
    const std::function<void(Document &)> &validateCandidate = {});
void applyArrangementCopy(Document &, const ArrangementCopyPlan &);
} // namespace Tracker
