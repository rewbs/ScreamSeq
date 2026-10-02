#pragma once
#include "HostedProject.hpp"
#include "GraphOperations.hpp"
#include "windows/Plugins/PluginLibrary.hpp"
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <chrono>
#include <string>
#include <utility>
namespace ScreamSeq {
// Serial document-worker owner. Baseline/editor instances never receive song
// automation. Rendering uses the independent prepared HostedProjectPlayback.
class PluginOperations {
  Tracker::Document &document_;
  Project::ProjectState &project_;
  std::function<void()> stop_;
  std::function<void(std::span<const Tracker::ParameterChange>)> liveParameters_;
  std::function<void(size_t,bool)> liveBypass_;
  std::function<std::optional<std::vector<Tracker::PluginAudioBus>>(const std::string &)> liveBuses_;
  std::function<std::function<void()>(const Tracker::NativeSong &)> prepareNativePublication_;
  std::function<std::function<void()>(const std::vector<Tracker::ParameterChange> &)> prepareRecordedPublication_;
  std::function<std::function<void()>(const std::vector<Tracker::PluginState> &,const std::vector<Tracker::ParameterChange> &,const Tracker::NativeSong &)> prepareRackPublication_;
  std::optional<std::pair<size_t,bool>> bypassOnly(const Json &,const Json &) const;
  std::optional<std::vector<Tracker::ParameterChange>> parameterOnlyChanges(const Json &,const Json &);
  struct History {Json plugins,automation;size_t bytes=0;uint64_t sequence=0;};
  std::deque<History> undo_,redo_;
  std::vector<std::pair<uint64_t,uint64_t>> historyGroups_;
  uint64_t knownHistorySequence_=0,historyFloor_=0;
  void synchronizeHistory();
  bool nextHistoryIsPlugin(bool redo) const;
  uint64_t historyHead(bool redo) const;
  void restoreHistory(bool redo,bool alreadyStopped=false);
  void trimHistory();
  std::map<std::string,std::unique_ptr<Tracker::NativePlugin>> editors_;
  std::unique_ptr<Tracker::NativePlugin> graphEditor_;
  Tracker::GraphPluginRecipe graphEditorRecipe_;
  uint64_t graphEditorGraph_=0,graphEditorNode_=0;
  std::string graphEditorID_;
  bool graphEditorWindowOpen_=false;
  std::set<std::string> openEditors_;
  std::map<std::string,std::map<uint32_t,float>> pendingParameters_;
  std::chrono::steady_clock::time_point lastEditorChange_{};
  std::chrono::steady_clock::time_point lastStateCapture_{};
  Json lastTouched_ = nullptr;
  uint64_t touchSequence_=0;
  Plugins::PluginLibrary library_;
  History snapshot() const;
  void commit(Json plugins,Json automation,bool keepEditors=false,bool parameterOnly=false,
    std::span<const Tracker::ParameterChange> changes={},const Tracker::NativeSong *native=nullptr);
  size_t slot(const Json &) const;
  Tracker::NativePlugin &editor(size_t);
  // A failed or unflushable editor instance is closed and dropped. The rack
  // keeps the last state captured from it; the warning is reported once.
  std::string editorWarning_;
  void dropEditor(const std::string &instance,const std::string &reason) noexcept;
public:
  PluginOperations(Tracker::Document &,Project::ProjectState &,std::function<void()> stop,
    std::function<void(std::span<const Tracker::ParameterChange>)> liveParameters={},
    std::optional<std::filesystem::path> libraryPath={});
  ~PluginOperations();
  void liveBypass(std::function<void(size_t,bool)> callback) {liveBypass_=std::move(callback);}
  void liveBuses(decltype(liveBuses_) callback) {liveBuses_=std::move(callback);}
  void nativePublication(std::function<std::function<void()>(const Tracker::NativeSong &)> callback) {prepareNativePublication_=std::move(callback);}
  void recordedPublication(std::function<std::function<void()>(const std::vector<Tracker::ParameterChange> &)> callback) {prepareRecordedPublication_=std::move(callback);}
  void rackPublication(decltype(prepareRackPublication_) callback) {prepareRackPublication_=std::move(callback);}
  static std::vector<std::string> reads();
  static std::vector<std::string> writes();
  Json invoke(const std::string &,const Json &);
  Json invokeAutomation(const std::string &,const Json &);
  Json invokeLibrary(const std::string &,const Json &);
  Json invokePath(const std::string &,const Json &);
  Json invokeGraph(const std::string &,const Json &,unsigned sampleRate,bool audioActive=false);
  bool flushEditors(bool force=false); // Debounce gestures; save/close forces capture.
  void editorWarning(std::string text){editorWarning_=std::move(text);}
  std::string takeEditorWarning(){return std::exchange(editorWarning_,std::string{});}
  std::vector<GraphRackRecord> graphRack() const;
  GraphRackClone cloneRackSlot(uint32_t);
  void prepareRecipe(Tracker::GraphPluginRecipe &);
  std::vector<Tracker::PluginAudioBus> audioBuses(size_t,bool required=false);
  std::vector<Tracker::PluginAudioBus> audioBusMetadata(const std::string &identity);
  std::vector<Tracker::PluginParameter> parameterMetadata(const std::string &identity);
  // Both API domain names and all UI Undo entry points share chronological
  // history. The callback applies one document entry; alreadyStopped prevents
  // repeating a fallible transport hook halfway through a grouped operation.
  void history(bool redo,const std::function<void(bool,bool)> &documentHistory,
    const std::function<void(const Tracker::NativeSong &)> &validateNative={},
    const std::function<void(bool,const std::function<void()> &)> &liveDocumentHistory={});
  bool canUndo() {synchronizeHistory();return historyHead(false)!=0;}
  bool canRedo() {synchronizeHistory();return historyHead(true)!=0;}
  size_t openEditorCount() const {return openEditors_.size()+size_t(graphEditorWindowOpen_);}
};
}
