#include "../App/NativeControls.hpp"
#include "../App/NativeReportList.hpp"
#include "PrivateGuiTest.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
std::wstring reportCell(size_t row,unsigned column){
  if(column==0)return row==0?L"Alpha / Café / 旋律 / a long native label that must be ellipsized":row==1?L"Beta":L"Row "+std::to_wstring(row);
  if(column==2)return L"64";
  if(column==3)return L"Section / 🎵";
  return {};
}
LRESULT CALLBACK reportProcedure(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR){
  if(m==WM_NCDESTROY)RemoveWindowSubclass(h,reportProcedure,id);
  if(m==WM_NOTIFY){const auto header=reinterpret_cast<NMHDR *>(l);if(header->hwndFrom==ListView_GetHeader(h)&&header->code==NM_CUSTOMDRAW)return ScreamSeq::NativeReportList::headerDraw(*reinterpret_cast<NMCUSTOMDRAW *>(l));}
  return DefSubclassProc(h,m,w,l);
}
LRESULT CALLBACK parentProc(HWND h,UINT m,WPARAM w,LPARAM l){
  if(ScreamSeq::NativeReportList::themeMessage(m))if(const auto list=GetDlgItem(h,101))ScreamSeq::NativeReportList::refresh(list);
  if(m==WM_NOTIFY){const auto header=reinterpret_cast<NMHDR *>(l);if(header->idFrom==101){
    if(header->code==NM_CUSTOMDRAW)return ScreamSeq::NativeReportList::customDraw(*reinterpret_cast<NMLVCUSTOMDRAW *>(l),reportCell);
    if(header->code==LVN_GETDISPINFOW){auto &info=*reinterpret_cast<NMLVDISPINFOW *>(l);if((info.item.mask&LVIF_TEXT)&&info.item.pszText)lstrcpynW(info.item.pszText,reportCell(size_t(info.item.iItem),unsigned(info.item.iSubItem)).c_str(),info.item.cchTextMax);return 0;}
    if(header->code==LVN_ODFINDITEMW){const auto &find=*reinterpret_cast<NMLVFINDITEMW *>(l);return find.lvfi.psz&&_wcsnicmp(find.lvfi.psz,L"Beta",wcslen(find.lvfi.psz))==0?1:-1;}
  }}
  if(m==WM_MEASUREITEM){reinterpret_cast<MEASUREITEMSTRUCT *>(l)->itemHeight=MulDiv(22,GetDpiForWindow(h),96);return TRUE;}
  if(m==WM_DRAWITEM){ScreamSeq::NativeControls::comboItem(*reinterpret_cast<DRAWITEMSTRUCT *>(l));return TRUE;}
  return DefWindowProcW(h,m,w,l);
}
struct Bitmap {
  HDC dc{};HBITMAP bitmap{};HGDIOBJ old{};void *pixels{};int width,height;
  Bitmap(int w,int h):width(w),height(h){
    dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),w,-h,1,32,BI_RGB};
    bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);require(dc&&bitmap,"Create DIB");old=SelectObject(dc,bitmap);
  }
  ~Bitmap(){SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);}
  COLORREF at(int x,int y){GdiFlush();auto *p=static_cast<unsigned char *>(pixels)+(y*width+x)*4;return RGB(p[2],p[1],p[0]);}
  void save(const std::filesystem::path &path){
    GdiFlush();BITMAPFILEHEADER file{0x4d42,DWORD(sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER)+width*height*4),0,0,sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER)};
    BITMAPINFOHEADER info{sizeof(BITMAPINFOHEADER),width,-height,1,32,BI_RGB,DWORD(width*height*4)};
    std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char *>(&file),sizeof(file));out.write(reinterpret_cast<const char *>(&info),sizeof(info));out.write(static_cast<const char *>(pixels),width*height*4);require(bool(out),"Write selector evidence");
  }
};
void reportDrawing(HWND parent,HFONT font){
  using namespace ScreamSeq::NativeReportList;
  INITCOMMONCONTROLSEX common{sizeof(common),ICC_LISTVIEW_CLASSES};require(InitCommonControlsEx(&common),"Initialize native report");
  const auto dpi=GetDpiForWindow(parent);
  auto list=CreateWindowW(WC_LISTVIEWW,L"Report drawing test",WS_CHILD|WS_VISIBLE|WS_TABSTOP|LVS_REPORT|LVS_OWNERDATA|LVS_SINGLESEL|LVS_SHOWSELALWAYS,
    0,80,MulDiv(520,dpi,96),MulDiv(140,dpi,96),parent,reinterpret_cast<HMENU>(101),GetModuleHandleW(nullptr),nullptr);
  require(list,"Create owner-data report");ScreamSeq::Tests::ownGuiWindow(list);
  ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
  install(list);require(SetWindowSubclass(list,reportProcedure,1,0),"Install native header test routing");SendMessageW(list,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
  int column=0;for(const auto label:{L"Order / a deliberately long heading",L"Pattern",L"Rows",L"Section"}){LVCOLUMNW item{};item.mask=LVCF_TEXT|LVCF_WIDTH;item.pszText=const_cast<wchar_t *>(label);item.cx=MulDiv(110,dpi,96);require(ListView_InsertColumn(list,column++,&item)>=0,"Create report column");}
  ListView_SetItemCountEx(list,30,LVSICF_NOSCROLL);ListView_SetItemState(list,0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
  RECT client{},row{};GetClientRect(list,&client);require(ListView_GetItemRect(list,0,&row,LVIR_BOUNDS),"Get native report row");Bitmap image(client.right,client.bottom);
  auto render=[&]{SendMessageW(list,WM_PRINT,reinterpret_cast<WPARAM>(image.dc),PRF_CLIENT|PRF_CHILDREN|PRF_ERASEBKGND);};
  const auto dark=palette(false),system=palette(true);const bool contrast=ScreamSeq::NativeControls::highContrast();
  require(system.background==GetSysColor(COLOR_WINDOW)&&system.text==GetSysColor(COLOR_WINDOWTEXT)&&system.selected==GetSysColor(COLOR_HIGHLIGHT)&&system.selectedText==GetSysColor(COLOR_HIGHLIGHTTEXT)&&system.disabled==GetSysColor(COLOR_GRAYTEXT),"High contrast does not use current system colors");
  SetActiveWindow(parent);SetFocus(list);SendMessageW(list,WM_CHANGEUISTATE,MAKEWPARAM(UIS_CLEAR,UISF_HIDEFOCUS),0);render();
  if(!contrast){
    require(image.at(client.right-8,(row.top+row.bottom)/2)==dark.selected,"Active report row did not use restrained selection");
    RECT empty{};Header_GetItemRect(ListView_GetHeader(list),1,&empty);MapWindowPoints(ListView_GetHeader(list),list,reinterpret_cast<POINT *>(&empty),2);
    for(int x=empty.left+2;x<empty.right-2;++x)require(image.at(x,(row.top+row.bottom)/2)==dark.selected,"Long first-column text escaped into its empty neighbor");
    bool focusVisible=false;for(int x=2;x<client.right-2;++x)focusVisible|=image.at(x,row.top+1)!=dark.selected;require(focusVisible,"Focused native row omitted its focus indicator");
  }
  const auto selected=ListView_GetNextItem(list,-1,LVNI_SELECTED);SetFocus(parent);render();
  if(!contrast){require(image.at(client.right-8,(row.top+row.bottom)/2)==dark.inactive,"Inactive selected row reverted to bright system highlight");for(int x=2;x<client.right-2;++x)require(image.at(x,row.top+1)==dark.inactive,"Inactive native row retained a focus indicator");}
  require(ListView_GetNextItem(list,-1,LVNI_SELECTED)==selected,"Painting or focus transition changed selected identity");
  // An owned top-level message must refresh children; Windows does not promise
  // to broadcast system changes to each list. Do not change global settings.
  for(UINT message:{WM_SETTINGCHANGE,WM_THEMECHANGED,WM_SYSCOLORCHANGE}){
    ListView_SetBkColor(list,RGB(250,0,250));SendMessageW(parent,message,0,0);
    require(ListView_GetBkColor(list)==palette(contrast).background&&ListView_GetNextItem(list,-1,LVNI_SELECTED)==selected&&GetFocus()==parent,"Theme refresh changed selection/focus or did not reach the report");
  }
  SetFocus(list);SendMessageW(list,WM_KEYDOWN,VK_DOWN,0);require(ListView_GetNextItem(list,-1,LVNI_SELECTED)==1,"Native report Down navigation changed");
  SendMessageW(list,WM_KEYDOWN,VK_HOME,0);SendMessageW(list,WM_CHAR,L'B',0);require(ListView_GetNextItem(list,-1,LVNI_SELECTED)==1,"Owner-data type-ahead changed");
  ListView_SetColumnWidth(list,0,MulDiv(300,dpi,96));ListView_Scroll(list,MulDiv(80,dpi,96),0);render();
  require(ListView_GetColumnWidth(list,0)==MulDiv(300,dpi,96)&&ListView_GetNextItem(list,-1,LVNI_SELECTED)==1,"Paint reset horizontal scroll/column geometry");
  RECT shifted{};Header_GetItemRect(ListView_GetHeader(list),0,&shifted);MapWindowPoints(ListView_GetHeader(list),list,reinterpret_cast<POINT *>(&shifted),2);require(shifted.left<0,"Report fixture did not horizontally scroll");
  // Direct callback checks exercise the DC contract without depending on an
  // implementation-specific WM_PRINT device context setup.
  const auto originalText=SetTextColor(image.dc,RGB(1,2,3));const auto originalBackground=SetBkMode(image.dc,OPAQUE);
  NMLVCUSTOMDRAW draw{};draw.nmcd.hdr.hwndFrom=list;draw.nmcd.hdc=image.dc;draw.nmcd.dwDrawStage=CDDS_ITEMPREPAINT;draw.nmcd.dwItemSpec=1;
  customDraw(draw,reportCell);require(GetTextColor(image.dc)==RGB(1,2,3)&&GetBkMode(image.dc)==OPAQUE,"Report drawing leaked GDI state");
  NMCUSTOMDRAW headerDrawInfo{};headerDrawInfo.hdr.hwndFrom=ListView_GetHeader(list);headerDrawInfo.hdc=image.dc;headerDrawInfo.dwDrawStage=CDDS_ITEMPREPAINT;Header_GetItemRect(headerDrawInfo.hdr.hwndFrom,0,&headerDrawInfo.rc);headerDraw(headerDrawInfo);
  require(GetTextColor(image.dc)==RGB(1,2,3)&&GetBkMode(image.dc)==OPAQUE,"Header drawing leaked GDI state");SetTextColor(image.dc,originalText);SetBkMode(image.dc,originalBackground);
  render();const auto resources=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
  for(int i=0;i<200;++i){SetFocus(i%2?list:parent);render();}
  require(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==resources,"Report paints leaked GDI resources");
  require(DestroyWindow(list)&&!IsWindow(list),"Destroy report test window");
  std::cout<<"PASS native report active/inactive/focus pixels, Unicode column clipping, four columns, keyboard/type-ahead, horizontal scroll, system palette, theme refresh and GDI lifetime\n";
}
void privateGuiFailureChecks(){
  auto expectedFailure=[](auto body,const std::string &expected){
    bool rejected=false;
    try{ScreamSeq::Tests::runPrivateGui(L"ScreamSeqGuiFailureCheck",body);}
    catch(const std::exception &error){rejected=true;require(error.what()==expected,"GUI failure self-check hid another cleanup/isolation failure");}
    require(rejected,"Private GUI harness accepted a deliberate fixture failure");
  };
  expectedFailure([]{
    struct Owned{HWND window{};~Owned(){if(window)DestroyWindow(window);}} owner;
    owner.window=CreateWindowExW(0,L"STATIC",L"Throwing fixture",WS_OVERLAPPED,0,0,40,40,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    require(owner.window,"Create throwing fixture window");ScreamSeq::Tests::ownGuiWindow(owner.window);
    throw std::runtime_error("Deliberate fixture failure");
  },"Fixture: Deliberate fixture failure\n");
  HWND leaked{};
  expectedFailure([&]{
    leaked=CreateWindowExW(0,L"STATIC",L"Deliberate owned leak",WS_OVERLAPPED,0,0,40,40,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    require(leaked,"Create deliberate fixture leak");ScreamSeq::Tests::ownGuiWindow(leaked);
  },"Fixture cleanup: Private GUI fixture left owned windows alive before thread exit\n");
  require(!IsWindow(leaked),"Worker exit did not release the deliberately leaked fixture after reporting failure");
  std::cout<<"Private GUI failure guards: throwing body preserved; owned leak rejected before thread exit\n";
}

}
int main(int argc,char **argv){
  try {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ScreamSeq::Tests::runPrivateGui(L"ScreamSeqControlTest",[&]{
    WNDCLASSW klass{};klass.lpfnWndProc=parentProc;klass.hInstance=GetModuleHandleW(nullptr);klass.lpszClassName=L"ScreamSeq.ControlTest";RegisterClassW(&klass);
    auto parent=CreateWindowW(klass.lpszClassName,L"Owned control tests",WS_OVERLAPPEDWINDOW,0,0,700,420,nullptr,nullptr,klass.hInstance,nullptr);require(parent,"Create parent");ScreamSeq::Tests::ownGuiWindow(parent);
    auto combo=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_VSCROLL,0,0,1,1,parent,reinterpret_cast<HMENU>(100),klass.hInstance,nullptr);require(combo,"Create combo");ScreamSeq::Tests::ownGuiWindow(combo);
    using namespace ScreamSeq::NativeControls;
    install(combo,true);require(state(combo),"Install retained selector");
    const auto dpi=GetDpiForWindow(combo);auto font=CreateFontW(-MulDiv(12,dpi,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    SendMessageW(combo,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
    for(auto label:{L"Pattern 00",L"Pattern 01 · Verse",L"Long pattern name / selection remains readable"})SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
    select(combo,1);place(combo,20,20,MulDiv(240,dpi,96),MulDiv(200,dpi,96));
    const auto positions=GetPropW(combo,L"ScreamSeq.LayoutCount"),selections=GetPropW(combo,L"ScreamSeq.SelectionCount");
    for(int i=0;i<100;++i){place(combo,20,20,MulDiv(240,dpi,96),MulDiv(200,dpi,96));select(combo,1);}
    require(GetPropW(combo,L"ScreamSeq.LayoutCount")==positions&&GetPropW(combo,L"ScreamSeq.SelectionCount")==selections,"Unchanged combo layout or selection rewrites");
    RECT r{};GetClientRect(combo,&r);Bitmap image(r.right,r.bottom);
    const bool contrast=highContrast();
    auto render=[&](const char *name,COLORREF border){
      SendMessageW(combo,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(image.dc),PRF_CLIENT);
      if(!contrast){require(image.at(0,0)==border&&image.at(r.right-1,r.bottom-1)==border,"Flat border");require(image.at(r.right-7,4)==RGB(22,31,41),"Flat arrow surface");}
      if(argc>1){std::filesystem::path out=argv[1];std::filesystem::create_directories(out);image.save(out/(std::string(name)+".bmp"));}
    };
    render("normal",RGB(53,68,82));
    ShowWindow(parent,SW_SHOWNOACTIVATE);SetFocus(combo);render("focused",RGB(104,193,178));
    EnableWindow(combo,FALSE);render("disabled",RGB(53,68,82));EnableWindow(combo,TRUE);
    SendMessageW(combo,WM_KEYDOWN,VK_DOWN,0);require(SendMessageW(combo,CB_GETCURSEL,0,0)==2,"Arrow-key selection");
    SendMessageW(combo,WM_KEYDOWN,VK_F4,0);require(SendMessageW(combo,CB_GETDROPPEDSTATE,0,0),"F4 popup");
    SendMessageW(combo,WM_KEYDOWN,VK_ESCAPE,0);require(!SendMessageW(combo,CB_GETDROPPEDSTATE,0,0),"Escape popup");
    SetFocus(parent);select(combo,-1);render("empty",RGB(53,68,82));
    const auto before=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    for(int i=0;i<200;++i){InvalidateRect(combo,nullptr,FALSE);UpdateWindow(combo);}
    require(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==before,"Paint leaks GDI resources");
    reportDrawing(parent,font);
    require(DestroyWindow(parent)&&!IsWindow(parent)&&!IsWindow(combo),"Destroy owned control windows");DeleteObject(font);
    std::cout<<"PASS retained layout, native selection/popup, flat normal/focused/disabled/empty surfaces, 200 paints without GDI leaks; DPI "<<dpi<<"; high contrast "<<contrast<<"\n";
    });privateGuiFailureChecks();return 0;
  }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
