#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <algorithm>
#include <atomic>
#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace ScreamSeq::Tests {
namespace PrivateGuiDetail {
inline std::string narrow(const wchar_t *value){
  const auto count=WideCharToMultiByte(CP_UTF8,0,value,-1,nullptr,0,nullptr,nullptr);
  std::string result(size_t(std::max(1,count)),0);
  if(count)WideCharToMultiByte(CP_UTF8,0,value,-1,result.data(),count,nullptr,nullptr);
  result.resize(size_t(std::max(1,count)-1));return result;
}
inline std::string error(const char *action,DWORD code){
  wchar_t message[512]{};FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,code,0,message,512,nullptr);
  return std::string(action)+": Win32 "+std::to_string(code)+" ("+narrow(message)+")";
}
inline std::string describe(std::exception_ptr failure){
  try{if(failure)std::rethrow_exception(failure);}catch(const std::exception &exception){return exception.what();}catch(...){return "non-standard exception";}return {};
}
inline bool fixtureClass(const wchar_t *name){
  if(_wcsnicmp(name,L"ScreamSeq",9)==0)return true;
  for(const auto known:{L"EDIT",L"COMBOBOX",L"ComboLBox",L"LISTBOX",L"SysListView32",L"SysHeader32",L"BUTTON",L"STATIC"})
    if(_wcsicmp(name,known)==0)return true;
  return false;
}
inline constexpr wchar_t ownedProperty[]=L"ScreamSeq.PrivateGuiTest.Owned";
struct Windows {
  std::vector<HWND> owned,seen;
  bool leaked=false;
  std::exception_ptr inventoryFailure;
  void track(HWND window){
    if(!window||GetWindowThreadProcessId(window,nullptr)!=GetCurrentThreadId())throw std::runtime_error("Track a live private GUI thread window");
    if(!SetPropW(window,ownedProperty,reinterpret_cast<HANDLE>(this)))throw std::runtime_error(error("Mark owned fixture window",GetLastError()));
    owned.push_back(window);
  }
  void inspect(HWND window){
    if(GetWindowThreadProcessId(window,nullptr)!=GetCurrentThreadId()||std::find(seen.begin(),seen.end(),window)!=seen.end())return;
    seen.push_back(window);wchar_t kind[256]{};
    if(!GetClassNameW(window,kind,256))throw std::runtime_error(error("Read remaining window class",GetLastError()));
    const bool fixture=GetPropW(window,ownedProperty)==reinterpret_cast<HANDLE>(this)||fixtureClass(kind);
    leaked|=fixture;
    std::cerr<<(fixture?"Leaked fixture":"Remaining OS/unknown")<<" HWND="<<window<<" class="<<narrow(kind)
      <<" thread="<<GetCurrentThreadId()<<" process="<<GetCurrentProcessId()<<" parent="<<GetParent(window)
      <<" owner="<<GetWindow(window,GW_OWNER)<<" visible="<<IsWindowVisible(window)<<'\n';
  }
  static BOOL CALLBACK child(HWND window,LPARAM value)noexcept{
    auto &self=*reinterpret_cast<Windows *>(value);try{self.inspect(window);}catch(...){self.inventoryFailure=std::current_exception();return FALSE;}return TRUE;
  }
  static BOOL CALLBACK root(HWND window,LPARAM value)noexcept{
    auto &self=*reinterpret_cast<Windows *>(value);try{self.inspect(window);}catch(...){self.inventoryFailure=std::current_exception();return FALSE;}
    EnumChildWindows(window,child,value);return self.inventoryFailure?FALSE:TRUE;
  }
  void verify(){
    // Window properties disappear at destruction, so recycled HWND values do
    // not turn an old fixture handle into a false leak of an unrelated window.
    for(const auto window:owned)if(IsWindow(window)&&GetPropW(window,ownedProperty)==reinterpret_cast<HANDLE>(this))inspect(window);
    // EnumThreadWindows also returns FALSE for an empty inventory;
    // EnumChildWindows has no meaningful return value. Callback exceptions
    // are retained explicitly, independent of those enumeration returns.
    EnumThreadWindows(GetCurrentThreadId(),root,reinterpret_cast<LPARAM>(this));
    for(HWND window=FindWindowExW(HWND_MESSAGE,nullptr,nullptr,nullptr);window;window=FindWindowExW(HWND_MESSAGE,window,nullptr,nullptr)){
      root(window,reinterpret_cast<LPARAM>(this));if(inventoryFailure)break;
    }
    if(inventoryFailure)std::rethrow_exception(inventoryFailure);
    if(leaked)throw std::runtime_error("Private GUI fixture left owned windows alive before thread exit");
  }
};
inline thread_local Windows *currentWindows=nullptr;
}

// Call immediately after constructing fixture-owned roots/controls. Teardown
// checks run even when a fixture throws, before Windows ends the GUI thread.
inline HWND ownGuiWindow(HWND window){
  if(!PrivateGuiDetail::currentWindows)throw std::runtime_error("No private GUI fixture is running");
  PrivateGuiDetail::currentWindows->track(window);return window;
}

// Geometry fixtures sometimes need a real wide HWND on a smaller headless
// desktop. Windows' default maximum tracking size otherwise clamps SetWindowPos
// before the editor sees WM_SIZE. Override only the fixture's maximum while
// resizing; retain its production minimum and verify the actual client result.
inline void sizeOwnedGuiClient(HWND window,int width,int height){
  if(!PrivateGuiDetail::currentWindows||GetWindowThreadProcessId(window,nullptr)!=GetCurrentThreadId()||
      GetPropW(window,PrivateGuiDetail::ownedProperty)!=reinterpret_cast<HANDLE>(PrivateGuiDetail::currentWindows))
    throw std::runtime_error("Client sizing requires an owned private GUI thread");
  const auto dpi=GetDpiForWindow(window);
  RECT frame{0,0,MulDiv(width,dpi,96),MulDiv(height,dpi,96)};
  const LONG expectedWidth=frame.right,expectedHeight=frame.bottom;
  if(!AdjustWindowRectExForDpi(&frame,DWORD(GetWindowLongPtrW(window,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(window,GWL_EXSTYLE)),dpi))
    throw std::runtime_error("Cannot calculate private client frame");
  POINT requested{frame.right-frame.left,frame.bottom-frame.top};
  const SUBCLASSPROC overrideMaximum=[](HWND h,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data)->LRESULT {
    const auto result=DefSubclassProc(h,message,w,l);
    if(message==WM_GETMINMAXINFO){const auto &requested=*reinterpret_cast<const POINT *>(data);auto &limits=*reinterpret_cast<MINMAXINFO *>(l);
      limits.ptMaxTrackSize.x=std::max(limits.ptMaxTrackSize.x,requested.x);
      limits.ptMaxTrackSize.y=std::max(limits.ptMaxTrackSize.y,requested.y);}
    return result;
  };
  constexpr UINT_PTR subclass=0x53515A45;
  if(!SetWindowSubclass(window,overrideMaximum,subclass,reinterpret_cast<DWORD_PTR>(&requested)))
    throw std::runtime_error("Cannot install private geometry fixture");
  const auto changed=SetWindowPos(window,nullptr,0,0,requested.x,requested.y,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
  RemoveWindowSubclass(window,overrideMaximum,subclass);
  RECT actual{};
  if(!changed||!GetClientRect(window,&actual)||actual.right!=expectedWidth||actual.bottom!=expectedHeight)
    throw std::runtime_error("Private client bounds differ: expected "+std::to_string(expectedWidth)+"x"+std::to_string(expectedHeight)+
      ", got "+std::to_string(actual.right)+"x"+std::to_string(actual.bottom)+", dpi "+std::to_string(dpi));
}

// The observer never leaves its original desktop. A fresh worker attaches
// before creating any GUI objects and exits on that private desktop. This
// avoids migrating a used GUI thread while retaining strict ownership and
// isolation checks; it does not identify the cause of historical error 170.
template<typename Body>void runPrivateGui(const wchar_t *prefix,Body body){
  const auto observer=GetCurrentThreadId();const auto original=GetThreadDesktop(observer);
  if(!original)throw std::runtime_error(PrivateGuiDetail::error("Read original GUI observer desktop",GetLastError()));
  const auto foreground=GetForegroundWindow();const auto clipboard=GetClipboardSequenceNumber();
  LARGE_INTEGER serial{};QueryPerformanceCounter(&serial);static std::atomic<unsigned> sequence{0};
  const auto name=std::wstring(prefix)+L"-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(serial.QuadPart)+L"-"+std::to_wstring(++sequence);
  const auto desktop=CreateDesktopW(name.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);
  std::exception_ptr bodyFailure,cleanupFailure,startFailure;
  DWORD creationError=desktop?ERROR_SUCCESS:GetLastError();
  if(desktop){
    try{
      std::jthread gui([&]{
        // This must be the worker's first GUI/desktop operation.
        if(!SetThreadDesktop(desktop)){try{throw std::runtime_error(PrivateGuiDetail::error("Attach private GUI worker",GetLastError()));}catch(...){bodyFailure=std::current_exception();}return;}
        PrivateGuiDetail::Windows windows;PrivateGuiDetail::currentWindows=&windows;
        try{body();}catch(...){bodyFailure=std::current_exception();}
        try{windows.verify();}catch(...){cleanupFailure=std::current_exception();}
        PrivateGuiDetail::currentWindows=nullptr;
        // Do not restore this used GUI thread or destroy unknown OS windows.
      });
      gui.join();
    }catch(...){startFailure=std::current_exception();}
  }
  // Evaluate every observer/cleanup guard even when the body or attach failed.
  const bool closed=!desktop||CloseDesktop(desktop);const DWORD closeError=closed?ERROR_SUCCESS:GetLastError();
  const bool sameDesktop=GetCurrentThreadId()==observer&&GetThreadDesktop(observer)==original;
  const bool sameForeground=GetForegroundWindow()==foreground;
  const bool sameClipboard=GetClipboardSequenceNumber()==clipboard;
  std::ostringstream failures;
  if(!desktop)failures<<PrivateGuiDetail::error("Create private GUI desktop",creationError)<<'\n';
  if(startFailure)failures<<"GUI worker: "<<PrivateGuiDetail::describe(startFailure)<<'\n';
  if(bodyFailure)failures<<"Fixture: "<<PrivateGuiDetail::describe(bodyFailure)<<'\n';
  if(cleanupFailure)failures<<"Fixture cleanup: "<<PrivateGuiDetail::describe(cleanupFailure)<<'\n';
  if(!closed)failures<<PrivateGuiDetail::error("Close private GUI desktop after worker exit",closeError)<<'\n';
  if(!sameDesktop)failures<<"Private GUI observer changed its original desktop\n";
  if(!sameForeground)failures<<"Private GUI tests changed the user's foreground window\n";
  if(!sameClipboard)failures<<"Private GUI tests changed the user's clipboard\n";
  if(!failures.str().empty())throw std::runtime_error(failures.str());
}
}
