#pragma once
#include <nlohmann/json.hpp>
#include <functional>
#include <string>
#include <vector>
namespace Tracker { class Document; struct Edit; }
namespace ScreamSeq {
using Json = nlohmann::json;
// Control-thread only. Caller checks/removes expectedRevision before invocation.
// Returns the Mac result.data object; the host owns revision/transport envelopes.
// Include selected sequence in the opaque host revision: sequence.select is
// navigation and deliberately does not increment Document::revision or history.
// Callbacks are control-thread hooks, must not mutate this Document or throw.
// publishEdits observes committed cells; queue overflow should stop playback,
// not claim the document edit failed. Empty callbacks are for offline use only.
// Recreate this object if loading replaces the referenced Document.
class DocumentOperations {
  Tracker::Document &document_;
  std::function<void()> stopPlayback_;
  std::function<void(const std::vector<Tracker::Edit>&)> publishEdits_;
public:
  explicit DocumentOperations(Tracker::Document &document,
    std::function<void()> stopPlayback = {},
    std::function<void(const std::vector<Tracker::Edit>&)> publishEdits = {});
  Json invoke(const std::string &method, const Json &params);
  static std::vector<std::string> reads();
  static std::vector<std::string> writes();
};
}
