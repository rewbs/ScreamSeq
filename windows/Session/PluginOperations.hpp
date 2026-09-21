#pragma once
#include "HostedProject.hpp"
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <chrono>
namespace ScreamSeq {
// Serial document-worker owner. Baseline/editor instances never receive song
// automation. Rendering uses the independent prepared HostedProjectPlayback.
class PluginOperations {
  Tracker::Document &document_;
  Project::ProjectState &project_;
  std::function<void()> stop_;
  std::function<void(std::span<const Tracker::ParameterChange>)> liveParameters_;
  struct History {Json plugins,automation;size_t bytes=0;};
  std::deque<History> undo_,redo_;
  std::map<std::string,std::unique_ptr<Tracker::NativePlugin>> editors_;
  std::set<std::string> openEditors_;
  std::map<std::string,std::map<uint32_t,float>> pendingParameters_;
  std::chrono::steady_clock::time_point lastEditorChange_{};
  std::chrono::steady_clock::time_point lastStateCapture_{};
  Json lastTouched_ = nullptr;
  uint64_t touchSequence_=0;
  History snapshot() const;
  void commit(Json plugins,Json automation,bool keepEditors=false,bool parameterOnly=false,
    std::span<const Tracker::ParameterChange> changes={});
  size_t slot(const Json &) const;
  Tracker::NativePlugin &editor(size_t);
public:
  PluginOperations(Tracker::Document &,Project::ProjectState &,std::function<void()> stop,
    std::function<void(std::span<const Tracker::ParameterChange>)> liveParameters={});
  ~PluginOperations();
  static std::vector<std::string> reads();
  static std::vector<std::string> writes();
  Json invoke(const std::string &,const Json &);
  bool flushEditors(bool force=false); // Debounce gestures; save/close forces capture.
  bool canUndo() const {return !undo_.empty();}
  bool canRedo() const {return !redo_.empty();}
  size_t openEditorCount() const {return openEditors_.size();}
};
}
