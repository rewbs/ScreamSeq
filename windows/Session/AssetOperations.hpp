#pragma once
#include <nlohmann/json.hpp>
#include "editor/SampleClipboard.hpp"
#include <functional>
#include <memory>
#include <span>
#include <cstdint>
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
public:
  // Allocate the complete result before Stop/transaction. Publish only after a
  // successful single asset transaction; cancellation destroys it unpublished.
  // Notification runs on the document worker and must never throw or edit music.
  struct ImportCommit {
    virtual ~ImportCommit()=default;
    virtual void committed()noexcept=0;
  };
  using PrepareImportCommit=std::function<std::unique_ptr<ImportCommit>(const Json &)>;
private:
  Tracker::Document &document_;
  std::function<void()> stopPlayback_;
  std::function<void(const Tracker::Document &)> validateImport_;
  std::function<std::function<void()>(unsigned,const Tracker::SampleEditGeometry &)> prepareSampleLoops_;
  const std::thread::id owner_=std::this_thread::get_id();
  std::optional<Tracker::SampleClipboard> clipboard_;
  std::string clipboardId_;
  Json dispatch(const std::string &,const Json &,const PrepareImportCommit &);
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
  void sampleLoops(std::function<std::function<void()>(unsigned,const Tracker::SampleEditGeometry &)> callback) {prepareSampleLoops_=std::move(callback);}
  Json invoke(const std::string &method,const Json &params,const PrepareImportCommit &prepare={});
  Json appendCapturedAudio(std::span<const float>,uint32_t rate,uint32_t channels,const std::string &name,bool instrument,bool dryRun,const PrepareImportCommit &prepare={});
  static std::vector<std::string> reads();
  static std::vector<std::string> writes();
};
}
