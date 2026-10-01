#include <windows.h>
#include <atomic>
// OS failure injection is local to this unit test; successful posts use Win32.
static std::atomic<bool> failPost=false;
static std::atomic<int> posts=0;
static BOOL testPost(HWND h,UINT m,WPARAM w,LPARAM l){if(failPost)return FALSE;auto ok=PostMessageW(h,m,w,l);++posts;return ok;}
#define PostMessageW testPost
#include "windows/Plugins/UiOwner.hpp"
#undef PostMessageW
#include <chrono>
#include <cstdlib>
#include <iostream>
using namespace Tracker::WindowsVST3;
using namespace std::chrono_literals;
static void check(bool b,const char *s){if(!b){std::cerr<<"FAIL "<<s<<std::endl;std::_Exit(1);}}
static void nestedPump(){auto end=GetTickCount64()+200;while(GetTickCount64()<end){MsgWaitForMultipleObjectsEx(0,nullptr,10,QS_ALLINPUT,MWMO_INPUTAVAILABLE);MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}}
struct WindowPump {UiOwner *owner;std::promise<void> entered,done;std::atomic<bool> nested=false,ranNested=false;};
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
 auto *s=reinterpret_cast<WindowPump*>(GetWindowLongPtrW(h,GWLP_USERDATA));
 if(m==WM_NCCREATE){s=static_cast<WindowPump*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));}
 if(m==WM_APP+1){s->owner->call([&]{s->nested=true;s->entered.set_value();nestedPump();s->nested=false;});s->done.set_value();return 0;}
 return DefWindowProcW(h,m,w,l);
}
int main(){
 USHORT emulated=0,native=0;check(IsWow64Process2(GetCurrentProcess(),&emulated,&native)&&native==IMAGE_FILE_MACHINE_ARM64&&!emulated,"native ARM64");auto foreground=GetForegroundWindow();
 // Genuine startup failure, not a leaked/joinable thread on constructor unwind.
 WNDCLASSW clash{};clash.lpfnWndProc=DefWindowProcW;clash.hInstance=GetModuleHandleW(nullptr);clash.lpszClassName=L"ScreamSeq.VST3.Dispatcher";check(RegisterClassW(&clash)!=0,"register clash");
 bool rejected=false;try{UiOwner invalid;}catch(const std::exception&){rejected=true;}check(rejected,"startup conflict ignored");check(UnregisterClassW(clash.lpszClassName,clash.hInstance),"unregister clash");
 UiOwner owner;std::promise<void> entered;auto ready=entered.get_future();std::atomic<bool> nested=false,ranNested=false;std::atomic<int> internal=0;
 auto pump=std::async(std::launch::async,[&]{owner.call([&]{owner.call([&]{++internal;});nested=true;entered.set_value();nestedPump();nested=false;});});
 ready.wait();auto work=std::async(std::launch::async,[&]{owner.call([&]{ranNested=nested.load();});});pump.get();
 check(work.wait_for(1s)==std::future_status::ready,"nested DispatchMessage pump orphaned queued work after vendor returned");work.get();check(!ranNested&&internal==1,"external work reentered vendor; same-owner calls must remain inline");
 bool threw=false;try{owner.call([]{throw std::runtime_error("vendor failure");});}catch(const std::exception&){threw=true;}check(threw,"work exception not delivered");owner.call([]{});
 // Real window callback (not a queued task) invokes a modal pump.
 WindowPump state{&owner};HWND window=nullptr;owner.call([&]{WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"ScreamSeq.UiOwner.Test";RegisterClassW(&cls);window=CreateWindowExW(0,cls.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,cls.hInstance,&state);});check(window!=nullptr,"test HWND");
 auto began=state.entered.get_future(),done=state.done.get_future();check(PostMessageW(window,WM_APP+1,0,0),"post test HWND");began.wait();auto callbackWork=std::async(std::launch::async,[&]{owner.call([&]{state.ranNested=state.nested.load();});});done.wait();check(callbackWork.wait_for(1s)==std::future_status::ready,"window callback orphaned work");callbackWork.get();check(!state.ranNested,"external work reentered a native vendor window callback");owner.call([&]{DestroyWindow(window);});
 for(bool postingFails:{false,true}){
  UiOwner cancelOwner;std::promise<void> running,release;auto gate=release.get_future();auto beganCall=running.get_future();
  auto active=std::async(std::launch::async,[&]{cancelOwner.call([&]{running.set_value();gate.wait();});});beganCall.wait();
  int before=posts;auto pending=std::async(std::launch::async,[&]{try{cancelOwner.call([]{});return false;}catch(const std::exception&){return true;}});
  auto end=GetTickCount64()+1000;while(posts==before&&GetTickCount64()<end)Sleep(1);check(posts>before,"queued cancellation test work");
  if(postingFails){failPost=true;bool failed=false;try{cancelOwner.call([]{});}catch(const std::exception&){failed=true;}failPost=false;check(failed,"post failure not surfaced");}else cancelOwner.stop();
  check(pending.wait_for(1s)==std::future_status::ready&&pending.get(),"unstarted promise not cancelled");release.set_value();active.get();
  bool stopped=false;try{cancelOwner.call([]{});}catch(const std::exception&){stopped=true;}check(stopped,"stopped owner accepted work");
 }
 check(GetForegroundWindow()==foreground,"dispatcher activated a window");
 std::cout<<"PASS native ARM64 nested HWND dispatch, same-owner calls, window callbacks, startup failure, post failure, cancellation, exceptions, teardown\n";
}
