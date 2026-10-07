#include "../App/RecoveryWindow.hpp"
#include "PrivateGuiTest.hpp"
#include <iostream>

namespace {
using Json=ScreamSeq::Api::Json;
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<typename Action>void rejected(Action action,const char *message){try{action();}catch(const std::exception &){return;}throw std::runtime_error(message);}
struct Owner {
  HWND window{},edit{};
  Owner(){WNDCLASSW kind{};kind.lpfnWndProc=DefWindowProcW;kind.hInstance=GetModuleHandleW(nullptr);kind.lpszClassName=L"ScreamSeq.Recovery.TestOwner";RegisterClassW(&kind);
    window=CreateWindowExW(0,kind.lpszClassName,L"Recovery test owner",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,1100,800,nullptr,nullptr,kind.hInstance,nullptr);require(window,"Create recovery test owner");ScreamSeq::Tests::ownGuiWindow(window);
    edit=CreateWindowExW(0,L"EDIT",L"Other retained draft",WS_CHILD|WS_VISIBLE|WS_TABSTOP,10,10,220,26,window,nullptr,kind.hInstance,nullptr);require(edit,"Create unrelated focus target");ScreamSeq::Tests::ownGuiWindow(edit);}
  ~Owner(){if(window)DestroyWindow(window);}
  void close(){const auto parent=window,child=edit;require(DestroyWindow(parent),"Destroy owned test owner");window=edit=nullptr;require(!IsWindow(parent)&&!IsWindow(child),"Owned test windows survived destruction");}
};
Json status(){return {{"enabled",true},{"intervalSeconds",10},{"generations",10},{"lastSavedAt",nullptr},{"lastCopy",nullptr},{"error",nullptr},{"saving",false}};}
Json copy(const std::string &id,const std::string &title,const Json &source=nullptr,bool recording=false){return {{"id",id},{"document","session"},{"savedAt","2026-10-07T12:34:56Z"},{"title",title},{"source",source},{"hasRecording",recording}};}
std::wstring text(HWND h){std::wstring result(size_t(GetWindowTextLengthW(h))+1,0);GetWindowTextW(h,result.data(),int(result.size()));result.resize(wcslen(result.c_str()));return result;}
struct Browser {
  unsigned reloads=0,saves=0;std::vector<std::string> restores;
  std::function<void()> onReload,onSave;std::function<void(const std::string &)> onRestore;
  ScreamSeq::RecoveryWindow tool;
  explicit Browser(HWND owner):tool(owner,[this]{++reloads;if(onReload)onReload();},[this]{++saves;if(onSave)onSave();},[this](std::string id){restores.push_back(id);if(onRestore)onRestore(id);}){ScreamSeq::Tests::ownGuiWindow(tool.window());}
  HWND control(int id)const{const auto h=GetDlgItem(tool.window(),id);require(h,"Recovery control missing");return h;}
  void click(int id){SendMessageW(tool.window(),WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),reinterpret_cast<LPARAM>(control(id)));}
  void select(int row){ListView_SetItemState(control(7001),-1,0,LVIS_SELECTED|LVIS_FOCUSED);if(row>=0)ListView_SetItemState(control(7001),row,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);}
  void enter(int id){SetFocus(control(id));SendMessageW(control(id),WM_KEYDOWN,VK_RETURN,0);}
  void doubleClick(int row){NMITEMACTIVATE notification{};notification.hdr={control(7001),7001,NM_DBLCLK};notification.iItem=row;SendMessageW(tool.window(),WM_NOTIFY,7001,reinterpret_cast<LPARAM>(&notification));}
  std::wstring cell(int row,int column){wchar_t buffer[1024]{};NMLVDISPINFOW info{};info.hdr={control(7001),7001,LVN_GETDISPINFOW};info.item.mask=LVIF_TEXT;info.item.iItem=row;info.item.iSubItem=column;info.item.pszText=buffer;info.item.cchTextMax=1024;SendMessageW(tool.window(),WM_NOTIFY,7001,reinterpret_cast<LPARAM>(&info));return buffer;}
};
void selectionAndDetails(Owner &owner){
  Browser browser(owner.window);auto &tool=browser.tool;tool.show();tool.update(Json::array(),status());
  require(!tool.snapshot()["restoreEnabled"].get<bool>()&&!IsWindowEnabled(browser.control(7004)),"Empty browser allows Restore");browser.enter(7001);browser.doubleClick(-1);require(browser.restores.empty(),"Empty browser restored a row");
  const auto first=copy("first.screamseq","A melodic phrase","C:\\Music\\A melodic phrase.screamseq",true),second=copy("second.screamseq","An older phrase");
  tool.update(Json::array({first,second}),status());require(tool.snapshot()["selected"]=="first.screamseq","First available copy was not selected");
  require(browser.cell(0,1)==L"Recording · A melodic phrase"&&browser.cell(0,2)==L"A melodic phrase.screamseq","Native report omitted leading recording badge or basename");
  require(text(browser.control(7006))==L"A melodic phrase"&&text(browser.control(7007))==L"C:\\Music\\A melodic phrase.screamseq","Full recovery details missing");
  browser.select(1);require(tool.snapshot()["selected"]=="second.screamseq","Native report selection did not bind the copy ID");
  SetFocus(browser.control(7006));SendMessageW(browser.control(7006),EM_SETSEL,3,8);
  const auto newer=copy("new.screamseq","Newest copy");tool.update(Json::array({newer,first,second}),status());
  require(tool.snapshot()["selected"]=="second.screamseq"&&ListView_GetNextItem(browser.control(7001),-1,LVNI_SELECTED)==2,"Reload redirected the selected copy by index");
  DWORD start=0,end=0;SendMessageW(browser.control(7006),EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));require(GetFocus()==browser.control(7006)&&start==3&&end==8,"Reload lost detail focus or selection");
  tool.show();require(GetFocus()==browser.control(7006),"Raising an existing browser stole its text focus");
  SendMessageW(browser.control(7006),WM_KEYDOWN,VK_TAB,0);require(GetFocus()==browser.control(7007),"Tab traversal skipped the full source detail");
  SetActiveWindow(owner.window);SetFocus(owner.edit);tool.updateStatus(status());tool.update(Json::array({second,first}),status());require(GetFocus()==owner.edit,"Background recovery update stole focus");
  tool.update(Json::array({first}),status());require(tool.snapshot()["selected"]==""&&!IsWindowEnabled(browser.control(7004)),"Missing selected copy silently redirected Restore");
  require(text(browser.control(7006)).empty()&&text(browser.control(7007)).empty(),"Removed copy left stale details");
  auto before=tool.snapshot();rejected([&]{tool.update(Json::array({first,first}),status());},"Duplicate IDs accepted");require(tool.snapshot()==before,"Rejected list changed retained state");
  auto invalid=first;invalid["title"]=false;rejected([&]{tool.update(Json::array({invalid}),status());},"Malformed entry accepted");require(tool.snapshot()==before,"Invalid metadata replaced the prior list");
  auto longRecording=first;longRecording["title"]=std::string(200,'A');tool.update(Json::array({longRecording}),status());browser.select(0);
  require(browser.cell(0,1).starts_with(L"Recording · ")&&text(browser.control(7006))==std::wstring(200,L'A'),"Long title hid the leading recording marker or changed full detail");
  tool.hide();tool.update(Json::array({first,second}),status());require(!tool.visible(),"Background refresh reopened a hidden browser");
}
void requestsAndFocus(Owner &owner){
  Browser browser(owner.window);auto &tool=browser.tool;const auto first=copy("one.screamseq","One"),second=copy("two.screamseq","Two");const auto rows=Json::array({first,second});
  tool.update(rows,status());tool.show();SetActiveWindow(tool.window());
  SetFocus(browser.control(7002));browser.click(7002);require(browser.reloads==1&&tool.snapshot()["operation"]=="reload"&&!IsWindowEnabled(browser.control(7004)),"Reload did not guard pending actions");
  browser.click(7002);browser.click(7003);browser.click(7004);require(browser.reloads==1&&browser.saves==0&&browser.restores.empty(),"Pending Reload started a second action");
  tool.update(rows,status());require(GetFocus()==browser.control(7002),"Completed Reload lost button focus");
  browser.select(1);browser.enter(7001);require(browser.restores==std::vector<std::string>{"two.screamseq"}&&tool.snapshot()["restoring"]=="two.screamseq","Enter failed to capture the selected opaque ID");
  tool.update(Json::array({copy("new.screamseq","New"),first,second}),status());require(tool.snapshot()["operation"]=="restore"&&tool.snapshot()["restoring"]=="two.screamseq","Refresh replaced an active restore target");
  tool.fail("A background list refresh failed");require(tool.snapshot()["operation"]=="restore"&&tool.snapshot()["restoring"]=="two.screamseq","Unrelated failure cleared restore ownership");
  tool.completeRestore("one.screamseq",true);require(tool.visible()&&tool.snapshot()["pending"].get<bool>(),"Unrelated restore completion closed the browser");
  tool.completeRestore("two.screamseq",false,"That copy is damaged. Choose an older copy.");require(tool.visible()&&tool.snapshot()["selected"]=="two.screamseq"&&tool.snapshot()["error"]=="That copy is damaged. Choose an older copy.","Failed Restore lost its copy or error");
  browser.select(1);browser.doubleClick(1);require(browser.restores.back()=="one.screamseq","Double-click did not restore its selected row");tool.completeRestore("one.screamseq",true);require(!tool.visible(),"Successful Restore left the browser open");
  tool.show();SetActiveWindow(tool.window());SetFocus(browser.control(7003));browser.click(7003);require(browser.saves==1,"Save callback missing");
  auto saving=status();saving["saving"]=true;tool.updateStatus(saving);tool.update(rows,saving);require(!IsWindowEnabled(browser.control(7003)),"Writer status did not keep Save disabled");
  SetActiveWindow(owner.window);SetFocus(owner.edit);tool.updateStatus(status());require(GetFocus()==owner.edit,"Save completion stole newer focus");
  SetActiveWindow(tool.window());SetFocus(browser.control(7001));tool.setBusy(true);browser.enter(7001);require(browser.restores.size()==2,"Application busy state allowed Restore");tool.setBusy(false);
  browser.onReload=[] {throw std::runtime_error("Cannot read recovery copies");};browser.click(7002);require(!tool.snapshot()["pending"].get<bool>()&&tool.snapshot()["error"]=="Cannot read recovery copies","Callback failure left the browser pending");
  auto failed=status();failed["error"]="Disk is full";tool.update(rows,failed);require(tool.snapshot()["status"].get<std::string>().find("Disk is full")!=std::string::npos,"Persistent autosave error missing");
  auto disabled=status();disabled["enabled"]=false;tool.updateStatus(disabled);browser.click(7003);require(browser.saves==1&&!IsWindowEnabled(browser.control(7003)),"Disabled recovery allowed a save");
  SendMessageW(browser.control(7001),WM_KEYDOWN,VK_ESCAPE,0);require(!tool.visible(),"Escape did not hide the browser");
  Browser synchronous(owner.window);synchronous.tool.update(rows,status());synchronous.tool.show();synchronous.onRestore=[&](const std::string &id){synchronous.tool.completeRestore(id,true);};synchronous.enter(7001);require(!synchronous.tool.visible()&&!synchronous.tool.snapshot()["pending"].get<bool>(),"Synchronous restore callback reentered incorrectly");
}
void boundsAndColumns(Owner &owner){
  Browser browser(owner.window);auto &tool=browser.tool;tool.update(Json::array({copy("one.screamseq","One")}),status());tool.show();
  const auto window=tool.window();const auto dpi=GetDpiForWindow(window);RECT frame{0,0,MulDiv(650,dpi,96),MulDiv(430,dpi,96)};
  require(AdjustWindowRectExForDpi(&frame,DWORD(GetWindowLongPtrW(window,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(window,GWL_EXSTYLE)),dpi),"Calculate minimum recovery frame");
  require(SetWindowPos(window,nullptr,0,0,frame.right-frame.left,frame.bottom-frame.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE),"Resize recovery browser");
  RECT client{};GetClientRect(window,&client);require(client.right==MulDiv(650,dpi,96)&&client.bottom==MulDiv(430,dpi,96),"Recovery client minimum differs from 650 x 430 DIPs");
  std::vector<RECT> boxes;for(auto child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))if(IsWindowVisible(child)){
    RECT box{};GetWindowRect(child,&box);MapWindowPoints(nullptr,window,reinterpret_cast<POINT *>(&box),2);require(box.left>=0&&box.top>=0&&box.right<=client.right&&box.bottom<=client.bottom,"Recovery control outside the minimum client");
    for(const auto &other:boxes){RECT overlap{};require(!IntersectRect(&overlap,&box,&other),"Recovery controls overlap at minimum size");}boxes.push_back(box);
  }
  auto list=browser.control(7001);const int custom=MulDiv(196,dpi,96);ListView_SetColumnWidth(list,0,custom);HDITEMW item{};item.mask=HDI_WIDTH;item.cxy=custom;
  NMHEADERW changed{};changed.hdr={ListView_GetHeader(list),0,HDN_ENDTRACKW};changed.iItem=0;changed.pitem=&item;SendMessageW(list,WM_NOTIFY,0,reinterpret_cast<LPARAM>(&changed));
  RECT outer{};GetWindowRect(window,&outer);SetWindowPos(window,nullptr,0,0,outer.right-outer.left+MulDiv(100,dpi,96),outer.bottom-outer.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
  require(ListView_GetColumnWidth(list,0)==custom,"Resize discarded the user's column width");
  std::cout<<"Recovery browser minimum 650 x 430 DIPs checked at "<<dpi<<" DPI\n";
}
}
int main(){try{SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);ScreamSeq::Tests::runPrivateGui(L"ScreamSeqRecoveryTest",[]{HWND parent{},edit{};{Owner owner;parent=owner.window;edit=owner.edit;selectionAndDetails(owner);requestsAndFocus(owner);boundsAndColumns(owner);owner.close();}require(!IsWindow(parent)&&!IsWindow(edit),"Destroy owned recovery test windows");});std::cout<<"PASS recovery browser: retained identity/focus, guarded asynchronous callbacks, keyboard, native columns and bounds\n";return 0;}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
