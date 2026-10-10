#pragma once
#include "RenderSurface.hpp"
#include "AutomationCanvas.hpp"
#include "NativeControls.hpp"
#include "NativeAccessibility.hpp"
#include "DocumentDraftRegistry.hpp"
#include <commctrl.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <algorithm>
#include <functional>
#include <memory>
#include <system_error>

namespace ScreamSeq {
inline constexpr wchar_t workspaceShortcutProperty[]=L"ScreamSeq.WorkspaceShortcutHandler";
inline constexpr wchar_t documentDraftRegistryProperty[]=L"ScreamSeq.DocumentDraftRegistry";
using WorkspaceShortcutHandler=std::function<bool(WPARAM,bool,bool)>;
// Musical typing follows key positions, not letters, so QWERTZ and AZERTY keep
// the same two piano rows. Returns the US-layout virtual key of the physical
// key that produced `key` in the active layout, or 0 outside the typing rows.
// Text entry and shortcuts keep their layout-dependent virtual keys.
inline WPARAM physicalMusicalKey(WPARAM key){
  if(!((key>='0'&&key<='9')||(key>='A'&&key<='Z')||(key>=VK_OEM_1&&key<=VK_OEM_102)))return 0;
  const UINT scan=MapVirtualKeyExW(UINT(key),MAPVK_VK_TO_VSC_EX,GetKeyboardLayout(0));
  if(!scan)return key<128?key:0; // No translation available: keep the letter.
  if(scan>0xFF)return 0;         // Extended keys are never piano keys.
  static const char digits[]="1234567890",top[]="QWERTYUIOP",home[]="ASDFGHJKL",bottom[]="ZXCVBNM";
  if(scan>=0x02&&scan<=0x0B)return WPARAM(digits[scan-0x02]);
  if(scan>=0x10&&scan<=0x19)return WPARAM(top[scan-0x10]);
  if(scan>=0x1E&&scan<=0x26)return WPARAM(home[scan-0x1E]);
  if(scan>=0x2C&&scan<=0x32)return WPARAM(bottom[scan-0x2C]);
  switch(scan) {
    case 0x0C:return '-';case 0x0D:return '=';case 0x1A:return '[';case 0x1B:return ']';
    case 0x27:return ';';case 0x28:return '\'';case 0x29:return '`';case 0x2B:return '\\';
    case 0x33:return ',';case 0x34:return '.';case 0x35:return '/';case 0x56:return '<';
    default:break;
  }
  return 0;
}
// Modeless native editor shell. Repaint is requested by edits and window events;
// a hidden or unchanged tool has no running presentation timer.
class NativeToolWindow {
  inline static constexpr auto toolProperty_=NativeControls::appearanceOwnerProperty;
  DocumentDraftRegistry::Registration draftRegistration_;
  bool retired_=false;
  std::optional<Tracker::DocumentDraft> parentDraftIdentity_;
  HWND dockParent_{};
  RECT lastFloatingRect_{};
  LONG_PTR floatingStyle_=WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN;
  LONG_PTR floatingExStyle_=WS_EX_TOOLWINDOW;
  bool relocating_=false,notifyingPlacement_=false;
  std::function<bool(WPARAM,bool)> workspaceKeys_;
  std::function<void()> workspaceDockAction_;
  bool workspaceShortcut(WPARAM key,bool repeat,bool prefixOnly) {
    for(auto host=owner_;host;host=GetParent(host))
      if(auto handler=reinterpret_cast<WorkspaceShortcutHandler *>(GetPropW(host,workspaceShortcutProperty)))return (*handler)(key,repeat,prefixOnly);
    return false;
  }
  std::function<void()> placementChanged_,focusPresentationChanged_;
  // Resolve only registered tools on this UI thread. Native GW_OWNER can be
  // Main for a floating child created from a docked editor; owner_ retains the
  // logical constructor owner through later dock/float transitions.
  static NativeToolWindow *presentationTool(HWND target){
    for(unsigned depth=0;target&&depth<64;++depth){
      DWORD process=0;
      if(GetWindowThreadProcessId(target,&process)!=GetCurrentThreadId()||process!=GetCurrentProcessId())return nullptr;
      if(auto *tool=reinterpret_cast<NativeToolWindow *>(GetPropW(target,toolProperty_));
          tool&&tool->window_==target&&tool->ready_)return tool;
      if(!(GetWindowLongPtrW(target,GWL_STYLE)&WS_CHILD))break;
      target=GetParent(target);
    }
    return nullptr;
  }
  void notifyFocusPresentation(){
    if(!ready_)return;
    auto *tool=this;
    for(unsigned depth=0;tool&&depth<64;++depth){
      // The bounded owner walk is presentation-only: no input/placement
      // ownership changes, focus moves, layout, timers or target reads.
      tool->requestPaint();if(tool->focusPresentationChanged_)tool->focusPresentationChanged_();
      auto *owner=presentationTool(tool->owner_);
      if(owner==tool)break;
      tool=owner;
    }
  }
  static void windowLong(HWND window,int index,LONG_PTR value) {
    SetLastError(ERROR_SUCCESS);
    if(!SetWindowLongPtrW(window,index,value) && GetLastError()!=ERROR_SUCCESS)
      throw std::system_error(GetLastError(),std::system_category(),"Change tool window style");
  }
  static void parentWindow(HWND window,HWND parent) {
    SetLastError(ERROR_SUCCESS);
    if(!SetParent(window,parent) && GetLastError()!=ERROR_SUCCESS)
      throw std::system_error(GetLastError(),std::system_category(),"Move tool window");
  }
  bool shown()const{return window_&&(GetWindowLongPtrW(window_,GWL_STYLE)&WS_VISIBLE)!=0;}
  void rememberFloatingBounds(){if(window_&&!dockParent_&&!relocating_&&!IsIconic(window_)&&!IsZoomed(window_))GetWindowRect(window_,&lastFloatingRect_);}
  static RECT reachableFloatingBounds(RECT rect) {
    MONITORINFO monitor{sizeof(monitor)};
    if(!GetMonitorInfoW(MonitorFromRect(&rect,MONITOR_DEFAULTTONEAREST),&monitor))
      throw std::system_error(GetLastError(),std::system_category(),"Find floating tool monitor");
    const auto &work=monitor.rcWork;
    const LONG width=LONG(std::clamp<int64_t>(int64_t(rect.right)-rect.left,1,int64_t(work.right)-work.left));
    const LONG height=LONG(std::clamp<int64_t>(int64_t(rect.bottom)-rect.top,1,int64_t(work.bottom)-work.top));
    rect.left=std::clamp(rect.left,work.left,work.right-width);
    rect.top=std::clamp(rect.top,work.top,work.bottom-height);
    rect.right=rect.left+width;rect.bottom=rect.top+height;return rect;
  }
  void notifyPlacement(){if(!placementChanged_||notifyingPlacement_)return;notifyingPlacement_=true;struct Guard{bool &active;~Guard(){active=false;}}guard{notifyingPlacement_};placementChanged_();}
  void relocate(HWND parent) {
    if(!window_)throw std::runtime_error("Tool window is unavailable");
    if(parent==dockParent_)return;
    if(parent) {
      DWORD process=0;const auto thread=GetWindowThreadProcessId(parent,&process);
      if(!IsWindow(parent)||parent==window_||IsChild(window_,parent)||thread!=GetCurrentThreadId()||process!=GetCurrentProcessId())
        throw std::invalid_argument("Dock tools only inside this application's UI thread");
      if(!AreDpiAwarenessContextsEqual(GetWindowDpiAwarenessContext(window_),GetWindowDpiAwarenessContext(parent)))
        throw std::invalid_argument("Dock host and tool must use the same DPI awareness");
    }
    const auto previousParent=dockParent_;
    const auto previousStyle=GetWindowLongPtrW(window_,GWL_STYLE),previousExStyle=GetWindowLongPtrW(window_,GWL_EXSTYLE);
    RECT previousRect{};GetWindowRect(window_,&previousRect);
    if(previousParent)MapWindowPoints(nullptr,previousParent,reinterpret_cast<POINT *>(&previousRect),2);
    if(!previousParent){rememberFloatingBounds();floatingStyle_=previousStyle&~(WS_VISIBLE|WS_MINIMIZE|WS_MAXIMIZE|WS_CHILD);floatingExStyle_=previousExStyle;}
    const auto floatingRect=parent?RECT{}:reachableFloatingBounds(lastFloatingRect_);
    const bool wasShown=shown();const auto focus=GetFocus();const bool restoreFocus=owns(focus);
    releaseMusicalInput();
    if(owns(GetCapture()))ReleaseCapture();
    relocating_=true;
    struct RelocationGuard{bool &active;~RelocationGuard(){active=false;}}guard{relocating_};
    ShowWindow(window_,SW_HIDE);
    try {
      if(parent) {
        windowLong(window_,GWL_STYLE,(previousStyle&~(WS_OVERLAPPEDWINDOW|WS_POPUP|WS_VISIBLE|WS_MINIMIZE|WS_MAXIMIZE))|WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS);
        windowLong(window_,GWL_EXSTYLE,(previousExStyle&~(WS_EX_APPWINDOW|WS_EX_TOOLWINDOW))|WS_EX_CONTROLPARENT);
        parentWindow(window_,parent);dockParent_=parent;
        if(!SetWindowPos(window_,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED))
          throw std::system_error(GetLastError(),std::system_category(),"Update docked tool frame");
      } else {
        parentWindow(window_,nullptr);
        windowLong(window_,GWL_STYLE,floatingStyle_);
        windowLong(window_,GWL_EXSTYLE,floatingExStyle_);
        windowLong(window_,GWLP_HWNDPARENT,reinterpret_cast<LONG_PTR>(owner_));dockParent_=nullptr;
        if(!SetWindowPos(window_,nullptr,floatingRect.left,floatingRect.top,floatingRect.right-floatingRect.left,floatingRect.bottom-floatingRect.top,SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED))
          throw std::system_error(GetLastError(),std::system_category(),"Restore floating tool bounds");
      }
      // Reparenting does not synchronize focus/accelerator cues automatically.
      const auto state=SendMessageW(parent?parent:owner_,WM_QUERYUISTATE,0,0);
      SendMessageW(window_,WM_UPDATEUISTATE,MAKEWPARAM(UIS_SET,state&(UISF_HIDEFOCUS|UISF_HIDEACCEL)),0);
      SendMessageW(window_,WM_UPDATEUISTATE,MAKEWPARAM(UIS_CLEAR,(~state)&(UISF_HIDEFOCUS|UISF_HIDEACCEL)),0);
      if(wasShown)ShowWindow(window_,SW_SHOWNOACTIVATE);
    } catch(...) {
      const auto failure=std::current_exception();
      // Win32 failures must not leave a retained editor in a half-floated state.
      try {
        if(previousParent){windowLong(window_,GWL_STYLE,(previousStyle&~WS_VISIBLE)|WS_CHILD);parentWindow(window_,previousParent);}
        else {parentWindow(window_,nullptr);windowLong(window_,GWL_STYLE,previousStyle&~WS_VISIBLE);windowLong(window_,GWLP_HWNDPARENT,reinterpret_cast<LONG_PTR>(owner_));}
        windowLong(window_,GWL_EXSTYLE,previousExStyle);dockParent_=previousParent;
        SetWindowPos(window_,nullptr,previousRect.left,previousRect.top,previousRect.right-previousRect.left,previousRect.bottom-previousRect.top,SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
        if(wasShown)ShowWindow(window_,SW_SHOWNOACTIVATE);
      }catch(...){}
      relocating_=false;if(restoreFocus&&wasShown&&IsWindow(focus))SetFocus(focus);try{layoutAll();if(visible())resumeVisiblePresentation();}catch(...){}std::rethrow_exception(failure);
    }
    relocating_=false;
    if(restoreFocus&&wasShown&&IsWindowVisible(focus))SetFocus(focus);
    layoutAll();if(visible())resumeVisiblePresentation();notifyPlacement();
  }
protected:
  static Tracker::DocumentDraft describeDraft(std::string document,std::string revision,std::string target,
      uint64_t generation,bool dirty,bool pending=false,bool uncertain=false) {
    Tracker::DocumentDraft result;result.document=std::move(document);result.revision=std::move(revision);
    result.target=std::move(target);result.generation=generation;result.dirty=dirty;result.pending=pending;result.uncertain=uncertain;return result;
  }
  const std::optional<Tracker::DocumentDraft> &parentDraftIdentity()const noexcept{return parentDraftIdentity_;}
  HWND owner_{},window_{};
  std::map<int,HWND> controls_;
  std::unique_ptr<RenderSurface> surface_;
  HFONT font_{};unsigned fontDpi_=0;bool ready_=false;
  int minimumWidth_=900,minimumHeight_=620;
  // Optional client-area minimums. Nonclient metrics do not scale linearly;
  // compute the actual frame at the HWND's DPI when Windows asks for limits.
  int minimumClientWidth_=0,minimumClientHeight_=0;
  std::wstring status_;
  HWND handledCharacterWindow_{};WPARAM handledCharacter_{};
  std::function<bool(HWND,WPARAM,bool)> musicalKey_;
  std::function<bool(WPARAM)> musicalRelease_;
  std::function<void()> musicalDeactivate_;
  // Opt-in first-open placement for floating forms. Keep the native minimum
  // size and the user's later placement; only move into the owner's work area.
  void clampToOwnerWorkArea() {
    MONITORINFO monitor{sizeof(monitor)};
    if(!GetMonitorInfoW(MonitorFromWindow(owner_,MONITOR_DEFAULTTONEAREST),&monitor))return;
    for(unsigned pass=0;pass<2;++pass){
      RECT bounds{};if(!GetWindowRect(window_,&bounds))return;
      const auto &work=monitor.rcWork;
      const auto x=std::clamp(bounds.left,work.left,std::max(work.left,work.right-(bounds.right-bounds.left)));
      const auto y=std::clamp(bounds.top,work.top,std::max(work.top,work.bottom-(bounds.bottom-bounds.top)));
      if(x==bounds.left&&y==bounds.top)return;
      SetWindowPos(window_,nullptr,x,y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
      // A move between monitors may deliver WM_DPICHANGED and resize the form.
    }
  }
  void releaseMusicalInput(){if(musicalDeactivate_)musicalDeactivate_();deactivate();}
  static WPARAM translatedCharacter(WPARAM key,LPARAM message) {
    BYTE keyboard[256]{};wchar_t characters[8]{};
    if(!GetKeyboardState(keyboard))return 0;
    const auto layout=GetKeyboardLayout(0);
    auto scan=UINT((message>>16)&0xff);if(!scan)scan=MapVirtualKeyExW(UINT(key),MAPVK_VK_TO_VSC,layout);
    // Match TranslateMessage's shifted/OEM character without changing the
    // keyboard's dead-key state (flag 4, supported by our Windows 10 target).
    return ToUnicodeEx(UINT(key),scan,keyboard,characters,8,4,layout)>0?WPARAM(characters[0]):0;
  }
  void handledKey(HWND control,WPARAM character){handledCharacterWindow_=control;handledCharacter_=character;}
  static std::wstring wide(const std::string &s){int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);std::wstring out(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),out.data(),n);return out;}
  static std::string utf8(const std::wstring &s){int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);if(!n&&!s.empty())throw std::runtime_error("Invalid text");std::string out(n,0);WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),int(s.size()),out.data(),n,nullptr,nullptr);return out;}
  HWND add(int id,const wchar_t *kind,const wchar_t *text,DWORD style){auto h=CreateWindowExW(_wcsicmp(kind,L"EDIT")==0?WS_EX_CLIENTEDGE:0,kind,text,WS_CHILD|(_wcsicmp(kind,L"STATIC")?WS_TABSTOP:0)|style,0,0,1,1,window_,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);if(!h)throw std::runtime_error("Cannot create editor control");controls_[id]=h;NativeControls::install(h,GetPropW(owner_,NativeControls::inspectionProperty)!=nullptr);SetWindowSubclass(h,childProc,1,reinterpret_cast<DWORD_PTR>(this));return h;}
  void button(int id,const wchar_t *text){add(id,L"BUTTON",text,BS_OWNERDRAW);}
  void edit(int id,const wchar_t *text,int limit){auto h=add(id,L"EDIT",text,ES_AUTOHSCROLL);SendMessageW(h,EM_SETLIMITTEXT,limit,0);}
  void combo(int id){add(id,L"COMBOBOX",L"",CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_VSCROLL);}
  void label(int id,const wchar_t *text){add(id,L"STATIC",text,SS_LEFT);}
  void accessibleName(int id,const wchar_t *text){check(NativeAccessibility::name(controls_.at(id),text),"Name native editor control");}
  void set(int id,const std::wstring &s){NativeControls::text(controls_.at(id),s);}
  void set(int id,const wchar_t *s){set(id,std::wstring(s));}
  void set(int id,const Api::Json &v){set(id,wide(v.is_string()?v.get<std::string>():v.dump()));}
  std::wstring field(int id)const{auto h=controls_.at(id);std::wstring s(size_t(GetWindowTextLengthW(h))+1,0);GetWindowTextW(h,s.data(),int(s.size()));s.resize(wcslen(s.c_str()));return s;}
  double number(int id)const{auto s=field(id);size_t end=0;auto value=std::stod(s,&end);if(end!=s.size()||!std::isfinite(value))throw std::runtime_error("Enter a finite number");return value;}
  void place(int id,float x,float y,float w,float h,bool show=true){const auto scale=GetDpiForWindow(window_)/96.0f;NativeControls::place(controls_.at(id),int(x*scale),int(y*scale),int(w*scale),int(h*scale),show);}
  std::pair<float,float> size()const{RECT r{};GetClientRect(window_,&r);const auto scale=96.0f/GetDpiForWindow(window_);return {r.right*scale,r.bottom*scale};}
  void requestPaint(){if(window_&&IsWindowVisible(window_))InvalidateRect(window_,nullptr,FALSE);}
  void layoutAll(){if(!ready_||relocating_)return;const auto dpi=GetDpiForWindow(window_);if(fontDpi_!=dpi){auto scale=dpi/96.0f;auto font=CreateFontW(-int(13*scale),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");for(auto [id,h]:controls_)SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);if(font_)DeleteObject(font_);font_=font;fontDpi_=dpi;fontsChanged();}layout();requestPaint();}
  unsigned renderFailures_=0;ULONGLONG renderRetry_=0;
  // A failed or lost frame retries through timer 2 with a back-off. WM_PAINT
  // has already validated the window, so the failure cannot repaint itself
  // into an endless WM_PAINT -> throw -> invalidate loop.
  void renderFailed(){renderFailures_=std::min(renderFailures_+1,20u);const UINT delay=std::min(100u*renderFailures_,2000u);renderRetry_=GetTickCount64()+delay;if(window_)SetTimer(window_,2,delay,nullptr);}
  void render(){
    if(!ready_||!IsWindowVisible(window_)||IsIconic(window_))return;
    if(renderFailures_){const auto now=GetTickCount64();if(now<renderRetry_){SetTimer(window_,2,UINT(renderRetry_-now)+1,nullptr);return;}}
    if(WaitForSingleObject(surface_->ready(),0)!=WAIT_OBJECT_0){SetTimer(window_,2,16,nullptr);return;}
    try{
      surface_->begin();
      // A failed paint must still end the D2D draw and pop its clips.
      try{paint(*surface_);}catch(...){surface_->abandon();throw;}
      if(surface_->finishDrawing())check(surface_->present(),"Present editor tool");
    }catch(...){renderFailed();throw;}
    // Device removed/reset: the surface discarded its resources and recreates
    // them on the next frame.
    if(surface_->lost()){renderFailed();return;}
    renderFailures_=0;
  }
  virtual void layout()=0;
  virtual void fontsChanged(){}
  // UI-thread presentation work may have been deferred while this retained
  // HWND was hidden. Never reload its target or move focus from this hook.
  virtual void resumeVisiblePresentation()noexcept{}
  virtual void paint(RenderSurface &)=0;
  virtual void action(int,unsigned)=0;
  virtual bool key(WPARAM,bool,bool){return false;}
  virtual bool keyUp(WPARAM){return false;}
  virtual bool controlScroll(UINT,WPARAM,HWND){return false;}
  virtual void controlCaptureChanged(HWND){}
  virtual void reviewDocumentDraft(){show();SetFocus(window_);}
  virtual bool contextMenu(HWND,POINT){return false;}
  bool hasWorkspaceDockAction()const{return bool(workspaceDockAction_);}
  void toggleWorkspaceDock(){if(workspaceDockAction_)workspaceDockAction_();}
  virtual void deactivate(){}
  virtual void mouse(UINT,float,float,WPARAM){}
  virtual bool wheel(UINT,float,float,WPARAM){return false;}
  virtual void timer(UINT_PTR){}
  virtual void error(const std::exception &e){status_=wide(e.what());requestPaint();}
  virtual void drawControl(const DRAWITEMSTRUCT &d){NativeControls::recordDraw(d.hwndItem);if(d.CtlType==ODT_COMBOBOX){NativeControls::comboItem(d);return;}if(d.CtlType==ODT_BUTTON)NativeControls::buttonItem(d,NativeControls::Surface::inspector);}
  static LRESULT CALLBACK childProc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR context){
    auto &self=*reinterpret_cast<NativeToolWindow *>(context);
    if(self.retired_&&m!=WM_NCDESTROY&&m!=WM_DESTROY)return 0;
    if(m==WM_CAPTURECHANGED)try{self.controlCaptureChanged(h);}catch(const std::exception &e){self.error(e);}
    if(m==WM_SETFOCUS||m==WM_KILLFOCUS)self.notifyFocusPresentation();
    if(m==WM_KEYUP||m==WM_SYSKEYUP)try{if((self.musicalRelease_&&self.musicalRelease_(w))||self.keyUp(w))return 0;}catch(const std::exception &e){self.error(e);return 0;}
    if(m==WM_KILLFOCUS&&!self.relocating_&&!self.owns(reinterpret_cast<HWND>(w)))self.releaseMusicalInput();
    // TranslateMessage may have queued a character before keyDown consumed an
    // editor command. Do not insert that Enter/Tab/Space into the text as well.
    if((m==WM_CHAR||m==WM_SYSCHAR)&&self.handledCharacterWindow_==h){const auto expected=self.handledCharacter_;self.handledCharacter_=0;self.handledCharacterWindow_=nullptr;if(expected&&(w==expected||(expected==VK_RETURN&&w=='\n')))return 0;}
    if(m==WM_KEYDOWN||m==WM_SYSKEYDOWN){
      // Capture before dispatch: a command may open a dialog, move focus, or
      // release modifiers while its already-translated character is queued.
      const auto character=translatedCharacter(w,l);
      try{
      self.handledCharacter_=0;self.handledCharacterWindow_=nullptr;
      // An open selector owns navigation/Enter/Escape. In particular, Escape
      // must dismiss its popup before an editor shortcut can close the tool.
      auto control=NativeControls::state(h);
      if(control&&control->combo&&SendMessageW(h,CB_GETDROPPEDSTATE,0,0)){
        if(w==VK_TAB)SendMessageW(h,CB_SHOWDROPDOWN,FALSE,0);
        else {if(w==VK_RETURN||w==VK_ESCAPE)self.handledKey(h,character);return DefSubclassProc(h,m,w,l);}
      }
      const bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0,shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
      const bool repeat=(l&(1LL<<30))!=0;
      bool handled=self.workspaceShortcut(w,repeat,true)||(self.musicalKey_&&self.musicalKey_(h,w,repeat))||self.key(w,ctrl,shift);
      if(!handled&&self.workspaceKeys_)handled=self.workspaceKeys_(w,repeat);
      if(!handled)handled=self.workspaceShortcut(w,repeat,false);
      if(!handled&&w==VK_TAB){auto next=GetNextDlgTabItem(self.window_,h,shift);if(next)SetFocus(next);handled=true;}
      if(handled){self.handledKey(h,character);return 0;}
      }catch(const std::exception &e){self.handledKey(h,character);self.error(e);return 0;}
    }
    return DefSubclassProc(h,m,w,l);
  }
  static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){auto self=reinterpret_cast<NativeToolWindow *>(GetWindowLongPtrW(h,GWLP_USERDATA));if(m==WM_NCCREATE){self=static_cast<NativeToolWindow *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);self->window_=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}if(!self)return DefWindowProcW(h,m,w,l);
    // Children may be constructed before this owner's finish(). Publish only
    // the registry context at HWND creation; register this owner's summary
    // after all its controls exist. Otherwise an early child escapes census.
    if(m==WM_NCCREATE)if(auto registry=GetPropW(self->owner_,documentDraftRegistryProperty))
      if(!SetPropW(h,documentDraftRegistryProperty,registry))return FALSE;
    try{
      if(self->retired_&&m!=WM_NCDESTROY&&m!=WM_DESTROY)return DefWindowProcW(h,m,w,l);
      if(m==WM_SETFOCUS||m==WM_KILLFOCUS)self->notifyFocusPresentation();
      switch(m){
      case WM_CLOSE:self->hide();return 0;
      case WM_CONTEXTMENU:self->workspaceShortcut(VK_ESCAPE,false,true);if(self->contextMenu(reinterpret_cast<HWND>(w),POINT{GET_X_LPARAM(l),GET_Y_LPARAM(l)}))return 0;break;
      case WM_ACTIVATE:if(LOWORD(w)==WA_INACTIVE&&!self->relocating_)self->releaseMusicalInput();break;
      case WM_KILLFOCUS:if(!self->relocating_&&!self->owns(reinterpret_cast<HWND>(w)))self->releaseMusicalInput();break;
      case WM_NCDESTROY:self->draftRegistration_.reset();RemovePropW(h,documentDraftRegistryProperty);RemovePropW(h,toolProperty_);self->window_=nullptr;self->ready_=false;self->retired_=true;self->dockParent_=nullptr;break;
      case WM_MOVE:self->rememberFloatingBounds();break;
      case WM_SIZE:self->rememberFloatingBounds();self->layoutAll();return 0;
      case WM_DPICHANGED:{if(!self->docked()){auto r=reinterpret_cast<RECT *>(l);SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);}self->layoutAll();return 0;}
      case WM_DPICHANGED_AFTERPARENT:self->layoutAll();return 0;
      case WM_SETTINGCHANGE:case WM_THEMECHANGED:case WM_SYSCOLORCHANGE:{
        const BOOL dark=!NativeControls::highContrast();DwmSetWindowAttribute(h,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
        NativeControls::refreshTheme(h,m,w,l);self->requestPaint();break;}
      case WM_GETMINMAXINFO:{if(self->docked())break;const auto dpi=GetDpiForWindow(h);const auto scale=dpi/96.0f;auto &minimum=reinterpret_cast<MINMAXINFO *>(l)->ptMinTrackSize;minimum={LONG(self->minimumWidth_*scale),LONG(self->minimumHeight_*scale)};
        if(self->minimumClientWidth_>0&&self->minimumClientHeight_>0){RECT r{0,0,LONG(std::ceil(self->minimumClientWidth_*scale)),LONG(std::ceil(self->minimumClientHeight_*scale))};if(AdjustWindowRectExForDpi(&r,DWORD(GetWindowLongPtrW(h,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(h,GWL_EXSTYLE)),dpi))minimum={r.right-r.left,r.bottom-r.top};}return 0;}
      case WM_ERASEBKGND:return 1;
      case WM_PAINT:{PAINTSTRUCT p{};BeginPaint(h,&p);EndPaint(h,&p);self->render();return 0;}
      case WM_TIMER:if(w==2){KillTimer(h,2);self->requestPaint();}else self->timer(w);return 0;
      case WM_COMMAND:if(self->ready_){const auto notification=HIWORD(w);if(notification==EN_UPDATE||notification==EN_SETFOCUS||notification==EN_KILLFOCUS||notification==EN_HSCROLL||notification==EN_VSCROLL)return 0;self->action(LOWORD(w),notification);self->layout();self->requestPaint();}return 0;
      case WM_HSCROLL:case WM_VSCROLL:if(self->ready_&&self->controlScroll(m,w,reinterpret_cast<HWND>(l)))return 0;break;
      case WM_DRAWITEM:self->drawControl(*reinterpret_cast<DRAWITEMSTRUCT *>(l));return TRUE;
      case WM_MEASUREITEM:reinterpret_cast<MEASUREITEMSTRUCT *>(l)->itemHeight=unsigned(22*GetDpiForWindow(h)/96);return TRUE;
      case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:return NativeControls::controlColor(reinterpret_cast<HDC>(w),reinterpret_cast<HWND>(l),NativeControls::Surface::inspector);
      case WM_KEYDOWN:case WM_SYSKEYDOWN:if(self->workspaceShortcut(w,(l&(1LL<<30))!=0,true)||(self->musicalKey_&&self->musicalKey_(GetFocus(),w,(l&(1LL<<30))!=0))||self->key(w,(GetKeyState(VK_CONTROL)&0x8000)!=0,(GetKeyState(VK_SHIFT)&0x8000)!=0)||(self->workspaceKeys_&&self->workspaceKeys_(w,(l&(1LL<<30))!=0))||self->workspaceShortcut(w,(l&(1LL<<30))!=0,false))return 0;break;
      case WM_KEYUP:case WM_SYSKEYUP:if((self->musicalRelease_&&self->musicalRelease_(w))||self->keyUp(w))return 0;break;
      case WM_MOUSEWHEEL:case WM_MOUSEHWHEEL:{POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(h,&p);const float scale=96.0f/GetDpiForWindow(h);if(self->wheel(m,p.x*scale,p.y*scale,w))return 0;break;}
      case WM_LBUTTONDBLCLK:case WM_LBUTTONDOWN:case WM_LBUTTONUP:case WM_MOUSEMOVE:case WM_CAPTURECHANGED:{const float scale=96.0f/GetDpiForWindow(h);self->mouse(m,GET_X_LPARAM(l)*scale,GET_Y_LPARAM(l)*scale,w);self->requestPaint();return 0;}
    }}catch(const std::exception &e){
      // A failed close must not reach the default handler, which destroys the
      // window while its owner still refers to it.
      try{self->error(e);}catch(...){}
      if(m==WM_CLOSE)return 0;
    }catch(...){if(m==WM_CLOSE)return 0;}
    return DefWindowProcW(h,m,w,l);
  }
  explicit NativeToolWindow(HWND owner):owner_(owner){
    if(auto *parent=presentationTool(owner))parentDraftIdentity_=parent->documentDraft();
  }
  void create(const wchar_t *className,const wchar_t *title,int width=960,int height=680,bool doubleClicks=false){WNDCLASSW wc{};wc.style=doubleClicks?CS_DBLCLKS:0;wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=className;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);RECT owner{};GetWindowRect(owner_,&owner);const auto scale=GetDpiForWindow(owner_)/96.0f;window_=CreateWindowExW(WS_EX_TOOLWINDOW,className,title,WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,owner.left+int(35*scale),owner.top+int(35*scale),int(width*scale),int(height*scale),owner_,nullptr,wc.hInstance,this);if(!window_)throw std::runtime_error("Cannot create editor window");const BOOL dark=!NativeControls::highContrast();DwmSetWindowAttribute(window_,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));surface_=std::make_unique<RenderSurface>(window_);}
  void finish(){
    if(!SetPropW(window_,toolProperty_,reinterpret_cast<HANDLE>(this)))throw std::system_error(GetLastError(),std::system_category(),"Identify native tool window");
    rememberFloatingBounds();ready_=true;layoutAll();
    if(auto *registry=reinterpret_cast<DocumentDraftRegistry *>(GetPropW(owner_,documentDraftRegistryProperty)))trackDocumentDrafts(*registry);
  }
public:
  virtual ~NativeToolWindow(){draftRegistration_.reset();ready_=false;if(window_)DestroyWindow(window_);if(font_)DeleteObject(font_);}
  // Editing owners opt in with their own raw-field semantics. Read-only
  // browsers, references, meters and global preferences have no song draft.
  // Clean identities are also useful to capture a nested editor's context;
  // the registry filters them from the departure census.
  virtual std::optional<Tracker::DocumentDraft> documentDraft()const{return {};}
  void trackDocumentDrafts(DocumentDraftRegistry &registry) {
    if(draftRegistration_.id())throw std::logic_error("Native draft owner already registered");
    std::wstring title(size_t(GetWindowTextLengthW(window_))+1,0);GetWindowTextW(window_,title.data(),int(title.size()));title.resize(wcslen(title.c_str()));
    if(!SetPropW(window_,documentDraftRegistryProperty,reinterpret_cast<HANDLE>(&registry)))throw std::system_error(GetLastError(),std::system_category(),"Register native draft context");
    try{draftRegistration_=registry.add(utf8(title),[this]{return documentDraft();},[this]{reviewDocumentDraft();},[this]()noexcept{
      // Worker adoption has succeeded and the host still holds its input
      // lease. Do not reenter editor actions, placement callbacks or reads.
      // Destroy HWNDs here; the host releases C++ owners after native refresh.
      retired_=true;ready_=false;
      if(window_){EnableWindow(window_,FALSE);ShowWindow(window_,SW_HIDE);return DestroyWindow(window_)!=FALSE||!window_;}
      return true;
    });}
    catch(...){RemovePropW(window_,documentDraftRegistryProperty);throw;}
  }
  bool visible()const{return window_&&IsWindowVisible(window_);}
  bool retired()const noexcept{return retired_;}
  HWND window()const{return window_;}
  bool docked()const{return dockParent_!=nullptr;}
  bool owns(HWND target)const{return window_&&target&&(target==window_||IsChild(window_,target));}
  bool presentationOwns(HWND target)const{
    auto *tool=presentationTool(target);
    for(unsigned depth=0;tool&&depth<64;++depth){
      if(tool==this)return true;
      auto *owner=presentationTool(tool->owner_);if(owner==tool)break;tool=owner;
    }
    return false;
  }
  static bool belongsToTool(HWND target){for(auto current=target;current;current=GetParent(current))if(GetPropW(current,toolProperty_))return true;return false;}
  void dock(HWND parent){if(retired_)throw std::logic_error("Document editor was retired");if(!parent)throw std::invalid_argument("Choose a dock host window");relocate(parent);}
  void floatWindow(){if(retired_)throw std::logic_error("Document editor was retired");relocate(nullptr);}
  void dockBounds(float x,float y,float width,float height){
    if(!docked())throw std::logic_error("Tool is not docked");
    for(const auto value:{x,y,width,height})if(!std::isfinite(value)||std::abs(value)>32768)throw std::invalid_argument("Invalid tool dock bounds");
    if(width<=0||height<=0)throw std::invalid_argument("Dock dimensions must be positive");
    const auto scale=GetDpiForWindow(dockParent_)/96.0f;
    const auto px=int(std::round(x*scale)),py=int(std::round(y*scale)),pw=std::max(1,int(std::round(width*scale))),ph=std::max(1,int(std::round(height*scale)));
    RECT current{};GetWindowRect(window_,&current);MapWindowPoints(nullptr,dockParent_,reinterpret_cast<POINT *>(&current),2);
    if(current.left==px&&current.top==py&&current.right-current.left==pw&&current.bottom-current.top==ph)return;
    if(!SetWindowPos(window_,nullptr,px,py,pw,ph,SWP_NOZORDER|SWP_NOACTIVATE))
      throw std::system_error(GetLastError(),std::system_category(),"Size docked tool");
  }
  void workspaceKeys(std::function<bool(WPARAM,bool)> keys){workspaceKeys_=std::move(keys);}
  void workspaceDockAction(std::function<void()> action){workspaceDockAction_=std::move(action);}
  void placementChanged(std::function<void()> changed){placementChanged_=std::move(changed);}
  // A focus event requests painting only; callers must not move focus or layout.
  void focusPresentationChanged(std::function<void()> changed){focusPresentationChanged_=std::move(changed);}
  void musicalTyping(std::function<bool(HWND,WPARAM,bool)> key,std::function<bool(WPARAM)> release,std::function<void()> deactivate){musicalKey_=std::move(key);musicalRelease_=std::move(release);musicalDeactivate_=std::move(deactivate);}
  void show(){if(retired_||!window_)throw std::logic_error("Document editor was retired");const bool changed=!shown();ShowWindow(window_,IsIconic(window_)?SW_RESTORE:SW_SHOW);SetWindowPos(window_,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|(docked()?SWP_NOACTIVATE:0));requestPaint();if(visible())resumeVisiblePresentation();if(changed)notifyPlacement();}
  virtual void hide(){
    const bool changed=shown(),focused=owns(GetFocus());releaseMusicalInput();
    if(window_){KillTimer(window_,2);ShowWindow(window_,SW_HIDE);}
    // A source-free child can belong to a retained but hidden native tool.
    // Return through that same ownership chain to the nearest usable window.
    // GetParent does not return the owner of a WS_OVERLAPPEDWINDOW tool.
    // Child windows have parents; other native tools have explicit owners.
    if(focused)for(auto owner=owner_;owner&&IsWindow(owner);
        owner=(GetWindowLongPtrW(owner,GWL_STYLE)&WS_CHILD)?GetParent(owner):GetWindow(owner,GW_OWNER))
      if(IsWindowVisible(owner)&&IsWindowEnabled(owner)){SetFocus(owner);break;}
    if(changed)notifyPlacement();
  }
};
}
