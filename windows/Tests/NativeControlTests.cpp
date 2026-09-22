#include "../App/NativeControls.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
LRESULT CALLBACK parentProc(HWND h,UINT m,WPARAM w,LPARAM l){
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
// Never switch the user's input desktop. Even a manual CTest run stays isolated.
struct Desktop {
  HDESK original=GetThreadDesktop(GetCurrentThreadId()),owned{};
  HWND foreground=GetForegroundWindow();DWORD clipboard=GetClipboardSequenceNumber();
  Desktop(){const auto name=L"ScreamSeqControlTest-"+std::to_wstring(GetCurrentProcessId());owned=CreateDesktopW(name.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);require(owned&&SetThreadDesktop(owned),"Create private control desktop");}
  ~Desktop(){SetThreadDesktop(original);CloseDesktop(owned);}
  void check(){require(SetThreadDesktop(original),"Restore thread desktop");require(GetClipboardSequenceNumber()==clipboard,"Clipboard changed");require(GetForegroundWindow()==foreground,"Input foreground changed");}
};
}
int main(int argc,char **argv){
  try {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Desktop desktop;
    WNDCLASSW klass{};klass.lpfnWndProc=parentProc;klass.hInstance=GetModuleHandleW(nullptr);klass.lpszClassName=L"ScreamSeq.ControlTest";RegisterClassW(&klass);
    auto parent=CreateWindowW(klass.lpszClassName,L"Owned control tests",WS_OVERLAPPEDWINDOW,0,0,700,420,nullptr,nullptr,klass.hInstance,nullptr);require(parent,"Create parent");
    auto combo=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_VSCROLL,0,0,1,1,parent,reinterpret_cast<HMENU>(100),klass.hInstance,nullptr);require(combo,"Create combo");
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
    DestroyWindow(parent);DeleteObject(font);desktop.check();
    std::cout<<"PASS retained layout, native selection/popup, flat normal/focused/disabled/empty surfaces, 200 paints without GDI leaks; DPI "<<dpi<<"; high contrast "<<contrast<<"\n";
    return 0;
  }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
