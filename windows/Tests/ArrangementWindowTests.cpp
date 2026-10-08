#include "../App/ArrangementWindow.hpp"
#include "PrivateGuiTest.hpp"
#include <iostream>

namespace {
using Json=ScreamSeq::Api::Json;
void require(bool condition,const char *message){if(!condition)throw std::runtime_error(message);}
struct Owner {
  HWND window{},edit{};
  Owner(){WNDCLASSW kind{};kind.lpfnWndProc=DefWindowProcW;kind.hInstance=GetModuleHandleW(nullptr);kind.lpszClassName=L"ScreamSeq.Arrangement.TestOwner";RegisterClassW(&kind);
    window=CreateWindowExW(0,kind.lpszClassName,L"Arrangement test owner",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,1100,800,nullptr,nullptr,kind.hInstance,nullptr);require(window,"Create arrangement owner");ScreamSeq::Tests::ownGuiWindow(window);
    edit=CreateWindowExW(0,L"EDIT",L"Other draft",WS_CHILD|WS_VISIBLE|WS_TABSTOP,10,10,220,26,window,nullptr,kind.hInstance,nullptr);require(edit,"Create unrelated text field");ScreamSeq::Tests::ownGuiWindow(edit);}
  ~Owner(){if(window)DestroyWindow(window);}
  void close(){const auto parent=window,child=edit;require(DestroyWindow(parent),"Destroy arrangement owner");window=edit=nullptr;require(!IsWindow(parent)&&!IsWindow(child),"Owned arrangement windows survived destruction");}
};
Json state(){return {{"documentId","document-a"},{"revision","r0"},{"busy",false},{"selectedOrderID","o2"},{"document",{
  {"title","Repeated phrases"},{"editable",true},{"orders",{0,0,65534,65535,1}},
  {"orderMetadata",Json::array({{{"id","o1"}},{{"id","o2"}},{{"id","skip"}},{{"id","stop"}},{{"id","o3"}}})},
  {"patterns",Json::array({{{"index",0},{"id","p1"},{"rows",64},{"name","Verse"}},{{"index",1},{"id","p2"},{"rows",32},{"name","Chorus"}}})},
  {"sequence",0},{"sequences",Json::array({{{"index",0},{"id","s1"},{"name","Song"}},{{"index",1},{"id","s2"},{"name","Alternate"}}})},
  {"formatLimits",{{"patternRowsMin",1},{"patternRowsMax",1024},{"patternsMax",4000},{"ordersMax",4000}}}
}}};}
struct Fixture {
  Json current=state();std::vector<std::pair<std::string,Json>> writes;std::vector<std::string> selected,played;unsigned returned=0;
  std::function<Json(std::string,Json)> onOperate;
  ScreamSeq::ArrangementWindow tool;
  explicit Fixture(HWND owner):tool(owner,{[this](std::string method,Json params){return operate(std::move(method),std::move(params));},
    [this](std::string id){selected.push_back(id);current["selectedOrderID"]=id;tool.update(current);},
    [this](std::string id){played.push_back(id);},[this]{++returned;}}){ScreamSeq::Tests::ownGuiWindow(tool.window());}
  HWND control(int id)const{const auto value=GetDlgItem(tool.window(),id);require(value,"Arrangement control missing");return value;}
  void click(int id){SendMessageW(tool.window(),WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),reinterpret_cast<LPARAM>(control(id)));}
  void text(const wchar_t *value){SetWindowTextW(control(8011),value);}
  void notes(const wchar_t *value){const auto edit=control(8027);SendMessageW(edit,EM_SETSEL,0,-1);SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(value));}
  void select(int index){ListView_SetItemState(control(8001),-1,0,LVIS_SELECTED|LVIS_FOCUSED);ListView_SetItemState(control(8001),index,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);}
  void choose(int id,int index){SendMessageW(control(id),CB_SETCURSEL,index,0);SendMessageW(tool.window(),WM_COMMAND,MAKEWPARAM(id,CBN_SELCHANGE),reinterpret_cast<LPARAM>(control(id)));}
  std::wstring cell(int row,int column){wchar_t buffer[256]{};NMLVDISPINFOW info{};info.hdr={control(8001),8001,LVN_GETDISPINFOW};info.item.mask=LVIF_TEXT;info.item.iItem=row;info.item.iSubItem=column;info.item.pszText=buffer;info.item.cchTextMax=256;SendMessageW(tool.window(),WM_NOTIFY,8001,reinterpret_cast<LPARAM>(&info));return buffer;}
  Json operate(std::string method,Json params){
    writes.emplace_back(method,params);if(onOperate)return onOperate(method,params);
    require(params.at("expectedRevision")==current.at("revision"),"Request silently rebased its captured revision");auto &doc=current["document"];Json result=Json::object();
    if(method=="order.edit"){
      const auto row=params.at("order").get<int>();const auto operation=params.at("operation").get<std::string>();auto &orders=doc["orders"],&metadata=doc["orderMetadata"];
      if(operation=="up"||operation=="down"){const auto target=row+(operation=="up"?-1:1);std::swap(orders[row],orders[target]);std::swap(metadata[row],metadata[target]);}
      else if(operation=="assign")orders[row]=params.at("pattern");
      else if(operation=="before"||operation=="after"){const auto position=row+(operation=="after");const auto id="inserted"+std::to_string(writes.size());orders.insert(orders.begin()+position,params.at("pattern"));metadata.insert(metadata.begin()+position,Json{{"id",id}});current["selectedOrderID"]=id;}
      else if(operation=="remove"){orders.erase(orders.begin()+row);metadata.erase(metadata.begin()+row);current["selectedOrderID"]=metadata[std::min<size_t>(size_t(row),metadata.size()-1)]["id"];}
    }else if(method=="pattern.create"){
      const auto index=doc["patterns"].size();const auto id="created"+std::to_string(writes.size());doc["patterns"].push_back({{"index",index},{"id","p"+id},{"rows",params.at("rows")},{"name",""}});doc["orders"].push_back(index);doc["orderMetadata"].push_back({{"id",id}});current["selectedOrderID"]=id;result["pattern"]=index;
    }else if(method=="sequence.select")doc["sequence"]=params.at("sequence");
    else if(method=="song.annotate"){
      Json *entity=nullptr;for(auto &value:doc["orderMetadata"])if(value.at("id")==params.at("id"))entity=&value;
      for(auto &value:doc["patterns"])if(value.at("id")==params.at("id"))entity=&value;
      require(entity,"Annotation target missing");for(const char *field:{"name","annotation","color"})if(params.contains(field))(*entity)[field]=params.at(field);result=*entity;
    }
    current["revision"]="r"+std::to_string(writes.size());return {{"state",current},{"result",result}};
  }
};
void stableOrders(Owner &owner){
  Fixture f(owner.window);f.tool.open(f.current);require(ListView_GetNextItem(f.control(8001),-1,LVNI_SELECTED)==1,"Repeated occurrence was selected by pattern rather than ID");
  require(f.cell(2,1)==L"+++ Skip"&&f.cell(3,1)==L"— Stop"&&f.cell(4,1)==L"1 · Chorus","Sentinel rows or orders after Stop were discarded");
  f.click(8007);require(f.writes.back().second.at("order")==1&&f.tool.snapshot()["selectedOrderID"]=="o2","Move did not use and retain selected occurrence");require(ListView_GetNextItem(f.control(8001),-1,LVNI_SELECTED)==0,"Equal-pattern move failed to refresh stable IDs");
  f.click(8008);f.choose(8003,1);f.click(8004);require(f.current["document"]["orders"][1]==1&&f.current["document"]["orderMetadata"][1]["id"]=="o2","Assign redirected or replaced an order identity");
  f.click(8005);require(f.tool.snapshot()["selectedOrderID"]=="inserted4","Insert completion did not adopt explicit new occurrence");
  f.click(8009);require(f.tool.snapshot()["selectedOrderID"]=="o2","Remove failed to choose the returned surviving occurrence");
  f.select(2);require(f.selected.back()=="skip"&&!IsWindowEnabled(f.control(8010)),"Skip could not be selected or allowed playback");f.click(8010);require(f.played.empty(),"Skip attempted selected-order playback");
  f.select(1);f.click(8010);require(f.played==std::vector<std::string>{"o2"},"Play selected lost occurrence identity");
  const auto writes=f.writes.size();SetFocus(f.control(8011));SendMessageW(f.control(8011),WM_KEYDOWN,VK_DELETE,0);require(f.writes.size()==writes,"Delete in row text removed an order");
  f.current["busy"]=true;f.tool.update(f.current);f.click(8009);f.click(8010);require(f.writes.size()==writes&&f.played.size()==1,"Busy tool activated a musical operation");f.current["busy"]=false;f.tool.update(f.current);
  SetFocus(f.control(8001));SendMessageW(f.control(8001),WM_KEYDOWN,VK_RETURN,0);require(f.selected.back()=="o2","Enter failed deliberate selected-order navigation");
  SendMessageW(f.control(8001),WM_KEYDOWN,VK_F6,0);require(f.returned==1,"F6 did not return to pattern");
  // NativeToolWindow forwards keys before imposing visibility/focus ownership.
  // Direct queued messages must not activate a hidden or unfocused editor.
  SetActiveWindow(owner.window);SetFocus(owner.edit);const auto inactiveWrites=f.writes.size();const auto inactiveReturns=f.returned;
  SendMessageW(f.tool.window(),WM_KEYDOWN,VK_DELETE,0);SendMessageW(f.tool.window(),WM_KEYDOWN,VK_F6,0);
  require(f.writes.size()==inactiveWrites&&f.returned==inactiveReturns,"Unfocused parent message activated a local command");
  f.tool.hide();SendMessageW(f.control(8001),WM_KEYDOWN,VK_DELETE,0);SendMessageW(f.tool.window(),WM_KEYDOWN,VK_F6,0);
  require(f.writes.size()==inactiveWrites&&f.returned==inactiveReturns,"Hidden editor handled a queued local key");f.tool.open(f.current);
  auto modified=[&](WPARAM key,int modifier){BYTE previous[256]{},next[256]{};require(GetKeyboardState(previous),"Read private test keyboard state");std::copy(std::begin(previous),std::end(previous),std::begin(next));next[modifier]|=0x80;require(SetKeyboardState(next),"Set private test modifier");SetActiveWindow(f.tool.window());SetFocus(f.control(8001));SendMessageW(f.control(8001),WM_KEYDOWN,key,0);require(SetKeyboardState(previous),"Restore private test modifier");};
  modified(VK_F6,VK_CONTROL);modified(VK_ESCAPE,VK_MENU);modified(VK_DELETE,VK_SHIFT);
  require(f.tool.visible()&&f.writes.size()==inactiveWrites&&f.returned==inactiveReturns,"Modified shortcut was intercepted as an unmodified arrangement action");
}
void draftsAndPumpedCompletion(Owner &owner){
  Fixture f(owner.window);f.tool.update(f.current);f.tool.open(f.current,"duplicate","p2");require(f.tool.snapshot()["sourcePatternID"]=="p2"&&f.tool.snapshot()["rowsText"]=="32","First Duplicate opening after a background update did not capture source and rows");
  SetActiveWindow(f.tool.window());SetFocus(f.control(8011));f.text(L"invalid");SendMessageW(f.control(8011),EM_SETSEL,1,4);
  f.tool.open(f.current,"new","p1");DWORD first=0,last=0;SendMessageW(f.control(8011),EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));require(f.tool.snapshot()["rowsText"]=="invalid"&&f.tool.snapshot()["sourcePatternID"]=="p2"&&GetFocus()==f.control(8011)&&first==1&&last==4,"Reopen replaced raw draft, source or caret");
  f.click(8014);require(f.writes.empty()&&!f.tool.snapshot()["error"].get<std::string>().empty(),"Invalid draft reached the worker");
  f.current["revision"]="external";f.tool.update(f.current);require(f.tool.snapshot()["stale"].get<bool>()&&f.tool.snapshot()["rowsText"]=="invalid","External edit rewrote a stale raw draft");
  f.click(8014);require(f.writes.empty(),"Stale creation bypassed its captured revision");f.click(8015);require(!f.tool.snapshot()["stale"].get<bool>()&&f.tool.snapshot()["rowsText"]=="32","Explicit Reload did not capture current target");
  f.text(L"48");f.click(8014);require(f.writes.size()==1&&f.writes[0].first=="pattern.create"&&f.writes[0].second.at("source")==1&&f.writes[0].second.at("rows")==48,"Duplicate request did not carry full captured fields");require(!f.tool.snapshot()["stale"].get<bool>(),"Successful unchanged creation draft was not advanced to its own result");
  f.onOperate=[&](std::string,Json){require(f.tool.snapshot()["pending"].get<bool>(),"Pending operation was not visible during pumped callback");const auto count=f.writes.size();f.click(8013);require(f.writes.size()==count,"Pumped callback started a duplicate write");f.text(L"0017");auto response=f.current;response["revision"]="pumped";f.current=response;return Json{{"state",response},{"result",Json::object()}};};
  f.click(8013);require(f.tool.snapshot()["rowsText"]=="0017"&&f.tool.snapshot()["stale"].get<bool>()&&f.tool.snapshot()["dirty"].get<bool>(),"Completion overwrote or rebased a newer draft");
  f.click(8015);const auto oldDocument=f.current;f.onOperate=[&](std::string,Json){auto replacement=state();replacement["documentId"]="replacement";replacement["revision"]="replacement-revision";replacement["selectedOrderID"]="o3";f.current=replacement;f.tool.update(replacement);auto stale=oldDocument;stale["revision"]="old-completion";return Json{{"state",stale},{"result",Json::object()}};};
  f.click(8013);require(f.tool.snapshot()["documentId"]=="replacement"&&f.tool.snapshot()["selectedOrderID"]=="o3"&&!f.tool.snapshot()["pending"].get<bool>(),"Old-document completion replaced the newer song or remained pending");
  f.tool.hide();const auto draft=f.tool.snapshot()["rowsText"];f.tool.open(f.current,"duplicate","p1");require(f.tool.snapshot()["rowsText"]==draft&&f.tool.snapshot()["stale"].get<bool>(),"Hidden reopen discarded a retained stale draft");
  auto invalid=f.current;invalid["document"]["orderMetadata"][1]["id"]="o1";const auto snapshot=f.tool.snapshot();bool rejected=false;try{f.tool.update(invalid);}catch(const std::exception &){rejected=true;}require(rejected&&f.tool.snapshot()==snapshot,"Rejected duplicate identity partially updated the tool");
  const auto returned=f.returned;SetActiveWindow(f.tool.window());SetFocus(f.control(8011));SendMessageW(f.tool.window(),WM_CLOSE,0,0);require(!f.tool.visible()&&f.returned==returned+1,"Native title-bar close did not return through the visible-pattern callback");
}
void boundsScrollAndLimits(Owner &owner){
  Fixture f(owner.window);f.tool.open(f.current);const auto window=f.tool.window();const auto dpi=GetDpiForWindow(window);MINMAXINFO minimum{};SendMessageW(window,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&minimum));
  SetWindowPos(window,nullptr,0,0,minimum.ptMinTrackSize.x,minimum.ptMinTrackSize.y,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);RECT client{};GetClientRect(window,&client);require(client.right==MulDiv(760,dpi,96)&&client.bottom==MulDiv(600,dpi,96),"Arrangement minimum is not a 760 x 600 client");
  std::vector<RECT> boxes;for(HWND child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){if(!IsWindowVisible(child))continue;RECT box{};GetWindowRect(child,&box);MapWindowPoints(nullptr,window,reinterpret_cast<POINT *>(&box),2);require(box.right>box.left&&box.bottom>box.top&&box.left>=0&&box.top>=0&&box.right<=client.right&&box.bottom<=client.bottom,"Arrangement control outside minimum client");for(const auto &other:boxes){RECT overlap{};require(!IntersectRect(&overlap,&box,&other),"Arrangement native controls overlap at minimum");}boxes.push_back(box);}
  auto many=f.current;many["document"]["orders"]=Json::array();many["document"]["orderMetadata"]=Json::array();for(int i=0;i<300;++i){many["document"]["orders"].push_back(i%2);many["document"]["orderMetadata"].push_back({{"id","large"+std::to_string(i)}});}many["selectedOrderID"]="large200";many["revision"]="large";f.current=many;f.tool.update(many);
  ListView_EnsureVisible(f.control(8001),200,FALSE);const auto top=ListView_GetTopIndex(f.control(8001));SetActiveWindow(owner.window);SetFocus(owner.edit);f.tool.update(many);require(ListView_GetTopIndex(f.control(8001))==top&&GetFocus()==owner.edit,"Polling reset large-list scroll or stole unrelated focus");
  const auto custom=MulDiv(104,dpi,96);ListView_SetColumnWidth(f.control(8001),0,custom);HDITEMW item{};item.mask=HDI_WIDTH;item.cxy=custom;NMHEADERW notification{};notification.hdr={ListView_GetHeader(f.control(8001)),0,HDN_ENDTRACKW};notification.iItem=0;notification.pitem=&item;SendMessageW(f.control(8001),WM_NOTIFY,0,reinterpret_cast<LPARAM>(&notification));
  RECT outer{};GetWindowRect(window,&outer);SetWindowPos(window,nullptr,0,0,outer.right-outer.left+MulDiv(80,dpi,96),outer.bottom-outer.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);require(ListView_GetColumnWidth(f.control(8001),0)==custom,"Resize discarded the user's order column width");
  f.current["document"]["formatLimits"]={{"patternRowsMin",64},{"patternRowsMax",64},{"patternsMax",2},{"ordersMax",300}};f.tool.update(f.current);f.click(8015);require(f.tool.snapshot()["rowsText"]=="64"&&!IsWindowEnabled(f.control(8013))&&!IsWindowEnabled(f.control(8005)),"Fixed row or full slot limits were ignored");
  f.current["document"]["formatLimits"]["patternsMax"]=3;f.current["document"]["formatLimits"]["ordersMax"]=301;f.current["document"]["patterns"][1]["index"]=2;f.tool.update(f.current);require(IsWindowEnabled(f.control(8013)),"Reusable hole was mistaken for a full pattern catalog");
  f.current["document"]["editable"]=false;f.tool.update(f.current);const auto count=f.writes.size();f.click(8009);f.click(8013);require(f.writes.size()==count,"Read-only inspection allowed an edit");
  const auto themedList=f.control(8001);const auto retainedFocus=GetFocus();const int retainedSelection=ListView_GetNextItem(themedList,-1,LVNI_SELECTED),retainedTop=ListView_GetTopIndex(themedList),retainedWidth=ListView_GetColumnWidth(themedList,0);
  for(UINT message:{WM_THEMECHANGED,WM_SETTINGCHANGE,WM_SYSCOLORCHANGE}){
    ListView_SetBkColor(themedList,RGB(250,0,250));SendMessageW(window,message,0,0);
    require(ListView_GetBkColor(themedList)==ScreamSeq::NativeReportList::palette(ScreamSeq::NativeControls::highContrast()).background,"Top-level theme message did not refresh report colors");
    require(GetFocus()==retainedFocus&&ListView_GetNextItem(themedList,-1,LVNI_SELECTED)==retainedSelection&&ListView_GetTopIndex(themedList)==retainedTop&&ListView_GetColumnWidth(themedList,0)==retainedWidth,"Theme refresh changed native report focus/selection/scroll/column width");
  }
  std::cout<<"Arrange orders minimum 760 x 600 DIPs checked at "<<dpi<<" DPI\n";
}
void firstOpenPlacement(Owner &owner){
  MONITORINFO monitor{sizeof(monitor)};require(GetMonitorInfoW(MonitorFromWindow(owner.window,MONITOR_DEFAULTTONEAREST),&monitor),"Read owner work area");
  RECT original{};require(GetWindowRect(owner.window,&original),"Read owner bounds");const auto &work=monitor.rcWork;
  require(SetWindowPos(owner.window,nullptr,work.right-80,work.bottom-80,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE),"Move owner near work-area edge");
  Fixture f(owner.window);require(SetWindowPos(f.tool.window(),nullptr,work.right-20,work.bottom-20,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE),"Place unopened tool past edge");
  f.tool.open(f.current);RECT fitted{};require(GetWindowRect(f.tool.window(),&fitted),"Read first-open bounds");
  require(fitted.left>=work.left&&fitted.top>=work.top,"First open remained outside owner work area");
  if(fitted.right-fitted.left<=work.right-work.left)require(fitted.right<=work.right,"First open extends past right work-area edge");
  if(fitted.bottom-fitted.top<=work.bottom-work.top)require(fitted.bottom<=work.bottom,"First open extends past lower work-area edge");
  require(SetWindowPos(f.tool.window(),nullptr,work.left+23,work.top+29,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE),"Set retained tool placement");
  RECT chosen{},reopened{};require(GetWindowRect(f.tool.window(),&chosen),"Read retained placement");f.tool.hide();f.tool.open(f.current);require(GetWindowRect(f.tool.window(),&reopened)&&EqualRect(&chosen,&reopened),"Reopen reset the user's window placement");
  require(SetWindowPos(owner.window,nullptr,original.left,original.top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE),"Restore owned fixture placement");
}
void pumpedHostSelectionReveal(Owner &owner){
  Fixture f(owner.window);auto &doc=f.current["document"];doc["orders"]=Json::array();doc["orderMetadata"]=Json::array();
  for(unsigned i=0;i<300;++i){doc["orders"].push_back(i%2);doc["orderMetadata"].push_back({{"id","first"+std::to_string(i)}});}
  f.current["selectedOrderID"]="first220";f.tool.open(f.current);ListView_EnsureVisible(f.control(8001),220,FALSE);require(ListView_GetTopIndex(f.control(8001))>0,"Long-list fixture did not scroll");
  f.onOperate=[&](std::string method,Json params){
    require(method=="sequence.select"&&params.at("sequence")==1,"Wrong captured sequence request");auto published=f.current;published["revision"]="published-sequence";published["document"]["sequence"]=1;published["selectedOrderID"]="second0";
    for(unsigned i=0;i<300;++i)published["document"]["orderMetadata"][i]["id"]="second"+std::to_string(i);
    // The real host publishes the result while its worker wait pumps messages.
    // This advances the tool's selection generation before operate returns.
    f.current=published;f.tool.update(published);require(f.tool.snapshot()["selectedOrderID"]=="second0","Host publication lost its selected identity");
    return Json{{"state",published},{"result",Json::object()}};
  };
  f.choose(8002,1);require(f.tool.snapshot()["selectedOrderID"]=="second0"&&ListView_GetNextItem(f.control(8001),-1,LVNI_SELECTED)==0,"Sequence completion lost the host-published order identity");
  require(ListView_GetTopIndex(f.control(8001))==0,"Accepted sequence command left its new selected occurrence offscreen");
  ListView_EnsureVisible(f.control(8001),220,FALSE);const auto scrolled=ListView_GetTopIndex(f.control(8001));f.tool.update(f.current);require(ListView_GetTopIndex(f.control(8001))==scrolled,"Ordinary publication reset deliberate user scroll");
}
void sectionNavigationAndDetails(Owner &owner){
  Fixture f(owner.window);auto &metadata=f.current["document"]["orderMetadata"];
  metadata[0]["name"]="Intro";metadata[2]["name"]=" ";metadata[3]["name"]="Ending";metadata[1]["annotation"]="Preserve order notes";metadata[1]["color"]=123456;
  f.current["document"]["patterns"][0]["annotation"]="Shared notes";f.current["document"]["patterns"][0]["color"]=4321;
  f.tool.open(f.current);require(Header_GetItemCount(ListView_GetHeader(f.control(8001)))==4&&f.cell(0,3)==L"Intro"&&f.cell(2,3)==L" ","Section column changed whitespace or lost native markers");
  f.click(8018);require(f.selected.back()=="o1"&&!IsWindowEnabled(f.control(8018)),"Previous section did not stop at first strict marker");
  f.click(8019);require(f.selected.back()=="skip","Next section ignored named Skip or whitespace marker");f.click(8019);require(f.selected.back()=="stop"&&!IsWindowEnabled(f.control(8019)),"Next section wrapped or ignored named Stop");
  const auto navigations=f.selected.size();f.click(8019);require(f.selected.size()==navigations&&f.played.empty()&&f.writes.empty(),"Section endpoint or navigation altered playback/song");
  f.select(1);f.click(8021);require(f.tool.snapshot()["sectionDraft"]["targetID"]=="o2","Section first visit did not capture repeated order identity");
  const auto sectionLabel=f.tool.snapshot()["sectionDraft"]["targetLabel"];require(sectionLabel.get<std::string>().find("Order 1")!=std::string::npos&&sectionLabel.get<std::string>().find("Verse")!=std::string::npos,"Captured order label lacks recognizable occurrence/pattern");SetWindowTextW(f.control(8023),L"Second occurrence");f.select(0);require(f.tool.snapshot()["sectionDraft"]["targetLabel"]==sectionLabel,"Selection rewrote the captured human label");require(f.tool.snapshot()["sectionDraft"]["selectionDiffers"].get<bool>()&&f.tool.snapshot()["sectionDraft"]["targetID"]=="o2","Selection redirected section draft");
  f.click(8024);require(f.writes.back().first=="song.annotate"&&f.writes.back().second.at("id")=="o2"&&f.writes.back().second.size()==3,"Set section patched the wrong entity or unrelated fields");
  require(metadata[1]["name"]=="Second occurrence"&&metadata[1]["annotation"]=="Preserve order notes"&&metadata[1]["color"]==123456&&f.tool.snapshot()["selectedOrderID"]=="o1","Section apply replaced unrelated fields or moved selection");
  require(!f.tool.snapshot()["sectionDraft"]["stale"].get<bool>()&&f.tool.snapshot()["sectionDraft"]["selectionDiffers"].get<bool>(),"Successful section apply silently retargeted its draft");
  f.click(8022);require(f.tool.snapshot()["patternDraft"]["targetID"]=="p1"&&f.tool.snapshot()["patternDraft"]["annotationText"]=="Shared notes","Pattern page did not capture shared pattern details");
  SetWindowTextW(f.control(8026),L"Shared phrase");f.notes(L"Notes across every occurrence\r\n音色 — café");f.select(4);
  f.click(8028);require(f.writes.back().second.at("id")=="p1"&&f.writes.back().second.size()==4,"Save details targeted new selection or overwrote color");
  require(f.current["document"]["patterns"][0]["color"]==4321&&f.cell(0,1)==L"0 · Shared phrase"&&f.cell(1,1)==L"0 · Shared phrase","Pattern metadata did not refresh all repeated occurrences");
  f.click(8021);require(f.tool.snapshot()["sectionDraft"]["stale"].get<bool>()&&f.tool.snapshot()["sectionDraft"]["nameText"]=="Second occurrence","Independent section draft was rebased by pattern apply");
  f.select(3);f.click(8025);SetWindowTextW(f.control(8023),L"");f.click(8024);require(f.current["document"]["orderMetadata"][3]["name"]=="","Section name clearing on Stop failed");
  f.click(8022);f.click(8029);require(!f.tool.snapshot()["patternDraft"]["bound"].get<bool>()&&!IsWindowEnabled(f.control(8028))&&!IsWindowEnabled(f.control(8027)),"Stop acquired nonexistent pattern details");
  const auto writes=f.writes.size();f.click(8028);require(f.writes.size()==writes,"Unbound pattern page submitted an edit");
  f.click(8020);require(IsWindowVisible(f.control(8011))&&f.tool.snapshot()["rowsText"]=="64","Details pages lost the independent creation draft");
}
void retainedAnnotationDrafts(Owner &owner){
  Fixture f(owner.window);f.tool.open(f.current,"pattern");
  std::wstring longNotes;for(int i=0;i<70;++i)longNotes+=L"音色 café \U0001f3b5 retained line\r\n";
  f.notes(longNotes.c_str());SetActiveWindow(f.tool.window());SetFocus(f.control(8027));SendMessageW(f.control(8027),EM_SETSEL,47,62);SendMessageW(f.control(8027),EM_LINESCROLL,0,35);
  const auto top=SendMessageW(f.control(8027),EM_GETFIRSTVISIBLELINE,0,0);require(top>0,"Multiline retention fixture did not scroll");
  f.click(8021);SetWindowTextW(f.control(8023),L"Independent section");f.select(4);f.tool.update(f.current);f.click(8022);
  DWORD first=0,last=0;SendMessageW(f.control(8027),EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));
  require(f.tool.snapshot()["patternDraft"]["targetID"]=="p1"&&f.tool.snapshot()["patternDraft"]["nameText"]=="Verse"&&first==47&&last==62&&SendMessageW(f.control(8027),EM_GETFIRSTVISIBLELINE,0,0)==top&&GetFocus()==f.control(8027),"Page/selection/polling lost captured pattern, caret, scroll or focus");
  const auto raw=f.tool.snapshot()["patternDraft"]["annotationText"];f.tool.hide();f.tool.open(f.current,"pattern");require(f.tool.snapshot()["patternDraft"]["annotationText"]==raw&&GetFocus()==f.control(8027),"Hidden reopen reset multiline draft/focus");
  RECT rect{};GetWindowRect(f.tool.window(),&rect);SetWindowPos(f.tool.window(),nullptr,0,0,rect.right-rect.left+20,rect.bottom-rect.top+20,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
  SendMessageW(f.control(8027),EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));require(first==47&&last==62&&f.tool.snapshot()["patternDraft"]["annotationText"]==raw,"Resize changed UTF-16 notes/caret");
  f.current["revision"]="outside";f.tool.update(f.current);require(f.tool.snapshot()["patternDraft"]["stale"].get<bool>()&&!IsWindowEnabled(f.control(8028)),"External revision did not disable stale details apply");
  const auto writes=f.writes.size();f.click(8028);require(f.writes.size()==writes&&f.tool.snapshot()["patternDraft"]["annotationText"]==raw,"Stale apply submitted or erased text");
  f.click(8029);require(f.tool.snapshot()["patternDraft"]["targetID"]=="p2"&&f.tool.snapshot()["patternDraft"]["nameText"]=="Chorus"&&!f.tool.snapshot()["patternDraft"]["stale"].get<bool>(),"Explicit Reload did not capture selected pattern");
  f.onOperate=[&](std::string method,Json params){require(method=="song.annotate"&&params.at("id")=="p2","Wrong pumped annotation target");const auto generation=f.tool.snapshot()["patternDraft"]["generation"].get<uint64_t>();f.notes(L"Newer unsent notes");require(f.tool.snapshot()["patternDraft"]["generation"].get<uint64_t>()>generation,"Native multiline replacement did not emit an edit notification");auto published=f.current;published["revision"]="accepted";published["document"]["patterns"][1]["annotation"]=params.at("annotation");f.current=published;f.tool.update(published);return Json{{"state",published},{"result",published["document"]["patterns"][1]}};};
  f.notes(L"Submitted notes");f.click(8028);require(f.tool.snapshot()["patternDraft"]["annotationText"]=="Newer unsent notes"&&f.tool.snapshot()["patternDraft"]["dirty"].get<bool>()&&f.tool.snapshot()["patternDraft"]["stale"].get<bool>(),"Pumped completion overwrote or rebased newer raw notes");
  f.click(8029);const auto oldLabel=f.tool.snapshot()["patternDraft"]["targetLabel"];const auto old=f.current;f.onOperate=[&](std::string,Json){auto replacement=state();replacement["documentId"]="replacement";replacement["revision"]="fresh";f.current=replacement;f.tool.update(replacement);auto completed=old;completed["revision"]="old-completion";return Json{{"state",completed},{"result",old["document"]["patterns"][1]}};};
  f.click(8028);require(f.tool.snapshot()["documentId"]=="replacement"&&f.tool.snapshot()["patternDraft"]["documentId"]=="document-a"&&f.tool.snapshot()["patternDraft"]["targetLabel"]==oldLabel&&f.tool.snapshot()["patternDraft"]["stale"].get<bool>()&&!f.tool.snapshot()["pending"].get<bool>(),"Old-document annotation completion replaced the new song or retargeted retained details");
}
void annotationBoundsAndKeys(Owner &owner){
  Fixture f(owner.window);f.tool.open(f.current);MINMAXINFO minimum{};SendMessageW(f.tool.window(),WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&minimum));SetWindowPos(f.tool.window(),nullptr,0,0,minimum.ptMinTrackSize.x,minimum.ptMinTrackSize.y,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
  RECT client{};GetClientRect(f.tool.window(),&client);
  for(int page:{8020,8021,8022}){f.click(page);std::vector<RECT> boxes;for(HWND child=GetWindow(f.tool.window(),GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){if(!IsWindowVisible(child))continue;RECT box{};GetWindowRect(child,&box);MapWindowPoints(nullptr,f.tool.window(),reinterpret_cast<POINT *>(&box),2);require(box.left>=0&&box.top>=0&&box.right<=client.right&&box.bottom<=client.bottom&&box.right>box.left&&box.bottom>box.top,"Annotation page control escapes minimum client");for(const auto &other:boxes){RECT overlap{};require(!IntersectRect(&overlap,&box,&other),"Annotation page controls overlap at minimum");}boxes.push_back(box);}}
  SetActiveWindow(f.tool.window());SetFocus(f.control(8027));SendMessageW(f.control(8027),WM_KEYDOWN,VK_DELETE,0);require(f.writes.empty(),"Delete in pattern notes removed an order");
  f.click(8021);f.click(8028);require(f.writes.empty(),"Hidden pattern Apply was interactive");
  SetFocus(f.control(8023));BYTE previous[256]{},modified[256]{};require(GetKeyboardState(previous),"Read annotation keyboard state");std::copy(std::begin(previous),std::end(previous),std::begin(modified));modified[VK_CONTROL]|=0x80;require(SetKeyboardState(modified),"Set annotation Ctrl key");SetWindowTextW(f.control(8023),L"Keyboard section");SendMessageW(f.control(8023),WM_KEYDOWN,VK_RETURN,0);require(SetKeyboardState(previous),"Restore annotation keyboard state");require(f.writes.size()==1&&f.writes.back().first=="song.annotate","Ctrl+Enter did not use active section action");
  const auto returned=f.returned;SendMessageW(f.control(8023),WM_KEYDOWN,VK_F6,0);require(f.returned==returned+1,"Detail field F6 did not return to pattern");
  f.current["document"]["editable"]=false;f.tool.update(f.current);f.click(8025);SetWindowTextW(f.control(8023),L"Read-only draft");f.click(8024);require(f.writes.size()==1,"Read-only details applied annotation");
}
}
int main(){try{SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);ScreamSeq::Tests::runPrivateGui(L"ScreamSeqArrangementTest",[]{HWND parent{},edit{};{Owner owner;parent=owner.window;edit=owner.edit;stableOrders(owner);draftsAndPumpedCompletion(owner);boundsScrollAndLimits(owner);firstOpenPlacement(owner);pumpedHostSelectionReveal(owner);sectionNavigationAndDetails(owner);retainedAnnotationDrafts(owner);annotationBoundsAndKeys(owner);owner.close();}require(!IsWindow(parent)&&!IsWindow(edit),"Destroy owned arrangement test windows");});std::cout<<"PASS arrangement window: repeated occurrence IDs, sentinels, independent annotation/creation drafts, section navigation, pumped completion guards, native keys, UTF-16 notes, scroll, bounds and first-open placement\n";return 0;}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
