#pragma once
#include "DocumentOperations.hpp"
#include "AssetOperations.hpp"
#include "SampleRecordingOperations.hpp"
#include "HostedProject.hpp"
#include "PluginOperations.hpp"
#include "PatternOperations.hpp"
#include "EnvelopeOperations.hpp"
#include "MixerOperations.hpp"
#include "TrackOperations.hpp"
#include "../Api/SessionAdapter.hpp"
#include "NativeCallReceipt.hpp"
#include "../Project/NativeProject.hpp"
#include "../Audio/PresentationClock.hpp"
#include "editor/NoteRecording.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <future>
#include <map>
#include <optional>
#include <mutex>
#include <set>
#include <thread>

namespace ScreamSeq {
struct PatternEffectView {unsigned pattern,channel;Tracker::PatternCommand command;};
struct PatternNoteView {unsigned pattern,channel;Tracker::PreciseNote note;};
struct PatternGraphLane {
  uint64_t target=0;unsigned column=0;std::string name;
  bool operator==(const PatternGraphLane &) const = default;
};
struct PatternGraphView {
  std::vector<Tracker::SignalCommand> commands; // Original order for cheap reuse comparisons.
  std::vector<size_t> sorted;
  std::vector<PatternGraphLane> lanes;
  std::map<uint64_t,uint16_t> numbers;
  const Tracker::SignalCommand *at(uint64_t pattern,unsigned row,uint64_t target,unsigned column) const;
};
struct NativePatternView {
  Tracker::PatternPerformance performance;
  Tracker::ScratchGestureLibrary scratchGestures;
  std::vector<Tracker::PreciseNote> preciseNotes;
  std::vector<PatternEffectView> effects;
  std::vector<PatternNoteView> notes;
  std::map<uint64_t,unsigned> patternIndexes,trackChannels;
};
// Immutable values only. Neither the HWND owner nor the pipe sees a Document.
// One compact wire cell per musical cell; no duplicate cells or project-sized
// array of heap-allocated display strings. Format only visible cells.
struct DocumentView {
  Api::SessionSnapshot session;
  std::map<unsigned,std::shared_ptr<const Api::PatternSnapshot>> patterns;
  std::map<unsigned,Json> samples;
  std::map<unsigned,std::shared_ptr<const std::vector<float>>> waves;
  std::map<unsigned,std::array<unsigned,120>> keyboards;
  std::array<std::wstring,256> noteNames;
  std::array<wchar_t,256> volumeLetters{},effectLetters{};
  std::array<uint8_t,256> effectMasks{};
  Json commands;
  Json recording; // Compact immutable take status; events are read explicitly.
  std::shared_ptr<const NativePatternView> nativePattern;
  std::shared_ptr<const PatternGraphView> graphPattern;
  std::vector<uint8_t> effectColumns;
  std::map<unsigned,unsigned> patternRowsPerBeat;
  size_t nativePatternBytes=0;
  std::filesystem::path path;
  bool dirty=false, hosted=false, hasOpenEditors=false;
  uint64_t catalogRevision=0;
  // Internal catalog invalidation, including entities in inactive sequences.
  // Never exposed as document data or used as musical identity.
  std::array<std::byte,32> metadataFingerprint{};
  size_t cacheBytes=0;
  unsigned channels=0, instruments=0;
  unsigned noteMin=1,noteMax=120;
  const Api::PatternSnapshot &pattern(unsigned p) const {return *patterns.at(p);}
  Tracker::Cell cell(unsigned p,unsigned r,unsigned c) const;
  std::wstring displayCell(unsigned p,unsigned r,unsigned c) const;
  std::optional<Tracker::PatternCommand> effect(unsigned p,unsigned r,unsigned c,unsigned column) const;
  std::span<const PatternNoteView> notesAt(unsigned p,unsigned r,unsigned c) const;
};
// Owned by the worker and returned immutable. Native bytes never become a
// project-sized JSON response; the disk service can write them independently.
struct RecoverySnapshot {
  std::shared_ptr<const std::vector<std::byte>> bytes;
  std::string revision,title,fingerprint,guardFingerprint;
  std::optional<std::filesystem::path> source;
  bool hasRecording=false,needsProtection=false,hasOpenEditors=false;
};
// Both callbacks execute on the native UI owner through service(). The host
// rechecks raw drafts/takes and obtains its short input lease in admit(). A
// throwing admit must leave no lease behind; finish() is called exactly once
// after successful admission, including when Stop refuses the replacement.
// The observer must outlive the controller. Construction of the first document
// has no departing source and invokes neither callback.
struct DocumentReplacementAdmission {
  virtual ~DocumentReplacementAdmission()=default;
  virtual void admit(const std::string &document,const std::string &revision)=0;
  virtual void finish(bool adopted)noexcept=0;
};
// One serial document owner. Work, cache construction and retired cache disposal
// run here. service() is called ONLY by the UI thread for playback hooks.
class DocumentController {
  struct RecordingTake {
    Tracker::NoteRecording notes;
    std::string id,baseRevision;
    double latencyMS=0;
    uint64_t startTimestamp=0,lastTimestamp=0,clockGeneration=0;
    std::shared_ptr<const RecordingTimeline> timeline;
    explicit RecordingTake(Tracker::NoteRecording value):notes(std::move(value)){}
  };
  std::unique_ptr<Tracker::Document> document_;
  std::unique_ptr<AssetOperations> assets_;
  std::unique_ptr<SampleRecordingOperations> sampleRecording_;
  std::unique_ptr<PluginOperations> plugins_;
  std::unique_ptr<HostedProjectPlayback> playback_;
  uint64_t playbackDocumentGeneration_=0;
  Project::ProjectState project_;
  std::unique_ptr<RecordingTake> recording_;
  std::string identity_;
  uint64_t generation_=0;
  std::shared_ptr<const DocumentView> view_;
  std::deque<std::shared_ptr<const DocumentView>> retired_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<std::function<void()>> jobs_, main_;
  bool closing_=false, scanPatterns_=true;
  std::set<unsigned> changedPatterns_;
  std::set<unsigned> changedSamples_;
  bool scanWaves_=true;
  // Internal worker-test seam, never exposed through the app/API/environment.
  std::function<void()> beforeView_;
  size_t maxCacheBytes_;
  std::atomic<bool> publicationPending_{false};
  std::function<void()> stop_;
  std::function<void(const std::vector<Tracker::Edit>&)> edits_;
  std::function<void(std::span<const Tracker::ParameterChange>)> liveParameters_;
  PlaybackHooks playbackHooks_;
  std::optional<std::filesystem::path> cataloguePath_;
  std::optional<std::filesystem::path> libraryPath_;
  DocumentReplacementAdmission *replacementAdmission_=nullptr;
  std::thread thread_; // Start only after every worker dependency is initialized.
  void loop();
  void onMain(std::function<void()> task);
  std::shared_ptr<DocumentView> buildView(Tracker::Document &document,const Project::ProjectState &project,uint64_t generation);
  void install(std::shared_ptr<const DocumentView> next);
  void publish();
  void publishCommitted(const std::string &method,const Json &result);
  std::unique_ptr<AssetOperations::ImportCommit> prepareAssetCompletion(const std::string &,const Json &);
  std::shared_ptr<NativeCallReceipt> nativeCallReceipt_; // Worker-only publication target for this invocation.
  std::shared_ptr<const Api::CompletedCall> completedCall_; // Worker-owned, scoped to one invocation.
  Api::CompletedCall invokeOperation(const std::string &method,Json params,const std::shared_ptr<NativeCallReceipt> &receipt={});
  void preflightGrowth(const std::string &method,const Json &params);
  void validateAssetCandidate(const Tracker::Document &candidate) const;
  void validateGraphViewGrowth(const Tracker::NativeSong &candidate) const;
  void validateDocumentCandidate(Tracker::Document &candidate);
  void validateNativeCandidate(const Tracker::NativeSong &candidate) const;
  PlaybackFeedback playbackFeedback();
  std::function<void()> prepareRackPublication(const std::vector<Tracker::PluginState> &,const std::vector<Tracker::ParameterChange> &,const Tracker::NativeSong &);
  std::function<void()> guardPlaybackPublication(HostedProjectPlayback *,uint64_t,std::function<bool()>);
  std::function<void()> prepareNativePublication(const Tracker::NativeSong &);
  void open(const std::filesystem::path &path);
  void installCandidate(Project::OpenedProject candidate,std::function<void()> beforeCommit={});
  std::shared_ptr<const RecoverySnapshot> recoverySnapshot();
  Json recordingSummary(const RecordingTake *,const std::string &atRevision,bool events=false) const;
  std::unique_ptr<RecordingTake> hydrateRecording(const Tracker::Document &,const Project::ProjectState &,const std::string &nextRevision) const;
  std::optional<Tracker::RecordedPosition> recordingPosition(RecordingTake &,uint64_t timestamp);
  std::optional<Tracker::RecordedPosition> recordingStopPosition(RecordingTake &);
  void overlayRecording(Project::ProjectState &,const RecordingTake &,bool closeHeld=true);
  void installRecording(std::unique_ptr<RecordingTake>,bool removePreserved=false);
  Json recordingOperation(const std::string &method,const Json &params);
  Json parameterActivityOperation(const std::string &method,const Json &params);
  std::function<void()> prepareNativeUpdate(const Tracker::NativeSong &before,const Tracker::NativeSong &next);
  std::string revision() const;
  std::string revision(uint64_t documentRevision) const;
  Json operation(const std::string &method,Json params);
public:
  DocumentController(const std::filesystem::path &input,std::string identity,
    std::function<void()> stop,std::function<void(const std::vector<Tracker::Edit>&)> edits,
    std::function<void()> beforeView={},size_t maxCacheBytes=64u*1024u*1024u,
    std::function<void(std::span<const Tracker::ParameterChange>)> liveParameters={},PlaybackHooks playbackHooks={},
    std::optional<std::filesystem::path> cataloguePath={},std::optional<std::filesystem::path> libraryPath={},
    DocumentReplacementAdmission *replacementAdmission=nullptr);
  ~DocumentController();
  bool publicationPending() const {return publicationPending_.load();}
  std::shared_ptr<const DocumentView> view();
  std::future<Json> invoke(std::string method,Json params);
  std::future<Api::CompletedCall> invokeCompleted(std::string method,Json params,std::shared_ptr<NativeCallReceipt> receipt={});
  std::future<std::shared_ptr<const RecoverySnapshot>> captureRecovery(std::string expectedRevision);
  // Parse, validate and allocate the complete next view before stopping or
  // replacing anything. A recovered document has a new identity, no file path,
  // and explicit unsaved state, without manufacturing music/history edits.
  std::future<std::shared_ptr<const DocumentView>> recover(
    std::shared_ptr<const std::vector<std::byte>> bytes,std::string expectedRevision,bool discard=false,
    std::string expectedFingerprint={});
  // Borrowed by the stopped UI/audio owner. Stop/join callbacks and release all
  // readers before calling prepare again; replacement and disposal run here.
  std::future<HostedProjectPlayback *> prepare(unsigned rate,Json settings,bool loop,bool offline=false,bool audition=false);
  std::future<bool> refreshPlaybackLatencies(); // Prepared live update; false retries after the active handoff.
  void service();
};
}
