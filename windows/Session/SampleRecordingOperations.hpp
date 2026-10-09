#pragma once
#include "windows/Audio/SampleCapture.hpp"
#include "windows/Api/SessionAdapter.hpp"
#include <functional>

namespace ScreamSeq {
// Document-worker owner; hardware is injected so lifecycle/error/retained-take
// contracts can be qualified without opening a microphone in CI.
class SampleRecordingOperations {
public:
  using Json=nlohmann::json;
  struct Hooks {
    std::function<std::vector<CaptureDevice>()> devices;
    std::function<std::unique_ptr<SampleCapture>()> capture;
    std::function<std::string()> uniqueID;
    // Append receives the validated take identity so its pre-commit receipt
    // can describe the full Keep result before the take is consumed.
    std::function<Json(std::span<const float>,uint32_t,uint32_t,const std::string &,bool,bool,const std::string &)> append;
  };
private:
  Hooks hooks_;
  std::unique_ptr<SampleCapture> capture_;
  std::string take_,document_,baseRevision_,permission_="notDetermined";
  CaptureOptions options_;
  Json state() const;
public:
  explicit SampleRecordingOperations(Hooks hooks):hooks_(std::move(hooks)){}
  ~SampleRecordingOperations(){if(capture_)capture_->stop();}
  Json invoke(const std::string &,const Json &,const std::string &document,const std::string &revision);
  bool hasTake() const noexcept {return bool(capture_);}
  void documentReplaced() noexcept {if(capture_)capture_->stop();} // Retain rather than silently import into the new song.
  static std::vector<std::string> reads(){return {"sample.recording.devices","sample.recording.get"};}
  static std::vector<std::string> writes(){return {"sample.recording.start","sample.recording.stop","sample.recording.commit","sample.recording.discard"};}
  static bool requiresRevision(const std::string &method){return method=="sample.recording.start"||method=="sample.recording.commit";}
};
}
