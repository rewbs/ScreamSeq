#pragma once
#include <windows.h>
#include <commctrl.h>
#include <exception>
#include <stdexcept>
#include <vector>

namespace Tracker::WindowsVST3 {
// A short, native-owner-only lease around vendor restoration. No audio thread
// or document state belongs here. Programmatic vendor presentation remains
// available; unexpected component-handler edits are retained by the caller.
class EditorInputBarrier final {
  static constexpr UINT_PTR subclassID=0x53434542;
  DWORD thread_=GetCurrentThreadId();
  HWND root_=nullptr,focus_=nullptr;
  bool enabled_=false;
  std::vector<HWND> windows_;
  static bool input(UINT m)noexcept {
    if((m>=WM_KEYFIRST&&m<=WM_KEYLAST)||(m>=WM_MOUSEFIRST&&m<=WM_MOUSELAST)||
       (m>=WM_NCLBUTTONDOWN&&m<=WM_NCXBUTTONDBLCLK)||
       (m>=WM_POINTERUPDATE&&m<=WM_POINTERLEAVE))return true;
    switch(m) {
    case WM_COMMAND:case WM_NOTIFY:case WM_CLOSE:case WM_SYSCOMMAND:
    case WM_CONTEXTMENU:case WM_HSCROLL:case WM_VSCROLL:case WM_TIMER:
    case WM_INPUT:case WM_APPCOMMAND:case WM_DROPFILES:case WM_TOUCH:case WM_GESTURE:
    case WM_IME_STARTCOMPOSITION:case WM_IME_ENDCOMPOSITION:case WM_IME_COMPOSITION:
    case WM_IME_CHAR:case WM_IME_KEYDOWN:case WM_IME_KEYUP:
    case WM_CUT:case WM_PASTE:case WM_CLEAR:case WM_UNDO:case BM_CLICK:return true;
    default:return false;
    }
  }
  static LRESULT CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR) {
    if(m==WM_NCDESTROY)RemoveWindowSubclass(h,procedure,id);
    else {
      const auto *notification=m==WM_NOTIFY?reinterpret_cast<const NMHDR *>(l):nullptr;
      const bool readNotification=notification&&GetParent(notification->hwndFrom)==h&&
        (notification->code==LVN_GETDISPINFOW||notification->code==LVN_GETDISPINFOA||notification->code==NM_CUSTOMDRAW);
      if(!readNotification&&input(m))return 0;
    }
    return DefSubclassProc(h,m,w,l);
  }
  void attach(HWND h) {
    DWORD process=0;
    if(GetWindowThreadProcessId(h,&process)!=thread_||process!=GetCurrentProcessId())
      throw std::runtime_error("Plugin input barrier requires native-owner windows");
    DWORD_PTR existing=0;
    if(GetWindowSubclass(h,procedure,subclassID,&existing))throw std::runtime_error("Plugin input is already protected");
    windows_.push_back(h);
    if(!SetWindowSubclass(h,procedure,subclassID,reinterpret_cast<DWORD_PTR>(this)))
      throw std::runtime_error("Cannot protect plugin editor input");
  }
  struct Enumeration {EditorInputBarrier &barrier;std::exception_ptr error;};
  static BOOL CALLBACK child(HWND h,LPARAM context)noexcept {
    auto &state=*reinterpret_cast<Enumeration *>(context);
    try{state.barrier.attach(h);return TRUE;}catch(...){state.error=std::current_exception();return FALSE;}
  }
  void release()noexcept {
    if(GetCurrentThreadId()!=thread_)std::terminate();
    DWORD_PTR rootOwner=0;
    const bool ownedRoot=root_&&GetWindowSubclass(root_,procedure,subclassID,&rootOwner)&&rootOwner==reinterpret_cast<DWORD_PTR>(this);
    if(ownedRoot&&IsWindow(root_)) {
      // Enabling can run vendor code which destroys/recreates a field. Keep
      // the identity subclasses installed until after that callback and focus
      // restoration, so a recycled HWND never receives the original caret.
      EnableWindow(root_,enabled_);
      DWORD_PTR focusOwner=0;
      const bool ownedFocus=focus_&&GetWindowSubclass(focus_,procedure,subclassID,&focusOwner)&&focusOwner==reinterpret_cast<DWORD_PTR>(this);
      if(ownedFocus&&(focus_==root_||IsChild(root_,focus_))&&IsWindowEnabled(focus_)&&IsWindowVisible(focus_)&&GetActiveWindow()==root_)SetFocus(focus_);
    }
    for(auto h:windows_) {
      DWORD_PTR owner=0;
      if(GetWindowSubclass(h,procedure,subclassID,&owner)&&owner==reinterpret_cast<DWORD_PTR>(this))
        RemoveWindowSubclass(h,procedure,subclassID);
    }
    windows_.clear();
  }
public:
  explicit EditorInputBarrier(HWND root):root_(root) {
    if(!root_)return; // Also prevents opening a new editor until the lease ends.
    enabled_=IsWindowEnabled(root_)!=FALSE;
    const auto capture=GetCapture();
    if(capture&&(capture==root_||IsChild(root_,capture)))throw std::runtime_error("Finish the active plugin drag before restoring its baseline");
    const auto focus=GetFocus();if(focus&&(focus==root_||IsChild(root_,focus)))focus_=focus;
    try {
      attach(root_);Enumeration state{*this,{}};EnumChildWindows(root_,child,reinterpret_cast<LPARAM>(&state));
      if(state.error)std::rethrow_exception(state.error);
      EnableWindow(root_,FALSE);
    }catch(...){release();throw;}
  }
  EditorInputBarrier(const EditorInputBarrier &)=delete;
  EditorInputBarrier &operator=(const EditorInputBarrier &)=delete;
  ~EditorInputBarrier(){release();}
};
}
