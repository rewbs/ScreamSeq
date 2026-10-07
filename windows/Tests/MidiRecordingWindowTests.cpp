#include "../App/MidiRecordingWindow.hpp"
#include "PrivateGuiTest.hpp"
#include <iostream>

namespace {
using Json=ScreamSeq::Api::Json;
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<typename Action>void rejected(Action action,const char *message){try{action();}catch(const std::exception &){return;}throw std::runtime_error(message);}
struct Owner {
  HWND window{},edit{};
  Owner(){WNDCLASSW type{};type.lpfnWndProc=DefWindowProcW;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"ScreamSeq.MidiRecording.TestOwner";RegisterClassW(&type);
    window=CreateWindowExW(0,type.lpszClassName,L"MIDI test owner",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,1100,800,nullptr,nullptr,type.hInstance,nullptr);require(window,"Create MIDI test owner");ScreamSeq::Tests::ownGuiWindow(window);
    edit=CreateWindowExW(0,L"EDIT",L"Unrelated retained draft",WS_CHILD|WS_VISIBLE|WS_TABSTOP,10,10,220,26,window,nullptr,type.hInstance,nullptr);require(edit,"Create unrelated focus target");ScreamSeq::Tests::ownGuiWindow(edit);}
  ~Owner(){if(window)DestroyWindow(window);}
  void close(){const auto parent=window,child=edit;require(DestroyWindow(parent),"Destroy owned test owner");window=edit=nullptr;require(!IsWindow(parent)&&!IsWindow(child),"Owned test windows survived destruction");}
};
Json settings(std::string revision="m1"){return {{"revision",revision},{"source","device-a"},{"armed",false},{"channelsCount",2},{"quantization",4096},{"latencyMS",0},{"connected",true},{"error",nullptr}};}
Json devices(){return Json::array({{{"id","device-a"},{"name","Keys"}},{{"id","device-b"},{"name","Pads"}}});}
Json take(std::string id="",unsigned count=0,bool capturing=false,bool compatible=true){return {{"take",id},{"baseRevision","song-1"},{"capturing",capturing},{"compatible",compatible},{"eventCount",count},{"missingTime",0},{"exhaustedVoices",0},{"overflow",0},{"events",Json::array()}};}
Json event(unsigned row,unsigned note=61){return {{"patternID","p-opaque"},{"track","n-opaque"},{"position",row*65536+123},{"note",note},{"instrument",4},{"velocity",100},{"patternLabel","Verse"},{"trackLabel","CH 2"},{"soundLabel","Piano"}};}
Json fullTake(std::string id,Json events){auto result=take(id,unsigned(events.size()));result["events"]=std::move(events);return result;}
Json initial(){return {{"settings",settings()},{"devices",devices()},{"target",{{"label","04 / Piano"},{"firstChannel",1},{"availableChannels",7}}},{"recording",take()},{"playing",false}};}
std::wstring text(HWND window){std::wstring result(size_t(GetWindowTextLengthW(window))+1,0);GetWindowTextW(window,result.data(),int(result.size()));result.resize(wcslen(result.c_str()));return result;}
struct Browser {
  unsigned scans=0,sounds=0;std::vector<Json> applies,navigations;std::vector<std::string> finishes,discards,reviews;
  std::function<void()> onScan,onSound;std::function<Json(Json)> onApply;std::function<void(std::string)> onFinish,onDiscard,onReview;std::function<void(std::string,Json)> onNavigate;
  ScreamSeq::MidiRecordingWindow tool;
  explicit Browser(HWND owner):tool(owner,{[this]{++scans;if(onScan)onScan();},[this]{++sounds;if(onSound)onSound();},[this](Json params){applies.push_back(params);if(onApply)return onApply(std::move(params));params.erase("expectedMidiRevision");params["revision"]="m-applied";params["connected"]=!params["source"].get<std::string>().empty();params["error"]=nullptr;return params;},
    [this](std::string id){finishes.push_back(id);if(onFinish)onFinish(id);},[this](std::string id){discards.push_back(id);if(onDiscard)onDiscard(id);},[this](std::string id){reviews.push_back(id);if(onReview)onReview(id);},[this](std::string id,Json event){navigations.push_back({{"take",id},{"event",event}});if(onNavigate)onNavigate(id,event);}}){ScreamSeq::Tests::ownGuiWindow(tool.window());}
  HWND control(int id)const{auto result=GetDlgItem(tool.window(),id);require(result,"MIDI control missing");return result;}
  void click(int id){SendMessageW(tool.window(),WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),reinterpret_cast<LPARAM>(control(id)));}
  void edit(int id,const wchar_t *value){SetWindowTextW(control(id),value);}
  void combo(int id,int index){SendMessageW(control(id),CB_SETCURSEL,index,0);SendMessageW(tool.window(),WM_COMMAND,MAKEWPARAM(id,CBN_SELCHANGE),reinterpret_cast<LPARAM>(control(id)));}
  void select(int row){ListView_SetItemState(control(7211),-1,0,LVIS_SELECTED|LVIS_FOCUSED);if(row>=0)ListView_SetItemState(control(7211),row,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);}
  void key(int id,WPARAM key,bool ctrl=false,bool shift=false){BYTE before[256]{};require(GetKeyboardState(before),"Read thread keyboard state");BYTE current[256]{};std::copy(std::begin(before),std::end(before),std::begin(current));current[VK_CONTROL]=ctrl?0x80:0;current[VK_SHIFT]=shift?0x80:0;require(SetKeyboardState(current),"Set private thread modifiers");SetFocus(control(id));SendMessageW(control(id),WM_KEYDOWN,key,0);require(SetKeyboardState(before),"Restore thread keyboard state");}
  std::wstring cell(int row,int column){wchar_t buffer[1024]{};NMLVDISPINFOW info{};info.hdr={control(7211),7211,LVN_GETDISPINFOW};info.item.mask=LVIF_TEXT;info.item.iItem=row;info.item.iSubItem=column;info.item.pszText=buffer;info.item.cchTextMax=1024;SendMessageW(tool.window(),WM_NOTIFY,7211,reinterpret_cast<LPARAM>(&info));return buffer;}
};
void settingsAndFocus(Owner &owner){
  Browser browser(owner.window);auto &tool=browser.tool;tool.update(initial());tool.show();SetActiveWindow(tool.window());
  require(!tool.retainedDraft()&&!IsWindowEnabled(browser.control(7209)),"Initial settings are dirty");
  browser.combo(7201,2);browser.edit(7208,L"-");SetFocus(browser.control(7208));SendMessageW(browser.control(7208),EM_SETSEL,1,1);
  auto incoming=settings("m2");incoming["source"]="device-a";incoming["latencyMS"]=25;
  tool.update({{"settings",incoming},{"devices",Json::array({{{"id","device-b"},{"name","Pads renamed"}}})}});
  auto snap=tool.snapshot();require(snap["draft"]["source"]=="device-b"&&snap["draft"]["baseRevision"]=="m1"&&text(browser.control(7208))==L"-","Polling replaced raw MIDI draft or captured revision");
  DWORD start=0,end=0;SendMessageW(browser.control(7208),EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));require(GetFocus()==browser.control(7208)&&start==1&&end==1,"Polling lost MIDI draft focus or caret");
  require(text(browser.control(7309)).find(L"changed elsewhere")!=std::wstring::npos,"Settings revision conflict not explained");
  browser.click(7209);require(browser.applies.empty()&&tool.retainedDraft()&&!tool.snapshot()["pending"].get<bool>(),"Invalid timing draft reached settings callback");
  browser.edit(7208,L"12.5");browser.edit(7206,L"0");browser.click(7209);require(browser.applies.empty(),"Zero note columns accepted");
  browser.edit(7206,L"3");browser.onApply=[](Json){throw std::runtime_error("MIDI settings changed; reload saved settings");return Json();};browser.click(7209);
  require(browser.applies.size()==1&&browser.applies[0]["expectedMidiRevision"]=="m1"&&browser.applies[0]["source"]=="device-b"&&browser.applies[0]["latencyMS"]==12.5&&tool.retainedDraft(),"Apply lost exact settings target or failed draft");
  browser.click(7210);require(!tool.retainedDraft()&&text(browser.control(7208))==L"25"&&tool.snapshot()["draft"]["baseRevision"]=="m2","Use saved settings did not load canonical state");
  browser.onApply={};browser.edit(7208,L"-18.25");SetFocus(browser.control(7208));browser.click(7209);require(!tool.retainedDraft()&&GetFocus()==browser.control(7208),"Apply did not retain the numeric field focus");
  browser.onApply=[&](Json params){browser.edit(7208,L"-");auto result=settings("m3");result["latencyMS"]=params["latencyMS"];return result;};browser.edit(7208,L"8");browser.click(7209);
  require(tool.retainedDraft()&&text(browser.control(7208))==L"-"&&tool.snapshot()["draft"]["baseRevision"]=="m-applied","Apply completion erased a newer retained draft");
  tool.show();require(GetFocus()==browser.control(7208),"Raising MIDI window replaced raw field focus");
  tool.hide();tool.update({{"settings",settings("m4")}});require(!tool.visible()&&text(browser.control(7208))==L"-","Hidden update reopened window or erased draft");
  SetActiveWindow(owner.window);SetFocus(owner.edit);tool.update({{"devices",devices()}});require(GetFocus()==owner.edit,"Background MIDI enumeration stole focus");
  const auto before=tool.snapshot();auto invalid=settings();invalid["latencyMS"]=501;rejected([&]{tool.update({{"settings",invalid}});},"Invalid MIDI settings accepted");require(tool.snapshot()==before,"Invalid snapshot partly published");
}
void reviewSelection(Owner &owner){
  Browser browser(owner.window);auto &tool=browser.tool;tool.update(initial());tool.show();const auto a=event(3),b=event(7,65);tool.update({{"recording",fullTake("take-a",Json::array({a,b}))}});
  require(ListView_GetItemCount(browser.control(7211))==2&&tool.snapshot()["selectedEvent"]==a,"First take review missing native selection");
  require(browser.cell(0,0)==L"Verse"&&browser.cell(0,1)==L"CH 2"&&browser.cell(0,2)==L"3 + 123/65536"&&browser.cell(0,3)==L"C-5"&&browser.cell(0,4)==L"Piano","Take review columns have wrong musical values");
  browser.select(1);SetFocus(browser.control(7211));auto renamed=b;renamed["patternLabel"]="Verse renamed";tool.update({{"recording",fullTake("take-a",Json::array({renamed,a}))}});
  require(tool.snapshot()["selectedEvent"]==renamed&&ListView_GetNextItem(browser.control(7211),-1,LVNI_SELECTED)==0&&GetFocus()==browser.control(7211),"Review refresh changed selected event identity or focus");
  tool.update({{"recording",take("take-a",5,true)}});require(tool.snapshot()["reviewedCount"]==2&&tool.snapshot()["selectedEvent"]==renamed&&text(browser.control(7310)).find(L"Reviewed 2 of 5")!=std::wstring::npos,"Growing compact summary erased review or hid review age");
  browser.key(7211,VK_RETURN);require(browser.navigations.size()==1&&browser.navigations[0]["take"]=="take-a"&&browser.navigations[0]["event"]==renamed,"Enter did not navigate exact captured event");
  auto before=tool.snapshot(),invalid=fullTake("take-a",Json::array({a,b}));invalid["events"][1]["velocity"]=0;rejected([&]{tool.update({{"recording",invalid}});},"Invalid event accepted");require(tool.snapshot()==before,"Invalid review partially replaced rows");
  tool.update({{"recording",fullTake("take-a",Json::array({a}))}});require(tool.snapshot()["selectedEvent"].is_null()&&!IsWindowEnabled(browser.control(7215)),"Removed selected event silently redirected navigation");
  tool.update({{"recording",take("take-b",8)}});require(tool.snapshot()["reviewedCount"]==0&&ListView_GetItemCount(browser.control(7211))==0,"New take retained old event rows");
  tool.update({{"recording",fullTake("take-b",Json::array({a,b,a}))}});browser.select(2);tool.update({{"recording",fullTake("take-b",Json::array({b,a,a}))}});
  require(ListView_GetNextItem(browser.control(7211),-1,LVNI_SELECTED)==2,"Duplicate review event lost its occurrence selection");
  tool.update({{"recording",take("take-b",0)}});require(tool.snapshot()["reviewedCount"]==0&&!IsWindowEnabled(browser.control(7215)),"Empty full review failed to clear retained events");
  Json many=Json::array();for(unsigned i=0;i<65536;++i)many.push_back(event(i%32768));tool.update({{"recording",fullTake("large",std::move(many))}});
  require(ListView_GetItemCount(browser.control(7211))==65536&&(GetWindowLongPtrW(browser.control(7211),GWL_STYLE)&LVS_OWNERDATA),"Maximum review is not a bounded virtual list");require(!browser.cell(65535,2).empty(),"Last virtual review event unavailable");
}
void callbacksAndKeyboard(Owner &owner){
  Browser browser(owner.window);auto &tool=browser.tool;tool.update(initial());tool.show();SetActiveWindow(tool.window());const auto row=event(2);tool.update({{"recording",fullTake("old",Json::array({row}))}});
  unsigned forwarded=0;tool.workspaceKeys([&](WPARAM,bool){++forwarded;return true;});browser.key(7203,VK_SPACE);require(tool.retainedDraft()&&tool.snapshot()["draft"]["armed"]==true&&IsWindowEnabled(browser.control(7209))&&forwarded==0,"Space on Arm escaped local ownership");
  browser.key(7208,VK_RETURN,true);require(browser.applies.size()==1&&!tool.retainedDraft()&&forwarded==0,"Ctrl+Enter did not apply MIDI settings locally");
  browser.onFinish=[&](std::string id){require(id=="old"&&tool.snapshot()["actionTake"]=="old"&&tool.snapshot()["pending"].get<bool>(),"Finish did not capture original take");browser.click(7212);browser.click(7213);browser.click(7214);require(browser.finishes.size()==1&&browser.discards.empty()&&browser.reviews.empty(),"Pending take operation reentered destructive callback");browser.click(7215);require(browser.navigations.size()==1&&tool.snapshot()["actionTake"]=="old","Navigation during Finish lost outer callback ownership");tool.update({{"recording",take("new",0)}});};
  browser.key(7211,VK_RETURN,true,true);require(browser.finishes==std::vector<std::string>{"old"}&&!tool.snapshot()["pending"].get<bool>(),"Finish completion redirected target or stayed pending");
  tool.update({{"recording",fullTake("review-old",Json::array({row}))}});browser.onReview=[&](std::string){tool.update({{"recording",take("review-new",0)}});tool.update({{"recording",fullTake("review-old",Json::array({row}))}});};browser.click(7214);
  require(tool.snapshot()["recording"]["take"]=="review-new"&&tool.snapshot()["reviewedCount"]==0&&tool.snapshot()["error"].get<std::string>().find("changed")!=std::string::npos,"Stale review callback republished an older take");
  auto incompatible=fullTake("stale",Json::array({row}));incompatible["compatible"]=false;incompatible["inputError"]="Input disconnected during the recovered performance";tool.update({{"recording",incompatible}});require(tool.snapshot()["status"]==incompatible["inputError"],"Recovered take lost its input error explanation");browser.click(7212);require(browser.finishes.size()==1&&!IsWindowEnabled(browser.control(7212))&&IsWindowEnabled(browser.control(7213))&&IsWindowEnabled(browser.control(7214)),"Incompatible take lost inspect/discard or allowed Finish");
  tool.setBusy(true);browser.click(7213);browser.click(7215);require(browser.discards.empty()&&browser.navigations.size()==2,"External busy state blocked navigation or allowed Discard");tool.setBusy(false);
  browser.onDiscard=[](std::string){throw std::runtime_error("Take changed before discard");};browser.click(7213);require(!tool.snapshot()["pending"].get<bool>()&&tool.snapshot()["error"]=="Take changed before discard","Failed take action lost error or pending guard");
  browser.onSound=[&]{SetActiveWindow(owner.window);SetFocus(owner.edit);};browser.click(7205);require(browser.sounds==1&&GetFocus()==owner.edit,"Choose sound completion stole the chosen focus");
  tool.workspaceKeys({});tool.show();browser.key(7206,VK_TAB);require(GetFocus()==browser.control(7207),"Tab skipped timing grid");browser.key(7211,VK_F5);require(browser.scans==1,"F5 did not rescan sources");browser.key(7211,VK_ESCAPE);require(!tool.visible(),"Escape did not hide MIDI window");
}
void boundsAndColumns(Owner &owner){
  Browser browser(owner.window);auto &tool=browser.tool;tool.update(initial());tool.show();const auto window=tool.window();const auto dpi=GetDpiForWindow(window);RECT frame{0,0,MulDiv(720,dpi,96),MulDiv(570,dpi,96)};
  require(AdjustWindowRectExForDpi(&frame,DWORD(GetWindowLongPtrW(window,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(window,GWL_EXSTYLE)),dpi),"Calculate MIDI minimum frame");require(SetWindowPos(window,nullptr,0,0,frame.right-frame.left,frame.bottom-frame.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE),"Resize MIDI window");
  RECT client{};GetClientRect(window,&client);require(client.right==MulDiv(720,dpi,96)&&client.bottom==MulDiv(570,dpi,96),"MIDI minimum client differs from 720 x 570 DIPs");std::vector<RECT> boxes;
  for(auto child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))if(IsWindowVisible(child)){RECT box{};GetWindowRect(child,&box);MapWindowPoints(nullptr,window,reinterpret_cast<POINT *>(&box),2);require(box.left>=0&&box.top>=0&&box.right<=client.right&&box.bottom<=client.bottom,"MIDI control outside minimum client");for(const auto &other:boxes){RECT overlap{};require(!IntersectRect(&overlap,&box,&other),"MIDI controls overlap at minimum size");}boxes.push_back(box);}
  auto list=browser.control(7211);const int custom=MulDiv(196,dpi,96);ListView_SetColumnWidth(list,0,custom);HDITEMW item{};item.mask=HDI_WIDTH;item.cxy=custom;NMHEADERW changed{};changed.hdr={ListView_GetHeader(list),0,HDN_ENDTRACKW};changed.iItem=0;changed.pitem=&item;SendMessageW(list,WM_NOTIFY,0,reinterpret_cast<LPARAM>(&changed));
  RECT outer{};GetWindowRect(window,&outer);SetWindowPos(window,nullptr,0,0,outer.right-outer.left+MulDiv(100,dpi,96),outer.bottom-outer.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);require(ListView_GetColumnWidth(list,0)==custom,"Resize replaced custom take column width");
  std::cout<<"MIDI recording minimum 720 x 570 DIPs checked at "<<dpi<<" DPI\n";
}
}
int main(){try{SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);ScreamSeq::Tests::runPrivateGui(L"ScreamSeqMidiRecordingTest",[]{HWND parent{},edit{};{Owner owner;parent=owner.window;edit=owner.edit;settingsAndFocus(owner);reviewSelection(owner);callbacksAndKeyboard(owner);boundsAndColumns(owner);owner.close();}require(!IsWindow(parent)&&!IsWindow(edit),"Destroy owned MIDI test windows");});std::cout<<"PASS MIDI recording window: retained drafts/focus, revision guards, take identity, virtual review, keyboard and bounds\n";return 0;}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
