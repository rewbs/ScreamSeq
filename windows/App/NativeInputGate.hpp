#pragma once
#include <windows.h>
#include <commctrl.h>
#include <exception>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace ScreamSeq {
// Short UI-thread lease between final departure admission and native refresh.
// Install on fully constructed roots (including floating owners), after their
// editor subclasses. New trees created by refresh must be protected before any
// message-pumping read or exposure. This is a native input boundary, not a
// replacement for the host's API/worker admission and queued-input guards.
class NativeInputGate final {
  inline static constexpr UINT_PTR subclassID=0x53434947;
  struct Permit {HWND window;UINT message;WPARAM w;LPARAM l;bool consumed=false;};
  DWORD thread_=GetCurrentThreadId();
  std::vector<HWND> windows_;
  Permit *permit_=nullptr;

  static bool presentationMessage(UINT m) noexcept {
    switch(m) {
    case WM_SETTEXT:case EM_SETSEL:case EM_REPLACESEL:case EM_EMPTYUNDOBUFFER:
    case CB_ADDSTRING:case CB_INSERTSTRING:case CB_DELETESTRING:case CB_RESETCONTENT:
    case CB_SETCURSEL:case CB_SETITEMDATA:
    case LB_ADDSTRING:case LB_INSERTSTRING:case LB_DELETESTRING:case LB_RESETCONTENT:
    case LB_SETCURSEL:case LB_SETSEL:case LB_SETITEMDATA:case LB_SETTOPINDEX:
    case BM_SETCHECK:return true;
    default:return false;
    }
  }
  static bool inputMessage(UINT m) noexcept {
    if((m>=WM_KEYFIRST&&m<=WM_KEYLAST)||(m>=WM_MOUSEFIRST&&m<=WM_MOUSELAST)||
        (m>=WM_NCLBUTTONDOWN&&m<=WM_NCXBUTTONDBLCLK)||
        (m>=WM_POINTERUPDATE&&m<=WM_POINTERLEAVE))return true;
    switch(m) {
    case WM_COMMAND:case WM_NOTIFY:case WM_CLOSE:case WM_SYSCOMMAND:
    case WM_CONTEXTMENU:case WM_HSCROLL:case WM_VSCROLL:case WM_TIMER:
    case WM_SETFOCUS:case WM_KILLFOCUS:case WM_NEXTDLGCTL:
    case WM_INPUT:case WM_APPCOMMAND:case WM_DROPFILES:case WM_TOUCH:case WM_GESTURE:
    case WM_IME_STARTCOMPOSITION:case WM_IME_ENDCOMPOSITION:case WM_IME_COMPOSITION:
    case WM_IME_CHAR:case WM_IME_KEYDOWN:case WM_IME_KEYUP:
    case WM_CUT:case WM_PASTE:case WM_CLEAR:case WM_UNDO:return true;
    default:return false;
    }
  }
  static bool controlMutation(HWND h,UINT m) noexcept {
    if(presentationMessage(m))return true;
    wchar_t kind[32]{};GetClassNameW(h,kind,32);
    if(!_wcsicmp(kind,L"EDIT"))switch(m) {
    case EM_UNDO:case EM_SETMODIFY:case EM_LIMITTEXT:case EM_SETREADONLY:
    case EM_SETHANDLE:case EM_FMTLINES:case EM_SETTABSTOPS:case EM_SETWORDBREAKPROC:
    case EM_SETPASSWORDCHAR:case EM_SCROLL:case EM_LINESCROLL:case EM_SCROLLCARET:
    case EM_SETMARGINS:return true;
    }
    if(!_wcsicmp(kind,L"COMBOBOX"))switch(m) {
    case CB_SELECTSTRING:case CB_SHOWDROPDOWN:case CB_DIR:case CB_SETEDITSEL:
    case CB_LIMITTEXT:case CB_SETTOPINDEX:case CB_SETLOCALE:return true;
    }
    if(!_wcsicmp(kind,L"LISTBOX"))switch(m) {
    case LB_SELECTSTRING:case LB_SELITEMRANGE:case LB_SELITEMRANGEEX:
    case LB_SETANCHORINDEX:case LB_SETCARETINDEX:case LB_SETCOUNT:case LB_DIR:
    case LB_ADDFILE:case LB_SETLOCALE:return true;
    }
    if(!_wcsicmp(kind,L"BUTTON"))switch(m) {
    case BM_CLICK:case BM_SETSTATE:case BM_SETSTYLE:return true;
    }
    if(!_wcsicmp(kind,WC_LISTVIEWW))switch(m) {
    case LVM_SETITEMSTATE:case LVM_SETITEMA:case LVM_SETITEMW:
    case LVM_SETITEMTEXTA:case LVM_SETITEMTEXTW:case LVM_INSERTITEMA:case LVM_INSERTITEMW:
    case LVM_DELETEITEM:case LVM_DELETEALLITEMS:case LVM_SETITEMCOUNT:
    case LVM_EDITLABELA:case LVM_EDITLABELW:case LVM_ENSUREVISIBLE:case LVM_SCROLL:
    case LVM_SORTITEMS:case LVM_SORTITEMSEX:case LVM_SETHOTITEM:case LVM_SETSELECTIONMARK:
    case LVM_INSERTCOLUMNA:case LVM_INSERTCOLUMNW:case LVM_SETCOLUMNA:case LVM_SETCOLUMNW:
    case LVM_DELETECOLUMN:case LVM_SETCOLUMNWIDTH:case LVM_SETCOLUMNORDERARRAY:return true;
    }
    if(!_wcsicmp(kind,WC_HEADERW))switch(m) {
    case HDM_SETITEMA:case HDM_SETITEMW:case HDM_INSERTITEMA:case HDM_INSERTITEMW:
    case HDM_DELETEITEM:case HDM_SETORDERARRAY:return true;
    }
    return false;
  }
  static LRESULT refusal(HWND h,UINT m) noexcept {
    wchar_t kind[32]{};GetClassNameW(h,kind,32);
    if(!_wcsicmp(kind,L"COMBOBOX")&&m>=CB_GETEDITSEL&&m<=CB_MSGMAX)return CB_ERR;
    if(!_wcsicmp(kind,L"LISTBOX")&&m>=LB_ADDSTRING&&m<=LB_MSGMAX)return LB_ERR;
    return 0;
  }
  static LRESULT CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data) {
    auto &gate=*reinterpret_cast<NativeInputGate *>(data);
    if(m==WM_NCDESTROY){RemoveWindowSubclass(h,procedure,id);return DefSubclassProc(h,m,w,l);}
    if(inputMessage(m)||controlMutation(h,m)) {
      auto *permit=gate.permit_;
      if(!permit||permit->consumed||permit->window!=h||permit->message!=m||permit->w!=w||permit->l!=l)
        return refusal(h,m);
      // Consume before native processing/notifications: recursive sends, even
      // of this exact message, must never inherit presentation authority.
      permit->consumed=true;
    }
    return DefSubclassProc(h,m,w,l);
  }
  void checkThread(HWND h)const {
    DWORD process=0;
    if(GetCurrentThreadId()!=thread_||GetWindowThreadProcessId(h,&process)!=thread_||process!=GetCurrentProcessId())
      throw std::logic_error("Native input gate requires owned UI-thread windows");
  }
  void attach(HWND h) {
    checkThread(h);DWORD_PTR existing=0;
    if(GetWindowSubclass(h,procedure,subclassID,&existing)) {
      if(existing!=reinterpret_cast<DWORD_PTR>(this))throw std::logic_error("Native window already has an input lease");
      return;
    }
    // Allocate tracking before installing; failure cannot leave a dangling
    // subclass when construction unwinds. SetWindowSubclass does not pump.
    windows_.push_back(h);
    if(!SetWindowSubclass(h,procedure,subclassID,reinterpret_cast<DWORD_PTR>(this)))
      throw std::runtime_error("Cannot protect native input during document replacement");
  }
  struct Enumeration {NativeInputGate &gate;std::exception_ptr error;};
  static BOOL CALLBACK child(HWND h,LPARAM opaque) noexcept {
    auto &state=*reinterpret_cast<Enumeration *>(opaque);
    try {state.gate.attach(h);return TRUE;}catch(...){state.error=std::current_exception();return FALSE;}
  }
  void release() noexcept {
    // The owner must destroy the lease on its UI thread. Never leave a dangling
    // pointer in a subclass after an accidental cross-thread destruction.
    if(GetCurrentThreadId()!=thread_)std::terminate();
    for(auto h:windows_) {
      DWORD_PTR data=0;
      if(GetWindowSubclass(h,procedure,subclassID,&data)&&data==reinterpret_cast<DWORD_PTR>(this))
        RemoveWindowSubclass(h,procedure,subclassID);
    }
    windows_.clear();
  }
public:
  explicit NativeInputGate(std::span<const HWND> roots) {
    try {for(auto root:roots)protect(root);}catch(...){release();throw;}
  }
  NativeInputGate(const NativeInputGate &)=delete;
  NativeInputGate &operator=(const NativeInputGate &)=delete;
  ~NativeInputGate(){release();}
  // Protect an additional fully initialized tree during refresh. Failure keeps
  // existing protection held; the caller must not expose the incomplete tree.
  void protect(HWND root) {
    attach(root);Enumeration state{*this,{}};
    EnumChildWindows(root,child,reinterpret_cast<LPARAM>(&state));
    if(state.error)std::rethrow_exception(state.error);
  }
  static bool protectedWindow(HWND h) noexcept {
    DWORD_PTR data=0;return GetWindowSubclass(h,procedure,subclassID,&data)!=FALSE;
  }
  // Only concrete presentation setters can bypass the gate. No general callback
  // scope, keyboard/click, WM_COMMAND or notification permission is exposed.
  static LRESULT present(HWND h,UINT m,WPARAM w=0,LPARAM l=0) {
    if(!presentationMessage(m))throw std::logic_error("Message is not a native presentation setter");
    DWORD process=0;
    if(GetWindowThreadProcessId(h,&process)!=GetCurrentThreadId()||process!=GetCurrentProcessId())
      throw std::logic_error("Native presentation requires an owned UI-thread window");
    DWORD_PTR data=0;
    if(!GetWindowSubclass(h,procedure,subclassID,&data))return SendMessageW(h,m,w,l);
    auto &gate=*reinterpret_cast<NativeInputGate *>(data);
    Permit permit{h,m,w,l};auto *previous=gate.permit_;gate.permit_=&permit;
    struct Reset {NativeInputGate &gate;Permit *previous;~Reset(){gate.permit_=previous;}}reset{gate,previous};
    const auto result=SendMessageW(h,m,w,l);
    if(!permit.consumed)throw std::runtime_error("Native presentation did not reach its input gate");
    return result;
  }
};
}
