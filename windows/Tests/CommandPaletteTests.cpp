// Isolated native message tests; no global input, clipboard writes or audio.
#include "PrivateGuiTest.hpp"
#include "../App/CommandPalette.hpp"
#include <iostream>
#include <map>
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
void key(HWND control,WPARAM code) {SendMessageW(control,WM_KEYDOWN,code,0);}
void ctrlKey(HWND control,WPARAM code) {
  BYTE original[256]{},modified[256]{};GetKeyboardState(original);std::copy(std::begin(original),std::end(original),std::begin(modified));
  modified[VK_CONTROL]|=0x80;SetKeyboardState(modified);key(control,code);SendMessageW(control,WM_CHAR,code-'A'+1,0);SetKeyboardState(original);
}
void modifiedKey(HWND control,WPARAM code,bool ctrl,bool alt,bool shift=false,LPARAM flags=0) {
  BYTE original[256]{},modified[256]{};GetKeyboardState(original);std::copy(std::begin(original),std::end(original),std::begin(modified));
  for(const auto key:{VK_CONTROL,VK_MENU,VK_SHIFT,VK_LWIN,VK_RWIN,VK_RMENU})modified[key]&=0x7f;
  if(ctrl)modified[VK_CONTROL]|=0x80;if(alt)modified[VK_MENU]|=0x80;if(shift)modified[VK_SHIFT]|=0x80;
  SetKeyboardState(modified);SendMessageW(control,alt?WM_SYSKEYDOWN:WM_KEYDOWN,code,flags);
  SendMessageW(control,alt?WM_SYSCHAR:WM_CHAR,code,flags);SetKeyboardState(original);
}
bool hasText(HWND parent,const wchar_t *value) {
  for(auto h=GetWindow(parent,GW_CHILD);h;h=GetWindow(h,GW_HWNDNEXT))if(text(h).find(value)!=std::wstring::npos)return true;
  return false;
}
void checkBounds(HWND popup) {
  RECT client{};GetClientRect(popup,&client);std::vector<RECT> controls;
  for(auto control=GetWindow(popup,GW_CHILD);control;control=GetWindow(control,GW_HWNDNEXT))if(IsWindowVisible(control)) {
    RECT r{};GetWindowRect(control,&r);MapWindowPoints(nullptr,popup,reinterpret_cast<POINT *>(&r),2);
    require(r.left>=0&&r.top>=0&&r.right<=client.right&&r.bottom<=client.bottom,"Palette control escaped minimum window bounds");
    for(const auto &other:controls){RECT overlap{};require(!IntersectRect(&overlap,&r,&other),"Palette visible controls overlap");}controls.push_back(r);
  }
}
void runTests() {
  const auto instance=GetModuleHandleW(nullptr);WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"ScreamSeqPaletteTestOwner";RegisterClassW(&wc);
  HWND owner=CreateWindowExW(0,wc.lpszClassName,L"Palette tests",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,1000,700,nullptr,nullptr,instance,nullptr);
  require(owner!=nullptr,"Cannot create test owner");ScreamSeq::Tests::ownGuiWindow(owner);
  HWND origin=CreateWindowExW(0,L"EDIT",L"Retained draft",WS_CHILD|WS_VISIBLE|WS_TABSTOP,10,10,200,24,owner,nullptr,instance,nullptr);
  require(origin!=nullptr,"Cannot create original focus target");ScreamSeq::Tests::ownGuiWindow(origin);SetActiveWindow(owner);SetFocus(origin);
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
      {16,L"Pattern / Focus graph command lanes",L"F6 from pattern",L"F6 from pattern"}};
    ScreamSeq::CommandPalette palette(owner,commands,[&](int id){executed=id;++executions;});palette.show();
    const HWND popup=GetAncestor(GetFocus(),GA_ROOT),search=GetDlgItem(popup,101),results=GetDlgItem(popup,102),run=GetDlgItem(popup,103);
    require(popup!=owner&&search&&results&&run,"Palette native controls were not created");ScreamSeq::Tests::ownGuiWindow(popup);
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
    checkBounds(popup);

    std::map<int,std::vector<std::string>> bindings;
    bindings[2]={"ctrl+s"};bindings[3]={"ctrl+shift+s"};bindings[7]={"space"};
    const auto defaults=bindings;int writes=0,resets=0;bool rejectWrite=false;
    palette.configureShortcuts([&](int id){return bindings[id];},[&](int id,const std::vector<std::string> &keys){
      if(rejectWrite)throw std::runtime_error("Shortcut conflict with another command");
      bindings[id]=keys;++writes;
    },[&](int id){if(rejectWrite)throw std::runtime_error("Cannot save shortcut preferences");const auto at=defaults.find(id);bindings[id]=at==defaults.end()?std::vector<std::string>{}:at->second;++resets;});
    const HWND set=GetDlgItem(popup,104),sequence=GetDlgItem(popup,105),clear=GetDlgItem(popup,106),reset=GetDlgItem(popup,107);
    require(set&&sequence&&clear&&reset&&IsWindowVisible(set),"Shortcut editing controls were not revealed");
    SetWindowTextW(search,L"open reusable");
    SendMessageW(set,BM_CLICK,0,0);require(GetFocus()==set&&!IsWindowEnabled(run),"Shortcut recording did not own focus or disable Run");
    modifiedKey(set,'G',false,false);
    require(writes==0&&text(search)==L"open reusable"&&hasText(popup,L"first key needs Ctrl or Alt"),"Unmodified capture changed preferences or query");
    modifiedKey(set,'G',true,true);
    require(writes==1&&bindings[6]==std::vector<std::string>{"ctrl+alt+g"},"Single shortcut was not normalized and saved once");
    require(text(search)==L"open reusable"&&IsWindowVisible(popup)&&GetFocus()==set&&item(results,0).find(L"Ctrl+Alt+G")!=std::wstring::npos,"Successful assignment lost query, focus, selection or effective hint");

    SendMessageW(sequence,BM_CLICK,0,0);modifiedKey(sequence,'J',true,true);
    modifiedKey(sequence,'J',true,true,false,LPARAM(1)<<30);
    key(sequence,VK_RETURN);require(writes==1&&hasText(popup,L"Add a second key"),"One stroke or key repeat committed a sequence");
    modifiedKey(sequence,'A',false,false);modifiedKey(sequence,'B',false,false);modifiedKey(sequence,'C',false,false);modifiedKey(sequence,'D',false,false);
    require(writes==1&&hasText(popup,L"Four keys recorded"),"Uncommitted or excessive sequence mutated preferences");
    key(sequence,VK_RETURN);SendMessageW(sequence,WM_CHAR,VK_RETURN,0);
    require(writes==2&&bindings[6]==std::vector<std::string>({"ctrl+alt+j","a","b","c"}),"Four-stroke sequence saved incorrect keys");
    require(item(results,0).find(L"Ctrl+Alt+J → A → B → C")!=std::wstring::npos&&text(search)==L"open reusable","Sequence label or search retention failed");
    SendMessageW(sequence,BM_CLICK,0,0);modifiedKey(sequence,'Q',true,true);key(sequence,VK_ESCAPE);SendMessageW(sequence,WM_CHAR,VK_ESCAPE,0);
    require(writes==2&&IsWindowVisible(popup)&&IsWindowEnabled(run),"Escape saved a partial sequence or dismissed the palette");

    rejectWrite=true;SendMessageW(set,BM_CLICK,0,0);modifiedKey(set,'Q',true,true);
    require(writes==2&&hasText(popup,L"Shortcut conflict")&&text(search)==L"open reusable"&&!IsWindowEnabled(run),"Failed assignment hid the error or changed state");
    rejectWrite=false;modifiedKey(set,'U',true,true);
    require(writes==3&&bindings[6]==std::vector<std::string>{"ctrl+alt+u"},"Retry appended to a failed single-stroke assignment");
    SendMessageW(clear,BM_CLICK,0,0);require(writes==4&&bindings[6].empty()&&item(results,0)==L"Graph / Open reusable routing and modulation canvas","Clear did not remove the effective sequence");

    SetWindowTextW(search,L"save native");SendMessageW(clear,BM_CLICK,0,0);
    require(bindings[2].empty()&&item(results,0)==L"File / Save native project","Clearing a default revived its old displayed shortcut");
    SendMessageW(reset,BM_CLICK,0,0);require(resets==1&&bindings[2]==std::vector<std::string>{"ctrl+s"}&&item(results,0).find(L"Ctrl+S")!=std::wstring::npos,"Reset did not refresh the default shortcut");
    rejectWrite=true;SendMessageW(reset,BM_CLICK,0,0);
    require(resets==1&&hasText(popup,L"Cannot save shortcut preferences")&&text(search)==L"save native","Failed reset changed state or lost query");rejectWrite=false;
    SetWindowTextW(search,L"ctrl+s");SendMessageW(clear,BM_CLICK,0,0);
    require(text(search)==L"ctrl+s"&&item(results,int(SendMessageW(results,LB_GETCURSEL,0,0)))==L"File / Save native project","Rebinding from a shortcut query hid the command being edited");
    SetWindowTextW(search,L"save");SetWindowTextW(search,L"ctrl+s");
    // Ctrl+Shift+S still contains the substring Ctrl+S; the cleared Save row
    // is retained only until the query changes, independently of row ranking.
    require(SendMessageW(results,LB_GETCOUNT,0,0)==1&&item(results,0).find(L"File / Save As")==0,"Retained edited row leaked into subsequent searches");

    SetWindowTextW(search,L"sample");key(search,VK_DOWN);const auto beforeRefresh=item(results,int(SendMessageW(results,LB_GETCURSEL,0,0)));
    bindings[6]={"ctrl+alt+h","m"};palette.refreshShortcuts();
    require(text(search)==L"sample"&&item(results,int(SendMessageW(results,LB_GETCURSEL,0,0)))==beforeRefresh,"External shortcut refresh changed selection or query");
    SetWindowTextW(search,L"ctrl+alt+h");require(SendMessageW(results,LB_GETCOUNT,0,0)==1&&item(results,0).find(L"Graph / Open reusable")==0,"Dynamic shortcut refresh did not update the search index");
    SetWindowTextW(search,L"ctrl+alt+g");require(SendMessageW(results,LB_GETCOUNT,0,0)==0,"Rebound shortcut remained in search results");
    require(!IsWindowEnabled(set)&&!IsWindowEnabled(sequence)&&!IsWindowEnabled(clear)&&!IsWindowEnabled(reset),"Empty results retained configurable command actions");
    SetWindowTextW(search,L"focus graph command");require(item(results,0).find(L"F6 from pattern")!=std::wstring::npos,"Intrinsic contextual hint disappeared with an empty global shortcut");
    SetFocus(run);key(run,VK_TAB);require(GetFocus()==set,"Keyboard navigation did not reach shortcut actions");
    key(set,VK_TAB);require(GetFocus()==sequence,"Keyboard navigation did not reach sequence action");
    key(sequence,VK_TAB);require(GetFocus()==clear,"Keyboard navigation did not reach Clear action");
    key(clear,VK_TAB);require(GetFocus()==reset,"Keyboard navigation did not reach Reset action");
    key(reset,VK_TAB);require(GetFocus()==search,"Keyboard navigation did not return from shortcut actions to search");

    SendMessageW(popup,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&limits));
    SetWindowPos(popup,nullptr,0,0,limits.ptMinTrackSize.x,limits.ptMinTrackSize.y,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);checkBounds(popup);
    key(search,VK_ESCAPE);SendMessageW(search,WM_CHAR,VK_ESCAPE,0);
    require(GetFocus()==origin&&!IsWindowVisible(popup),"Escape did not restore original focus");
    palette.show();ShowWindow(origin,SW_HIDE);key(search,VK_ESCAPE);
    require(GetFocus()==owner,"Unavailable original focus did not fall back to the owner");
  }
  require(DestroyWindow(owner)&&!IsWindow(owner)&&!IsWindow(origin),"Destroy owned palette test windows");
}
}
int main() {
  try {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ScreamSeq::Tests::runPrivateGui(L"ScreamSeqPaletteTests",[]{INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    runTests();});std::cout<<"Command palette native message tests passed\n";return 0;
  } catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
