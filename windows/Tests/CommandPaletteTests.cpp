// Isolated native message tests; no global input, clipboard writes or audio.
#include "../App/CommandPalette.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition,const char *message) {if(!condition)throw std::runtime_error(message);}
std::wstring text(HWND h) {
  std::wstring value(size_t(GetWindowTextLengthW(h))+1,0);
  GetWindowTextW(h,value.data(),int(value.size()));value.resize(wcslen(value.c_str()));return value;
}
std::wstring item(HWND list,int row) {
  const auto length=SendMessageW(list,LB_GETTEXTLEN,row,0);
  require(length>=0,"Result row is missing");std::wstring value(size_t(length)+1,0);
  SendMessageW(list,LB_GETTEXT,row,reinterpret_cast<LPARAM>(value.data()));value.resize(size_t(length));return value;
}
struct PrivateDesktop {
  HDESK original=GetThreadDesktop(GetCurrentThreadId()),isolated{};
  HWND foreground=GetForegroundWindow();DWORD clipboard=GetClipboardSequenceNumber();
  PrivateDesktop() {
    const auto name=L"ScreamSeqPaletteTests-"+std::to_wstring(GetCurrentProcessId());
    isolated=CreateDesktopW(name.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);
    require(isolated!=nullptr,"Cannot create private test desktop");
    if(!SetThreadDesktop(isolated)){CloseDesktop(isolated);isolated=nullptr;throw std::runtime_error("Cannot attach private test desktop");}
  }
  ~PrivateDesktop() {SetThreadDesktop(original);if(isolated)CloseDesktop(isolated);}
  void verifyUnchanged() const {
    require(GetClipboardSequenceNumber()==clipboard,"Palette tests changed the clipboard");
    // GetForegroundWindow is desktop-local; check the musician's desktop only
    // after all test windows have been destroyed and this thread is restored.
    require(SetThreadDesktop(original)!=FALSE,"Cannot restore original test-thread desktop");
    require(GetForegroundWindow()==foreground,"Palette tests changed the user's foreground window");
  }
};
void key(HWND control,WPARAM code) {SendMessageW(control,WM_KEYDOWN,code,0);}
void ctrlKey(HWND control,WPARAM code) {
  BYTE original[256]{},modified[256]{};GetKeyboardState(original);std::copy(std::begin(original),std::end(original),std::begin(modified));
  modified[VK_CONTROL]|=0x80;SetKeyboardState(modified);key(control,code);SendMessageW(control,WM_CHAR,code-'A'+1,0);SetKeyboardState(original);
}
void runTests() {
  const auto instance=GetModuleHandleW(nullptr);WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"ScreamSeqPaletteTestOwner";RegisterClassW(&wc);
  HWND owner=CreateWindowExW(0,wc.lpszClassName,L"Palette tests",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,1000,700,nullptr,nullptr,instance,nullptr);
  require(owner!=nullptr,"Cannot create test owner");
  HWND origin=CreateWindowExW(0,L"EDIT",L"Retained draft",WS_CHILD|WS_VISIBLE|WS_TABSTOP,10,10,200,24,owner,nullptr,instance,nullptr);
  require(origin!=nullptr,"Cannot create original focus target");SetActiveWindow(owner);SetFocus(origin);
  int executed=0,executions=0;
  {
    std::vector<ScreamSeq::WorkspaceCommand> commands={
      {1,L"Sample / Set normal loop to selection",L"L in samples"},
      {2,L"File / Save native project",L"Ctrl+S"},
      {3,L"File / Save As",L"Ctrl+Shift+S"},
      {4,L"Sample / Detailed waveform, drawing and crossfade",L""},
      {5,L"Sample / Set sustain loop to selection",L"Shift+L in samples"},
      {6,L"Graph / Open reusable routing and modulation canvas",L""},
      {7,L"Playback / Play",L"Space"},
      {8,L"Pattern / Clear selection",L"Delete"},
      {9,L"Pattern / Copy rectangular cells",L"Ctrl+C"},
      {10,L"Pattern / Paste rectangular cells",L"Ctrl+V"},
      {11,L"Panel / Toggle pin",L"Ctrl+P"},
      {12,L"Workspace / Compose",L"Ctrl+1"},
      {13,L"Workspace / Pattern focus",L"Ctrl+2"},
      {14,L"Workspace / Sound design",L"Ctrl+3"},
      {15,L"No category command",L""},
      {16,L"Pattern / Focus graph command lanes",L"F6 from pattern"}};
    ScreamSeq::CommandPalette palette(owner,commands,[&](int id){executed=id;++executions;});palette.show();
    const HWND popup=GetAncestor(GetFocus(),GA_ROOT),search=GetDlgItem(popup,101),results=GetDlgItem(popup,102),run=GetDlgItem(popup,103);
    require(popup!=owner&&search&&results&&run,"Palette native controls were not created");
    require(GetFocus()==search,"Palette did not focus the search field");
    require(SendMessageW(results,LB_GETCOUNT,0,0)==LRESULT(commands.size()),"Initial results omitted commands");
    require(item(results,0).find(L"File / Save As")==0,"Initial commands are not grouped alphabetically");

    SetWindowTextW(search,L"  LOOP   sample  ");
    require(SendMessageW(results,LB_GETCOUNT,0,0)==2,"Unordered category/action words or whitespace did not match");
    key(search,VK_DOWN);require(SendMessageW(results,LB_GETCURSEL,0,0)==1,"Search arrow did not choose the next result");
    const auto chosen=item(results,1);SetWindowTextW(search,L"sample");
    require(item(results,int(SendMessageW(results,LB_GETCURSEL,0,0)))==chosen,"Filtering redirected the selected command");
    palette.show();require(text(search)==L"sample","Raising an already-visible palette discarded its search");
    require(item(results,int(SendMessageW(results,LB_GETCURSEL,0,0)))==chosen,"Raising the palette discarded its selection");
    key(search,VK_RETURN);SendMessageW(search,WM_CHAR,VK_RETURN,0);
    require(executed==5&&executions==1,"Enter ran the wrong command or ran more than once");
    require(!IsWindowVisible(popup)&&GetFocus()==origin,"Executing did not restore the captured control focus");
    require(text(origin)==L"Retained draft","Palette changed the original text draft");

    palette.show();SetWindowTextW(search,L"ctrl+shift+s");
    require(SendMessageW(results,LB_GETCOUNT,0,0)==1,"Shortcut search did not isolate Save As");
    SendMessageW(run,BM_CLICK,0,0);require(executed==3&&executions==2,"Run button did not execute its search result");
    palette.show();SetWindowTextW(search,L"missing command zzz");
    require(SendMessageW(results,LB_GETCOUNT,0,0)==0&&!IsWindowEnabled(run),"Empty search left an executable selection");
    key(search,VK_RETURN);key(search,VK_DOWN);key(search,VK_NEXT);
    require(IsWindowVisible(popup)&&executions==2,"Empty search ran or dismissed the palette");
    bool emptyGuidance=false;
    for(auto h=GetWindow(popup,GW_CHILD);h;h=GetWindow(h,GW_HWNDNEXT))if(text(h).find(L"No matching commands")!=std::wstring::npos)emptyGuidance=true;
    require(emptyGuidance,"Empty results gave no recovery guidance");

    SetWindowTextW(search,L"");SetFocus(results);key(results,VK_END);
    require(SendMessageW(results,LB_GETCURSEL,0,0)==LRESULT(commands.size()-1),"End did not choose the final result");
    require(SendMessageW(results,LB_GETTOPINDEX,0,0)>0,"Navigation did not reveal the final result");
    key(results,VK_HOME);require(SendMessageW(results,LB_GETCURSEL,0,0)==0,"Home did not choose the first result");
    key(search,VK_NEXT);require(SendMessageW(results,LB_GETCURSEL,0,0)>1,"Page Down did not page the results from search");
    key(search,VK_PRIOR);require(SendMessageW(results,LB_GETCURSEL,0,0)==0,"Page Up did not return to the first result");
    SetFocus(results);SendMessageW(results,WM_CHAR,L's',0);
    require(GetFocus()==search&&text(search)==L"s","Typing from results did not return to search");
    key(search,VK_TAB);SendMessageW(search,WM_CHAR,VK_TAB,0);
    require(GetFocus()==results&&text(search)==L"s","Tab inserted text or failed to focus results");
    key(results,VK_TAB);require(GetFocus()==run,"Tab did not reach the Run action");
    key(run,VK_TAB);require(GetFocus()==search,"Tab did not cycle back to search");
    SetWindowTextW(search,L"sample");ctrlKey(search,'A');
    DWORD begin=0,end=0;SendMessageW(search,EM_GETSEL,reinterpret_cast<WPARAM>(&begin),reinterpret_cast<LPARAM>(&end));
    require(begin==0&&end==6,"Ctrl+A did not select the query text");
    SetFocus(results);ctrlKey(results,'K');
    require(GetFocus()==search&&text(search)==L"sample","Ctrl+K inserted a control character or failed to focus search");
    SendMessageW(search,EM_GETSEL,reinterpret_cast<WPARAM>(&begin),reinterpret_cast<LPARAM>(&end));
    require(begin==0&&end==6,"Ctrl+K did not select the query for replacement");
    SetWindowTextW(search,L"unmatched reset");SetWindowTextW(search,L"pattern");SetWindowTextW(search,L"pattern focus");
    require(SendMessageW(results,LB_GETCOUNT,0,0)==2,"Pattern focus query did not expose both matching commands");
    require(item(results,0).find(L"Workspace / Pattern focus")==0&&SendMessageW(results,LB_GETCURSEL,0,0)==0,"Exact title did not outrank scattered category/title words while refining search");
    key(search,VK_RETURN);require(executed==13&&executions==3,"Pattern focus search ran graph focus instead of the exact layout command");
    palette.show();

    MINMAXINFO limits{};SendMessageW(popup,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&limits));
    SetWindowPos(popup,nullptr,0,0,limits.ptMinTrackSize.x,limits.ptMinTrackSize.y,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    RECT client{};GetClientRect(popup,&client);
    for(auto control:{search,results,run}) {
      RECT r{};GetWindowRect(control,&r);MapWindowPoints(nullptr,popup,reinterpret_cast<POINT *>(&r),2);
      require(r.left>=0&&r.top>=0&&r.right<=client.right&&r.bottom<=client.bottom,"Palette control escaped minimum window bounds");
    }
    key(search,VK_ESCAPE);SendMessageW(search,WM_CHAR,VK_ESCAPE,0);
    require(GetFocus()==origin&&!IsWindowVisible(popup),"Escape did not restore original focus");
    palette.show();ShowWindow(origin,SW_HIDE);key(search,VK_ESCAPE);
    require(GetFocus()==owner,"Unavailable original focus did not fall back to the owner");
  }
  DestroyWindow(owner);
}
}
int main() {
  try {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    PrivateDesktop desktop;INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    runTests();desktop.verifyUnchanged();std::cout<<"Command palette native message tests passed\n";return 0;
  } catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
