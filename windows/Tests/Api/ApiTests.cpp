#include <nlohmann/json.hpp>
#include <iostream>
#include <stdexcept>
#include "SessionAdapter.hpp"
#include <windows.h>
#include <aclapi.h>
#include <atomic>
#include <chrono>
#include <thread>
#include "PipeServer.hpp"
void pipeTests();
HANDLE connectPipe(const std::wstring &name) {
  for(int i=0;i<200;++i) {
    HANDLE h=CreateFileW(name.c_str(), GENERIC_READ|GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if(h!=INVALID_HANDLE_VALUE) return h;
    Sleep(5);
  }
  throw std::runtime_error("connect failed");
}
std::string pipeExchange(const std::wstring &name, const std::string &input) {
  HANDLE h=connectPipe(name); DWORD n=0;
  if(!WriteFile(h,input.data(),DWORD(input.size()),&n,nullptr)) { CloseHandle(h); throw std::runtime_error("write failed"); }
  std::string reply; char buffer[4096];
  while(ReadFile(h,buffer,sizeof(buffer),&n,nullptr) && n) { reply.append(buffer,n); if(reply.find('\n')!=std::string::npos) break; }
  CloseHandle(h); return reply;
}
using nlohmann::json;
using namespace ScreamSeq::Api;
void check(bool ok, const char *what) { if(!ok) throw std::runtime_error(what); }
json request(std::string method, json params = json::object(), std::string id = "test") {
  return {{"jsonrpc","2.0"},{"id",id},{"method",method},{"params",params}};
}
struct TestHost : SessionHost {
  SessionSnapshot state;
  int plays=0, stops=0;
  TestHost() {
    state.revision="test:0:0:0"; state.documentId="test";
    state.document={{"title","fixture"},{"channels",2},{"orders",{0}},{"patterns",{{{"index",0},{"rows",4}}}}};
    state.context={{"pattern",0},{"row",1},{"contextRevision","cursor:0"}};
    state.transport={{"playing",false},{"loop",false},{"region",json::object()}};
  }
  SessionSnapshot snapshot() override { return state; }
  PatternSnapshot pattern(unsigned index) override {
    if(index!=0) throw ApiError(-32602,"Pattern does not exist");
    PatternSnapshot p; p.rows=4; p.channels=2; p.cells.resize(8); p.cells[3]={49,1,1,32,0,0}; return p;
  }
  void play(const json &settings) override { ++plays; state.transport["playing"]=true; state.transport["region"]=settings; }
  void stop() override { ++stops; state.transport["playing"]=false; }
};
void transientInputTests() {
  struct Host : TestHost {
    unsigned inputs=0,watches=0;
    bool supportsDocumentOperations() const override{return true;}
    std::vector<std::string> additionalDocumentWrites() const override{return {"parameter.activity.watch"};}
    Json workspace(const std::string &method,const Json &p) override {
      check(method=="workspace.input","Unexpected workspace dispatch");++inputs;
      state.context["contextRevision"]="cursor:"+std::to_string(inputs);
      return {{"instrument",p.value("instrument",1)},{"octave",p.value("octave",4)},{"contextRevision",state.context.at("contextRevision")}};
    }
    Json documentOperation(const std::string &method,const Json &) override {
      check(method=="parameter.activity.watch","Unexpected activity dispatch");++watches;return {{"token","1:1"}};
    }
  } host;SessionAdapter api(host);
  const auto params=Json{{"expectedRevision",host.state.revision},{"expectedContext","cursor:0"},{"instrument",3.0},{"octave",5.0}};
  auto q=request("workspace.input",params,"input");const auto result=api.handle(q);
  check(result.at("result").at("contextChanged")==true&&result.at("result").at("changed")==false&&host.inputs==1,"Input changes only context");
  check(api.handle(q)==result&&host.inputs==1,"Input replay must not advance context again");
  check(api.handle(request("workspace.input",params,"stale"))["error"]["code"]==-32001,"Input requires fresh context");
  for(auto invalid:{Json{{"instrument",true}},Json{{"instrument",0}},Json{{"octave",9}},Json{{"octave",1.5}},Json::object()}) {
    invalid["expectedRevision"]=host.state.revision;invalid["expectedContext"]=host.state.context.at("contextRevision");
    check(api.handle(request("workspace.input",invalid,"invalid"))["error"]["code"]==-32602,"Invalid input must reject before host");
  }
  check(host.inputs==1&&host.plays==0&&host.stops==0,"Input rejection changed transport or context");
  const auto watch=request("parameter.activity.watch",{{"target","copy"},{"parameter",1}},"watch");const auto watched=api.handle(watch);
  check(watched.contains("result")&&api.handle(watch)==watched&&host.watches==1,"Monitor watch has transient cached write semantics");
  const auto description=api.handle(request("api.describe"))["result"]["data"];
  check(description["revisionGuards"]["parameter.activity.watch"]==Json::array(),"Monitor watch must not advertise a musical revision guard");
  std::cout<<"PASS guarded input context and transient monitor replay\n";
}
void sessionTests() {
  TestHost host; SessionAdapter api(host);
  auto doc=api.handle(request("document.get"));
  check(doc["result"]["data"]["title"]=="fixture" && doc["result"]["documentId"]=="test", "document snapshot envelope");
  auto pattern=api.handle(request("pattern.get",{{"pattern",0},{"startRow",1},{"rowCount",1}}));
  check(pattern["result"]["data"]["cells"].size()==2 && pattern["result"]["data"]["cells"][1]["note"]==49,"pattern rectangle matches snapshot cells");
  for(auto p:{json{{"pattern",true}},json{{"pattern",0},{"startRow",4}},json{{"pattern",0},{"rowCount",-1}},json{{"pattern",0},{"unknown",1}},json{{"pattern",0},{"channelCount",3}}})
    check(api.handle(request("pattern.get",p))["error"]["code"]==-32602,"pattern params reject without coercion");
  check(api.handle(request("context.get"))["result"]["contextChanged"]==false,"read-only context");
  check(api.handle(request("transport.play",{{"expectedRevision","stale"}}))["error"]["code"]==-32001,"transport revision guard");
  check(api.handle(request("transport.play",{{"expectedRevision",host.state.revision},{"loop",1}}))["error"]["code"]==-32602,"strict transport boolean");
  check(api.handle(request("transport.play",{{"expectedRevision",host.state.revision},{"startRow",1}}))["error"]["code"]==-32602,"row bounds require pattern");
  check(host.plays==0,"invalid transport cannot reach host");
  auto play=request("transport.play",{{"expectedRevision",host.state.revision},{"pattern",0},{"cursorRow",1},{"endRow",3}},"play-1");
  auto played=api.handle(play);
  check(played["result"]["data"]["playing"]==true && played["result"]["changed"]==false,"play does not change document revision");
  check(api.handle(play)==played && host.plays==1,"successful transport request deduplicated");
  auto reused=play; reused["params"]["cursorRow"]=2;
  check(api.handle(reused)["error"]["code"]==-32600,"id reuse with changed payload rejects");
  auto stopped=api.handle(request("transport.stop",{{"expectedRevision",host.state.revision}},"stop-1"));
  check(stopped["result"]["playbackStopped"]==true && host.stops==1,"stop envelope reports stopped playback");
  json wrongThread; std::thread other([&]{ wrongThread=api.handle(request("document.get")); }); other.join();
  check(wrongThread["error"]["code"]==-32002,"owner thread enforced before host access");
  std::cout << "PASS snapshot reads, pagination, strict params, revision guard, transport, dedupe, owner thread\n";
}
void playbackLoopTests() {
  struct LoopHost : TestHost {
    unsigned calls=0;bool refuse=false;
    bool supportsPlaybackLoop() const override {return true;}
    void setPlaybackLoop(bool enabled) override {
      if(refuse)throw ApiError(-32002,"Document worker is busy; loop was not changed",Tracker::WriteOutcome{Tracker::CommitOutcome::NotCommitted});
      ++calls;state.transport["loop"]=enabled;
    }
  } host;
  SessionAdapter api(host);
  const auto description=api.handle(request("api.describe"))["result"]["data"];
  check(std::find(description["writes"].begin(),description["writes"].end(),"transport.loop")!=description["writes"].end()&&
    description["revisionGuards"]["transport.loop"]==json::array({"expectedRevision"}),"Loop capability missing its revision contract");
  const auto initial=host.state.transport;
  check(api.handle(request("transport.loop",{{"enabled",true}}))["error"]["code"]==-32602,"Loop needs revision");
  check(api.handle(request("transport.loop",{{"enabled",true},{"expectedRevision","stale"}}))["error"]["code"]==-32001,"Loop ignores stale revision");
  for(const auto &params:{json{{"expectedRevision",host.state.revision}},json{{"enabled",1},{"expectedRevision",host.state.revision}},
      json{{"enabled",true},{"unknown",true},{"expectedRevision",host.state.revision}}})
    check(api.handle(request("transport.loop",params))["error"]["code"]==-32602,"Invalid loop fields reached the host");
  check(host.calls==0&&host.state.transport==initial,"Invalid loop request changed transport");
  host.refuse=true;
  check(api.handle(request("transport.loop",{{"enabled",true},{"expectedRevision",host.state.revision}},"refused-loop"))["error"]["code"]==-32002&&
    host.calls==0&&host.state.transport==initial,"Busy loop mutation was not atomic");
  host.refuse=false;
  const auto set=request("transport.loop",{{"enabled",true},{"expectedRevision",host.state.revision}},"loop-on");
  const auto applied=api.handle(set);
  check(applied["result"]["data"]==json{{"loop",true}}&&applied["result"]["changed"]==false&&host.state.transport["loop"]==true,
    "Stopped loop setting changed history or returned the wrong state");
  check(api.handle(set)==applied&&host.calls==1,"Loop request replay repeated its host mutation");
  host.state.transport["playing"]=true;const auto region=host.state.transport["region"];
  const auto current=host.state.context;
  const auto off=api.handle(request("transport.loop",{{"enabled",false},{"expectedRevision",host.state.revision}},"loop-off"));
  check(off["result"]["playbackStopped"]==false&&host.state.transport["playing"]==true&&host.state.transport["loop"]==false&&
    host.state.transport["region"]==region&&host.state.context==current&&host.plays==0&&host.stops==0,"Live loop restarted playback or redirected context");
  TestHost unsupported;SessionAdapter other(unsupported);
  check(other.handle(request("transport.loop",{{"enabled",true},{"expectedRevision",unsupported.state.revision}}))["error"]["code"]==-32601,
    "Host without loop support fabricated a success");
  std::cout<<"PASS playback loop capability, strict guards, stopped/live state, refusal and replay\n";
}
void pipeTests() {
  const auto name = L"\\\\.\\pipe\\ScreamSeq.Api.Test." + std::to_wstring(GetCurrentProcessId());
  PipeServer server(name, [](const json &q) { return json{{"jsonrpc","2.0"},{"id",q["id"]},{"result",q["params"]}}; });
  server.start();
  const auto reply=json::parse(pipeExchange(name, request("echo",{{"value",42}}).dump()+"\n"));
  check(reply["result"]["value"]==42,"real pipe JSON roundtrip");
  check(json::parse(pipeExchange(name,"bad-json\n"))["error"]["code"]==-32600,"malformed framing rejected");
  check(json::parse(pipeExchange(name,std::string(PipeServer::maxRequestBytes,'x')+"\n"))["error"]["code"]==-32600,"oversize request rejected");
  check(json::parse(pipeExchange(name,"[]\n"))["error"]["code"]==-32600,"batch rejected");
  const auto deep=std::string(100,'[')+"0"+std::string(100,']');
  check(json::parse(pipeExchange(name,"{\"jsonrpc\":\"2.0\",\"id\":\"deep\",\"method\":\"echo\",\"params\":{\"nested\":"+deep+"}}\n"))["error"]["code"]==-32600,"excessive nesting rejected before dispatch");
  bool collision=false;
  try { PipeServer duplicate(name, [](const json &q){ return q; }); duplicate.start(); }
  catch(const std::exception &) { collision=true; }
  check(collision,"first-instance collision must fail closed");
  HANDLE stalled=connectPipe(name);
  PACL acl=nullptr; PSECURITY_DESCRIPTOR sd=nullptr;
  check(GetSecurityInfo(stalled,SE_KERNEL_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,&acl,nullptr,&sd)==ERROR_SUCCESS,"read actual pipe ACL");
  check(acl && acl->AceCount==1,"exactly one user ACE, not Everyone or inherited grants");
  void *rawAce=nullptr; check(GetAce(acl,0,&rawAce)!=FALSE,"read ACE");
  auto *ace=static_cast<ACCESS_ALLOWED_ACE *>(rawAce);
  check(ace->Header.AceType==ACCESS_ALLOWED_ACE_TYPE,"only allow ACE");
  HANDLE token=nullptr; check(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)!=FALSE,"token query");
  DWORD tokenBytes=0; GetTokenInformation(token,TokenUser,nullptr,0,&tokenBytes);
  std::vector<unsigned char> tokenData(tokenBytes);
  check(GetTokenInformation(token,TokenUser,tokenData.data(),tokenBytes,&tokenBytes)!=FALSE,"token user");
  check(EqualSid(&ace->SidStart,reinterpret_cast<TOKEN_USER *>(tokenData.data())->User.Sid)!=FALSE,"pipe DACL grants current SID only");
  SECURITY_DESCRIPTOR_CONTROL control=0; DWORD sdRevision=0;
  check(GetSecurityDescriptorControl(sd,&control,&sdRevision) && (control&SE_DACL_PROTECTED),"DACL protected from inheritance");
  CloseHandle(token); LocalFree(sd);
  const auto start=std::chrono::steady_clock::now(); server.stop();
  check(std::chrono::steady_clock::now()-start<std::chrono::seconds(1),"stalled read shutdown must be bounded");
  CloseHandle(stalled); server.stop(); server.start(); server.stop();
  check(CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr)==INVALID_HANDLE_VALUE,"stop removes endpoint");
  std::atomic<bool> handling{false};
  PipeServer writer(name,[&](const json &q) { handling=true; return json{{"jsonrpc","2.0"},{"id",q["id"]},{"result",std::string(256*1024,'x')}}; });
  writer.start(); HANDLE unread=connectPipe(name); auto line=request("large").dump()+"\n"; DWORD written=0;
  check(WriteFile(unread,line.data(),DWORD(line.size()),&written,nullptr)!=FALSE,"start stalled writer");
  for(int i=0;i<200 && !handling;++i) Sleep(5);
  check(handling,"large reply handler reached"); Sleep(100);
  const auto writeStart=std::chrono::steady_clock::now(); writer.stop();
  check(std::chrono::steady_clock::now()-writeStart<std::chrono::seconds(1),"stalled write shutdown must be bounded");
  CloseHandle(unread);
  std::cout << "PASS real pipe, private SID DACL, collision, stalled read/write stop, restart\n";
}
void serializationPipeTests() {
  struct SerializationHost : TestHost {
    unsigned calls=0;
    bool supportsDocumentOperations() const override { return true; }
    Json documentOperation(const std::string &,const Json &) override {
      // Deliberately invalid host output after a protocol-fixture commit.
      ++calls;state.revision="committed:1";
      return {{"invalid",std::string("\xFF",1)}};
    }
  } host;
  std::unique_ptr<SessionAdapter> api;
  const auto name=L"\\\\.\\pipe\\ScreamSeq.Api.SerializationTest."+std::to_wstring(GetCurrentProcessId());
  PipeServer server(name,[&](const json &q) {
    if(!api) api=std::make_unique<SessionAdapter>(host);
    return api->handle(q);
  });
  server.start();
  const auto q=request("pattern.apply",{{"expectedRevision",host.state.revision}},"serialization-failure");
  const auto wire=pipeExchange(name,q.dump()+"\n");
  check(wire.size()<1024,"serialization fallback is a bounded ordinary JSON response");
  const auto reply=json::parse(wire);
  check(reply["error"]["code"]==-32003 && reply["id"]==q["id"],"adapter preserves serialization failure request identity");
  check(!reply.contains("result") && reply["error"]["data"]==json({{"writeOutcome","unknown"}}),"serialization failure must not assert unchanged or return a pre-commit revision");
  const auto readback=json::parse(pipeExchange(name,request("document.get").dump()+"\n"));
  check(readback["result"]["revision"]=="committed:1","transport failure leaves committed state visible on readback");
  const auto retry=json::parse(pipeExchange(name,q.dump()+"\n"));
  check(retry==reply,"retained serialization outcome must replay instead of re-entering host");
  server.stop();
  check(host.calls==1,"serialization replay entered host twice");
  std::cout<<"PASS bounded real-pipe serialization outcome/replay and committed-state readback\n";
}
void completionPipeTests() {
  struct OutcomeHost : TestHost {
    unsigned effects=0;
    bool failSnapshot=false,typedSnapshot=false;
    bool supportsDocumentOperations() const override {return true;}
    SessionSnapshot snapshot()override {
      if(failSnapshot){
        failSnapshot=false;
        if(typedSnapshot)throw ApiError(-32003,"Injected snapshot refusal",Tracker::WriteOutcome{Tracker::CommitOutcome::NotCommitted,"other-document","other-revision"});
        throw ApiError(-32003,"Injected snapshot failure");
      }
      state.document["effects"]=effects;return state;
    }
    Json documentOperation(const std::string &,const Json &p)override {
      const auto stage=p.at("stage").get<std::string>();
      if(stage=="before")throw ApiError(-32003,"Injected boundary failure",
        Tracker::WriteOutcome{Tracker::CommitOutcome::NotCommitted,state.documentId,state.revision});
      ++effects;
      if(p.value("bump",true))state.revision="committed:"+std::to_string(effects);
      if(stage=="after")throw ApiError(-32003,"Injected boundary failure",
        Tracker::WriteOutcome{Tracker::CommitOutcome::Committed,state.documentId,state.revision});
      if(stage=="snapshot"||stage=="snapshot-rejected"){failSnapshot=true;typedSnapshot=stage=="snapshot-rejected";}
      if(stage=="serialize")return {{"invalid",std::string("\xFF",1)}};
      if(stage=="oversize")return {{"large",std::string(PipeServer::maxResponseBytes,'x')}};
      return {{"effects",effects}};
    }
  }host;
  std::unique_ptr<SessionAdapter> api;
  const auto name=L"\\\\.\\pipe\\ScreamSeq.Api.Outcomes."+std::to_wstring(GetCurrentProcessId());
  PipeServer server(name,[&](const json &q){if(!api)api=std::make_unique<SessionAdapter>(host);return api->handle(q);});
  server.start();
  const auto send=[&](const json &q){return json::parse(pipeExchange(name,q.dump()+"\n"));};
  auto current=send(request("document.get"))["result"];
  const auto rejectedRequest=request("pattern.apply",{{"expectedRevision",current["revision"]},{"stage","before"}},"rejected");
  const auto rejected=send(rejectedRequest);
  check(rejected["error"]["data"]["writeOutcome"]=="notCommitted","pipe lost proven rejection");
  check(send(rejectedRequest)==rejected,"proven rejection retry changed outcome");
  const auto afterRequest=request("pattern.apply",{{"expectedRevision",current["revision"]},{"stage","after"}},"committed");
  const auto after=send(afterRequest);
  check(after["error"]["code"]==rejected["error"]["code"] &&
    after["error"]["message"]==rejected["error"]["message"] &&
    after["error"]["data"]["writeOutcome"]=="committed","pipe collapsed equal code/message outcomes");
  check(after["error"]["data"]["documentId"]=="test" &&
    after["error"]["data"]["revision"]!=current["revision"],"committed pipe error lost actual identity/revision");
  check(send(afterRequest)==after,"committed pipe receipt not retained");
  auto changed=afterRequest;changed["params"]["bump"]=false;
  check(send(changed)["error"]["code"]==-32600,"retained error allowed changed request payload");
  for(const auto *stage:{"snapshot","snapshot-rejected","serialize","oversize"}) {
    current=send(request("document.get"))["result"];
    const auto q=request("pattern.apply",{{"expectedRevision",current["revision"]},{"stage",stage},{"bump",false}},stage);
    const auto result=send(q);
    check(result["error"]["code"]==-32003 && result["id"]==stage &&
      result["error"]["data"]==json({{"writeOutcome","unknown"}}),"post-host failure asserted rejection or stale revision");
    check(send(q)==result,"unchanged-revision completion error repeated side effect");
    const auto read=send(request("document.get"))["result"];
    check(read["revision"]==current["revision"] &&
      read["data"]["effects"].get<unsigned>()==current["data"]["effects"].get<unsigned>()+1,
      "domain readback must reveal exactly one effect despite unchanged song revision");
  }
  check(send(afterRequest)==after,"later failures lost earlier committed receipt");
  server.stop();
  check(host.effects==5,"pipe completion scenarios duplicated effects");
  std::cout<<"PASS real-pipe typed boundaries, unchanged-revision effects, snapshot/serialization/size failures and exact replay\n";
}
void requestBoundaryTests() {
  const auto name=L"\\\\.\\pipe\\ScreamSeq.Api.Bounds."+std::to_wstring(GetCurrentProcessId());
  std::atomic<unsigned> calls=0;
  PipeServer server(name,[&](const json &q){++calls;return json{{"id",q["id"]},{"bytes",q["params"]["data"].get_ref<const std::string &>().size()}};},15000);
  server.start();auto q=request("size",{{"data",""}});
  const auto base=q.dump().size()+1;
  q["params"]["data"]=std::string(PipeServer::maxRequestBytes-base,'A');
  auto line=q.dump()+"\n";
  check(line.size()==PipeServer::maxRequestBytes,"construct exact newline-inclusive request boundary");
  auto result=json::parse(pipeExchange(name,line));
  check(result["bytes"]==PipeServer::maxRequestBytes-base && calls==1,"exact 32 MiB request reaches handler intact");
  line.insert(line.size()-1,1,' ');
  auto rejected=json::parse(pipeExchange(name,line));
  check(rejected["error"]["code"]==-32600 && calls==1,"one-byte excess is rejected before dispatch");
  check(json::parse(pipeExchange(name,request("size",{{"data","ok"}}).dump()+"\n"))["bytes"]==2,"server recovers after oversize request");
  server.stop();std::cout<<"PASS exact 32 MiB request boundary and post-rejection recovery\n";
}
int main(int argc, char **argv) {
  try {
    if(argc==2 && std::string(argv[1])=="--serve") {
      TestHost host;
      std::unique_ptr<SessionAdapter> api;
      const auto name=L"\\\\.\\pipe\\ScreamSeq.Api.PythonTest."+std::to_wstring(GetCurrentProcessId());
      PipeServer server(name,[&](const json &q) {
        // This fixture has no GUI/audio thread; transfer ownership to its worker.
        if(!api) api=std::make_unique<SessionAdapter>(host);
        return api->handle(q);
      });
      server.start(); std::wcout << name << std::endl;
      std::cin.get(); server.stop(); return 0;
    }
    SessionAdapter session;
    auto result = session.handle(request("pattern.apply"));
    check(result.is_object() && result.contains("error") && result["error"]["code"] == -32601,
      "unsupported editing must return -32601");
    std::cout << "PASS unsupported editing\n";
    for(auto bad : {json::array(), json(nullptr), request("document.get",json::array()),
                    request("document.get",json::object(),""), json{{"jsonrpc","2.0"},{"id",17},{"method","document.get"},{"params",json::object()}}}) {
      const auto reply = session.handle(bad);
      check(reply["error"]["code"] == -32600 && reply["id"].is_null(), "invalid envelopes reject with null id");
    }
    auto extra = request("document.get"); extra["extra"] = true;
    check(session.handle(extra)["error"]["code"] == -32600,"unknown envelope fields reject");
    std::cout << "PASS envelope validation\n";
    auto described = session.handle(request("api.describe"));
    check(described.contains("result"), "api.describe must work without a host");
    check(described["result"]["data"]["writes"] == json::array({"transport.play","transport.stop","context.set","workspace.input","workspace.panel","workspace.layout","workspace.shortcut.set"}), "only implemented navigation/workspace/transport methods advertised");
    check(session.handle(request("api.describe",{{"unknown",1}}))["error"]["code"] == -32602, "describe rejects params");
    std::cout << "PASS minimal capabilities\n";
    check(session.handle(request("document.get"))["error"]["code"] == -32002, "unbound session must not invent document data");
    sessionTests();
    playbackLoopTests();
    transientInputTests();
    pipeTests();
    serializationPipeTests();
    completionPipeTests();
    requestBoundaryTests();
    return 0;
  } catch(const std::exception &e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
