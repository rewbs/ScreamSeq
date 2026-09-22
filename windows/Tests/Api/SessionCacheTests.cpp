#include "SessionAdapter.hpp"
#include <iostream>
#include <stdexcept>

using namespace ScreamSeq::Api;
namespace {
constexpr std::size_t byteBudget=8*1024*1024;
constexpr std::size_t entryBudget=64;
void check(bool ok, const char *message) {
  if(!ok) throw std::runtime_error(message);
}
Json request(const std::string &method, const Json &params, const std::string &id) {
  return {{"jsonrpc","2.0"},{"id",id},{"method",method},{"params",params}};
}
void error(const Json &reply,int code,const char *message) {
  check(reply.contains("error") && reply.at("error").at("code")==code,message);
}
// Protocol-only host: realistic pattern.apply-sized dictionaries, not a second
// musical model. These tests exercise the real adapter, not DSP or Document edits.
struct CacheHost : SessionHost {
  SessionSnapshot state;
  unsigned calls=0, commits=0, navigations=0;
  int failure=0;
  bool noOp=false, invalidUtf8=false;
  std::size_t paddingBytes=0;
  CacheHost() {
    state.revision="cache:0"; state.documentId="cache-document";
    state.document={{"channels",64},{"orders",{0}},{"patterns",{{{"index",0},{"rows",64}}}}};
    state.context={{"pattern",0},{"row",0},{"channel",0},{"contextRevision","cursor:0"}};
    state.transport={{"playing",false}};
  }
  SessionSnapshot snapshot() override { return state; }
  bool supportsDocumentOperations() const override { return true; }
  PatternSnapshot pattern(unsigned) override { return {}; }
  void play(const Json &) override { state.transport["playing"]=true; }
  void stop() override { state.transport["playing"]=false; }
  void navigate(const Json &context) override {
    ++navigations; state.context=context;
    state.context["contextRevision"]="cursor:"+std::to_string(navigations);
  }
  Json documentOperation(const std::string &method,const Json &params) override {
    check(method=="pattern.apply","fixture only implements pattern.apply");
    ++calls;
    if(failure) throw ApiError(failure,"Injected host rejection before commit");
    Json changes=Json::array();
    const Json before={{"note",49},{"instrument",1},{"volumeCommand",1},{"volume",32},{"effect",0},{"parameter",0}};
    if(!noOp) for(const auto &cell:params.at("cells")) {
      auto after=cell; after.erase("pattern"); after.erase("row"); after.erase("channel");
      changes.push_back({{"pattern",cell.at("pattern")},{"row",cell.at("row")},{"channel",cell.at("channel")},
        {"before",before},{"after",std::move(after)}});
    }
    const bool dry=params.value("dryRun",false);
    if(!dry && !noOp) { ++commits; state.revision="cache:"+std::to_string(commits); }
    Json result={{"dryRun",dry},{"changedCells",changes.size()},{"changes",std::move(changes)}};
    if(paddingBytes) result["padding"]=std::string(paddingBytes,'x');
    if(invalidUtf8) result["invalid"]=std::string("\xFF",1);
    return result;
  }
};
Json cells(unsigned count=1) {
  Json result=Json::array();
  for(unsigned i=0;i<count;++i) result.push_back({{"pattern",0},{"row",i/64},{"channel",i%64},
    {"note",61},{"instrument",255},{"volumeCommand",1},{"volume",64},{"effect",1},{"parameter",255}});
  return result;
}
Json edit(CacheHost &host,const std::string &id,bool dry=false) {
  return request("pattern.apply",{{"expectedRevision",host.state.revision},{"cells",cells()},{"dryRun",dry}},id);
}
Json cacheInfo(SessionAdapter &api) {
  return api.handle(request("api.describe",Json::object(),"describe"))["result"]["data"]["writeReplayCache"];
}
void usage(SessionAdapter &api,std::size_t entries,std::size_t bytes) {
  const auto info=cacheInfo(api);
  check(info.at("retainedEntries")==entries && info.at("retainedSerializedBytes")==bytes,"exact aggregate retention accounting");
  check(entries<=entryBudget && bytes<=byteBudget,"both retention limits hold");
}
std::size_t charge(const Json &q,const Json &reply) { return q.dump().size()+reply.dump().size(); }
void discovery() {
  SessionAdapter api;
  const auto info=cacheInfo(api);
  check(info.is_object() && info.value("maxEntries",0)==entryBudget && info.value("maxSerializedBytes",0)==byteBudget,
    "discovery advertises both write replay retention limits");
  usage(api,0,0);
  check(info.at("accounting")=="compact UTF-8 JSON request plus response; excludes newlines; not heap usage" &&
    info.at("eviction")=="oldest insertion first; replay does not refresh" && info.at("durable")==false,
    "discovery states precise accounting, eviction and non-durability");
  check(info.size()==9,"introspection exposes policy and totals only, no request contents or identifiers");
  std::cout<<"PASS truthful cache discovery and aggregate-only introspection\n";
}
void byteEviction() {
  CacheHost host; SessionAdapter api(host);
  const auto maximumCells=cells(4096);
  struct Entry { Json q,reply; std::size_t bytes; };
  std::deque<Entry> expected;
  Json oldest;
  std::size_t totalBytes=0, retained=0;
  for(unsigned i=0;i<80;++i) {
    auto q=request("pattern.apply",{{"expectedRevision",host.state.revision},{"cells",maximumCells}},"batch-"+std::to_string(i));
    if(i==0) oldest=q;
    check(q.dump().size()+1<=1024*1024,"4096-cell fixture fits private pipe request limit");
    const auto reply=api.handle(q);
    check(reply.contains("result") && reply["result"]["changed"]==true,"maximum-cell write succeeded");
    const auto bytes=charge(q,reply); totalBytes+=bytes; retained+=bytes;
    expected.push_back({q,reply,bytes});
    while(retained>byteBudget || expected.size()>entryBudget) { retained-=expected.front().bytes; expected.pop_front(); }
    usage(api,expected.size(),retained);
    check(api.handle(q)==reply && host.calls==i+1,"latest exact successful write replays without host entry");
    // The oldest retained reply survives later revisions; reading/replaying it
    // must not refresh its insertion age or change the accounting.
    check(api.handle(expected.front().q)==expected.front().reply,"oldest retained response replays exactly");
    usage(api,expected.size(),retained);
  }
  check(totalBytes>byteBudget && expected.size()<entryBudget,"byte limit, not count limit, constrains large writes");
  error(api.handle(oldest),-32001,"byte-evicted write must hit stale-revision guard, not replay");
  check(host.calls==80 && host.commits==80,"evicted stale write never reaches host");
  std::cout<<"PASS 80 maximum-cell writes, FIFO byte eviction and replay; submitted="<<totalBytes
    <<", retained="<<retained<<", entries="<<expected.size()<<'\n';
}
void entryEviction() {
  CacheHost host; SessionAdapter api(host);
  std::vector<Json> requests,replies;
  std::size_t retained=0;
  for(unsigned i=0;i<entryBudget;++i) {
    requests.push_back(edit(host,"small-"+std::to_string(i)));
    replies.push_back(api.handle(requests.back()));
    retained+=charge(requests.back(),replies.back());
  }
  usage(api,entryBudget,retained);
  check(api.handle(requests.front())==replies.front(),"replay before entry eviction");
  const auto next=edit(host,"small-64"), reply=api.handle(next);
  retained+=charge(next,reply); retained-=charge(requests.front(),replies.front());
  usage(api,entryBudget,retained);
  error(api.handle(requests.front()),-32001,"replay does not rescue oldest entry from FIFO eviction");
  check(api.handle(requests[1])==replies[1] && host.calls==65,"next-oldest survives count eviction");
  std::cout<<"PASS 64-entry limit independently of bytes; replay does not extend retention\n";
}
void boundariesAndOversize() {
  // Both request and response are individually below budget. Their SUM is the
  // charge; also exercise compact UTF-8 and JSON escaping, not string lengths.
  for(const auto extra:{std::size_t(0),std::size_t(1)}) {
    CacheHost probe; SessionAdapter probeApi(probe); probe.paddingBytes=1;
    auto q=edit(probe,"boundary-\xC3\xA9",true);
    q["params"]["fixtureText"]="line\n\"\\\xC3\xA9";
    const auto base=charge(q,probeApi.handle(q));
    CacheHost host; SessionAdapter api(host); host.paddingBytes=byteBudget-base+1+extra;
    const auto reply=api.handle(q);
    check(reply.contains("result") && reply["result"]["changed"]==false,"boundary dry run succeeds");
    check(charge(q,reply)==byteBudget+extra,"exact serialized-byte boundary fixture");
    usage(api,extra ? 0 : 1,extra ? 0 : byteBudget);
    check(host.commits==0,"dry run never commits in protocol fixture");
  }
  CacheHost host; SessionAdapter api(host);
  const auto seed=edit(host,"seed"), seeded=api.handle(seed);
  const auto seedBytes=charge(seed,seeded);
  host.paddingBytes=byteBudget;
  const auto huge=edit(host,"huge"), committed=api.handle(huge);
  check(committed.contains("result") && committed["result"]["changed"]==true && host.commits==2,
    "oversize response retention cannot misreport committed success");
  usage(api,1,seedBytes);
  check(api.handle(seed)==seeded,"oversize entry does not evict existing replay window");
  error(api.handle(huge),-32001,"uncached oversize write still has stale-write protection");
  host.paddingBytes=0;
  auto hugeRequest=edit(host,"huge-request");
  // Direct-adapter stress only: this request intentionally exceeds the pipe's
  // unchanged 1 MiB request limit and is NOT a supported musical parameter.
  hugeRequest["params"]["fixturePadding"]=std::string(byteBudget,'x');
  check(api.handle(hugeRequest).contains("result") && host.commits==3,"oversize request retention preserves success");
  usage(api,1,seedBytes);
  error(api.handle(hugeRequest),-32001,"oversize request is not retained");
  std::cout<<"PASS inclusive byte boundary, UTF-8/escaping, oversize skip without losing success or old entries\n";
}
void failuresAndExactReplay() {
  CacheHost host; SessionAdapter api(host);
  auto q=edit(host,"retry-after-failure");
  for(const int code:{-32602,-32002,-32003}) {
    host.failure=code;
    error(api.handle(q),code,"host rejection returned");
    error(api.handle(q),code,"failure must be dispatched again, not replayed");
    usage(api,0,0);
  }
  check(host.calls==6 && host.commits==0,"no failures retained or committed");
  host.failure=0;
  const auto reply=api.handle(q);
  check(reply.contains("result") && host.calls==7,"failed id can succeed on a later attempt");
  const auto bytes=charge(q,reply); usage(api,1,bytes);
  auto changed=q; changed["params"]["cells"][0]["note"]=62;
  error(api.handle(changed),-32600,"same id with changed params rejects while retained");
  changed=q; changed["params"]["cells"][0]["note"]=61.0;
  error(api.handle(changed),-32600,"integer-to-float parameter change is not an exact replay");
  changed=q; changed["method"]="document.patch";
  error(api.handle(changed),-32600,"same id with changed write method rejects while retained");
  changed=q; changed["params"]["expectedRevision"]=host.state.revision;
  error(api.handle(changed),-32600,"rebased params do not reuse a retained successful id");
  check(api.handle(q)==reply && host.calls==7,"conflicts preserve original response and never enter host");
  check(api.handle(Json::parse(q.dump(2)))==reply && host.calls==7,"formatting and parsed unsigned integers preserve exact replay");
  auto bad=edit(host,"bad-revision"); bad["params"]["expectedRevision"]=true;
  error(api.handle(bad),-32602,"strict revision validation remains active on cache miss");
  bad["params"]["expectedRevision"]="stale";
  error(api.handle(bad),-32001,"stale validation remains active on cache miss");
  usage(api,1,bytes);
  const auto read=request("document.get",Json::object(),"fresh-read");
  const auto before=api.handle(read);
  const auto next=api.handle(edit(host,"next"));
  check(before["result"]["revision"]!=api.handle(read)["result"]["revision"],"reads remain fresh, never cached by id");
  check(next["result"]["changed"]==true,"next guarded edit commits");
  std::cout<<"PASS failures uncached, changed id content rejected, original replay preserved, fresh reads and revision guards\n";
}
void noOpDryRunAndContext() {
  CacheHost host; SessionAdapter api(host);
  const auto dry=edit(host,"dry",true), preview=api.handle(dry);
  host.noOp=true;
  const auto noop=edit(host,"noop"), unchanged=api.handle(noop);
  check(preview["result"]["changed"]==false && unchanged["result"]["changed"]==false && host.commits==0,
    "successful dry run and no-op keep revision stable");
  usage(api,2,charge(dry,preview)+charge(noop,unchanged));
  host.noOp=false; api.handle(edit(host,"commit"));
  check(api.handle(dry)==preview && api.handle(noop)==unchanged && host.calls==3,
    "no-op and dryRun successful replies replay after later commits");
  auto apply=dry; apply["params"]["dryRun"]=false;
  error(api.handle(apply),-32600,"dryRun-to-apply must use a new id");
  auto nav=request("context.set",{{"expectedRevision",host.state.revision},{"expectedContext","cursor:0"},{"row",1}},"nav");
  const auto moved=api.handle(nav);
  check(moved["result"]["contextChanged"]==true && host.navigations==1,"actual adapter validates context change");
  auto stale=nav; stale["id"]="stale-context";
  error(api.handle(stale),-32001,"new id cannot bypass context revision guard");
  check(api.handle(nav)==moved && host.navigations==1,"exact retained context write replay precedes stale guard");
  for(unsigned i=0;i<entryBudget;++i)
    api.handle(request("transport.stop",{{"expectedRevision",host.state.revision}},"stop-"+std::to_string(i)));
  error(api.handle(nav),-32001,"evicted context write rechecks context token");
  error(api.handle(dry),-32001,"evicted dry run rechecks document token");
  check(host.navigations==1 && host.calls==3,"evicted stale writes do not execute again");
  std::cout<<"PASS no-op/dryRun retention and document/context stale safety after eviction\n";
}
void serializationFailure() {
  CacheHost host; SessionAdapter api(host);
  const auto seed=edit(host,"seed"), seeded=api.handle(seed);
  host.invalidUtf8=true;
  const auto q=edit(host,"invalid-host-data"), reply=api.handle(q);
  check(reply.contains("result") && reply["result"]["changed"]==true && reply["result"]["revision"]==host.state.revision && host.commits==2,
    "cache serialization failure cannot return an unchanged/pre-commit error for a committed write");
  bool throws=false; try { reply.dump(); } catch(const Json::type_error &) { throws=true; }
  check(throws,"fixture actually forces strict JSON serialization failure");
  usage(api,1,charge(seed,seeded));
  error(api.handle(q),-32001,"unserializable success was not retained");
  check(api.handle(seed)==seeded,"serialization failure does not discard earlier retained success");
  std::cout<<"PASS cache serialization failure skips retention without falsifying commit outcome\n";
}
void independentServices() {
  struct LibraryHost : CacheHost {
    unsigned snapshots=0,libraryCalls=0;
    std::function<void()> nested;
    SessionSnapshot snapshot()override{++snapshots;return CacheHost::snapshot();}
    std::vector<std::string> independentReads()const override{return {"sample.library.get"};}
    std::vector<std::string> independentWrites()const override{return {"sample.library.roots.set"};}
    Json independentGuards()const override{return {{"sample.library.roots.set",{"expectedLibraryRevision"}}};}
    Json independentOperation(const std::string &method,const Json &params)override{
      ++libraryCalls;
      if(nested){auto call=std::move(nested);call();}
      if(method=="sample.library.roots.set"&&params.value("expectedLibraryRevision",std::string{})!="test-library")throw ApiError(-32001,"Library changed");
      return {{"revision","library:test-library"},{"changed",false},{"playbackStopped",false},{"data",{{"roots",Json::array()}}}};
    }
  }host;
  SessionAdapter api(host);const auto before=host.state;
  auto info=api.handle(request("api.describe",Json::object(),"describe"))["result"]["data"];
  check(info["revisionGuards"]["sample.library.roots.set"]==Json::array({"expectedLibraryRevision"}),"independent guard discovery");
  host.snapshots=0;
  auto read=api.handle(request("sample.library.get",Json::object(),"library-read"));
  check(read["result"]["revision"]=="library:test-library"&&!read["result"].contains("documentId"),"independent envelope polluted with document identity");
  auto q=request("sample.library.roots.set",{{"expectedLibraryRevision","test-library"}},"library-write");
  host.nested=[&]{error(api.handle(q),-32002,"in-flight independent write entered twice while pumping UI messages");};
  auto reply=api.handle(q);check(reply.contains("result"),"library write incorrectly requires song revision");
  check(api.handle(q)==reply&&host.libraryCalls==2,"independent write replay re-entered the host");
  auto conflict=q;conflict["params"]["expectedLibraryRevision"]="stale";error(api.handle(conflict),-32600,"conflicting independent request ID accepted");
  conflict["id"]="stale-library";auto rejected=api.handle(conflict);error(rejected,-32001,"independent stale guard bypassed");
  check(!rejected["error"].contains("data"),"independent error advertised a song revision");
  check(host.snapshots==0&&host.calls==0&&host.state.revision==before.revision,"independent service used song snapshot/operation/history");
  std::cout<<"PASS independent revision/envelope, guarded write replay and absence of song ownership\n";
}
}
int main() {
  try {
    discovery(); byteEviction(); entryEviction(); boundariesAndOversize(); failuresAndExactReplay();
    noOpDryRunAndContext(); serializationFailure(); independentServices();return 0;
  } catch(const std::exception &e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
