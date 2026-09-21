#pragma once

#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <stdexcept>
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
struct ApiError : std::runtime_error {
  int code;
  ApiError(int c, const std::string &message) : std::runtime_error(message), code(c) {}
};
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
  virtual PatternSnapshot pattern(unsigned index) = 0;
  // Called on the owning control thread only, after complete validation.
  // Preserve requested region settings and existing loop default. Throw ApiError
  // (-32003 for engine failure) rather than reporting queued-but-not-started play.
  virtual void play(const Json &settings) = 0;
  virtual void stop() = 0;
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
  // Successful writes only, FIFO by insertion (replays do not refresh age).
  // Charge compact UTF-8 JSON request + response bytes, excluding newlines.
  // This bounds retained serialized content, not JSON DOM/allocator heap usage.
  void cacheSuccess(const Json &request, const Json &response) noexcept {
    try {
      auto canonical=request.dump();
      const auto requestBytes=canonical.size();
      if(requestBytes>maxCacheBytes) return;
      const auto responseBytes=response.dump().size();
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
      {"reads",{"api.describe","document.get","pattern.get","transport.get","context.get","workspace.get"}},
      {"writes",{"transport.play","transport.stop","context.set","workspace.panel","workspace.layout"}},{"maxPatternCells",4096},
      {"coordinates","Patterns, rows, channels and orders are zero-based. Samples and instruments are one-based; zero means none."},
      {"noteEncoding","0=empty; 1=C-0, 49=C-4, 61=C-5. Special notes and format command IDs follow document.get."},
      {"platform","windows"},{"musicalEditing",false},{"fullApiParity",false},
      {"writeReplayCache",{{"maxEntries",maxCacheEntries},{"maxSerializedBytes",maxCacheBytes},
        {"retainedEntries",cache_.size()},{"retainedSerializedBytes",cacheBytes_},
        {"accounting","compact UTF-8 JSON request plus response; excludes newlines; not heap usage"},
        {"eviction","oldest insertion first; replay does not refresh"},
        {"scope","successful writes, including no-op and dryRun; exact parsed request replay while retained"},
        {"oversize","success returned without caching; retention failure does not reject a completed write"},
        {"durable",false}}},
      {"revisionGuards",{{"transport.play",{"expectedRevision"}},{"transport.stop",{"expectedRevision"}},
        {"context.set",{"expectedRevision","expectedContext"}},
        {"workspace.panel",Json::array()},{"workspace.layout",Json::array()}}},
      {"workspaceSubset",{{"panels",{"notes","samples"}},{"placements",{"right","hide"}},
        {"layouts",{"Compose","Pattern focus","Sound design"}}}},
      {"transport","Private explicit named pipe; 32 MiB request and response, including newline; one request per connection. Transport writes require expectedRevision; context.set requires expectedRevision and expectedContext. Workspace operations accept neither revision token; unsupported parameters reject."}};
    if(host_ && host_->supportsDocumentOperations()) {
      for(const auto *m:{"pattern.commands","sample.get","sample.waveform.get","pattern.notes.get","document.timing.get","automation.formula.reference","automation.formula.preview"}) result["reads"].push_back(m);
      for(const auto *m:{"pattern.apply","history.undo","history.redo","document.patch","pattern.create","order.edit","sequence.select","document.save","document.open","pattern.notes.set","document.timing.set"}) {
        result["writes"].push_back(m);result["revisionGuards"][m]={"expectedRevision"};
      }
      result["musicalEditing"]=true;
      for(const auto &m:host_->additionalDocumentReads()) result["reads"].push_back(m);
      for(const auto &m:host_->additionalDocumentWrites()) {
        result["writes"].push_back(m);result["revisionGuards"][m]={"expectedRevision"};
      }
      result["windowsExtensions"]={{"document.open","absolute path, expectedRevision, discard:true required for unsaved work"},
        {"plugin.editor.open","slot and expectedRevision; native VST3 editor on the private STA; no musical change unless the vendor emits edits"},
        {"plugin.editor.close","slot and expectedRevision; flush pending baseline edits before closing"}};
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
    n["column"]=integer(p,"column",n.value("column",0u),0,4);
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
    const bool workspace=method=="workspace.get" || method=="workspace.panel" || method=="workspace.layout";
    const auto reads=host_ ? host_->additionalDocumentReads() : std::vector<std::string>{};
    const auto writes=host_ ? host_->additionalDocumentWrites() : std::vector<std::string>{};
    const bool docRead=host_ && host_->supportsDocumentOperations() && (std::find(reads.begin(),reads.end(),method)!=reads.end() || method=="pattern.commands" || method=="sample.get" || method=="sample.waveform.get" || method=="pattern.notes.get" || method=="document.timing.get" || method=="automation.formula.reference" || method=="automation.formula.preview");
    const bool docWrite=host_ && host_->supportsDocumentOperations() && (std::find(writes.begin(),writes.end(),method)!=writes.end() || method=="pattern.apply" || method=="history.undo" || method=="history.redo" || method=="document.patch" || method=="pattern.create" || method=="order.edit" || method=="sequence.select" || method=="document.save" || method=="document.open" || method=="pattern.notes.set" || method=="document.timing.set");
    const bool write=docWrite || method=="transport.play" || method=="transport.stop" || method=="context.set" || (workspace && method!="workspace.get");
    if(!write && !docRead && !workspace && method!="api.describe" && method!="document.get" && method!="context.get" && method!="pattern.get" && method!="transport.get")
      return errorResponse(q["id"],-32601,"Unknown method; call api.describe");
    std::string revision;
    try {
      if(write) for(const auto &cached:cache_) if(cached.id==q["id"].get_ref<const std::string &>()) {
        // Canonical JSON ignores object key order/whitespace but preserves
        // integer vs floating-point parameters (JSON operator== does not).
        if(cached.request!=q.dump()) return errorResponse(q["id"],-32600,"Request id was already used with different content");
        return cached.response;
      }
      if(!host_ && method!="api.describe") throw ApiError(-32002,"Session is not attached");
      SessionSnapshot before;
      if(host_) before=host_->snapshot();
      else before.revision="unbound";
      revision=before.revision;
      if(write && !workspace) {
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
      else if(docRead || docWrite) data=host_->documentOperation(method,p);
      else if(workspace) data=host_->workspace(method,p);
      else if(method=="context.set") {
        host_->navigate(prepareNavigation(p,before)); data=host_->snapshot().context;
      }
      else if(method=="transport.play") {
        validatePlay(p,before); Json settings=p; settings.erase("expectedRevision");
        host_->play(settings); data={{"playing",true},{"region",settings}};
      } else {
        keys(p,{"expectedRevision"}); host_->stop(); data={{"playing",false}};
      }
      const auto after=(write || docRead) ? host_->snapshot() : before;
      Json result={{"revision",after.revision},{"changed",before.revision!=after.revision},
        {"playbackStopped",before.transport.value("playing",false) && !after.transport.value("playing",false)}, {"data",std::move(data)}};
      if(method=="context.get" || method=="context.set")
        result["contextChanged"]=before.context.at("contextRevision")!=after.context.at("contextRevision");
      else if(host_) result["documentId"]=after.documentId;
      Json response={{"jsonrpc","2.0"},{"id",q["id"]},{"result",std::move(result)}};
      if(write) cacheSuccess(q,response);
      return response;
    } catch(const ApiError &e) {
      auto reply=errorResponse(q["id"],e.code,e.what());
      if(!revision.empty()) reply["error"]["data"]={{"revision",revision}};
      return reply;
    } catch(const std::exception &e) {
      auto reply=errorResponse(q["id"],-32003,e.what());
      if(!revision.empty()) reply["error"]["data"]={{"revision",revision}};
      return reply;
    }
  }
};
}
