#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>
namespace Tracker { class Document; }
namespace ScreamSeq {
using Json = nlohmann::json;
struct EnvelopeHostHooks {
  // Both hooks are required ONLY when creating a new parameter lane. Resolve
  // the persistent instance ID and actual parameter catalog, never a rack index.
  // Return false for missing/unavailable parameters; true for conflicting host
  // (non-musical) automation. Native performance conflicts are checked here.
  std::function<bool(const std::string &,uint32_t)> parameterAvailable;
  std::function<bool(const std::string &,uint32_t)> parameterAutomationConflicts;
};
// Control-thread only. Caller checks/removes expectedRevision; invoke returns
// result.data. Read hooks must not mutate Document; stopPlayback must not throw
// or mutate it. All validation precedes stopPlayback. Recreate after document
// replacement. No UI drafts, audio instances, implicit paths or global settings.
// An absent catalogue path disables catalogue operations (including publication
// in inspection sessions). Supply an absolute, isolated file path for tests.
class EnvelopeOperations {
  Tracker::Document &document_;
  std::function<void()> stopPlayback_;
  EnvelopeHostHooks host_;
  std::optional<std::filesystem::path> cataloguePath_;
public:
  explicit EnvelopeOperations(Tracker::Document &,std::function<void()> stopPlayback = {},
    EnvelopeHostHooks host = {},std::optional<std::filesystem::path> cataloguePath = {});
  Json invoke(const std::string &method,const Json &params);
  static std::vector<std::string> reads();
  static std::vector<std::string> writes();
};
}
