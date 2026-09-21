#pragma once
#include <nlohmann/json.hpp>
#include "editor/SampleClipboard.hpp"
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <thread>
namespace Tracker { class Document; }
namespace ScreamSeq {
using Json=nlohmann::json;
// Own on the document control worker, never call from the audio callback.
// Caller validates/removes expectedRevision and wraps returned Mac result.data.
// stopPlayback must not throw or mutate Document. Empty hook is offline-only.
// Clipboard lives for this document session. Recreate on document replacement.
class AssetOperations {
  Tracker::Document &document_;
  std::function<void()> stopPlayback_;
  std::function<void(const Tracker::Document &)> validateImport_;
  const std::thread::id owner_=std::this_thread::get_id();
  std::optional<Tracker::SampleClipboard> clipboard_;
  std::string clipboardId_;
  Json dispatch(const std::string &,const Json &);
  Json clipboardInfo() const;
public:
  // Empty validator explicitly means no external plugin assignments/capacity.
  // A host with plugin state MUST validate the fully imported candidate against
  // its real reserved instrument slots and resource budgets. If that state is
  // unavailable, throw ApiError(-32601); never infer assignments from names.
  // The hook runs for dry runs too, before stop, and must not mutate Document.
  explicit AssetOperations(Tracker::Document &,std::function<void()> stopPlayback={},
    std::function<void(const Tracker::Document &)> validateImport={});
  AssetOperations(const AssetOperations &)=delete;
  AssetOperations &operator=(const AssetOperations &)=delete;
  Json invoke(const std::string &method,const Json &params);
  static std::vector<std::string> reads();
  static std::vector<std::string> writes();
};
}
