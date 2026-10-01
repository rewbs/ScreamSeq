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
    bool supportsDocumentOperations() const override { return true; }
    Json documentOperation(const std::string &,const Json &) override {
      // Deliberately invalid host output after a protocol-fixture commit.
      state.revision="committed:1";
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
  check(reply["error"]["code"]==-32003 && reply["id"].is_null(),"existing transport serialization fallback");
  check(!reply.contains("result") && !reply["error"].contains("data"),"serialization fallback must not assert unchanged or return a pre-commit revision");
  const auto readback=json::parse(pipeExchange(name,request("document.get").dump()+"\n"));
  check(readback["result"]["revision"]=="committed:1","transport failure leaves committed state visible on readback");
  const auto retry=json::parse(pipeExchange(name,q.dump()+"\n"));
  check(retry["error"]["code"]==-32001,"unserializable reply was not cached; revision guard prevents reexecution");
  server.stop();
  std::cout<<"PASS bounded real-pipe serialization fallback and committed-state readback (outcome uncertain until read)\n";
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
    check(described["result"]["data"]["writes"] == json::array({"transport.play","transport.stop","context.set","workspace.panel","workspace.layout"}), "only implemented navigation/workspace/transport methods advertised");
    check(session.handle(request("api.describe",{{"unknown",1}}))["error"]["code"] == -32602, "describe rejects params");
    std::cout << "PASS minimal capabilities\n";
    check(session.handle(request("document.get"))["error"]["code"] == -32002, "unbound session must not invent document data");
    sessionTests();
    pipeTests();
    serializationPipeTests();
    requestBoundaryTests();
    return 0;
  } catch(const std::exception &e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
