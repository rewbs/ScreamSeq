// Real owner-message dispatch and named pipe: an offline export can take longer
// than ordinary edits, but queued cancellation, replay and close stay bounded.
#include "../../App/ApiDispatch.hpp"
#include <future>

void dispatchDeadlineTests() {
  struct Host : TestHost {
    unsigned calls=0;std::function<void()> action;
    bool supportsDocumentOperations()const override{return true;}
    Json documentOperation(const std::string &,const Json &)override {
      ++calls;if(action)action();return {{"written",true}};
    }
  } host;
  WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"ScreamSeq.ApiDispatchTest";
  cls.lpfnWndProc=[](HWND window,UINT message,WPARAM w,LPARAM l)->LRESULT {
    if(message==ScreamSeq::ApiDispatch::message) {
      if(auto *dispatch=reinterpret_cast<ScreamSeq::ApiDispatch *>(GetWindowLongPtrW(window,GWLP_USERDATA)))dispatch->drain();
      return 0;
    }
    return DefWindowProcW(window,message,w,l);
  };
  check(RegisterClassW(&cls)!=0,"Register dispatch test owner");
  const HWND window=CreateWindowW(cls.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,cls.hInstance,nullptr);
  check(window!=nullptr,"Create message-only dispatch owner");
  struct WindowGuard {HWND window;~WindowGuard(){DestroyWindow(window);}} windowGuard{window};
  auto dispatch=std::make_unique<ScreamSeq::ApiDispatch>(window,host);
  SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(dispatch.get()));
  const auto pipe=L"\\\\.\\pipe\\ScreamSeq.Api."+std::to_wstring(GetCurrentProcessId());
  const auto send=[&](const Json &q){return std::async(std::launch::async,[&,wire=q.dump()+"\n"]{return pipeExchange(pipe,wire);});};
  const auto pump=[&](auto &reply) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(12);
    while(reply.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready) {
      MSG message{};
      while(PeekMessageW(&message,window,0,0,PM_REMOVE))DispatchMessageW(&message);
      if(std::chrono::steady_clock::now()>=deadline){dispatch->close();throw std::runtime_error("Dispatch reply exceeded bounded test deadline");}
      Sleep(1);
    }
    return Json::parse(reply.get());
  };
  const auto exported=request("document.exportAudio",{{"expectedRevision",host.state.revision}},"slow-export");
  host.action=[]{Sleep(5250);};
  auto reply=send(exported);const auto result=pump(reply);
  check(result.contains("result")&&host.calls==1,"Full-song export lost its completion after the five-second edit deadline");
  reply=send(exported);
  check(pump(reply)==result&&host.calls==1,"Completed slow export replay published twice");

  // Ordinary requests retain the existing budget; do not extend all writes.
  reply=send(request("document.save",{{"expectedRevision",host.state.revision}},"slow-save"));
  const auto ordinary=pump(reply);
  check(ordinary.at("error").at("code")==-32003&&host.calls==2,"Ordinary in-flight deadline lost uncertain-outcome classification");

  host.action={};
  reply=send(request("document.exportAudio",{{"expectedRevision",host.state.revision}},"queued-export"));
  check(reply.wait_for(std::chrono::seconds(7))==std::future_status::ready,"Queued export did not retain the five-second admission bound");
  check(Json::parse(reply.get()).at("error").at("code")==-32002,"Unstarted export was classified as committed or uncertain");
  MSG message{};while(PeekMessageW(&message,window,0,0,PM_REMOVE))DispatchMessageW(&message);
  check(host.calls==2,"Cancelled queued export later executed");

  // Simulate native shutdown while the owner is inside a long operation.
  // Closing must wake and join the pipe owner, not wait 120 seconds for itself.
  std::chrono::steady_clock::duration closeDuration{};
  host.action=[&]{const auto start=std::chrono::steady_clock::now();dispatch->close();closeDuration=std::chrono::steady_clock::now()-start;};
  reply=send(request("document.exportAudio",{{"expectedRevision",host.state.revision}},"closing-export"));
  const auto closeDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  while(reply.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready) {
    while(PeekMessageW(&message,window,0,0,PM_REMOVE))DispatchMessageW(&message);
    check(std::chrono::steady_clock::now()<closeDeadline,"Active shutdown did not finish");Sleep(1);
  }
  // Transport closure may prevent delivery; neither an empty response nor a
  // closed-during-execution error proves absence of the already-begun write.
  const auto closed=reply.get();
  check(host.calls==3,"Shutdown duplicated or skipped the entered host operation");
  check(closeDuration<std::chrono::seconds(1),"Close joined a waiting active dispatch without waking it");
  if(!closed.empty())check(Json::parse(closed).at("error").at("code")==-32003,"Active shutdown claimed pre-execution cancellation");
  SetWindowLongPtrW(window,GWLP_USERDATA,0);dispatch.reset();
  std::cout<<"PASS export reply deadline, exact replay, queued cancellation, ordinary timeout and active close\n";
}
