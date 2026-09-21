#pragma once
#include <windows.h>
#include <thread>
#include <future>
#include <functional>
#include <stdexcept>
#include <deque>
#include <mutex>
namespace Tracker::WindowsVST3 {
// Private STA; never called by rendering. The queue owns tasks, not messages.
// Nested vendor pumps may dispatch our HWND wakeup, but external work is deferred
// until the active call returns. Same-owner internal calls intentionally run inline.
class UiOwner {
 struct Work { std::function<void()> fn; std::promise<void> result; };
 std::thread thread_;
 DWORD id_=0;
 HWND window_=nullptr;
 HANDLE stopEvent_=nullptr;
 std::mutex mutex_;
 std::deque<std::shared_ptr<Work>> pending_;
 bool stopping_=false;
 bool dispatching_=false; // owner thread only
 static constexpr UINT wake_=WM_APP+73;
 void cancelLocked() noexcept {
  stopping_=true;
  // Dropping queue ownership completes every unstarted promise (broken_promise).
  pending_.clear();
  SetEvent(stopEvent_);
 }
 void drain() noexcept {
  if(dispatching_)return;
  dispatching_=true;
  for(;;){
   std::shared_ptr<Work> work;
   {std::lock_guard lock(mutex_);if(stopping_||pending_.empty())break;work=std::move(pending_.front());pending_.pop_front();}
   try{work->fn();work->result.set_value();}catch(...){work->result.set_exception(std::current_exception());}
  }
  dispatching_=false;
 }
 static LRESULT CALLBACK windowProc(HWND hwnd,UINT msg,WPARAM w,LPARAM l) {
  auto *self=reinterpret_cast<UiOwner*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
  if(msg==WM_NCCREATE){self=static_cast<UiOwner*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
  if(self&&msg==wake_){self->drain();return 0;}
  if(self&&msg==WM_NCDESTROY){std::lock_guard lock(self->mutex_);self->window_=nullptr;self->cancelLocked();SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);}
  return DefWindowProcW(hwnd,msg,w,l);
 }
 void run(std::promise<void> &ready) noexcept {
  const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
  try{
   if(FAILED(hr))throw std::runtime_error("VST3 UI COM initialization failed");
   id_=GetCurrentThreadId();
   WNDCLASSW cls{};cls.lpfnWndProc=windowProc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"ScreamSeq.VST3.Dispatcher";
   if(!RegisterClassW(&cls)){
    WNDCLASSW existing{};
    if(GetLastError()!=ERROR_CLASS_ALREADY_EXISTS||!GetClassInfoW(cls.hInstance,cls.lpszClassName,&existing)||existing.lpfnWndProc!=windowProc)
     throw std::runtime_error("Cannot register VST3 UI dispatcher");
   }
   window_=CreateWindowExW(0,cls.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,cls.hInstance,this);
   if(!window_)throw std::runtime_error("Cannot create VST3 UI dispatcher");
   ready.set_value();
   bool quit=false;
   while(!quit){
    const auto wait=MsgWaitForMultipleObjectsEx(1,&stopEvent_,INFINITE,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
    if(wait!=WAIT_OBJECT_0+1)break;
    // PeekMessage can itself deliver sent messages. Guard it as well as
    // DispatchMessage, including arbitrary plugin-owned window procedures.
    call([&]{
     MSG m{};
     if(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){
      if(m.message==WM_QUIT)quit=true;
      else {TranslateMessage(&m);DispatchMessageW(&m);}
     }
    });
   }
  }catch(...){try{ready.set_exception(std::current_exception());}catch(...){} }
  stop();
  if(window_)DestroyWindow(window_);
  if(SUCCEEDED(hr))CoUninitialize();
 }
public:
 UiOwner(){
  stopEvent_=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  if(!stopEvent_)throw std::runtime_error("Cannot create VST3 UI stop event");
  try{std::promise<void> ready;auto result=ready.get_future();thread_=std::thread([this,ready=std::move(ready)]()mutable{run(ready);});result.get();}
  catch(...){if(thread_.joinable())thread_.join();CloseHandle(stopEvent_);throw;}
 }
 ~UiOwner(){stop();thread_.join();CloseHandle(stopEvent_);}
 UiOwner(const UiOwner&)=delete;
 UiOwner &operator=(const UiOwner&)=delete;
 // Cancel unstarted calls immediately; a running vendor call must return before
 // thread teardown can finish. Callers must retain the owner until calls return.
 void stop() noexcept {std::lock_guard lock(mutex_);cancelLocked();}
 void call(std::function<void()> fn){
  if(GetCurrentThreadId()==id_){
   if(dispatching_){fn();return;}
   dispatching_=true;
   try{fn();}catch(...){dispatching_=false;drain();throw;}
   dispatching_=false;drain();return;
  }
  auto work=std::make_shared<Work>();work->fn=std::move(fn);auto result=work->result.get_future();
  {
   std::lock_guard lock(mutex_);
   if(stopping_)throw std::runtime_error("VST3 UI owner unavailable");
   pending_.push_back(std::move(work));
   if(!PostMessageW(window_,wake_,0,0))cancelLocked();
  }
  result.get();
 }
 static UiOwner &instance(){static UiOwner owner;return owner;}
};
inline void pluginMainCall(std::function<void()> f){UiOwner::instance().call(std::move(f));}
}
