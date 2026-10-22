#pragma once
#include "HostedProject.hpp"
#include "GraphOperations.hpp"
#include "windows/Plugins/PluginLibrary.hpp"
#include "editor/hosted/RecordedGestureCapture.hpp"
#include "editor/hosted/RecordedGestureTake.hpp"
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
  std::function<std::function<void()>(const Tracker::NativeSong &,const Tracker::NativeSong &)> prepareNativeUpdate_;
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
  std::string graphParameterGesture_;
  uint64_t graphParameterGestureSequence_=0,graphParameterGestureRevision_=0,graphParameterGestureGraph_=0,graphParameterGestureNode_=0;
  uint32_t graphParameterGestureID_=0;
  std::set<std::string> openEditors_;
  std::map<std::string,std::map<uint32_t,float>> pendingParameters_;
  // Vendor notifications are consumed once. Keep their latest values until
  // the complete live batch is admitted, independently of opaque-state capture.
  std::map<std::string,std::map<uint32_t,float>> pendingLiveParameters_;
  struct RecordedEditors {
    std::shared_ptr<Tracker::RecordedGestureTake> take;
    std::function<std::pair<std::string,uint64_t>()> context;
    std::function<void(std::span<const Tracker::ParameterChange>)> preview;
    std::map<std::string,std::vector<Tracker::PluginParameter>> catalogs;
    std::map<std::string,Tracker::PluginEditBoundary> releaseCuts;
    size_t previewedEvents=0;
  };
  std::unique_ptr<RecordedEditors> recordedEditors_;
  bool recordedPublicationActive_=false;
  bool ownsRecordedEditor(const std::string &)const noexcept;
  void requireManualEditors()const;
  void requireManualOperation(const std::string &)const;
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
  void nativeUpdates(std::function<std::function<void()>(const Tracker::NativeSong &,const Tracker::NativeSong &)> callback) {prepareNativeUpdate_=std::move(callback);}
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
  // Copy manual rack editor state into an independent recovery project only.
  // Does not consume notifications, commit history, stop playback or apply the
  // graph recipe editor's explicit-Apply draft.
  bool overlayRecoveryState(Project::ProjectState &copy) const;
  // Archive already captured original-time input without touching providers,
  // playback, manual state or history. Restoring the archive never rearms it.
  bool overlayRecordedRecovery(Project::ProjectState &copy) const;
  void editorWarning(std::string text){editorWarning_=std::move(text);}
  std::string takeEditorWarning(){return std::exchange(editorWarning_,std::string{});}
  std::vector<GraphRackRecord> graphRack() const;
  GraphRackClone cloneRackSlot(uint32_t);
  void prepareRecipe(Tracker::GraphPluginRecipe &);
  std::vector<Tracker::PluginAudioBus> audioBuses(size_t,bool required=false);
  std::vector<Tracker::PluginAudioBus> audioBusMetadata(const std::string &identity);
  std::vector<Tracker::PluginParameter> parameterMetadata(const std::string &identity);
  Tracker::PluginEditSource parameterEditSource(const std::string &identity);
  // Internal worker adapter; not a public recording API. The session must
  // flush earlier manual input and hold the native input barrier while arming.
  // The scope retains the take even after failure/completion until restoration
  // succeeds. Preview must use transient playback controls, never manual writes.
  void beginRecordedEditors(std::shared_ptr<Tracker::RecordedGestureTake>,
    std::function<std::pair<std::string,uint64_t>()> context,
    std::function<void(std::span<const Tracker::ParameterChange>)> preview);
  // One bounded poll per provider. Fixed cuts acknowledge their own completion.
  // Without cuts, true acknowledges empty paired drains only when the caller
  // holds an actual native release barrier. False means another poll is needed.
  bool pollRecordedEditors(bool releaseBarrier=false);
  // Capture all VST3 queue cuts on their one native owner, then seal once.
  // Later polls stop at these cuts even when new input continues to arrive.
  // This is an input boundary, not a lease for baseline restoration: the
  // session still needs the exclusive native barrier required by release.
  uint64_t sealRecordedEditors();
  bool hasRecordedEditors()const noexcept{return bool(recordedEditors_);}
  // Called under the same input barrier after commit or explicit discard.
  // Restore audible previews before releasing ownership; refusal retains it.
  // An open vendor must restore its complete state in place. If hidden state
  // cannot be restored from parameters, close it explicitly and retry so its
  // accepted baseline can replace the closed provider without a false capture.
  // Post-cut input blocks restoration. Only explicit discard with the editor
  // closed may abandon that later transient input along with the stopped take.
  bool releaseRecordedEditors(bool discard,const std::function<bool()> &restorePreview);
  // Worker-only capture completion. The session supplies its current full
  // revision/clock epoch and retains the owners through this call. Normal
  // manual/vendor ingestion, release barriers and recovery belong to that
  // session; this entry point never flushes or serializes a live vendor.
  bool commitRecordedGestures(std::span<Tracker::RecordedGestureCapture *const>,
    const std::string &revision,uint64_t playbackEpoch,bool acknowledgeLoss=false);
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
