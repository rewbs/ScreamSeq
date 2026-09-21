#pragma once
#include "RenderSurface.hpp"
#include "AutomationCanvas.hpp"
#include <commctrl.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <functional>
#include <memory>

namespace ScreamSeq {
// Modeless native editor shell. Repaint is requested by edits and window events;
// a hidden or unchanged tool has no running presentation timer.
class NativeToolWindow {
protected:
  HWND owner_{},window_{};
  std::map<int,HWND> controls_;
  std::unique_ptr<RenderSurface> surface_;
  HFONT font_{};bool ready_=false;
  int minimumWidth_=900,minimumHeight_=620;
  std::wstring status_;
  HWND handledCharacterWindow_{};WPARAM handledCharacter_{};
  void handledKey(HWND control,WPARAM key,bool ctrl){handledCharacterWindow_=control;handledCharacter_=0;if(key==VK_RETURN||key==VK_TAB||key==VK_SPACE||key==VK_ESCAPE)handledCharacter_=key;else if(ctrl&&key>='A'&&key<='Z')handledCharacter_=key-'A'+1;}
  static std::wstring wide(const std::string &s){int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);std::wstring out(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),out.data(),n);return out;}
  static std::string utf8(const std::wstring &s){int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);if(!n&&!s.empty())throw std::runtime_error("Invalid text");std::string out(n,0);WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),int(s.size()),out.data(),n,nullptr,nullptr);return out;}
  HWND add(int id,const wchar_t *kind,const wchar_t *text,DWORD style){auto h=CreateWindowExW(_wcsicmp(kind,L"EDIT")==0?WS_EX_CLIENTEDGE:0,kind,text,WS_CHILD|(_wcsicmp(kind,L"STATIC")?WS_TABSTOP:0)|style,0,0,1,1,window_,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);if(!h)throw std::runtime_error("Cannot create editor control");controls_[id]=h;SetWindowSubclass(h,childProc,1,reinterpret_cast<DWORD_PTR>(this));return h;}
  void button(int id,const wchar_t *text){add(id,L"BUTTON",text,BS_OWNERDRAW);}
  void edit(int id,const wchar_t *text,int limit){auto h=add(id,L"EDIT",text,ES_AUTOHSCROLL);SendMessageW(h,EM_SETLIMITTEXT,limit,0);}
  void combo(int id){add(id,L"COMBOBOX",L"",CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_VSCROLL);}
  void label(int id,const wchar_t *text){add(id,L"STATIC",text,SS_LEFT);}
  void set(int id,const std::wstring &s){SetWindowTextW(controls_.at(id),s.c_str());}
  void set(int id,const wchar_t *s){set(id,std::wstring(s));}
  void set(int id,const Api::Json &v){set(id,wide(v.is_string()?v.get<std::string>():v.dump()));}
  std::wstring field(int id)const{auto h=controls_.at(id);std::wstring s(size_t(GetWindowTextLengthW(h))+1,0);GetWindowTextW(h,s.data(),int(s.size()));s.resize(wcslen(s.c_str()));return s;}
  double number(int id)const{auto s=field(id);size_t end=0;auto value=std::stod(s,&end);if(end!=s.size()||!std::isfinite(value))throw std::runtime_error("Enter a finite number");return value;}
  void place(int id,float x,float y,float w,float h,bool show=true){auto control=controls_.at(id);if(show){const auto scale=GetDpiForWindow(window_)/96.0f;SetWindowPos(control,nullptr,int(x*scale),int(y*scale),std::max(1,int(w*scale)),std::max(1,int(h*scale)),SWP_NOZORDER|SWP_NOACTIVATE);ShowWindow(control,SW_SHOWNOACTIVATE);}else ShowWindow(control,SW_HIDE);}
  std::pair<float,float> size()const{RECT r{};GetClientRect(window_,&r);const auto scale=96.0f/GetDpiForWindow(window_);return {r.right*scale,r.bottom*scale};}
  void requestPaint(){if(window_&&IsWindowVisible(window_))InvalidateRect(window_,nullptr,FALSE);}
  void layoutAll(){if(!ready_)return;auto scale=GetDpiForWindow(window_)/96.0f;auto font=CreateFontW(-int(13*scale),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");for(auto [id,h]:controls_)SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);if(font_)DeleteObject(font_);font_=font;fontsChanged();layout();requestPaint();}
  void render(){if(!ready_||!IsWindowVisible(window_)||IsIconic(window_))return;if(WaitForSingleObject(surface_->ready(),0)!=WAIT_OBJECT_0){SetTimer(window_,2,16,nullptr);return;}surface_->begin();paint(*surface_);surface_->finishDrawing();check(surface_->present(),"Present editor tool");}
  virtual void layout()=0;
  virtual void fontsChanged(){}
  virtual void paint(RenderSurface &)=0;
  virtual void action(int,unsigned)=0;
  virtual bool key(WPARAM,bool,bool){return false;}
  virtual void mouse(UINT,float,float,WPARAM){}
  virtual void timer(UINT_PTR){}
  virtual void error(const std::exception &e){status_=wide(e.what());requestPaint();}
  virtual void drawControl(const DRAWITEMSTRUCT &d){RECT r=d.rcItem;const bool disabled=(d.itemState&ODS_DISABLED)!=0;SetDCBrushColor(d.hDC,RGB(35,49,63));FillRect(d.hDC,&r,reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH)));SetBkMode(d.hDC,TRANSPARENT);SetTextColor(d.hDC,disabled?RGB(103,119,133):RGB(218,232,241));SelectObject(d.hDC,font_);std::wstring text;
    if(d.CtlType==ODT_COMBOBOX){if(d.itemID!=UINT(-1)){auto length=SendMessageW(d.hwndItem,CB_GETLBTEXTLEN,d.itemID,0);if(length>=0){text.resize(size_t(length)+1);SendMessageW(d.hwndItem,CB_GETLBTEXT,d.itemID,reinterpret_cast<LPARAM>(text.data()));text.resize(size_t(length));}}}else {text.resize(size_t(GetWindowTextLengthW(d.hwndItem))+1);GetWindowTextW(d.hwndItem,text.data(),int(text.size()));text.resize(wcslen(text.c_str()));}
    r.left+=7;r.right-=5;DrawTextW(d.hDC,text.c_str(),int(text.size()),&r,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|(d.CtlType==ODT_BUTTON?DT_CENTER:DT_LEFT));if(d.itemState&ODS_FOCUS){r=d.rcItem;InflateRect(&r,-3,-3);DrawFocusRect(d.hDC,&r);}}
  static LRESULT CALLBACK childProc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR context){
    auto &self=*reinterpret_cast<NativeToolWindow *>(context);
    // TranslateMessage may have queued a character before keyDown consumed an
    // editor command. Do not insert that Enter/Tab/Space into the text as well.
    if(m==WM_CHAR&&self.handledCharacterWindow_==h){const auto expected=self.handledCharacter_;self.handledCharacter_=0;self.handledCharacterWindow_=nullptr;if(expected&&(w==expected||(expected==VK_RETURN&&w=='\n')))return 0;}
    if(m==WM_KEYDOWN||m==WM_SYSKEYDOWN)try{
      self.handledCharacter_=0;self.handledCharacterWindow_=nullptr;
      const bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0,shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
      bool handled=self.key(w,ctrl,shift);
      if(!handled&&w==VK_TAB){auto next=GetNextDlgTabItem(self.window_,h,shift);if(next)SetFocus(next);handled=true;}
      if(handled){self.handledKey(h,w,ctrl);return 0;}
    }catch(const std::exception &e){self.handledKey(h,w,(GetKeyState(VK_CONTROL)&0x8000)!=0);self.error(e);return 0;}
    return DefSubclassProc(h,m,w,l);
  }
  static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){auto self=reinterpret_cast<NativeToolWindow *>(GetWindowLongPtrW(h,GWLP_USERDATA));if(m==WM_NCCREATE){self=static_cast<NativeToolWindow *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);self->window_=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}if(!self)return DefWindowProcW(h,m,w,l);
    try{switch(m){
      case WM_CLOSE:self->hide();return 0;
      case WM_NCDESTROY:self->window_=nullptr;self->ready_=false;break;
      case WM_SIZE:self->layoutAll();return 0;
      case WM_DPICHANGED:{auto r=reinterpret_cast<RECT *>(l);SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);self->layoutAll();return 0;}
      case WM_GETMINMAXINFO:{const auto scale=GetDpiForWindow(h)/96.0f;reinterpret_cast<MINMAXINFO *>(l)->ptMinTrackSize={LONG(self->minimumWidth_*scale),LONG(self->minimumHeight_*scale)};return 0;}
      case WM_ERASEBKGND:return 1;
      case WM_PAINT:{PAINTSTRUCT p{};BeginPaint(h,&p);EndPaint(h,&p);self->render();return 0;}
      case WM_TIMER:if(w==2){KillTimer(h,2);self->requestPaint();}else self->timer(w);return 0;
      case WM_COMMAND:if(self->ready_){const auto notification=HIWORD(w);if(notification==EN_UPDATE||notification==EN_SETFOCUS||notification==EN_KILLFOCUS||notification==EN_HSCROLL||notification==EN_VSCROLL)return 0;self->action(LOWORD(w),notification);self->layout();self->requestPaint();}return 0;
      case WM_DRAWITEM:self->drawControl(*reinterpret_cast<DRAWITEMSTRUCT *>(l));return TRUE;
      case WM_MEASUREITEM:reinterpret_cast<MEASUREITEMSTRUCT *>(l)->itemHeight=unsigned(22*GetDpiForWindow(h)/96);return TRUE;
      case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:SetTextColor(reinterpret_cast<HDC>(w),RGB(218,232,241));SetBkColor(reinterpret_cast<HDC>(w),RGB(24,34,45));SetDCBrushColor(reinterpret_cast<HDC>(w),RGB(24,34,45));return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
      case WM_KEYDOWN:case WM_SYSKEYDOWN:if(self->key(w,(GetKeyState(VK_CONTROL)&0x8000)!=0,(GetKeyState(VK_SHIFT)&0x8000)!=0))return 0;break;
      case WM_LBUTTONDOWN:case WM_LBUTTONUP:case WM_MOUSEMOVE:case WM_CAPTURECHANGED:{const float scale=96.0f/GetDpiForWindow(h);self->mouse(m,GET_X_LPARAM(l)*scale,GET_Y_LPARAM(l)*scale,w);self->requestPaint();return 0;}
    }}catch(const std::exception &e){self->error(e);}return DefWindowProcW(h,m,w,l);
  }
  explicit NativeToolWindow(HWND owner):owner_(owner){}
  void create(const wchar_t *className,const wchar_t *title,int width=960,int height=680){WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=className;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);RECT owner{};GetWindowRect(owner_,&owner);const auto scale=GetDpiForWindow(owner_)/96.0f;window_=CreateWindowExW(WS_EX_TOOLWINDOW,className,title,WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,owner.left+int(35*scale),owner.top+int(35*scale),int(width*scale),int(height*scale),owner_,nullptr,wc.hInstance,this);if(!window_)throw std::runtime_error("Cannot create editor window");const BOOL dark=TRUE;DwmSetWindowAttribute(window_,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));surface_=std::make_unique<RenderSurface>(window_);}
  void finish(){ready_=true;layoutAll();}
public:
  virtual ~NativeToolWindow(){ready_=false;if(window_)DestroyWindow(window_);if(font_)DeleteObject(font_);}
  bool visible()const{return window_&&IsWindowVisible(window_);}
  HWND window()const{return window_;}
  void show(){ShowWindow(window_,IsIconic(window_)?SW_RESTORE:SW_SHOW);SetWindowPos(window_,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);requestPaint();}
  void hide(){if(window_){KillTimer(window_,2);ShowWindow(window_,SW_HIDE);}SetFocus(owner_);}
};
}
