#include "../../App/NativeToolWindow.hpp"
#include "../../App/GraphTrimsWindow.hpp"
#include "../../App/PatternSampleRenderWindow.hpp"
#include "../../App/MultisampleImportWindow.hpp"
#include "../../App/PluginPathWindow.hpp"
#include "../../App/SampleRecordingWindow.hpp"
#include "../PrivateGuiProcessTest.hpp"
#include <iostream>

namespace {
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<typename Action>void rejected(Action action,const char *message){try{action();}catch(const std::exception &){return;}throw std::runtime_error(message);}
struct Window {
  HWND value{};
  Window(const wchar_t *kind,HWND parent=nullptr){value=CreateWindowExW(0,kind,L"Dock test",parent?WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN:WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,0,0,1100,800,parent,nullptr,GetModuleHandleW(nullptr),nullptr);require(value,"Create dock test host");ScreamSeq::Tests::ownGuiWindow(value);}
  ~Window(){if(value)DestroyWindow(value);}
  void close(){const auto window=value;require(DestroyWindow(window),"Destroy dock test host");value=nullptr;require(!IsWindow(window),"Dock test host survived destruction");}
};
class Tool final:public ScreamSeq::NativeToolWindow {
public:
  unsigned localKeys=0,deactivations=0,layouts=0;
  explicit Tool(HWND owner):NativeToolWindow(owner){minimumWidth_=300;minimumHeight_=220;create(L"ScreamSeq.DockTest.Tool",L"Retained editor",640,420);edit(1,L"Captured draft",128);button(2,L"Apply");combo(3);SendMessageW(controls_.at(3),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"First"));SendMessageW(controls_.at(3),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Second"));SendMessageW(controls_.at(3),CB_SETCURSEL,1,0);finish();ScreamSeq::Tests::ownGuiWindow(window());}
  HWND control(int id)const{return controls_.at(id);}
  void minimumClient(int width,int height){minimumClientWidth_=width;minimumClientHeight_=height;}
  void layout()override{++layouts;const auto [w,h]=size();place(1,8,8,std::max(1.f,w-16),24);place(2,8,40,90,24);place(3,106,40,150,160);}
  void paint(ScreamSeq::RenderSurface &surface)override{surface.fill(0,0,surface.width(),surface.height(),0x18222d);}
  void action(int,unsigned)override{}
  bool key(WPARAM key,bool,bool)override{if(key==VK_F6){++localKeys;return true;}return false;}
  void deactivate()override{++deactivations;}
};
std::wstring text(HWND window){std::wstring value(size_t(GetWindowTextLengthW(window))+1,0);GetWindowTextW(window,value.data(),int(value.size()));value.resize(wcslen(value.c_str()));return value;}
void minimumClientBounds(HWND main,HWND host){
  Tool tool(main);tool.minimumClient(440,500);
  const auto window=tool.window();const auto dpi=GetDpiForWindow(window);const float scale=dpi/96.f;
  const LONG width=LONG(std::ceil(440*scale)),height=LONG(std::ceil(500*scale));
  RECT expected{0,0,width,height};
  require(AdjustWindowRectExForDpi(&expected,DWORD(GetWindowLongPtrW(window,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(window,GWL_EXSTYLE)),dpi),"Calculate floating frame at its current DPI");
  MINMAXINFO minimum{};SendMessageW(window,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&minimum));
  require(minimum.ptMinTrackSize.x==expected.right-expected.left&&minimum.ptMinTrackSize.y==expected.bottom-expected.top,"Floating minimum scaled a 96-DPI frame instead of using the current DPI frame");
  require(SetWindowPos(window,nullptr,0,0,expected.right-expected.left,expected.bottom-expected.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE),"Size floating tool to its exact minimum client area");
  RECT client{};GetClientRect(window,&client);
  require(client.right==width&&client.bottom==height,"Floating client minimum is not exactly 440 by 500 DIPs");
  tool.dock(host);
  MINMAXINFO docked{},nativeDefault{};docked.ptMinTrackSize=nativeDefault.ptMinTrackSize={17,19};
  DefWindowProcW(window,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&nativeDefault));
  SendMessageW(window,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&docked));
  require(docked.ptMinTrackSize.x==nativeDefault.ptMinTrackSize.x&&docked.ptMinTrackSize.y==nativeDefault.ptMinTrackSize.y,"Docked tool imposed its floating minimum track size");
  tool.dockBounds(0,0,320,240);GetClientRect(window,&client);const auto parentScale=GetDpiForWindow(host)/96.f;
  require(client.right==LONG(std::round(320*parentScale))&&client.bottom==LONG(std::round(240*parentScale)),"Docked tool cannot use a client area smaller than its floating minimum");
  std::cout<<"Native client minimum: exact 440 x 500 DIPs at "<<dpi<<" DPI; docked bounds unrestricted\n";
}
void compactHeaderMeasurement(){
  Window owner(L"STATIC");ScreamSeq::RenderSurface surface(owner.value);
  const std::wstring full=L"PATTERN 1234 / 1024 rows",compact=L"P1234 · 1024 rows",shortest=L"P1234";
  const auto fullWidth=surface.uiTextWidth(full),compactWidth=surface.uiTextWidth(compact),shortWidth=surface.uiTextWidth(shortest);
  require(fullWidth>compactWidth&&compactWidth>shortWidth,"Header fixture labels do not have ordered measured widths");
  require(surface.fittingUiText({full,compact,shortest},fullWidth)==full,"Full fitting header was discarded");
  require(surface.fittingUiText({full,compact,shortest},compactWidth)==compact,"Compact fitting header was clipped instead of selected");
  require(surface.fittingUiText({full,compact,shortest},shortWidth)==shortest,"Shortest fitting header was clipped instead of selected");
  require(surface.fittingUiText({full,compact,shortest},shortWidth-1).empty(),"Header draws a partial label when no complete title fits");
  require(surface.fittingUiText({L"PATTERN 0 / 64 rows",L"P0 · 64 rows",L"P0"},107)==L"P0 · 64 rows","Minimum-width pattern title is not fully readable");
  const auto entries=surface.textCacheSize();const auto misses=surface.textMisses();
  for(int i=0;i<200;++i)require(surface.fittingUiText({full,compact,shortest},compactWidth)==compact,"Repeated header choice changed");
  require(surface.textCacheSize()==entries&&surface.textMisses()==misses,"Unchanged header measurements rebuild DirectWrite layouts");
  for(int i=0;i<4200;++i)surface.uiTextWidth(L"Bounded measurement "+std::to_wstring(i));
  require(surface.textCacheSize()<=4096,"Header measurement bypassed the retained text budget");
  std::cout<<"Measured full/compact/short pattern headers fit and reuse the bounded DirectWrite cache\n";
}
void retainedDock(HWND main,HWND host){
  Tool tool(main);
  const auto window=tool.window(),edit=tool.control(1),combo=tool.control(3);
  require(!tool.docked()&&ScreamSeq::NativeToolWindow::belongsToTool(window)&&ScreamSeq::NativeToolWindow::belongsToTool(edit),"Floating tool marker missing");
  require(!ScreamSeq::NativeToolWindow::belongsToTool(main)&&!ScreamSeq::NativeToolWindow::belongsToTool(host),"Tool marker escaped its subtree");
  SetWindowPos(window,nullptr,80,90,640,420,SWP_NOZORDER|SWP_NOACTIVATE);
  RECT floating{};GetWindowRect(window,&floating);
  tool.show();SetFocus(edit);SendMessageW(edit,EM_SETSEL,2,7);
  unsigned changed=0,globals=0,releases=0;
  tool.placementChanged([&]{++changed;});
  tool.workspaceKeys([&](WPARAM key,bool){++globals;return key==VK_F8;});
  tool.musicalTyping({},[&](WPARAM key){if(key=='Z'){++releases;return true;}return false;},[]{});
  unsigned focusPaints=0;
  tool.focusPresentationChanged([&]{++focusPaints;});
  tool.dock(host);tool.dockBounds(10,12,520,360);
  require(tool.docked()&&GetParent(window)==host&&(GetWindowLongPtrW(window,GWL_STYLE)&WS_CHILD),"Tool did not become a docked child");
  require(tool.window()==window&&tool.control(1)==edit&&tool.control(3)==combo,"Docking replaced a retained HWND");
  require(GetFocus()==edit&&text(edit)==L"Captured draft","Docking lost focus or draft text");
  DWORD first=0,last=0;SendMessageW(edit,EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));require(first==2&&last==7,"Docking lost text selection");
  require(SendMessageW(combo,CB_GETCURSEL,0,0)==1&&changed==1,"Docking reset native selection or duplicated placement notification");
  RECT bounds{};GetWindowRect(window,&bounds);MapWindowPoints(nullptr,host,reinterpret_cast<POINT *>(&bounds),2);
  const float scale=GetDpiForWindow(host)/96.f;
  require(bounds.left==int(std::round(10*scale))&&bounds.top==int(std::round(12*scale))&&bounds.right-bounds.left==int(std::round(520*scale)),"Dock bounds are not in parent DIPs");
  const auto unchangedLayouts=tool.layouts;tool.dockBounds(10,12,520,360);require(tool.layouts==unchangedLayouts,"Unchanged dock bounds triggered a redundant layout");
  const auto beforeFocusPaints=focusPaints;
  SetFocus(window);require(focusPaints>beforeFocusPaints,"Native canvas focus did not request presentation");
  const auto canvasFocusPaints=focusPaints;SetFocus(edit);
  require(focusPaints>canvasFocusPaints&&tool.layouts==unchangedLayouts&&text(edit)==L"Captured draft",
      "Native child focus did not request presentation or caused layout/draft mutation");
  SendMessageW(edit,EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));
  require(first==2&&last==7,"Focus presentation changed the retained native caret");
  tool.focusPresentationChanged({});
  SendMessageW(edit,WM_KEYDOWN,VK_F6,0);require(tool.localKeys==1&&globals==0,"Workspace key intercepted a local editor shortcut");
  SendMessageW(edit,WM_KEYDOWN,VK_F8,0);require(globals==1,"Unhandled key did not reach workspace callback exactly once");
  SendMessageW(edit,WM_KEYUP,'Z',0);require(releases==1,"Docked key-up failed to release musical input");
  tool.workspaceKeys([&](WPARAM,bool){++globals;return false;});
  SetFocus(combo);SendMessageW(combo,CB_SHOWDROPDOWN,TRUE,0);const auto before=globals;
  SendMessageW(combo,WM_KEYDOWN,VK_ESCAPE,0);
  require(!SendMessageW(combo,CB_GETDROPPEDSTATE,0,0)&&globals==before,"Workspace callback intercepted a selector popup");
  SetFocus(edit);SendMessageW(edit,WM_KEYDOWN,VK_TAB,0);require(GetFocus()==tool.control(2),"Docked native Tab traversal escaped the editor");
  SetFocus(edit);tool.floatWindow();
  require(!tool.docked()&&!(GetWindowLongPtrW(window,GWL_STYLE)&WS_CHILD)&&GetWindow(window,GW_OWNER)==main,"Floating did not restore native owner/styles");
  GetWindowRect(window,&bounds);require(EqualRect(&bounds,&floating),"Floating did not restore original screen bounds");
  require(tool.window()==window&&GetFocus()==edit&&text(edit)==L"Captured draft"&&changed==2,"Floating lost retained state or focus");
  require(tool.deactivations>=2,"Placement transitions did not release musical ownership");
  tool.dock(host);tool.dockBounds(0,0,520,360);SetFocus(main);tool.hide();
  require(!tool.visible()&&GetFocus()==main,"Hiding inactive dock stole keyboard focus");
  const auto hiddenChanges=changed;tool.hide();require(changed==hiddenChanges,"Repeated hide emitted a placement change");
  tool.floatWindow();require(!tool.visible()&&!tool.docked(),"Floating a hidden tool made it visible");
  tool.show();require(tool.visible()&&tool.window()==window,"Showing hidden tool did not retain its window");
  rejected([&]{tool.dock(edit);},"Allowed docking a tool into its own child");
  rejected([&]{tool.dockBounds(0,0,520,360);},"Accepted dock bounds for a floating tool");
  tool.dock(host);const auto parent=GetParent(window);const auto changes=changed;
  rejected([&]{tool.dockBounds(0,0,-1,360);},"Accepted negative dock bounds");
  require(GetParent(window)==parent&&changed==changes&&text(edit)==L"Captured draft","Rejected dock mutation changed state");
  const auto layouts=tool.layouts;SendMessageW(window,WM_DPICHANGED_AFTERPARENT,0,0);require(tool.layouts>layouts,"Child DPI change did not refresh layout");
  tool.floatWindow();SetWindowPos(window,nullptr,-30000,-30000,640,420,SWP_NOZORDER|SWP_NOACTIVATE);tool.dock(host);tool.floatWindow();
  GetWindowRect(window,&bounds);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromRect(&bounds,MONITOR_DEFAULTTONEAREST),&monitor);
  require(bounds.left>=monitor.rcWork.left&&bounds.top>=monitor.rcWork.top&&bounds.right<=monitor.rcWork.right&&bounds.bottom<=monitor.rcWork.bottom,"Floating restored inaccessible monitor coordinates");
  unsigned reentrant=0;tool.placementChanged([&]{++reentrant;tool.hide();});tool.dock(host);
  require(reentrant==1&&!tool.visible(),"Placement notification reentered during retained hide");
  tool.placementChanged({});tool.floatWindow();tool.hide();
}
#include "DraftRequestRetentionTests.inc"
#include "DraftImportRetentionTests.inc"
#include "NativeWriteCompletionTests.inc"
}
int wmain(int argc,wchar_t **argv){
  try{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);ScreamSeq::Tests::runPrivateGuiProcess(L"ScreamSeqDockTest",argc,argv,[]{
    WNDCLASSW type{};type.lpfnWndProc=DefWindowProcW;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"ScreamSeq.DockTest.Host";RegisterClassW(&type);
    HWND mainWindow{},hostWindow{};{Window main(type.lpszClassName);Window host(type.lpszClassName,main.value);mainWindow=main.value;hostWindow=host.value;ShowWindow(main.value,SW_SHOWNOACTIVATE);minimumClientBounds(main.value,host.value);retainedDock(main.value,host.value);compactHeaderMeasurement();trimRequestRetention(main.value);renderRequestRetention(main.value);multisampleRequestRetention(main.value);pluginPathRequestRetention(main.value);recordingSetupRetention(main.value);host.close();main.close();}
    require(!IsWindow(mainWindow)&&!IsWindow(hostWindow),"Destroy owned dock test hosts");
    {Window main(type.lpszClassName);completionClassification();renderCompletionReview(main.value);importCompletionReview(main.value);recordingCompletionReview(main.value);}
    std::cout<<"Native result review: render/import/Keep retain postcommit failures without repeating writes\n";
    });std::cout<<"Native tool docking: retained HWNDs, focus, keyboard, ownership and bounds passed\n";return 0;
  }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
