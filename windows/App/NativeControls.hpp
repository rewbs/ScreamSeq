#pragma once
#include "NativeInputGate.hpp"
#include <windows.h>
#include <commctrl.h>
#include <algorithm>
#include <array>
#include <cwchar>
#include <string>
#include <string_view>

namespace ScreamSeq::NativeControls {
// Presentation belongs to the UI thread. Keep requested geometry: a combo's
// actual window height is its closed height, not the requested dropdown height.
struct State {
  std::array<int,4> bounds{};
  bool placed=false,active=false,combo=false,edit=false,button=false,hot=false,inspect=false,contrast=false;
};
inline constexpr UINT_PTR subclassID=0x534351;
inline constexpr wchar_t inspectionProperty[]=L"ScreamSeq.ControlInspection";
inline constexpr wchar_t appearanceOwnerProperty[]=L"ScreamSeq.NativeToolWindow";
inline LRESULT CALLBACK procedure(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
inline State *state(HWND h){DWORD_PTR data=0;return GetWindowSubclass(h,procedure,subclassID,&data)?reinterpret_cast<State *>(data):nullptr;}
inline void count(HWND h,const wchar_t *name){if(GetPropW(h,inspectionProperty))SetPropW(h,name,reinterpret_cast<HANDLE>(reinterpret_cast<UINT_PTR>(GetPropW(h,name))+1));}
inline void recordDraw(HWND h){count(h,L"ScreamSeq.DrawCount");}
inline bool highContrast(){HIGHCONTRASTW value{sizeof(value)};return SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(value),&value,0)&&(value.dwFlags&HCF_HIGHCONTRASTON);}
inline bool themeMessage(UINT message){return message==WM_SETTINGCHANGE||message==WM_THEMECHANGED||message==WM_SYSCOLORCHANGE;}
enum class Surface {main,inspector};
struct ControlColors {COLORREF background,text,disabled,button,pressed,hot,active,activeText;};
// COLORREF is BGR; Direct2D's packed color argument is RGB.
inline constexpr UINT32 renderColor(COLORREF value) {
  return (UINT32(GetRValue(value))<<16)|(UINT32(GetGValue(value))<<8)|UINT32(GetBValue(value));
}
static_assert(renderColor(RGB(0x18,0x22,0x2d))==0x18222d);
inline ControlColors colors(Surface surface,bool contrast) {
  if(contrast)return {GetSysColor(COLOR_WINDOW),GetSysColor(COLOR_WINDOWTEXT),GetSysColor(COLOR_GRAYTEXT),
    GetSysColor(COLOR_BTNFACE),GetSysColor(COLOR_BTNFACE),GetSysColor(COLOR_BTNFACE),
    GetSysColor(COLOR_HIGHLIGHT),GetSysColor(COLOR_HIGHLIGHTTEXT)};
  return {surface==Surface::main?RGB(22,31,41):RGB(24,34,45),
    surface==Surface::main?RGB(212,224,235):RGB(218,232,241),RGB(111,126,140),
    surface==Surface::main?RGB(42,51,63):RGB(35,49,63),RGB(66,80,94),RGB(51,65,79),
    RGB(46,67,77),RGB(125,228,206)};
}
inline LRESULT controlColor(HDC dc,HWND control,Surface surface,bool contrast=highContrast()) {
  const auto palette=colors(surface,contrast);
  SetTextColor(dc,IsWindowEnabled(control)?palette.text:palette.disabled);
  SetBkColor(dc,palette.background);SetDCBrushColor(dc,palette.background);
  return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
}
// Broadcasts reach top-level windows, not every retained child. Forward only
// appearance notifications to owned descendants; never reload data or focus.
inline void refreshTheme(HWND owner,UINT message,WPARAM w,LPARAM l) {
  static thread_local bool forwarding=false;
  if(forwarding)return;
  forwarding=true;struct Guard{bool &flag;~Guard(){flag=false;}} guard{forwarding};
  struct Change{UINT message;WPARAM w;LPARAM l;} change{message,w,l};
  EnumChildWindows(owner,[](HWND child,LPARAM context)->BOOL{
    DWORD process=0;
    if(GetWindowThreadProcessId(child,&process)!=GetCurrentThreadId()||process!=GetCurrentProcessId())return TRUE;
    if(!state(child)&&!GetPropW(child,appearanceOwnerProperty))return TRUE;
    const auto &change=*reinterpret_cast<const Change *>(context);
    SendMessageW(child,change.message,change.w,change.l);return TRUE;
  },reinterpret_cast<LPARAM>(&change));
  RedrawWindow(owner,nullptr,nullptr,RDW_INVALIDATE|RDW_FRAME|RDW_ALLCHILDREN);
}
inline std::wstring itemText(HWND h,UINT item){
  if(item==UINT(-1))return {};
  auto length=SendMessageW(h,CB_GETLBTEXTLEN,item,0);if(length<0)return {};
  std::wstring text(size_t(length)+1,0);SendMessageW(h,CB_GETLBTEXT,item,reinterpret_cast<LPARAM>(text.data()));text.resize(size_t(length));return text;
}
inline void fill(HDC dc,const RECT &r,COLORREF color){SetDCBrushColor(dc,color);FillRect(dc,&r,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));}
struct SavedDC {
  HDC dc;int saved;
  explicit SavedDC(HDC value):dc(value),saved(SaveDC(value)){}
  ~SavedDC(){if(saved)RestoreDC(dc,saved);}
  SavedDC(const SavedDC &)=delete;SavedDC &operator=(const SavedDC &)=delete;
};
struct ListColors {COLORREF background,text,detail;};
inline ListColors listColors(bool selected,bool disabled,bool contrast=highContrast()) {
  if(contrast){const auto foreground=GetSysColor(disabled?COLOR_GRAYTEXT:selected?COLOR_HIGHLIGHTTEXT:COLOR_WINDOWTEXT);
    return {GetSysColor(selected?COLOR_HIGHLIGHT:COLOR_WINDOW),foreground,foreground};}
  const auto foreground=disabled?RGB(111,126,140):selected?RGB(164,240,221):RGB(218,232,241);
  return {selected?RGB(43,73,80):RGB(24,34,45),foreground,disabled?foreground:selected?RGB(164,240,221):RGB(153,181,196)};
}
inline void buttonItem(const DRAWITEMSTRUCT &d,Surface surface,bool active=false,bool contrast=highContrast()) {
  const SavedDC saved(d.hDC);if(!saved.saved)return;const auto palette=colors(surface,contrast);
  const bool disabled=(d.itemState&ODS_DISABLED)!=0,pressed=!disabled&&(d.itemState&ODS_SELECTED)!=0;
  const auto retained=state(d.hwndItem);const bool hot=!disabled&&retained&&retained->hot;
  const bool highlighted=(active||(retained&&retained->active))&&!disabled;
  fill(d.hDC,d.rcItem,highlighted?palette.active:pressed?palette.pressed:hot?palette.hot:palette.button);
  auto rect=d.rcItem;
  if(contrast) {
    // Use native system button edges and colors. A pressed edge remains visible
    // even when the user's button-face and highlight colors happen to match.
    DrawEdge(d.hDC,&rect,pressed?EDGE_SUNKEN:EDGE_RAISED,BF_RECT);
  }
  SetBkMode(d.hDC,TRANSPARENT);
  SetTextColor(d.hDC,disabled?palette.disabled:highlighted?palette.activeText:contrast?GetSysColor(COLOR_BTNTEXT):palette.text);
  if(const auto font=reinterpret_cast<HFONT>(SendMessageW(d.hwndItem,WM_GETFONT,0,0)))SelectObject(d.hDC,font);
  const int inset=std::max(1,MulDiv(6,GetDpiForWindow(d.hwndItem),96));InflateRect(&rect,-inset,-1);
  if(pressed)OffsetRect(&rect,1,1);
  std::wstring text(size_t(GetWindowTextLengthW(d.hwndItem))+1,0);GetWindowTextW(d.hwndItem,text.data(),int(text.size()));text.resize(wcslen(text.c_str()));
  DrawTextW(d.hDC,text.data(),int(text.size()),&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
  if((d.itemState&ODS_FOCUS)&&!(d.itemState&ODS_NOFOCUSRECT)) {
    rect=d.rcItem;const int focusInset=std::max(2,MulDiv(3,GetDpiForWindow(d.hwndItem),96));InflateRect(&rect,-focusInset,-focusInset);DrawFocusRect(d.hDC,&rect);
  }
}
inline void listItem(const DRAWITEMSTRUCT &d,std::wstring_view text,bool contrast=highContrast()) {
  const SavedDC saved(d.hDC);if(!saved.saved)return;
  const auto palette=listColors((d.itemState&ODS_SELECTED)!=0,(d.itemState&ODS_DISABLED)!=0,contrast);
  fill(d.hDC,d.rcItem,palette.background);SetBkMode(d.hDC,TRANSPARENT);SetTextColor(d.hDC,palette.text);
  if(const auto font=reinterpret_cast<HFONT>(SendMessageW(d.hwndItem,WM_GETFONT,0,0)))SelectObject(d.hDC,font);
  auto rect=d.rcItem;const int inset=std::max(1,MulDiv(7,GetDpiForWindow(d.hwndItem),96));rect.left+=inset;rect.right-=inset;
  DrawTextW(d.hDC,text.data(),int(text.size()),&rect,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
  if((d.itemState&ODS_FOCUS)&&!(d.itemState&ODS_NOFOCUSRECT)){InflateRect(&rect,-1,-1);DrawFocusRect(d.hDC,&rect);}
}
inline void comboItem(const DRAWITEMSTRUCT &d){
  const int saved=SaveDC(d.hDC);
  const bool contrast=highContrast(),disabled=(d.itemState&ODS_DISABLED)!=0;
  // The closed field keeps its surface color; only popup rows are highlighted.
  const bool selected=(d.itemState&ODS_SELECTED)&&!(d.itemState&ODS_COMBOBOXEDIT);
  fill(d.hDC,d.rcItem,contrast?GetSysColor(selected?COLOR_HIGHLIGHT:COLOR_WINDOW):selected?RGB(43,73,80):RGB(22,31,41));
  SetBkMode(d.hDC,TRANSPARENT);
  SetTextColor(d.hDC,contrast?GetSysColor(disabled?COLOR_GRAYTEXT:selected?COLOR_HIGHLIGHTTEXT:COLOR_WINDOWTEXT):disabled?RGB(111,126,140):selected?RGB(164,240,221):RGB(212,224,235));
  auto font=reinterpret_cast<HFONT>(SendMessageW(d.hwndItem,WM_GETFONT,0,0));if(font)SelectObject(d.hDC,font);
  auto r=d.rcItem;const int inset=std::max(1,MulDiv(7,GetDpiForWindow(d.hwndItem),96));r.left+=inset;r.right-=inset;
  const auto text=itemText(d.hwndItem,d.itemID);DrawTextW(d.hDC,text.c_str(),int(text.size()),&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
  if((d.itemState&ODS_FOCUS)&&!(d.itemState&ODS_NOFOCUSRECT)&&selected){InflateRect(&r,-1,-1);DrawFocusRect(d.hDC,&r);}
  RestoreDC(d.hDC,saved);
}
inline void paintCombo(HWND h,HDC dc){
  const int saved=SaveDC(dc);RECT r{};GetClientRect(h,&r);
  const int dpi=GetDpiForWindow(h),line=std::max(1,MulDiv(1,dpi,96));
  const bool enabled=IsWindowEnabled(h),focused=GetFocus()==h;
  const bool dropped=SendMessageW(h,CB_GETDROPPEDSTATE,0,0)!=0;
  const auto background=RGB(22,31,41),border=enabled&&(focused||dropped)?RGB(104,193,178):RGB(53,68,82);
  fill(dc,r,border);RECT inside=r;InflateRect(&inside,-line,-line);fill(dc,inside,background);
  COMBOBOXINFO info{sizeof(info)};GetComboBoxInfo(h,&info);
  const int arrowWidth=std::max(MulDiv(24,dpi,96),int(info.rcButton.right-info.rcButton.left));
  RECT label=inside;label.right=std::max(label.left,r.right-arrowWidth);
  DRAWITEMSTRUCT item{};item.CtlType=ODT_COMBOBOX;item.hwndItem=h;item.hDC=dc;item.rcItem=label;
  item.itemID=UINT(SendMessageW(h,CB_GETCURSEL,0,0));item.itemState=ODS_COMBOBOXEDIT|(enabled?0:ODS_DISABLED);comboItem(item);
  // A single flat chevron, without the stock raised button or bevel.
  const int x=r.right-arrowWidth/2,y=(r.bottom+r.top)/2,half=std::max(2,MulDiv(4,dpi,96));
  auto pen=CreatePen(PS_SOLID,std::max(1,MulDiv(1,dpi,96)),enabled?RGB(181,204,216):RGB(111,126,140));auto old=SelectObject(dc,pen);
  POINT points[3]{{x-half,y-half/2},{x,y+half/2},{x+half,y-half/2}};Polyline(dc,points,3);SelectObject(dc,old);DeleteObject(pen);
  RestoreDC(dc,saved);
}
inline LRESULT CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data){
  auto *s=reinterpret_cast<State *>(data);
  if(s->inspect){
    if(m==WM_PAINT)count(h,L"ScreamSeq.PaintCount");
    if(m==WM_WINDOWPOSCHANGED)count(h,L"ScreamSeq.LayoutCount");
    if(m==WM_SETTEXT)count(h,L"ScreamSeq.TextCount");
    if(m==CB_SETCURSEL&&s->combo)count(h,L"ScreamSeq.SelectionCount");
    if(m==WM_ENABLE)count(h,L"ScreamSeq.EnableCount");
  }
  if(m==WM_NCDESTROY){
    RemoveWindowSubclass(h,procedure,id);
    for(auto name:{inspectionProperty,L"ScreamSeq.PaintCount",L"ScreamSeq.LayoutCount",L"ScreamSeq.TextCount",L"ScreamSeq.SelectionCount",L"ScreamSeq.EnableCount",L"ScreamSeq.DrawCount"})RemovePropW(h,name);
    delete s;return DefSubclassProc(h,m,w,l);
  }
  // Classic EDIT has no uniform Select All key across Windows versions/styles.
  // Handle its translated Ctrl+A locally; RichEdit keeps its own implementation.
  // The ordinary EM_SETSEL send remains subject to any active departure lease.
  if(s->edit&&m==WM_CHAR&&w==1){SendMessageW(h,EM_SETSEL,0,-1);return 0;}
  if(themeMessage(m)){s->contrast=highContrast();InvalidateRect(h,nullptr,FALSE);}
  if(s->button){
    if(m==WM_MOUSEMOVE&&!s->hot&&IsWindowEnabled(h)){
      TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE,h,0};
      if(TrackMouseEvent(&tracking)){s->hot=true;InvalidateRect(h,nullptr,FALSE);}
    }
    if((m==WM_MOUSELEAVE||m==WM_ENABLE)&&s->hot){s->hot=false;InvalidateRect(h,nullptr,FALSE);}
  }
  if(s->combo&&!s->contrast){
    if(m==WM_ERASEBKGND)return 1;
    if(m==WM_PRINTCLIENT){paintCombo(h,reinterpret_cast<HDC>(w));return 0;}
    if(m==WM_PAINT){
      PAINTSTRUCT paint{};auto dc=BeginPaint(h,&paint);RECT r{};GetClientRect(h,&r);
      auto buffer=CreateCompatibleDC(dc);auto bitmap=CreateCompatibleBitmap(dc,std::max(1L,r.right),std::max(1L,r.bottom));
      if(buffer&&bitmap){auto old=SelectObject(buffer,bitmap);paintCombo(h,buffer);BitBlt(dc,0,0,r.right,r.bottom,buffer,0,0,SRCCOPY);SelectObject(buffer,old);}else paintCombo(h,dc);
      if(bitmap)DeleteObject(bitmap);if(buffer)DeleteDC(buffer);EndPaint(h,&paint);return 0;
    }
    // The native class retains hit testing, popup, type-ahead, keyboard and
    // accessibility. Repaint its surface when those native states change.
    const auto result=DefSubclassProc(h,m,w,l);
    if(m==WM_SETFOCUS||m==WM_KILLFOCUS||m==WM_ENABLE||m==CB_SETCURSEL||m==CB_SHOWDROPDOWN||m==WM_LBUTTONDOWN||m==WM_LBUTTONUP||m==WM_KEYDOWN||m==WM_SYSKEYDOWN||m==WM_THEMECHANGED||m==WM_SYSCOLORCHANGE)InvalidateRect(h,nullptr,FALSE);
    return result;
  }
  return DefSubclassProc(h,m,w,l);
}
inline void install(HWND h,bool inspect=false){
  if(state(h))return;
  wchar_t name[32]{};GetClassNameW(h,name,32);
  auto *s=new State;s->combo=_wcsicmp(name,L"COMBOBOX")==0&&(GetWindowLongPtrW(h,GWL_STYLE)&3)==CBS_DROPDOWNLIST;s->edit=_wcsicmp(name,L"EDIT")==0;s->button=_wcsicmp(name,L"BUTTON")==0;s->inspect=inspect;s->contrast=highContrast();
  if(!SetWindowSubclass(h,procedure,subclassID,reinterpret_cast<DWORD_PTR>(s))){delete s;return;}
  if(inspect)SetPropW(h,inspectionProperty,reinterpret_cast<HANDLE>(1));
}
inline void show(HWND h,bool visible){if(bool(GetWindowLongPtrW(h,GWL_STYLE)&WS_VISIBLE)!=visible)ShowWindow(h,visible?SW_SHOWNOACTIVATE:SW_HIDE);}
inline void place(HWND h,int x,int y,int width,int height,bool visible=true){
  auto *s=state(h);const std::array bounds{x,y,std::max(1,width),std::max(1,height)};
  if(!s||!s->placed||s->bounds!=bounds){
    if(SetWindowPos(h,nullptr,bounds[0],bounds[1],bounds[2],bounds[3],SWP_NOZORDER|SWP_NOACTIVATE)&&s){s->bounds=bounds;s->placed=true;}
  }
  show(h,visible);
}
inline void text(HWND h,std::wstring_view value){
  const int length=GetWindowTextLengthW(h);
  if(size_t(length)==value.size()){
    std::wstring current(size_t(length)+1,0);GetWindowTextW(h,current.data(),length+1);current.resize(size_t(length));if(current==value)return;
  }
  const std::wstring next(value);
  NativeInputGate::present(h,WM_SETTEXT,0,reinterpret_cast<LPARAM>(next.c_str()));
}
inline void select(HWND h,LRESULT index){if(SendMessageW(h,CB_GETCURSEL,0,0)!=index)NativeInputGate::present(h,CB_SETCURSEL,WPARAM(index),0);}
inline void active(HWND h,bool value){auto *s=state(h);if(!s||s->active!=value){if(s)s->active=value;InvalidateRect(h,nullptr,FALSE);}}
}
