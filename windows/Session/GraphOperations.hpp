#pragma once
#include <nlohmann/json.hpp>
#include "editor/SignalGraph.hpp"
#include <functional>
#include <string>
#include <vector>
namespace Tracker { class Document; struct NativeSong; }
namespace ScreamSeq {
using Json = nlohmann::json;
struct GraphRackRecord {
  Json descriptor = Json::object(); // Actual host descriptor dictionary.
  std::string id;
  uint32_t slot = 0;
  bool bypass = false;
  std::vector<uint16_t> instruments; // Actual assigned tracker instrument slots.
};
struct GraphRackClone {
  Tracker::GraphPluginRecipe recipe; // Baseline opaque state AND enabled aux buses.
  bool instrument = false;
  std::vector<uint16_t> instruments;
};
struct GraphHostHooks {
  // Snapshots must represent the same serialized control-thread host state.
  // Without callbacks, supplied caches are authoritative offline snapshots;
  // empty defaults mean an actual empty offline rack and no playback activity.
  std::function<std::vector<GraphRackRecord>()> rack;
  std::vector<GraphRackRecord> cachedRack;
  std::function<std::vector<Tracker::SignalActivity>()> activity;
  std::vector<Tracker::SignalActivity> cachedActivity;
  // Must return the requested real slot's baseline state, never an automated
  // live state. Throw ApiError for absent/unavailable slots. No fallback recipe.
  std::function<GraphRackClone(uint32_t)> cloneRackSlot;
  std::function<void(const Tracker::NativeSong &)> validateCandidate;
};
// Control-thread only. Caller checks/removes expectedRevision and constructs the
// outer Mac response envelope. This layer returns result.data, not fake host data.
// stopPlayback runs only after complete candidate validation, before annotation;
// it must not throw or mutate Document. Read hooks must not mutate it either.
// Recreate after loading replaces Document. No UI/audio/plugin instance ownership.
class GraphOperations {
  Tracker::Document &document_;
  std::function<void()> stopPlayback_;
  GraphHostHooks host_;
public:
  explicit GraphOperations(Tracker::Document &, std::function<void()> stopPlayback = {}, GraphHostHooks host = {});
  Json invoke(const std::string &method, const Json &params);
  static std::vector<std::string> reads();
  static std::vector<std::string> writes();
};
}
