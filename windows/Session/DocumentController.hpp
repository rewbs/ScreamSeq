#pragma once
#include "DocumentOperations.hpp"
#include "AssetOperations.hpp"
#include "HostedProject.hpp"
#include "PluginOperations.hpp"
#include "PatternOperations.hpp"
#include "EnvelopeOperations.hpp"
#include "MixerOperations.hpp"
#include "../Api/SessionAdapter.hpp"
#include "../Project/NativeProject.hpp"
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
  std::shared_ptr<const NativePatternView> nativePattern;
  std::shared_ptr<const PatternGraphView> graphPattern;
  std::vector<uint8_t> effectColumns;
  size_t nativePatternBytes=0;
  std::filesystem::path path;
  bool dirty=false, hosted=false;
  uint64_t catalogRevision=0;
  size_t cacheBytes=0;
  unsigned channels=0, instruments=0;
  const Api::PatternSnapshot &pattern(unsigned p) const {return *patterns.at(p);}
  Tracker::Cell cell(unsigned p,unsigned r,unsigned c) const;
  std::wstring displayCell(unsigned p,unsigned r,unsigned c) const;
  std::optional<Tracker::PatternCommand> effect(unsigned p,unsigned r,unsigned c,unsigned column) const;
  std::span<const PatternNoteView> notesAt(unsigned p,unsigned r,unsigned c) const;
};
// One serial document owner. Work, cache construction and retired cache disposal
// run here. service() is called ONLY by the UI thread for playback hooks.
class DocumentController {
  std::unique_ptr<Tracker::Document> document_;
  std::unique_ptr<AssetOperations> assets_;
  std::unique_ptr<PluginOperations> plugins_;
  std::unique_ptr<HostedProjectPlayback> playback_;
  Project::ProjectState project_;
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
  std::thread thread_; // Start only after every worker dependency is initialized.
  void loop();
  void onMain(std::function<void()> task);
  std::shared_ptr<DocumentView> buildView(Tracker::Document &document,const Project::ProjectState &project,uint64_t generation);
  void install(std::shared_ptr<const DocumentView> next);
  void publish();
  void preflightGrowth(const std::string &method,const Json &params);
  void validateAssetCandidate(const Tracker::Document &candidate) const;
  void validateGraphViewGrowth(const Tracker::NativeSong &candidate) const;
  PlaybackFeedback playbackFeedback();
  void open(const std::filesystem::path &path);
  std::string revision() const;
  Json operation(const std::string &method,Json params);
public:
  DocumentController(const std::filesystem::path &input,std::string identity,
    std::function<void()> stop,std::function<void(const std::vector<Tracker::Edit>&)> edits,
    std::function<void()> beforeView={},size_t maxCacheBytes=64u*1024u*1024u,
    std::function<void(std::span<const Tracker::ParameterChange>)> liveParameters={},PlaybackHooks playbackHooks={},
    std::optional<std::filesystem::path> cataloguePath={},std::optional<std::filesystem::path> libraryPath={});
  ~DocumentController();
  bool publicationPending() const {return publicationPending_.load();}
  std::shared_ptr<const DocumentView> view();
  std::future<Json> invoke(std::string method,Json params);
  // Borrowed by the stopped UI/audio owner. Stop/join callbacks and release all
  // readers before calling prepare again; replacement and disposal run here.
  std::future<HostedProjectPlayback *> prepare(unsigned rate,Json settings,bool loop,bool offline=false,bool audition=false);
  std::future<bool> refreshPlaybackLatencies(); // Caller has stopped/joined the device.
  void service();
};
}
