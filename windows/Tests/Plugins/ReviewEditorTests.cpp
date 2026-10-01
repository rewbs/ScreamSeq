#include "windows/Plugins/WindowsVST3.hpp"
#include "windows/Plugins/UiOwner.hpp"
#include <windows.h>
#include <iostream>
#include <filesystem>
#include <stdexcept>
using namespace Tracker;
static void check(bool b,const char*s){if(!b)throw std::runtime_error(s);}
int main(int argc,char**argv){try{
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);check(argc==5,"args");USHORT emulated=0,native=0;check(IsWow64Process2(GetCurrentProcess(),&emulated,&native)&&native==IMAGE_FILE_MACHINE_ARM64&&!emulated,"native ARM64");
 SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 WindowsVST3::configure(argv[1],argv[3]);auto ds=WindowsVST3::rescan(argv[2]);auto p=platformPluginBackendFactory().create(PluginState{ds[0]},48000,true);
 auto dll=GetModuleHandleW(std::filesystem::u8path(ds[0].path).c_str());auto mode=reinterpret_cast<void(*)(int)>(GetProcAddress(dll,"FixtureReviewMode"));auto metric=reinterpret_cast<int(*)(int)>(GetProcAddress(dll,"FixtureViewMetric"));auto request=reinterpret_cast<int(*)(int,int)>(GetProcAddress(dll,"FixtureRequestSize"));auto extreme=reinterpret_cast<int(*)()>(GetProcAddress(dll,"FixtureExtremeSize"));check(mode&&metric&&request&&extreme,"exports");
 auto foreground=GetForegroundWindow();int testMode=std::stoi(argv[4]);
 WindowsVST3::pluginMainCall([&]{SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);mode(testMode);});
 p->showEditor();check(p->editorOpen(),"open editor");
 WindowsVST3::pluginMainCall([&]{
  auto w=FindWindowW(L"ScreamSeq.VST3.PrivateEditor",L"Resonance Test Gain");DWORD pid=0;GetWindowThreadProcessId(w,&pid);check(w&&pid==GetCurrentProcessId(),"exact own HWND");
  auto size=[&](int x,int y){RECT r{};GetClientRect(w,&r);if(r.right!=x||r.bottom!=y)std::cerr<<"client="<<r.right<<"x"<<r.bottom<<" expected="<<x<<"x"<<y<<'\n';check(r.right==x&&r.bottom==y,"wrong native client dimensions");check(metric(2)==x&&metric(3)==y,"wrong plugin onSize dimensions");};
  auto setSize=[&](int x,int y){RECT r{0,0,x,y};check(AdjustWindowRectEx(&r,DWORD(GetWindowLongPtrW(w,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(w,GWL_EXSTYLE))),"adjust rect");check(SetWindowPos(w,nullptr,0,0,r.right-r.left,r.bottom-r.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE),"set window size");};
  size(520,220);
  if(testMode==40){
   check(!(GetWindowLongPtrW(w,GWL_STYLE)&(WS_THICKFRAME|WS_MAXIMIZEBOX)),"fixed plugin has user-resize frame");
   int count=metric(1);setSize(700,300);size(520,220);check(metric(1)==count,"fixed view received user resize");
   check(request(600,240)==0,"fixed plugin cannot request own layout");size(600,240);
  }else{
   check(GetWindowLongPtrW(w,GWL_STYLE)&WS_THICKFRAME,"resizable view has no frame");
   int count=metric(1),queries=metric(4);setSize(700,300);size(700,testMode==41?350:300);
   check(metric(4)>queries&&metric(1)==count+1,"constraint missing or duplicate onSize callback");
   if(testMode==41){
    RECT outer{30,40,930,640},before=outer;SendMessageW(w,WM_SIZING,WMSZ_BOTTOMRIGHT,reinterpret_cast<LPARAM>(&outer));
    RECT frame{0,0,0,0};AdjustWindowRectEx(&frame,DWORD(GetWindowLongPtrW(w,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(w,GWL_EXSTYLE)));
    check(outer.right-outer.left-(frame.right-frame.left)==800&&outer.bottom-outer.top-(frame.bottom-frame.top)==400,"WM_SIZING did not constrain aspect/maximum");
    setSize(1000,600);size(800,400);
   }
   check(metric(5)==0,"same-size reentrant resize failed");
  }
  int count=metric(1);ShowWindow(w,SW_SHOWMINNOACTIVE);check(IsIconic(w),"minimize native HWND");check(metric(1)==count&&metric(0)==0,"minimized zero size forwarded to plugin");ShowWindow(w,SW_SHOWNOACTIVATE);
  check(extreme()!=0&&request(0,220)!=0&&request(8193,220)!=0,"unsafe plugin-requested dimension accepted");
 });
 p->closeEditor();check(!p->editorOpen(),"close editor");check(GetForegroundWindow()==foreground,"editor activated foreground");
 std::cout<<"PASS native ARM64 actual HWND sizing mode="<<testMode<<" no activation\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
