#pragma once
#include "NativeControls.hpp"
#include <stdexcept>

namespace ScreamSeq::NativeReportList {
// Paint the retained native report, not a replacement list. Windows continues
// to own selection, accessibility, type-ahead, scrolling and column geometry.
struct Palette {
  COLORREF background,text,selected,inactive,selectedText,disabled,header,headerText;
};
inline Palette palette(bool contrast){
  if(contrast)return {GetSysColor(COLOR_WINDOW),GetSysColor(COLOR_WINDOWTEXT),
    GetSysColor(COLOR_HIGHLIGHT),GetSysColor(COLOR_BTNFACE),GetSysColor(COLOR_HIGHLIGHTTEXT),
    GetSysColor(COLOR_GRAYTEXT),GetSysColor(COLOR_BTNFACE),GetSysColor(COLOR_BTNTEXT)};
  return {RGB(24,34,45),RGB(218,232,241),RGB(43,73,80),RGB(42,53,67),
    RGB(164,240,221),RGB(111,126,140),RGB(35,49,63),RGB(169,196,207)};
}
struct SavedDC {
  HDC dc;int saved;
  explicit SavedDC(HDC value):dc(value),saved(SaveDC(value)){}
  ~SavedDC(){if(saved)RestoreDC(dc,saved);}
};
inline bool themeMessage(UINT message){return message==WM_SETTINGCHANGE||message==WM_THEMECHANGED||message==WM_SYSCOLORCHANGE;}
inline void refresh(HWND list){
  const auto colors=palette(NativeControls::highContrast());
  if(ListView_GetBkColor(list)!=colors.background)ListView_SetBkColor(list,colors.background);
  if(ListView_GetTextBkColor(list)!=colors.background)ListView_SetTextBkColor(list,colors.background);
  if(ListView_GetTextColor(list)!=colors.text)ListView_SetTextColor(list,colors.text);
  RedrawWindow(list,nullptr,nullptr,RDW_INVALIDATE|RDW_FRAME|RDW_ALLCHILDREN);
}
inline constexpr UINT_PTR subclassID=0x5351524c;
inline LRESULT CALLBACK procedure(HWND h,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR){
  if(message==WM_NCDESTROY)RemoveWindowSubclass(h,procedure,id);
  const auto result=DefSubclassProc(h,message,w,l);
  if(themeMessage(message))refresh(h);
  else if(message==WM_SETFOCUS||message==WM_KILLFOCUS||message==WM_ENABLE||message==WM_UPDATEUISTATE||message==WM_SETFONT)
    InvalidateRect(h,nullptr,FALSE);
  return result;
}
inline void install(HWND list){
  if(!SetWindowSubclass(list,procedure,subclassID,0))throw std::runtime_error("Cannot initialize native report drawing");
  refresh(list);
}
template<typename Cell> LRESULT customDraw(NMLVCUSTOMDRAW &draw,Cell cell){
  // System drawing owns all high-contrast selection/focus conventions.
  if(NativeControls::highContrast())return CDRF_DODEFAULT;
  if(draw.nmcd.dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;
  if(draw.nmcd.dwDrawStage!=CDDS_ITEMPREPAINT)return CDRF_DODEFAULT;
  const auto list=draw.nmcd.hdr.hwndFrom,header=ListView_GetHeader(list);
  const auto row=int(draw.nmcd.dwItemSpec);
  RECT bounds{},client{};
  if(row<0||row>=ListView_GetItemCount(list)||!ListView_GetItemRect(list,row,&bounds,LVIR_BOUNDS)||!GetClientRect(list,&client))return CDRF_DODEFAULT;
  const SavedDC saved(draw.nmcd.hdc);if(!saved.saved)return CDRF_DODEFAULT;
  const auto dc=draw.nmcd.hdc;IntersectClipRect(dc,client.left,client.top,client.right,client.bottom);
  const auto colors=palette(false);const bool enabled=IsWindowEnabled(list)!=FALSE;
  const bool focused=enabled&&GetFocus()==list;
  const auto state=ListView_GetItemState(list,row,LVIS_SELECTED|LVIS_FOCUSED);
  const bool selected=(state&LVIS_SELECTED)!=0;
  const auto background=selected?(focused?colors.selected:colors.inactive):colors.background;
  RECT fill=bounds;fill.left=client.left;fill.right=client.right;
  NativeControls::fill(dc,fill,background);SetBkMode(dc,TRANSPARENT);
  SetTextColor(dc,!enabled?colors.disabled:selected&&focused?colors.selectedText:colors.text);
  if(const auto font=reinterpret_cast<HFONT>(SendMessageW(list,WM_GETFONT,0,0)))SelectObject(dc,font);
  const auto inset=std::max(1,MulDiv(7,GetDpiForWindow(list),96));
  for(int column=0;column<Header_GetItemCount(header);++column){
    RECT part{};if(!Header_GetItemRect(header,column,&part))continue;
    MapWindowPoints(header,list,reinterpret_cast<POINT *>(&part),2);
    part.top=bounds.top;part.bottom=bounds.bottom;
    if(part.right<=client.left||part.left>=client.right||part.right<=part.left)continue;
    const SavedDC columnDC(dc);if(!columnDC.saved)continue;
    IntersectClipRect(dc,part.left,part.top,part.right,part.bottom);
    part.left+=inset;part.right-=inset;if(part.right<=part.left)continue;
    HDITEMW item{};item.mask=HDI_FORMAT;Header_GetItem(header,column,&item);
    const auto alignment=(item.fmt&HDF_JUSTIFYMASK)==HDF_RIGHT?DT_RIGHT:(item.fmt&HDF_JUSTIFYMASK)==HDF_CENTER?DT_CENTER:DT_LEFT;
    const auto text=cell(size_t(row),unsigned(column));
    DrawTextW(dc,text.data(),int(text.size()),&part,alignment|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
  }
  if(focused&&(state&LVIS_FOCUSED)&&!(SendMessageW(list,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS)){
    InflateRect(&fill,-1,-1);DrawFocusRect(dc,&fill);
  }
  return CDRF_SKIPDEFAULT;
}
inline LRESULT headerDraw(NMCUSTOMDRAW &draw){
  if(NativeControls::highContrast())return CDRF_DODEFAULT;
  if(draw.dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW|CDRF_NOTIFYPOSTPAINT;
  if(draw.dwDrawStage==CDDS_POSTPAINT){
    const auto header=draw.hdr.hwndFrom;const SavedDC saved(draw.hdc);if(!saved.saved)return CDRF_DODEFAULT;
    RECT client{};if(!GetClientRect(header,&client))return CDRF_DODEFAULT;
    // The native header paints unused space separately from its items. Preserve
    // every item's actual geometry while coloring the uncovered client area.
    for(int column=0;column<Header_GetItemCount(header);++column){
      RECT item{};if(Header_GetItemRect(header,column,&item))ExcludeClipRect(draw.hdc,item.left,item.top,item.right,item.bottom);
    }
    NativeControls::fill(draw.hdc,client,palette(false).header);return CDRF_DODEFAULT;
  }
  if(draw.dwDrawStage!=CDDS_ITEMPREPAINT)return CDRF_DODEFAULT;
  const auto header=draw.hdr.hwndFrom;const SavedDC saved(draw.hdc);if(!saved.saved)return CDRF_DODEFAULT;
  IntersectClipRect(draw.hdc,draw.rc.left,draw.rc.top,draw.rc.right,draw.rc.bottom);
  const auto colors=palette(false);NativeControls::fill(draw.hdc,draw.rc,colors.header);
  SetBkMode(draw.hdc,TRANSPARENT);SetTextColor(draw.hdc,colors.headerText);
  if(const auto font=reinterpret_cast<HFONT>(SendMessageW(GetParent(header),WM_GETFONT,0,0)))SelectObject(draw.hdc,font);
  wchar_t text[512]{};HDITEMW item{};item.mask=HDI_TEXT|HDI_FORMAT;item.pszText=text;item.cchTextMax=int(std::size(text));
  if(Header_GetItem(header,int(draw.dwItemSpec),&item)){
    auto rect=draw.rc;const auto pad=std::max(1,MulDiv(7,GetDpiForWindow(header),96));rect.left+=pad;rect.right-=pad;
    const auto alignment=(item.fmt&HDF_JUSTIFYMASK)==HDF_RIGHT?DT_RIGHT:(item.fmt&HDF_JUSTIFYMASK)==HDF_CENTER?DT_CENTER:DT_LEFT;
    if(rect.right>rect.left)DrawTextW(draw.hdc,text,-1,&rect,alignment|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
  }
  return CDRF_SKIPDEFAULT;
}
}
