#pragma once

#include <nlohmann/json.hpp>
#include "../../editor/WriteOutcome.hpp"
#include "ProtocolLimits.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <memory>
#include <stdexcept>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace ScreamSeq::Api {
using Json = nlohmann::json;
inline Json errorResponse(const Json &id, int code, const std::string &message) {
  return {{"jsonrpc","2.0"},{"id",id},{"error",{{"code",code},{"message",message}}}};
}
inline bool validEnvelope(const Json &q) {
  const auto bounded = [](const Json &v) {
    return v.is_string() && !v.get_ref<const std::string &>().empty() && v.get_ref<const std::string &>().size() <= 80;
  };
  return q.is_object() && q.size() == 4 && q.contains("jsonrpc") && q["jsonrpc"] == "2.0"
    && q.contains("id") && bounded(q["id"]) && q.contains("method") && bounded(q["method"])
    && q.contains("params") && q["params"].is_object();
}
// Internal native completion receipt. This is not added to the wire error or
// inferred from a snapshot: only the operation which returned may supply it.
struct CompletedCall {
  std::string method,document,revision;
  Json result;
};
struct ApiError : std::runtime_error {
  int code;
  std::optional<Tracker::WriteOutcome> outcome;
  std::shared_ptr<const CompletedCall> completed;
  ApiError(int c, const std::string &message, std::optional<Tracker::WriteOutcome> effect = {},
      std::shared_ptr<const CompletedCall> returned = {})
    : std::runtime_error(message), code(c), outcome(std::move(effect)),completed(std::move(returned)) {}
};
inline Json outcomeData(const Tracker::WriteOutcome &outcome) {
  const char *state = "unknown";
  switch(outcome.state) {
  case Tracker::CommitOutcome::NotCommitted: state="notCommitted"; break;
  case Tracker::CommitOutcome::NoChange: state="noChange"; break;
  case Tracker::CommitOutcome::Committed: state="committed"; break;
  case Tracker::CommitOutcome::Unknown: break;
  }
  Json data={{"writeOutcome",state}};
  if(!outcome.document.empty())data["documentId"]=outcome.document;
  if(!outcome.revision.empty())data["revision"]=outcome.revision;
  return data;
}
// Owned value snapshots, not references into Document or GUI state. The host
// supplies the existing Mac wire data dictionaries; no fabricated song defaults.
struct SessionSnapshot {
  std::string revision, documentId;
  Json document = Json::object(), context = Json::object(), transport = Json::object();
};
struct PatternSnapshot {
  unsigned rows = 0, channels = 0;
  // row-major note, instrument, volumeCommand, volume, effect, parameter
  std::vector<std::array<std::uint8_t, 6>> cells;
};
class SessionHost {
public:
  virtual ~SessionHost() = default;
  virtual SessionSnapshot snapshot() = 0;
  virtual bool supportsDocumentOperations() const {return false;}
  virtual std::vector<std::string> additionalDocumentReads() const {return {};}
  virtual std::vector<std::string> additionalDocumentWrites() const {return {};}
  virtual Json documentOperation(const std::string &,const Json &) {throw ApiError(-32601,"Document operations unavailable");}
  // Non-document services own their revision/envelope; never snapshot the song
  // or impose its history/transport semantics on a library preference write.
  virtual std::vector<std::string> independentReads() const {return {};}
  virtual std::vector<std::string> independentWrites() const {return {};}
  virtual Json independentGuards() const {return Json::object();}
  virtual Json independentOperation(const std::string &,const Json &) {throw ApiError(-32601,"Independent service unavailable");}
  virtual PatternSnapshot pattern(unsigned index) = 0;
  // Called on the owning control thread only, after complete validation.
  // Preserve requested region settings and existing loop default. Throw ApiError
  // (-32003 for engine failure) rather than reporting queued-but-not-started play.
  virtual void play(const Json &settings) = 0;
  virtual void stop() = 0;
  virtual bool supportsPlaybackLoop() const { return false; }
  virtual void setPlaybackLoop(bool) { throw ApiError(-32601,"Live playback loop is not available in this host"); }
  virtual void navigate(const Json &) { throw ApiError(-32601,"Navigation is not available in this host"); }
  virtual Json workspace(const std::string &, const Json &) { throw ApiError(-32601,"Workspace is not available in this host"); }
};
class SessionAdapter {
  SessionHost *host_ = nullptr;
  const std::thread::id owner_ = std::this_thread::get_id();
  static constexpr std::size_t maxCacheEntries = 64;
  static constexpr std::size_t maxCacheBytes = 8 * 1024 * 1024;
  struct Cached { std::string id, request; Json response; std::size_t bytes; };
  std::deque<Cached> cache_;
  std::size_t cacheBytes_ = 0;
  std::set<std::string> activeWrites_;
  // Successful writes and classified committed/unknown errors, FIFO by
  // insertion (replays do not refresh age). Unclassified errors remain uncached.
  // Charge compact UTF-8 JSON request + response bytes, excluding newlines.
  // This bounds retained serialized content, not JSON DOM/allocator heap usage.
  void cacheResponse(const Json &request, const Json &response, std::optional<std::size_t> serializedBytes = {}) noexcept {
    try {
      auto canonical=request.dump();
      const auto requestBytes=canonical.size();
      if(requestBytes>maxCacheBytes) return;
      const auto responseBytes=serializedBytes ? *serializedBytes : response.dump().size();
      if(responseBytes>maxCacheBytes-requestBytes) return;
      const auto bytes=requestBytes+responseBytes;
      Cached entry{request.at("id").get<std::string>(),std::move(canonical),response,bytes};
      while(!cache_.empty() && (cache_.size()>=maxCacheEntries || cacheBytes_>maxCacheBytes-bytes)) {
        cacheBytes_-=cache_.front().bytes;
        cache_.pop_front();
      }
      cache_.push_back(std::move(entry));
      cacheBytes_+=bytes;
    } catch(...) {
      // Retention is best effort. Serialization/allocation can fail AFTER the
      // host committed; never turn that successful write into an error envelope
      // with the pre-write revision. Oversize/unserializable entries are not cached.
    }
  }
  Json completeWrite(const Json &request, Json response) {
    // The host has returned, but JSON construction, strict UTF-8 serialization
    // or the wire bound may still fail. The caller classifies this as unknown
    // (not as a rejected write) and retains a small, replayable error instead.
    // Do this before caching: the transport cannot repair the adapter's receipt.
    std::size_t bytes=0;
    try {bytes=response.dump().size();}
    catch(const Json::exception &) {throw ApiError(-32003,"Write completed but response serialization failed; read state before retrying.");}
    if(bytes>=maxProtocolResponseBytes)
      throw ApiError(-32003,"Write completed but response exceeds transport limit; read state before retrying.");
    cacheResponse(request,response,bytes);
    return response;
  }
  static void require(bool condition, const char *message) {
    if(!condition) throw ApiError(-32602,message);
  }
  static void keys(const Json &p, std::initializer_list<const char *> allowed) {
    for(auto it=p.begin();it!=p.end();++it)
      require(std::any_of(allowed.begin(),allowed.end(),[&](const char *key){return it.key()==key;}),"Unknown parameter");
  }
  static unsigned integer(const Json &p, const char *key, unsigned fallback, unsigned low, unsigned high, bool required=false) {
    if(!p.contains(key)) { require(!required,"Missing integer parameter"); return fallback; }
    const auto &v=p[key];
    require(v.is_number_integer() && !v.is_boolean(),"Expected an integer (not a boolean)");
    require(v>=low && v<=high,"Integer parameter is outside its range");
    return v.get<unsigned>();
  }
  Json describe() const {
    Json result= {{"protocol","ScreamSeq local API"},{"version",1},
      {"reads",{"api.describe","document.get","pattern.get","transport.get","context.get","workspace.get","workspace.commands.get"}},
      {"writes",{"transport.play","transport.stop","context.set","workspace.input","workspace.panel","workspace.layout","workspace.shortcut.set"}},{"maxPatternCells",4096},
      {"coordinates","Patterns, rows, channels and orders are zero-based. Samples and instruments are one-based; zero means none."},
      {"noteEncoding","0=empty; 1=C-0, 49=C-4, 61=C-5. Special notes and format command IDs follow document.get."},
      {"audioPortTrims","graph.trim.get/set use stable port keys, -48..48 dB gains, inverse input/output links and dB source modulation. Recipe or song nodes, buses, stages and group boundaries; dryRun, revision guards and unified Undo. Audio followers are not trim sources."},{"platform","windows"},{"musicalEditing",false},{"fullApiParity",false},
      {"writeReplayCache",{{"maxEntries",maxCacheEntries},{"maxSerializedBytes",maxCacheBytes},
        {"retainedEntries",cache_.size()},{"retainedSerializedBytes",cacheBytes_},
        {"accounting","compact UTF-8 JSON request plus response; excludes newlines; not heap usage"},
        {"eviction","oldest insertion first; replay does not refresh"},
        {"scope","successful writes, including no-op and dryRun, and classified committed/unknown errors; exact parsed request replay while retained"},
        {"oversize","success returned without caching; retention failure does not reject a completed write"},
        {"durable",false}}},
      {"revisionGuards",{{"transport.play",{"expectedRevision"}},{"transport.stop",{"expectedRevision"}},
        {"context.set",{"expectedRevision","expectedContext"}},
        {"workspace.input",{"expectedRevision","expectedContext"}},
        {"workspace.panel",Json::array()},{"workspace.layout",Json::array()},{"workspace.shortcut.set",Json::array()}}},
      {"workspaceSubset",{{"panels",{"notes","samples","automation","instruments","graphCurve","preciseNotes"}},{"placements",{"right","hide"}},
        {"editorPlacements",{{"automation",{"right","bottom","secondary","float","hide"}},{"instruments",{"right","bottom","secondary","float","hide"}},{"graphCurve",{"right","bottom","secondary","float","hide"}},{"preciseNotes",{"right","bottom","secondary","float","hide"}}}},
        {"layouts",{"Compose","Pattern focus","Sound design","Connected","Graph editing","Save custom","Restore custom","Delete custom","Reload saved"}},
        {"namedLayouts",{{"optionalField","savedName"},{"default","Custom"},{"maximum",24},{"nameCharacters",64}}},
        {"schema","windows/Api/workspace.schema.json"}}},
      {"transport","Private explicit named pipe; 32 MiB request and response, including newline; one request per connection. Transport writes require expectedRevision; context.set requires expectedRevision and expectedContext. workspace.input also requires both tokens; other workspace operations accept neither token. Unsupported parameters reject."}};
    if(host_ && host_->supportsPlaybackLoop()) {
      result["writes"].push_back("transport.loop");
      result["revisionGuards"]["transport.loop"]={"expectedRevision"};
    }
    if(host_ && host_->supportsDocumentOperations()) {
      for(const auto *m:{"pattern.commands","sample.get","sample.waveform.get","pattern.notes.get","document.timing.get","arrangement.get","arrangement.matrix","automation.formula.reference","automation.formula.preview"}) result["reads"].push_back(m);
      for(const auto *m:{"pattern.apply","history.undo","history.redo","document.patch","pattern.create","order.edit","sequence.select","document.save","document.open","pattern.notes.set","document.timing.set","song.annotate","arrangement.copyBlock"}) {
        result["writes"].push_back(m);result["revisionGuards"][m]={"expectedRevision"};
      }
      result["musicalEditing"]=true;
      result["arrangementMatrix"]={{"maximumOrders",128},{"maximumChannels",32},{"defaultOrders",64},{"defaultChannels",16},{"densityBins",16},
        {"density","events = occupied tracker cells + precise on/off events + native FX records; notes = pitched tracker cells + precise onsets. trackerEvents, preciseEvents and nativeFxEvents expose the stored layers separately; counts and bins are uint32."},
        {"copy","Whole channel blocks in the current sequence, including precise notes and every FX column. makeUnique preserves exact destination pattern timing and unrelated native lanes/links. Explicit clip copies the overlapping span and excludes events at its end."},
        {"preview","dryRun validates the complete candidate without consuming IDs/history. wouldChange includes native-only edits; changedCells counts six-field cell edits only. Changed Apply creates one Undo and stops playback; no-op preserves playback and Redo."}};
      for(const auto &m:host_->additionalDocumentReads()) if(std::find(result["reads"].begin(),result["reads"].end(),m)==result["reads"].end())result["reads"].push_back(m);
      for(const auto &m:host_->additionalDocumentWrites()) {
        if(std::find(result["writes"].begin(),result["writes"].end(),m)==result["writes"].end())result["writes"].push_back(m);result["revisionGuards"][m]=(m=="parameter.activity.watch"||m=="sample.recording.stop"||m=="sample.recording.discard")?Json::array():Json::array({m=="plugin.library.set"?"expectedLibraryRevision":"expectedRevision"});
      }
      result["windowsExtensions"]={{"document.open","absolute path, expectedRevision, discard:true required for unsaved work"},
        {"scratch.gestures.get/set/clone/remove","Song-local SK gesture slots 1..255. Two normalized motion/fader lanes use 65536 units/cycle. Set accepts a preset seed and independent overrides; live edits publish a prepared bank, used gestures cannot be removed. Clone optionally reassigns its captured SK cell in one Undo."},
        {"plugin.editor.open","slot and expectedRevision; native VST3 editor on the private STA; no musical change unless the vendor emits edits"},
        {"plugin.editor.close","slot and expectedRevision; flush pending baseline edits before closing"},
        {"plugin.path.get/scan/set","Explicit VST3 location repair by stable plugin ID. Scan/set require expectedRevision; set also requires path and expectedModuleSHA256 from get. Dry set verifies the scanned binary without vendor-state decoding. Actual set changes only path and uses plugin Undo."},
        {"graph.plugin.path.get/scan/set","The same Windows VST3 location workflow for a graph/node target, using document Undo and preserving the graph recipe's state, ports and routing."}};
      result["patternEffects"]={{"columns","1–8 FX columns per channel. Code/value cursor fields are 3+2*column and 4+2*column."},
        {"methods","pattern.effects.get/set and pattern.performance.get/set merge ordinary FX 1 with all native commands. pattern.effect.set edits one cell; null clears it."},
        {"commands","tracker, parameter-set, parameter-slide, pitch-set, pitch-slide, note-cut, nudge-forward (NF), nudge-reverse (NR). Nudges: strength value 0..1, durationBeats 1/65536..65536 (default1), bounded by the remaining pattern; legacy row-unit duration is rejected; sample-only, reversal above 0.5 opposing strength. Use pattern.commands for source-format IDs and two-character displayCode; its native array supplies descriptor-driven kind:native operations, typed named parameters, ranges, units, defaults, scope and equivalents. Native UI time defaults to beats, converted with the current pattern rowsPerBeat."},
        {"timing","65536 units per row; tracker commands require row boundaries. Bindings use stable plugin instance and parameter IDs."},
        {"transforms","pattern.transform uses shared selection/channel/note-track/pattern/song transforms; field effect includes all FX columns. Precise notes remain independent."}};
      const auto extraReads=host_->additionalDocumentReads();
      if(std::find(extraReads.begin(),extraReads.end(),"recording.get")!=extraReads.end())
        result["recording"]={{"schema","windows/Api/recording.schema.json"},{"maxCaptureEvents",1024},{"maxTakeEvents",65536},
          {"hostClock","QPC converted to 100 ns units; timestamps are decimal uint64 strings"},
          {"timing","65536 units per row; zero quantization retains exact timing. Positive latencyMS places input earlier."},
          {"history","Start/capture/stop/discard do not change musical revision. A stopped compatible take commits in one document Undo. Failed commits retain the take."},
          {"recovery","Autosave copies a live take without stopping it; restored takes are stopped, with fresh IDs and preserved compatibility."}};
    }
    if(host_) {
      for(const auto &m:host_->independentReads())result["reads"].push_back(m);
      for(const auto &m:host_->independentWrites())result["writes"].push_back(m);
      result["revisionGuards"].update(host_->independentGuards());
      const auto independent=host_->independentReads();
      if(std::find(independent.begin(),independent.end(),"recovery.status")!=independent.end())
        result["recovery"]={{"schema","windows/Api/recovery.schema.json"},{"intervalSeconds",10},{"generations",10},
          {"restore","Opaque listed ID; protects the current unsaved song, then opens an unsaved document with a new identity."},
          {"recording","Live and imported unfinished takes are preserved. Restored takes are stopped and remain reviewable; incompatibility prevents silent commit."}};
      if(std::find(independent.begin(),independent.end(),"midi.settings.get")!=independent.end())
        result["midi"]={{"schema","windows/Api/midi.schema.json"},{"input","WinMM device-interface IDs; empty source disconnects. Discovery and connection run on a control worker."},
          {"timestampPrecision","Driver milliseconds anchored to the advertised QPC 100 ns clock, with reported anchor uncertainty."},
          {"overflow","Bounded callback queue; loss quarantines pending input, releases held audition notes and retains a stopped take for review."},
          {"settings","Independent expectedMidiRevision; settings affect the next take, without changing the pinned target of a retained take."}};
    }
    return result;
  }
  Json getPattern(const Json &p) {
    keys(p,{"pattern","startRow","rowCount","startChannel","channelCount"});
    const auto index=integer(p,"pattern",0,0,65535,true);
    const auto pat=host_->pattern(index);
    if(!pat.rows || !pat.channels || pat.rows>65535 || pat.channels>65535 || pat.cells.size()!=std::size_t(pat.rows)*pat.channels)
      throw ApiError(-32003,"Host returned an invalid pattern snapshot");
    const auto row=integer(p,"startRow",0,0,pat.rows-1);
    const auto channel=integer(p,"startChannel",0,0,pat.channels-1);
    const auto nc=integer(p,"channelCount",pat.channels-channel,1,pat.channels-channel);
    const auto nr=integer(p,"rowCount",std::min(pat.rows-row,4096/nc),1,pat.rows-row);
    require(nr && std::uint64_t(nr)*nc<=4096,"Read at most 4096 cells; paginate by row/channel");
    Json cells=Json::array();
    for(unsigned r=row;r<row+nr;++r) for(unsigned c=channel;c<channel+nc;++c) {
      const auto &v=pat.cells[std::size_t(r)*pat.channels+c];
      cells.push_back({{"row",r},{"channel",c},{"note",v[0]},{"instrument",v[1]},
        {"volumeCommand",v[2]},{"volume",v[3]},{"effect",v[4]},{"parameter",v[5]}});
    }
    return {{"pattern",index},{"rows",pat.rows},{"channels",pat.channels},{"cells",std::move(cells)},
      {"startRow",row},{"rowCount",nr},{"startChannel",channel},{"channelCount",nc}};
  }
  static unsigned patternRows(const SessionSnapshot &s, unsigned index) {
    for(const auto &p:s.document.at("patterns")) if(p.at("index")==index) return p.at("rows").get<unsigned>();
    throw ApiError(-32602,"Pattern does not exist");
  }
  static Json prepareNavigation(const Json &p, const SessionSnapshot &s) {
    keys(p,{"expectedRevision","expectedContext","pattern","row","channel","column","following"});
    require(p.size()>2,"Supply at least one navigation field and both revision tokens");
    require(p.contains("expectedContext") && p["expectedContext"].is_string(),"expectedContext is required");
    if(p["expectedContext"]!=s.context.at("contextRevision"))
      throw ApiError(-32001,"Edit cursor changed; read context.get and prepare navigation again");
    Json n=s.context;
    const auto oldPattern=n.value("pattern",0u);
    const auto index=integer(p,"pattern",oldPattern,0,65535);
    const auto rows=patternRows(s,index), channels=s.document.at("channels").get<unsigned>();
    require(rows>0 && channels>0,"Choose an allocated nonempty pattern");
    n["pattern"]=index;
    n["row"]=integer(p,"row",std::min(n.value("row",0u),rows-1),0,rows-1);
    n["channel"]=integer(p,"channel",std::min(n.value("channel",0u),channels-1),0,channels-1);
    unsigned fields=4;
    if(s.document.contains("effectColumns")){const auto count=s.document.at("effectColumns").at(n.at("channel").get<unsigned>()).get<unsigned>();require(count>=1&&count<=8,"Invalid effect-column count");fields=2+2*count;}
    n["column"]=integer(p,"column",std::min(n.value("column",0u),fields),0,fields);
    if(p.contains("following")) require(p["following"].is_boolean(),"following must be boolean");
    n["following"]=p.value("following",index==oldPattern ? n.value("following",true) : false);
    return n;
  }
  static void validatePlay(const Json &p, const SessionSnapshot &s) {
    keys(p,{"expectedRevision","order","pattern","startRow","endRow","cursorRow","loop"});
    const auto order=integer(p,"order",0,0,65535);
    const auto &orders=s.document.at("orders");
    require(orders.is_array() && order<orders.size(),"Select a playable order");
    const auto orderRows=patternRows(s,orders[order].get<unsigned>());
    const auto cursor=integer(p,"cursorRow",0,0,65535);
    if(p.contains("loop")) require(p["loop"].is_boolean(),"loop must be boolean");
    if(p.contains("pattern")) {
      const auto rows=patternRows(s,integer(p,"pattern",0,0,65535,true));
      const auto start=integer(p,"startRow",0,0,65535);
      const auto end=integer(p,"endRow",rows,1,65535);
      require(start<end && end<=rows && cursor>=start && cursor<end,"Cursor must lie inside the playback range");
    } else {
      require(!p.contains("startRow") && !p.contains("endRow"),"Row bounds require a pattern");
      require(cursor<orderRows,"Cursor is outside the pattern");
    }
  }
public:
  SessionAdapter() = default; // only api.describe until a real host is attached
  explicit SessionAdapter(SessionHost &host) : host_(&host) {}
  SessionAdapter(const SessionAdapter &) = delete;
  SessionAdapter &operator=(const SessionAdapter &) = delete;
  Json handle(const Json &q) {
    if(!validEnvelope(q)) return errorResponse(nullptr,-32600,"Use jsonrpc 2.0, a unique string id, method and object params");
    if(std::this_thread::get_id()!=owner_) return errorResponse(q["id"],-32002,"Dispatch onto the session control thread");
    const std::string method=q["method"];
    const auto &p=q["params"];
    const bool workspace=method=="workspace.get" || method=="workspace.panel" || method=="workspace.layout" || method=="workspace.commands.get" || method=="workspace.shortcut.set" || method=="workspace.input";
    const auto reads=host_ ? host_->additionalDocumentReads() : std::vector<std::string>{};
    const auto writes=host_ ? host_->additionalDocumentWrites() : std::vector<std::string>{};
    const auto separateReads=host_?host_->independentReads():std::vector<std::string>{};
    const auto separateWrites=host_?host_->independentWrites():std::vector<std::string>{};
    const bool independentRead=std::find(separateReads.begin(),separateReads.end(),method)!=separateReads.end();
    const bool independentWrite=std::find(separateWrites.begin(),separateWrites.end(),method)!=separateWrites.end();
    const bool docRead=host_ && host_->supportsDocumentOperations() && (std::find(reads.begin(),reads.end(),method)!=reads.end() || method=="pattern.commands" || method=="sample.get" || method=="sample.waveform.get" || method=="pattern.notes.get" || method=="document.timing.get" || method=="arrangement.get" || method=="arrangement.matrix" || method=="automation.formula.reference" || method=="automation.formula.preview");
    const bool docWrite=host_ && host_->supportsDocumentOperations() && (std::find(writes.begin(),writes.end(),method)!=writes.end() || method=="pattern.apply" || method=="history.undo" || method=="history.redo" || method=="document.patch" || method=="pattern.create" || method=="order.edit" || method=="sequence.select" || method=="document.save" || method=="document.open" || method=="pattern.notes.set" || method=="document.timing.set" || method=="song.annotate" || method=="arrangement.copyBlock");
    const bool liveLoop=method=="transport.loop" && host_ && host_->supportsPlaybackLoop();
    const bool write=independentWrite || docWrite || method=="transport.play" || method=="transport.stop" || liveLoop || method=="context.set" || (workspace && method!="workspace.get" && method!="workspace.commands.get");
    if(!write && !independentRead && !docRead && !workspace && method!="api.describe" && method!="document.get" && method!="context.get" && method!="pattern.get" && method!="transport.get")
      return errorResponse(q["id"],-32601,"Unknown method; call api.describe");
    std::string revision;
    bool hostReturned=false;
    // Completion can fail after a host accepted a write, including a no-op or
    // an independently revisioned side effect. Never fabricate a rejection or
    // advertise the pre-write song revision as authoritative in that case.
    const auto failure=[&](int code,const std::string &message,std::optional<Tracker::WriteOutcome> outcome) {
      // A later snapshot/callback can classify its own failure, not the write
      // that already returned. Only an exception from the host write itself
      // may supply that write's authoritative outcome and identity.
      if(write&&hostReturned)outcome=Tracker::WriteOutcome{};
      auto reply=errorResponse(q["id"],code,message);
      if(outcome)reply["error"]["data"]=outcomeData(*outcome);
      else if(!revision.empty())reply["error"]["data"]={{"revision",revision}};
      if(write&&outcome&&outcome->needsReconciliation())cacheResponse(q,reply);
      return reply;
    };
    try {
      if(write) for(const auto &cached:cache_) if(cached.id==q["id"].get_ref<const std::string &>()) {
        // Canonical JSON ignores object key order/whitespace but preserves
        // integer vs floating-point parameters (JSON operator== does not).
        if(cached.request!=q.dump()) return errorResponse(q["id"],-32600,"Request id was already used with different content");
        return cached.response;
      }
      if(!host_ && method!="api.describe") throw ApiError(-32002,"Session is not attached");
      const auto requestId=q["id"].get<std::string>();
      if(write&&!activeWrites_.insert(requestId).second)throw ApiError(-32002,"Request is still running; retry with the same ID after it completes");
      struct ActiveGuard {std::set<std::string> &ids;const std::string &id;bool active;~ActiveGuard(){if(active)ids.erase(id);}}active{activeWrites_,requestId,write};
      if(independentRead || independentWrite) {
        auto result=host_->independentOperation(method,p);hostReturned=true;
        Json response={{"jsonrpc","2.0"},{"id",q["id"]},{"result",std::move(result)}};
        return write ? completeWrite(q,std::move(response)) : response;
      }
      SessionSnapshot before;
      if(host_) before=host_->snapshot();
      else before.revision="unbound";
      revision=before.revision;
      if(write && (!workspace || method=="workspace.input") && method!="plugin.library.set" && method!="parameter.activity.watch" && method!="sample.recording.stop" && method!="sample.recording.discard") {
        require(p.contains("expectedRevision") && p["expectedRevision"].is_string(),"expectedRevision is required");
        const auto expected=p["expectedRevision"].get<std::string>();
        require(!expected.empty() && expected.size()<=200 && expected.find('\0')==std::string::npos,"Invalid expectedRevision");
        if(expected!=before.revision) throw ApiError(-32001,"Song changed; read its current revision and prepare the edit again");
      }
      Json data;
      if(method=="api.describe") { keys(p,{}); data=describe(); }
      else if(method=="document.get") { keys(p,{}); data=before.document; }
      else if(method=="context.get") { keys(p,{}); data=before.context; }
      else if(method=="transport.get") { keys(p,{}); data=before.transport; }
      else if(method=="pattern.get") data=getPattern(p);
      else if(docRead || docWrite) {data=host_->documentOperation(method,p);hostReturned=true;}
      else if(workspace) {
        if(method=="workspace.input") {
          keys(p,{"expectedRevision","expectedContext","instrument","octave"});
          require(p.contains("expectedContext")&&p.at("expectedContext").is_string(),"expectedContext is required");
          if(p.at("expectedContext")!=before.context.at("contextRevision"))throw ApiError(-32001,"Workspace context changed; read it again");
          require(p.contains("instrument")||p.contains("octave"),"Supply instrument or octave");
          const auto inputInteger=[&](const char *key,unsigned low,unsigned high) {
            if(!p.contains(key))return;
            const auto &raw=p.at(key);require(raw.is_number(),"Input selection must be an integer number");
            const auto value=raw.get<double>();
            require(std::isfinite(value)&&std::floor(value)==value&&value>=low&&value<=high,"Input selection is outside its integer range");
          };
          inputInteger("instrument",1,255);inputInteger("octave",0,8);
        }
        data=host_->workspace(method,p);hostReturned=true;
      }
      else if(method=="context.set") {
        host_->navigate(prepareNavigation(p,before));hostReturned=true; data=host_->snapshot().context;
      }
      else if(method=="transport.play") {
        validatePlay(p,before); Json settings=p; settings.erase("expectedRevision");
        host_->play(settings);hostReturned=true; data={{"playing",true},{"region",settings}};
      } else if(liveLoop) {
        keys(p,{"expectedRevision","enabled"});require(p.contains("enabled") && p.at("enabled").is_boolean(),"enabled must be boolean");
        const bool enabled=p.at("enabled").get<bool>();
        host_->setPlaybackLoop(enabled);hostReturned=true;data={{"loop",enabled}};
      } else {
        keys(p,{"expectedRevision"}); host_->stop();hostReturned=true; data={{"playing",false}};
      }
      const auto after=(write || docRead) ? host_->snapshot() : before;
      Json result={{"revision",after.revision},{"changed",before.revision!=after.revision},
        {"playbackStopped",before.transport.value("playing",false) && !after.transport.value("playing",false)}, {"data",std::move(data)}};
      if(method=="context.get" || method=="context.set" || method=="workspace.input")
        result["contextChanged"]=before.context.at("contextRevision")!=after.context.at("contextRevision");
      else if(host_) result["documentId"]=after.documentId;
      Json response={{"jsonrpc","2.0"},{"id",q["id"]},{"result",std::move(result)}};
      return write ? completeWrite(q,std::move(response)) : response;
    } catch(const ApiError &e) {
      return failure(e.code,e.what(),e.outcome);
    } catch(const std::exception &e) {
      return failure(-32003,e.what(),{});
    }
  }
};
}
