#include "PipeServer.hpp"
#include "SessionAdapter.hpp"
#include <windows.h>
#include <sddl.h>
#include <algorithm>
#include <thread>
#include <vector>
#include <system_error>

namespace ScreamSeq::Api {
namespace {
struct Handle {
  HANDLE value = nullptr;
  explicit Handle(HANDLE v = nullptr) : value(v) {}
  ~Handle() { if(value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
  Handle(const Handle &) = delete;
  Handle &operator=(const Handle &) = delete;
};
[[noreturn]] void winError(const char *what) {
  throw std::system_error(int(GetLastError()), std::system_category(), what);
}
struct PrivateSecurity {
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  SECURITY_ATTRIBUTES attributes{};
  PrivateSecurity() {
    Handle token;
    if(!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value)) winError("OpenProcessToken");
    DWORD bytes = 0;
    GetTokenInformation(token.value, TokenUser, nullptr, 0, &bytes);
    if(!bytes) winError("TokenUser size");
    std::vector<unsigned char> storage(bytes);
    if(!GetTokenInformation(token.value, TokenUser, storage.data(), bytes, &bytes)) winError("TokenUser");
    LPWSTR sid = nullptr;
    if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(storage.data())->User.Sid, &sid)) winError("User SID");
    // Protected DACL: a single allow ACE for this user, no inherited, Everyone,
    // network, Administrators or SYSTEM entries. Remote rejection is separate.
    const std::wstring sddl = L"D:P(A;;GA;;;" + std::wstring(sid) + L")";
    LocalFree(sid);
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) winError("Private DACL");
    attributes = {sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
  }
  ~PrivateSecurity() { if(descriptor) LocalFree(descriptor); }
};
}
struct PipeServer::Impl {
  std::wstring path;
  Handler handler;
  unsigned timeout;
  Handle stopEvent{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
  HANDLE pipe = INVALID_HANDLE_VALUE;
  std::thread worker;
  Impl(std::wstring p, Handler h, unsigned t) : path(std::move(p)), handler(std::move(h)), timeout(t) {
    const std::wstring prefix=L"\\\\.\\pipe\\";
    if(path.compare(0,prefix.size(),prefix)!=0 || path.size()<=prefix.size() || path.size()>240
        || path.find_first_of(L"\\/",prefix.size())!=std::wstring::npos || path.find(L'\0')!=std::wstring::npos)
      throw std::invalid_argument("Use an explicit local \\\\.\\pipe\\name (include the target PID)");
    if(!handler || timeout==0 || timeout>120000) throw std::invalid_argument("Handler and bounded I/O timeout required");
    if(!stopEvent.value) winError("CreateEvent");
  }
  bool stopped() const { return WaitForSingleObject(stopEvent.value, 0)==WAIT_OBJECT_0; }
  // The OVERLAPPED and its buffers live until Windows acknowledges completion,
  // including cancellation. No synchronous ReadFile/WriteFile/FlushFileBuffers.
  bool finish(OVERLAPPED &op, BOOL immediate, DWORD &bytes, DWORD waitMs) {
    if(immediate) return !stopped();
    if(GetLastError()!=ERROR_IO_PENDING) return false;
    HANDLE waits[]={stopEvent.value,op.hEvent};
    const DWORD waited=WaitForMultipleObjects(2, waits, FALSE, waitMs);
    if(waited!=WAIT_OBJECT_0+1) {
      CancelIoEx(pipe, &op);
      GetOverlappedResult(pipe, &op, &bytes, TRUE); // drain cancelled kernel I/O
      return false;
    }
    return GetOverlappedResult(pipe,&op,&bytes,FALSE)!=FALSE && !stopped();
  }
  DWORD remaining(ULONGLONG deadline) const {
    const auto now=GetTickCount64();
    return now>=deadline ? 0 : DWORD(std::min<ULONGLONG>(deadline-now,timeout));
  }
  bool read(char *data, DWORD capacity, DWORD &bytes, ULONGLONG deadline) {
    Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));
    if(!event.value || stopped() || !remaining(deadline)) return false;
    OVERLAPPED op{}; op.hEvent=event.value;
    const BOOL ok=ReadFile(pipe,data,capacity,&bytes,&op);
    return finish(op,ok,bytes,remaining(deadline));
  }
  bool write(const std::string &data) {
    const auto deadline=GetTickCount64()+timeout;
    std::size_t offset=0;
    while(offset<data.size() && !stopped() && remaining(deadline)) {
      Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));
      if(!event.value) return false;
      OVERLAPPED op{}; op.hEvent=event.value; DWORD bytes=0;
      const BOOL ok=WriteFile(pipe,data.data()+offset,DWORD(data.size()-offset),&bytes,&op);
      if(!finish(op,ok,bytes,remaining(deadline)) || !bytes) return false;
      offset+=bytes;
    }
    return offset==data.size();
  }
  void serve() {
    std::string input;
    const auto deadline=GetTickCount64()+timeout;
    bool framed=false;
    while(input.size()<=maxRequestBytes && !stopped()) {
      char buffer[4096]; DWORD bytes=0;
      if(!read(buffer,DWORD(std::min<std::size_t>(sizeof(buffer),maxRequestBytes+1-input.size())),bytes,deadline) || !bytes) return;
      input.append(buffer,bytes);
      // Previous chunks have no newline. Scanning the accumulated request on
      // every read makes large PCM/clipboard requests quadratic in their size.
      const auto newline=std::string_view(buffer,bytes).find('\n');
      if(newline!=std::string_view::npos) { framed=newline==bytes-1 && input.size()<=maxRequestBytes; break; }
    }
    if(stopped()) return;
    Json response=errorResponse(nullptr,-32600,"Expected one bounded JSON request followed by a newline");
    if(framed) {
      Json request;
      try {
        request=Json::parse(input,[](int depth, Json::parse_event_t, Json &) {
          if(depth>64) throw std::runtime_error("JSON nesting exceeds limit");
          return true;
        },false);
      } catch(...) { request=nullptr; }
      if(!request.is_discarded() && validEnvelope(request)) {
        try { response=handler(request); }
        catch(const std::exception &) { response=errorResponse(request["id"],-32003,"Request handler failed"); }
        catch(...) { response=errorResponse(request["id"],-32003,"Request handler failed"); }
      }
    }
    std::string output;
    try { output=response.dump(); }
    catch(...) { output=errorResponse(nullptr,-32003,"Response serialization failed").dump(); }
    if(output.size()+1>maxResponseBytes) output=errorResponse(response.value("id",Json(nullptr)),-32003,"Response exceeds transport limit").dump();
    output+='\n';
    if(write(output)) {
      // DisconnectNamedPipe discards unread bytes. Give the client time to read
      // its reply and close without the unbounded FlushFileBuffers trap. Never
      // dispatch a second request, and don't let dribbled bytes extend timeout.
      char discard[256]; DWORD bytes=0;
      const auto closeDeadline=GetTickCount64()+timeout;
      while(read(discard,sizeof(discard),bytes,closeDeadline) && bytes) {}
    }
  }
  void run() noexcept {
    while(!stopped()) {
      Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));
      if(!event.value) break;
      OVERLAPPED op{}; op.hEvent=event.value; DWORD bytes=0;
      const BOOL connected=ConnectNamedPipe(pipe,&op);
      const DWORD error=connected ? ERROR_SUCCESS : GetLastError();
      bool ready=connected || error==ERROR_PIPE_CONNECTED;
      if(error==ERROR_IO_PENDING) ready=finish(op,FALSE,bytes,INFINITE);
      if(ready && !stopped()) { try { serve(); } catch(...) {} }
      DisconnectNamedPipe(pipe);
    }
  }
};
PipeServer::PipeServer(std::wstring name, Handler handler, unsigned timeout)
  : impl_(std::make_unique<Impl>(std::move(name),std::move(handler),timeout)) {}
PipeServer::~PipeServer() { stop(); }
const std::wstring &PipeServer::name() const noexcept { return impl_->path; }
void PipeServer::start() {
  auto &s=*impl_;
  if(s.worker.joinable()) return;
  PrivateSecurity security;
  ResetEvent(s.stopEvent.value);
  s.pipe=CreateNamedPipeW(s.path.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
    PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,65536,65536,s.timeout,&security.attributes);
  if(s.pipe==INVALID_HANDLE_VALUE) winError("CreateNamedPipe private first instance");
  try { s.worker=std::thread([&s]{s.run();}); }
  catch(...) { CloseHandle(s.pipe); s.pipe=INVALID_HANDLE_VALUE; throw; }
}
void PipeServer::stop() noexcept {
  auto &s=*impl_;
  SetEvent(s.stopEvent.value);
  if(s.worker.joinable()) s.worker.join();
  if(s.pipe!=INVALID_HANDLE_VALUE) { CloseHandle(s.pipe); s.pipe=INVALID_HANDLE_VALUE; }
}
}
