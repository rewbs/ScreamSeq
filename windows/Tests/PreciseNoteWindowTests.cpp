// Standalone private-process fixture; no audio, application or external MIDI.
// Observable sole-owner tests; the request double is not a document/Undo oracle.
#include "PreciseNoteWindow.hpp"
#include "PrivateGuiProcessTest.hpp"
#include <iostream>
#include <map>
#include <set>

namespace {
using Json=ScreamSeq::Api::Json;
using Tool=ScreamSeq::PreciseNoteWindow;
constexpr unsigned units=65536;
void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
template<class Action>void rejected(Action action,const char *message){try{action();}catch(const std::exception &){return;}throw std::runtime_error(message);}
struct Owner {
  HWND window{},edit{};
  Owner(){
    WNDCLASSW type{};type.lpfnWndProc=DefWindowProcW;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"ScreamSeq.PreciseNoteOwner.Test";RegisterClassW(&type);
    window=CreateWindowExW(0,type.lpszClassName,L"Owned precise-note fixture",WS_OVERLAPPEDWINDOW|WS_VISIBLE|WS_CLIPCHILDREN,0,0,1200,900,nullptr,nullptr,type.hInstance,nullptr);
    require(window,"Create precise-note fixture owner");ScreamSeq::Tests::ownGuiWindow(window);
    edit=CreateWindowExW(0,L"EDIT",L"Unrelated retained text",WS_CHILD|WS_VISIBLE|WS_TABSTOP,8,8,260,26,window,nullptr,type.hInstance,nullptr);
    require(edit,"Create unrelated native edit");ScreamSeq::Tests::ownGuiWindow(edit);
  }
  ~Owner(){if(window)DestroyWindow(window);}
  void focus(){SetActiveWindow(window);SetFocus(edit);require(GetFocus()==edit,"Focus unrelated native field");}
  void close(){const auto parent=window,child=edit;require(DestroyWindow(window),"Destroy precise-note fixture owner");window=edit=nullptr;require(!IsWindow(parent)&&!IsWindow(child),"Fixture HWND survived explicit teardown");}
};
void sizeClient(HWND window,int width,int height){
  const auto dpi=GetDpiForWindow(window);RECT frame{0,0,MulDiv(width,dpi,96),MulDiv(height,dpi,96)};
  require(AdjustWindowRectExForDpi(&frame,DWORD(GetWindowLongPtrW(window,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(window,GWL_EXSTYLE)),dpi),"Calculate precise-note frame");
  require(SetWindowPos(window,nullptr,0,0,frame.right-frame.left,frame.bottom-frame.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE),"Resize precise-note client");
}
std::wstring text(HWND window){std::wstring value(size_t(GetWindowTextLengthW(window))+1,0);GetWindowTextW(window,value.data(),int(value.size()));value.resize(wcslen(value.c_str()));return value;}
std::pair<DWORD,DWORD> caret(HWND window){DWORD first=0,last=0;SendMessageW(window,EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));return {first,last};}
void focus(HWND window){SetActiveWindow(GetAncestor(window,GA_ROOT));SetFocus(window);require(GetFocus()==window,"Focus owned precise-note HWND");}
void type(HWND window,const wchar_t *value){require(IsWindowVisible(window)&&IsWindowEnabled(window),"Raw field is unavailable");focus(window);SendMessageW(window,EM_SETSEL,0,-1);SendMessageW(window,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(value));}
void press(HWND window){require(IsWindowVisible(window)&&IsWindowEnabled(window),"Native action is unavailable");SendMessageW(window,BM_CLICK,0,0);}
struct Keys {
  BYTE before[256]{};
  Keys(bool ctrl=false,bool shift=false,bool alt=false){require(GetKeyboardState(before),"Read private-thread keyboard state");BYTE value[256]{};std::copy(std::begin(before),std::end(before),std::begin(value));
    for(int id:{VK_CONTROL,VK_LCONTROL,VK_RCONTROL,VK_SHIFT,VK_LSHIFT,VK_RSHIFT,VK_MENU,VK_LMENU,VK_RMENU})value[id]&=0x7f;
    if(ctrl)value[VK_CONTROL]|=0x80;if(shift)value[VK_SHIFT]|=0x80;if(alt)value[VK_MENU]|=0x80;require(SetKeyboardState(value),"Set private-thread modifiers");}
  ~Keys(){SetKeyboardState(before);}
};
void key(HWND window,WPARAM code,bool ctrl=false,bool shift=false,bool alt=false){Keys keys(ctrl,shift,alt);SendMessageW(window,WM_KEYDOWN,code,0);SendMessageW(window,WM_KEYUP,code,0);}
void mouse(HWND window,UINT message,double x,double y,WPARAM buttons=0){const auto scale=GetDpiForWindow(window)/96.;SendMessageW(window,message,buttons,MAKELPARAM(int(std::lround(x*scale)),int(std::lround(y*scale))));}
Json event(unsigned channel,unsigned row,unsigned offset,unsigned note=65,unsigned velocity=80){return {{"channel",channel},{"position",row*units+offset},{"note",note},{"instrument",note<128?1:0},{"velocity",note<128?velocity:127}};}
Json eventsA(){return Json::array({event(0,2,16384),event(0,2,49152,67,90),event(0,3,100,70,50),event(1,2,16384,72,60)});}
Json effects(){return Json::array({{{"command",0},{"parameterMask",0},{"parameterValue",0},{"suggestedParameter",0},{"displayCode","--"},{"name","None"},{"allowedParameters",Json::array({0})}},
  {{"command",1},{"parameterMask",0},{"parameterValue",0},{"suggestedParameter",0x37},{"displayCode","A"},{"name","Owned supported local FX"},{"allowedParameters",Json::array({0x37})}}});}
Json retained(const Json &value){Json result;for(const auto *key:{"initialized","document","revision","patternID","trackID","pattern","row","channel","generation","selected","units","snap","raw","tools","draft"})result[key]=value.at(key);return result;}
struct Fixture {
  Tool::Context current;
  std::map<std::string,Json> saved{{"pattern-a",eventsA()},{"pattern-b",Json::array({event(1,5,2048,74,97)})}};
  std::vector<std::pair<std::string,Json>> calls;
  std::vector<Json> submissions;
  std::function<void(const std::string &,Json &)> beforeReply;
  unsigned reads=0,writes=0,revision=0,returns=0;
  Tool tool;
  static Tool::Context context(){Tool::Context c;c.document="doc-a";c.revision="r0";
    c.patterns=Json::array({{{"id","pattern-a"},{"index",0},{"rows",8},{"name","A"}},{{"id","pattern-b"},{"index",1},{"rows",8},{"name","B"}}});
    c.tracks=Json::array({{{"id","track-a"},{"index",0},{"name","Lead"}},{{"id","track-b"},{"index",1},{"name","Other"}}});
    c.selected=Tool::Target{"pattern-a","track-a",0,2,0};c.selectionGeneration=1;c.cell=[](unsigned,unsigned,unsigned){return Tool::SeedCell{};};
    for(unsigned i=0;i<256;++i)c.noteNames.push_back(L"Fixture note "+std::to_wstring(i));return c;}
  explicit Fixture(HWND owner,bool open=true):current(context()),tool(owner,[this](const auto &method,const auto &params){return request(method,params);},[this]{return current;},[this]{++returns;}){
    ScreamSeq::Tests::ownGuiWindow(tool.window());if(open)tool.openAt();}
  Json request(const std::string &method,const Json &params){
    calls.emplace_back(method,params);const auto found=std::find_if(current.patterns.begin(),current.patterns.end(),[&](const auto &p){return p.at("index")==params.at("pattern");});
    require(found!=current.patterns.end(),"Request addressed an absent pattern slot");const auto id=found->at("id").get<std::string>();Json result;
    if(method=="pattern.notes.get"){++reads;result={{"pattern",params.at("pattern")},{"patternID",id},{"rows",found->at("rows")},{"rowsPerBeat",4},{"unitsPerRow",units},{"events",saved.at(id)},{"effects",effects()}};}
    else if(method=="pattern.notes.set"){
      require(params.at("expectedRevision")==current.revision,"Apply did not use its captured revision");submissions.push_back(params);
      const bool dryRun=params.at("dryRun").get<bool>(),changed=params.at("events")!=saved.at(id);
      if(!dryRun&&changed){saved.at(id)=params.at("events");current.revision="r"+std::to_string(++revision);++writes;}
      result={{"changed",!dryRun&&changed},{"dryRun",dryRun}};
    }else throw std::runtime_error("Unexpected precise-note fixture request: "+method);
    if(beforeReply)beforeReply(method,result);return result;
  }
  HWND control(int id)const{const auto window=GetDlgItem(tool.window(),id);require(window,"Missing precise-note control");return window;}
  void click(int id){press(control(id));}
  void page(int page){click(Tool::pageTimeline+page);}
  void field(int id,const wchar_t *value){const auto before=tool.operationGuard().at("generation");type(control(id),value);require(tool.operationGuard().at("generation")>before&&tool.retainedDraft(),"Native field edit did not register a retained draft");}
  void choose(int id,int value){require(IsWindowVisible(control(id))&&IsWindowEnabled(control(id)),"Combo is unavailable");SendMessageW(control(id),CB_SETCURSEL,value,0);SendMessageW(tool.window(),WM_COMMAND,MAKEWPARAM(id,CBN_SELCHANGE),reinterpret_cast<LPARAM>(control(id)));}
  void select(int value){require(IsWindowVisible(control(Tool::list))&&IsWindowEnabled(control(Tool::list)),"List is unavailable");SendMessageW(control(Tool::list),LB_SETCURSEL,value,0);SendMessageW(tool.window(),WM_COMMAND,MAKEWPARAM(Tool::list,LBN_SELCHANGE),reinterpret_cast<LPARAM>(control(Tool::list)));require(tool.snapshot().at("selected")==value,"Native list did not select requested event");}
  void dock(HWND parent,int height=300){tool.dock(parent);tool.dockBounds(0,48,440,float(height));}
  void selectB(){current.selected=Tool::Target{"pattern-b","track-b",1,5,1};++current.selectionGeneration;}
};
void bounds(Fixture &f,std::set<int> *reachable=nullptr){
  RECT client{};GetClientRect(f.tool.window(),&client);std::vector<std::pair<int,RECT>> boxes;
  for(HWND child=GetWindow(f.tool.window(),GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){
    if(!IsWindowVisible(child))continue;const auto id=GetDlgCtrlID(child);RECT box{};GetWindowRect(child,&box);MapWindowPoints(nullptr,f.tool.window(),reinterpret_cast<POINT *>(&box),2);
    if(box.left<0||box.top<0||box.right>client.right||box.bottom>client.bottom||box.right<=box.left||box.bottom<=box.top)throw std::runtime_error("Precise-note native control outside client: "+std::to_string(id));
    for(const auto &[otherID,other]:boxes){RECT overlap{};if(IntersectRect(&overlap,&box,&other))throw std::runtime_error("Precise-note native control overlap: "+std::to_string(id)+" / "+std::to_string(otherID));}
    boxes.emplace_back(id,box);if(reachable)reachable->insert(id);
  }
  const auto snapshot=f.tool.snapshot(false);if(snapshot.at("canvasVisible").get<bool>()){
    const auto &plot=snapshot.at("timeline");const auto scale=GetDpiForWindow(f.tool.window())/96.;
    RECT canvas{0,LONG(std::lround(80*scale)),LONG(std::lround((plot.at("x").get<double>()+plot.at("width").get<double>())*scale)),LONG(std::lround((plot.at("y").get<double>()+plot.at("height").get<double>())*scale))};
    require(canvas.top>=0&&canvas.right<=client.right&&canvas.bottom<=client.bottom,"Precise timeline/axis region escapes client");
    for(const auto &[id,box]:boxes){RECT overlap{};if(IntersectRect(&overlap,&canvas,&box))throw std::runtime_error("Native control overlaps timeline/axes: "+std::to_string(id));}
  }
}
void minimumPages(Owner &owner){
  Fixture f(owner.window);const auto hwnd=f.tool.window();const auto reads=f.reads;std::set<int> reachable;
  f.dock(owner.window);for(int height:{300,310}){f.tool.dockBounds(0,48,440,float(height));for(int page:{0,1,2}){
    f.page(page);bounds(f,&reachable);for(int id:{Tool::applyDraft,Tool::checkDraft,Tool::reloadCapturedCommand,Tool::statusLabel})require(IsWindowVisible(f.control(id)),"Short page hid a fixed action/status");
    if(page==0){const auto h=f.tool.snapshot(false).at("timeline").at("height").get<double>();require(h>=100&&h<=120,"Short timeline lost its usable 100–120 DIP height");}
  }}
  for(int id=Tool::list;id<=Tool::endVelocity;++id)require(reachable.contains(id),"An original precise-note control is unreachable across pages");
  f.tool.floatWindow();sizeClient(hwnd,440,500);for(int page:{0,1,2}){f.page(page);bounds(f);}
  MINMAXINFO minimum{};SendMessageW(hwnd,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&minimum));const auto dpi=GetDpiForWindow(hwnd);RECT frame{0,0,MulDiv(440,dpi,96),MulDiv(500,dpi,96)};
  require(AdjustWindowRectExForDpi(&frame,DWORD(GetWindowLongPtrW(hwnd,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(hwnd,GWL_EXSTYLE)),dpi),"Calculate expected floating minimum");
  require(minimum.ptMinTrackSize.x==frame.right-frame.left&&minimum.ptMinTrackSize.y==frame.bottom-frame.top,"Floating minimum is not exactly440×500 client DIPs");
  const auto full=f.tool.snapshot(),compact=f.tool.snapshot(false);auto expected=full;expected.erase("draft");
  require(compact==expected&&!compact.contains("draft")&&full.at("draft")==Json::array({event(0,2,16384),event(0,2,49152,67,90)}),"Compact diagnostic changed owner meaning or leaked full events");
  require(f.tool.window()==hwnd&&f.reads==reads&&f.submissions.empty()&&f.current.revision=="r0","Page/resize changed ownership, read or saved music");
}
void retainedRawCaretAndVirtualList(Owner &owner){
  Fixture f(owner.window,false);f.saved["pattern-a"]=Json::array();for(unsigned i=0;i<1000;++i)f.saved["pattern-a"].push_back(event(0,2,i*64,61+i%12,64));f.tool.openAt();f.dock(owner.window);f.page(1);
  const auto hwnd=f.tool.window(),list=f.control(Tool::list),field=f.control(Tool::offset),option=f.control(Tool::repeatCount);
  require(SendMessageW(list,LB_GETCOUNT,0,0)==1000,"Virtual list did not expose all events");f.select(601);SendMessageW(list,LB_SETTOPINDEX,600,0);const auto top=SendMessageW(list,LB_GETTOPINDEX,0,0);
  require(top==600,"Virtual list fixture could not establish late scroll position");f.page(2);f.field(Tool::repeatCount,L"unfinished 04");SendMessageW(option,EM_SETSEL,1,5);
  f.page(1);f.field(Tool::offset,L"00--.125");SendMessageW(field,EM_SETSEL,2,5);const auto before=retained(f.tool.snapshot());const auto reads=f.reads;
  f.tool.dockBounds(0,48,460,310);f.tool.floatWindow();sizeClient(hwnd,440,500);
  require(GetFocus()==field&&caret(field)==std::pair<DWORD,DWORD>{2,5},"Float/resize lost focused raw field caret");
  f.dock(owner.window);require(GetFocus()==field,"Short transition displaced a visible shared field");
  for(int page:{0,2,1}){f.page(page);bounds(f);require(retained(f.tool.snapshot())==before,"Page navigation changed retained precise-note data");}
  f.tool.hide();f.tool.show();f.page(1);require(f.tool.window()==hwnd&&f.control(Tool::list)==list&&f.control(Tool::offset)==field&&f.control(Tool::repeatCount)==option,"Placement/hide recreated a retained HWND");
  require(text(field)==L"00--.125"&&caret(field)==std::pair<DWORD,DWORD>{2,5}&&text(option)==L"unfinished 04"&&caret(option)==std::pair<DWORD,DWORD>{1,5},"Page/hide lost raw point/tools text or caret");
  require(SendMessageW(list,LB_GETTOPINDEX,0,0)==top&&SendMessageW(list,LB_GETCURSEL,0,0)==601&&retained(f.tool.snapshot())==before,"Virtual list selection/scroll or capture changed through reflow");
  owner.focus();f.tool.dockBounds(0,48,440,310);f.tool.observeContext();require(GetFocus()==owner.edit&&f.reads==reads&&f.submissions.empty(),"Reflow/context polling stole foreign focus or performed a request");
}
void hiddenInitializationAndStrictReplies(Owner &owner){
  Fixture existing(owner.window);existing.dock(owner.window);existing.field(Tool::offset,L"retained invalid offset");const auto untouched=retained(existing.tool.snapshot());owner.focus();
  {
    Fixture fresh(owner.window,false);fresh.tool.initializeHidden();require(!fresh.tool.visible()&&!fresh.tool.docked()&&GetFocus()==owner.edit&&fresh.tool.capturedCurrent()&&fresh.reads==1,"Hidden initialization showed/focused/reloaded the owner incorrectly");
    require(fresh.tool.capturedTarget()->patternID=="pattern-a"&&fresh.tool.capturedTarget()->trackID=="track-a","Hidden initialization lost stable source IDs");
    rejected([&]{fresh.tool.initializeHidden();},"Repeat hidden initialization was accepted");fresh.tool.show();rejected([&]{fresh.tool.initializeHidden();},"Visible initialization was accepted");
  }
  owner.focus();
  const std::vector<std::function<void(Json &)>> malformed={
    [](Json &r){r["patternID"]="pattern-b";},[](Json &r){r["pattern"]=1;},[](Json &r){r["rows"]=7;},[](Json &r){r["unitsPerRow"]=256;},[](Json &r){r["rowsPerBeat"]=0;},
    [](Json &r){r["events"]=Json::object();},[](Json &r){r["events"][0]["position"]=-1;},[](Json &r){r["events"].push_back(r["events"][0]);},
    [](Json &r){r["events"][0]["channel"]=99;},[](Json &r){r["events"][0]["note"]=255;r["events"][0]["instrument"]=1;},
    [](Json &r){r["effects"]=Json::array();},[](Json &r){r["events"][0]["effect"]=99;r["events"][0]["parameter"]=1;}};
  for(const auto &corrupt:malformed){Fixture fresh(owner.window,false);const auto pristine=retained(fresh.tool.snapshot());fresh.beforeReply=[&](const std::string &,Json &reply){corrupt(reply);};
    rejected([&]{fresh.tool.initializeHidden();},"Malformed initial read was adopted");require(!fresh.tool.visible()&&GetFocus()==owner.edit&&retained(fresh.tool.snapshot())==pristine,"Failed initial read mutated/showed/focused its candidate");
    require(retained(existing.tool.snapshot())==untouched&&existing.tool.visible(),"Failed candidate touched another retained owner");}
  {
    Fixture fresh(owner.window,false);fresh.beforeReply=[](const std::string &,Json &){throw std::runtime_error("Owned read failure");};rejected([&]{fresh.tool.initializeHidden();},"Throwing initial read was swallowed");
    fresh.beforeReply={};fresh.tool.initializeHidden();require(fresh.tool.capturedCurrent()&&!fresh.tool.visible()&&fresh.reads==2&&GetFocus()==owner.edit,"Failed read poisoned a later explicit successful initialization");}
  {
    Fixture partition(owner.window,false);partition.saved["pattern-a"].push_back(event(0,2,16384,255));partition.tool.initializeHidden();
    require(partition.tool.snapshot().at("draftCount")==3&&!partition.tool.visible()&&GetFocus()==owner.edit,"Legal release and onset at one timestamp were collapsed/rejected");}
  {
    Fixture empty(owner.window,false);empty.current.selected.reset();empty.tool.initializeHidden();require(empty.tool.operationGuard().at("initialized")==true&&!empty.tool.capturedTarget()&&empty.reads==0&&!empty.tool.visible(),"Empty selection manufactured a captured row");}
}
void stablePatternAndTrackIdentity(Owner &owner){
  Fixture f(owner.window);f.dock(owner.window);f.field(Tool::offset,L"broken");const auto before=retained(f.tool.snapshot());f.selectB();require(!f.tool.followSelection()&&retained(f.tool.snapshot())==before,"Follow overwrote a captured raw draft");
  // Slots move, but both stable identities survive. Reload must address the old
  // captured row, never the current pattern-b selection.
  f.current.patterns[0]["index"]=4;f.current.patterns[1]["index"]=0;f.current.tracks[0]["index"]=2;f.current.tracks[1]["index"]=0;
  for(auto &[id,events]:f.saved)for(auto &e:events)e["channel"]=e.at("channel")==0?2:0;
  f.current.selected=Tool::Target{"pattern-b","track-b",0,5,0};f.current.revision="external-1";++f.current.selectionGeneration;
  f.tool.reloadCaptured();const auto moved=f.tool.snapshot();require(moved.at("patternID")=="pattern-a"&&moved.at("trackID")=="track-a"&&moved.at("pattern")==4&&moved.at("channel")==2&&moved.at("row")==2,"Reload redirected stable captured identity after slot movement");
  require(f.calls.back().first=="pattern.notes.get"&&f.calls.back().second.at("pattern")==4,"Reload requested the cursor pattern instead of captured stable pattern");
  f.field(Tool::offset,L"still invalid");const auto retainedDeleted=retained(f.tool.snapshot());
  // Test each identity independently: a surviving pattern must not mask a
  // missing track, and a surviving track must not mask a missing pattern.
  for(bool deleteTrack:{true,false}){
    f.current.tracks[0]["id"]=deleteTrack?"replacement-track":"track-a";
    f.current.patterns[0]["id"]=deleteTrack?"pattern-a":"replacement-pattern";f.current.revision=deleteTrack?"removed-track":"removed-pattern";
    const auto callCount=f.calls.size();f.click(Tool::applyDraft);rejected([&]{f.tool.reloadCaptured();},"Reload accepted a deleted stable identity at its reused slot");
    require(f.calls.size()==callCount&&retained(f.tool.snapshot())==retainedDeleted&&f.submissions.empty(),"Deleted source guard queued a request or changed the retained draft");
  }
  f.page(2);f.field(Tool::repeatCount,L"keep this raw option");const auto option=text(f.control(Tool::repeatCount));f.click(Tool::loadTarget);
  const auto recaptured=f.tool.snapshot();require(recaptured.at("patternID")=="pattern-b"&&recaptured.at("trackID")=="track-b"&&recaptured.at("row")==5&&recaptured.at("channel")==0&&f.tool.capturedCurrent(),"Explicit successful Load selection did not recover onto the selected stable source");
  require(text(f.control(Tool::repeatCount))==option&&f.tool.retainedDraft(),"Explicit recapture erased independent raw retrigger options");
}
void pumpedReadGuards(Owner &owner){
  for(int variant=0;variant<5;++variant){
    Fixture f(owner.window);f.dock(owner.window);f.field(Tool::offset,L"old unfinished offset");const auto old=retained(f.tool.snapshot());f.selectB();bool reached=false;
    f.beforeReply=[&](const std::string &method,Json &){if(method!="pattern.notes.get")return;reached=true;
      require(f.tool.pending(),"Read callback did not expose pending state");
      if(variant==0)f.current.document="another-document";
      if(variant==1)f.current.revision="external-edit";
      if(variant==2){++f.current.selectionGeneration;f.current.selected=Tool::Target{"pattern-a","track-a",0,3,0};}
      if(variant==3)f.current.tracks[1]["index"]=3;
      if(variant==4){const auto generation=f.tool.operationGuard().at("generation");SendMessageW(f.control(Tool::offset),WM_SETTEXT,0,reinterpret_cast<LPARAM>(L"newer pumped raw"));require(f.tool.operationGuard().at("generation")>generation,"Pumped EN_CHANGE did not advance draft generation");}
    };
    rejected([&]{f.tool.loadSelection();},"Pumped read adopted a changed source/draft");require(reached&&!f.tool.pending()&&f.submissions.empty(),"Rejected read left pending or queued a mutation");
    auto after=retained(f.tool.snapshot());if(variant==4){require(after.at("raw").at("offset")=="newer pumped raw"&&after.at("generation")>old.at("generation"),"Rejected read erased newer pumped raw text");after["raw"]=old.at("raw");after["generation"]=old.at("generation");}
    require(after==old,"Rejected pumped read replaced the previous captured target/events/options");
  }
}
void requestMergeAndCheckBoundary(Owner &owner){
  Fixture f(owner.window,false);f.saved["pattern-a"]=Json::array({event(0,3,100,70,50),event(1,2,16384,72,60)});
  // Published context supplies an ordinary row note plus a supported local FX.
  // One UI request must move those together; only the actual app/model suites
  // prove document Undo/persistence and tracker-cell clearing.
  f.current.cell=[](unsigned pattern,unsigned row,unsigned channel){require(pattern==0&&row==2&&channel==0,"Seed read used mutable cursor or wrong captured cell");return Tool::SeedCell{65,1,1,64,1,0x37};};
  f.tool.openAt();f.dock(owner.window);f.field(Tool::velocity,L"100");const auto source=f.saved.at("pattern-a"),draft=f.tool.snapshot().at("draft");f.click(Tool::checkDraft);
  require(f.submissions.size()==1&&f.submissions[0].at("dryRun")==true&&f.saved.at("pattern-a")==source&&f.current.revision=="r0"&&f.writes==0,"Check changed source data or submitted multiple requests");
  const auto check=f.submissions[0];require(check.at("clearRows")==Json::array({{{"row",2},{"channel",0}}})&&check.at("clearRowEffects")==true&&check.at("events").size()==3,"Seed conversion lost exact row/FX clear intent");
  require(check.at("events")[0]==source[0]&&check.at("events")[1]==source[1]&&check.at("events")[2]==draft[0]&&draft[0].at("effect")==1&&draft[0].at("parameter")==0x37,"Row replacement lost unrelated row/channel or seeded local FX");
  f.click(Tool::applyDraft);require(f.submissions.size()==2&&f.writes==1&&f.reads==1&&f.tool.capturedCurrent()&&f.saved.at("pattern-a")==check.at("events"),"Apply did not submit exactly one captured read/merge/write");
  require(f.tool.snapshot().at("revision")==f.current.revision&&!f.tool.retainedDraft(),"Accepted own write did not adopt completed revision/clean baseline");
  f.click(Tool::applyDraft);require(f.submissions.size()==3&&f.writes==1&&f.current.revision=="r1","Repeated unchanged Apply changed the fixture source");
}
void pendingNewerRawAndFocus(Owner &owner){
  for(bool unrelated:{false,true}){
    Fixture f(owner.window);f.dock(owner.window);f.field(Tool::velocity,L"90");const auto field=f.control(Tool::velocity);SendMessageW(field,EM_SETSEL,0,1);const auto selection=caret(field);bool sawPending=false;
    f.beforeReply=[&](const std::string &method,Json &){if(method!="pattern.notes.set")return;sawPending=true;
      require(f.tool.pending()&&!IsWindowEnabled(field)&&GetFocus()==nullptr,"Apply did not expose disabled-field/transient-null interval");if(unrelated)owner.focus();};
    key(field,VK_RETURN);require(sawPending&&!f.tool.pending()&&f.writes==1&&f.submissions.size()==1&&caret(field)==selection&&text(field)==L"90","Apply completion changed valid submitted field/caret");
    require(GetFocus()==(unrelated?owner.edit:field),"Apply stole valid unrelated focus or lost its transient-null field");
  }
  {
    Fixture f(owner.window);f.dock(owner.window);f.field(Tool::velocity,L"90");const auto field=f.control(Tool::velocity);bool sawNewer=false;
    f.beforeReply=[&](const std::string &method,Json &){if(method!="pattern.notes.set")return;const auto before=f.tool.operationGuard().at("generation");
      require(f.tool.pending()&&!IsWindowEnabled(field),"Newer-field fixture missed the pending interval");
      SendMessageW(field,WM_SETTEXT,0,reinterpret_cast<LPARAM>(L"111"));SendMessageW(field,EM_SETSEL,1,2);
      require(f.tool.operationGuard().at("generation")>before,"Newer raw pending edit failed to register");sawNewer=true;};
    f.click(Tool::applyDraft);const auto state=f.tool.snapshot();require(sawNewer&&f.submissions.size()==1&&f.reads==1&&state.at("raw").at("velocity")=="111"&&caret(field)==std::pair<DWORD,DWORD>{1,2}&&state.at("selectedEvent").at("velocity")==90&&f.tool.retainedDraft(),"Completion replaced newer raw text or silently applied it");
    require(f.tool.capturedCurrent()&&state.at("revision")==f.current.revision,"Newer raw draft was not kept on the completed own revision");
    f.beforeReply={};f.click(Tool::applyDraft);require(f.submissions.size()==2&&f.writes==2&&f.tool.snapshot().at("selectedEvent").at("velocity")==111&&!f.tool.retainedDraft(),"Explicit next Apply did not save the retained newer raw value");
  }
}
void nativeTimelineKeysDragAndCancel(Owner &owner){
  Fixture f(owner.window,false);f.saved["pattern-a"]=Json::array();f.tool.openAt();f.dock(owner.window);auto plot=f.tool.snapshot(false).at("timeline");
  const auto x=plot.at("x").get<double>(),y=plot.at("y").get<double>(),w=plot.at("width").get<double>(),h=plot.at("height").get<double>();
  mouse(f.tool.window(),WM_LBUTTONDBLCLK,x+w*.25,y+h*.25);auto initial=f.tool.snapshot().at("selectedEvent");
  require(f.tool.snapshot().at("draftCount")==1&&std::abs(int(initial.at("position").get<unsigned>()%units)-16384)<=128&&std::abs(initial.at("velocity").get<int>()-95)<=2,"DPI-scaled timeline insertion did not map to quarter-row/velocity95");
  const auto px=x+w*(initial.at("position").get<unsigned>()%units)/units,py=y+h*(1-initial.at("velocity").get<double>()/127);
  mouse(f.tool.window(),WM_LBUTTONDOWN,px,py,MK_LBUTTON);require(GetCapture()==f.tool.window(),"Hit drag did not own mouse capture");mouse(f.tool.window(),WM_MOUSEMOVE,x+w*.75,y+h*.5,MK_LBUTTON);
  require(f.tool.snapshot().at("selectedEvent").at("position")>initial.at("position"),"Drag did not stage a later hit");key(f.tool.window(),VK_ESCAPE);
  require(GetCapture()!=f.tool.window()&&f.tool.snapshot().at("selectedEvent")==initial&&f.returns==0,"Escape failed to cancel exact draft/capture or incorrectly returned to pattern");
  key(f.tool.window(),VK_RIGHT);auto nudged=f.tool.snapshot().at("selectedEvent");require(nudged.at("position").get<unsigned>()==initial.at("position").get<unsigned>()+256,"Free-grid Right nudge changed its established256-unit step");
  key(f.tool.window(),VK_RIGHT,false,false,true);require(f.tool.snapshot().at("selectedEvent").at("position").get<unsigned>()==nudged.at("position").get<unsigned>()+1,"Alt nudge did not use one precise unit");
  // Invalid raw input must not be overwritten by a later canvas arrow.
  f.field(Tool::offset,L"unfinished");const auto invalid=retained(f.tool.snapshot());focus(f.tool.window());key(f.tool.window(),VK_RIGHT);require(retained(f.tool.snapshot())==invalid&&text(f.control(Tool::offset))==L"unfinished","Canvas arrow clamped/replaced invalid raw timing");
  f.tool.reloadCaptured();mouse(f.tool.window(),WM_LBUTTONDBLCLK,x+w*.25,y+h*.25);initial=f.tool.snapshot().at("selectedEvent");f.choose(Tool::snap,1);
  mouse(f.tool.window(),WM_LBUTTONDOWN,x+w*.25,y+h*(1-initial.at("velocity").get<double>()/127),MK_LBUTTON);
  {Keys modifiers(false,true,false);mouse(f.tool.window(),WM_MOUSEMOVE,x+w*.75,y+h*.8,MK_LBUTTON|MK_SHIFT);}
  mouse(f.tool.window(),WM_LBUTTONUP,x+w*.75,y+h*.8);const auto shifted=f.tool.snapshot().at("selectedEvent");
  require(shifted.at("position")==2*units+49152&&shifted.at("velocity")==initial.at("velocity")&&GetCapture()!=f.tool.window(),"Snapped Shift-drag lost exact grid or velocity lock");
  focus(f.tool.window());key(f.tool.window(),VK_F6);require(f.tool.snapshot(false).at("page")=="hit"&&GetFocus()==f.control(Tool::list),"LocalF6 did not select retained Hit list");
  const auto hidden=retained(f.tool.snapshot());focus(f.tool.window());key(f.tool.window(),VK_DELETE);key(f.tool.window(),VK_RIGHT);mouse(f.tool.window(),WM_LBUTTONDBLCLK,x+w*.5,y+h*.5);
  require(retained(f.tool.snapshot())==hidden,"Hidden timeline accepted canvas mouse/Delete/arrow input");
  focus(f.control(Tool::offset));SendMessageW(f.control(Tool::offset),EM_SETSEL,0,0);const auto editState=retained(f.tool.snapshot());key(f.control(Tool::offset),VK_RIGHT);
  require(caret(f.control(Tool::offset))==std::pair<DWORD,DWORD>{1,1}&&retained(f.tool.snapshot())==editState,"Native Edit arrow was consumed as musical timing input");
  key(f.control(Tool::offset),VK_F6);require(f.tool.snapshot(false).at("page")=="timeline"&&GetFocus()==f.tool.window(),"LocalF6 did not restore Timeline canvas");
  key(f.tool.window(),'D',true);require(f.tool.snapshot().at("draftCount")==2,"Canvas duplicate did not stage another hit");key(f.tool.window(),VK_DELETE);require(f.tool.snapshot().at("draftCount")==1,"Canvas Delete did not remove only selected hit");
  require(f.submissions.empty()&&f.current.revision=="r0"&&f.saved.at("pattern-a").empty(),"Local gestures saved musical data before Apply");
  key(f.tool.window(),VK_ESCAPE);require(f.returns==1,"Idle Escape did not invoke Return exactly once");
}
void focusedButtonEnterRunsItsOwnAction(Owner &owner){
  // Enter reaches the editor before the native Button procedure. A dirty row
  // makes unintended Apply observable for every navigation/read/local action.
  for(int id:{Tool::pageTimeline,Tool::pageHit,Tool::pageTools,Tool::close,Tool::returnPattern,
      Tool::reloadCapturedCommand,Tool::loadTarget,Tool::checkDraft,Tool::addHit,
      Tool::removeHit,Tool::replaceRow,Tool::fillRow,Tool::applyDraft}){
    Fixture f(owner.window);f.dock(owner.window);f.field(Tool::velocity,L"90");
    if(id>=Tool::pageTimeline&&id<=Tool::pageTools)f.page((id-Tool::pageTimeline+1)%3);
    else if(id==Tool::close||id==Tool::returnPattern||id==Tool::loadTarget||id==Tool::replaceRow||id==Tool::fillRow)f.page(2);
    if(id==Tool::loadTarget)f.selectB();
    const auto saved=f.saved;const auto reads=f.reads;const auto draftCount=f.tool.snapshot().at("draftCount").get<size_t>();
    const auto button=f.control(id);require(IsWindowVisible(button)&&IsWindowEnabled(button),"Enter fixture button is not reachable");
    focus(button);key(button,VK_RETURN);
    if(id==Tool::applyDraft){
      require(f.submissions.size()==1&&f.submissions[0].at("dryRun")==false&&f.writes==1&&f.tool.capturedCurrent(),"Focused Apply Enter did not submit exactly one save");
      const auto &events=f.saved.at("pattern-a");const auto savedHit=std::find_if(events.begin(),events.end(),[](const auto &e){return e.at("channel")==0&&e.at("position")==2*units+16384;});
      require(savedHit!=events.end()&&savedHit->at("velocity")==90,"Focused Apply Enter saved the wrong row value");
    }else{
      require(f.saved==saved&&f.writes==0&&f.current.revision=="r0","Enter on a non-Apply button saved music");
      if(id==Tool::checkDraft)require(f.submissions.size()==1&&f.submissions[0].at("dryRun")==true,"Focused Check Enter was not exactly one dry run");
      else require(f.submissions.empty(),"Focused non-Apply action submitted a write/check request");
    }
    const auto state=f.tool.snapshot();
    if(id>=Tool::pageTimeline&&id<=Tool::pageTools)
      require(state.at("page")==std::array<const char *,3>{"timeline","hit","tools"}.at(size_t(id-Tool::pageTimeline)),"Focused page Enter did not select that page");
    if(id==Tool::close)require(!f.tool.visible()&&f.returns==0,"Focused Close Enter did not hide only its owner");
    if(id==Tool::returnPattern)require(f.returns==1,"Focused Return Enter did not invoke Return exactly once");
    if(id==Tool::reloadCapturedCommand)require(f.reads==reads+1&&state.at("row")==2&&state.at("patternID")=="pattern-a"&&state.at("selectedEvent").at("velocity")==80,"Focused Reload Enter did not reload its captured row");
    if(id==Tool::loadTarget)require(f.reads==reads+1&&state.at("row")==5&&state.at("patternID")=="pattern-b"&&state.at("trackID")=="track-b","Focused Load selection Enter did not capture the selected stable row");
    if(id!=Tool::reloadCapturedCommand&&id!=Tool::loadTarget)require(f.reads==reads,"Focused local action unexpectedly reread the source");
    if(id==Tool::addHit)require(state.at("draftCount")==draftCount+1,"Focused Add Enter did not stage one hit");
    if(id==Tool::removeHit)require(state.at("draftCount")==draftCount-1,"Focused Remove Enter did not remove one draft hit");
    if(id==Tool::replaceRow)require(state.at("tools").at("replaceLegacy")==false,"Focused Replace-row Enter did not toggle its own option");
    if(id==Tool::fillRow)require(state.at("draftCount")==draftCount+3,"Focused Fill Enter did not stage the default four-hit retrigger");
  }
  // Explicit Ctrl+Enter retains the existing Apply shortcut even on Check.
  {Fixture f(owner.window);f.dock(owner.window);f.field(Tool::velocity,L"91");const auto button=f.control(Tool::checkDraft);
    focus(button);key(button,VK_RETURN,true);require(f.submissions.size()==1&&f.submissions[0].at("dryRun")==false&&f.writes==1,"Ctrl+Enter no longer applies the captured row");}
}

void musicalDetailsAndReadonlyKeys(Owner &owner){
  Fixture f(owner.window,false);f.saved["pattern-a"][0]["effect"]=1;f.saved["pattern-a"][0]["parameter"]=0x37;
  const std::string description="Retained note-local instruction: preserve ordinary row effects until this hit overrides them. ";
  f.beforeReply=[&](const std::string &method,Json &reply){if(method=="pattern.notes.get"){
    std::string longDescription;for(int i=0;i<30;++i)longDescription+=description;reply["effects"][1]["description"]=longDescription;}};
  f.tool.openAt();f.dock(owner.window);const auto row=f.tool.snapshot().at("selectedHitSummary");
  require(row==Json::array({"0.0625 b","0.25 r","Fixture note 65","I1","V80","A 37"}),"Hit overview omitted musical timing/instrument/effect");
  const auto plot=f.tool.snapshot().at("timeline");require(plot.at("x")==40&&plot.at("y")==98&&plot.at("width")==392&&plot.at("height")==104,"Presentation changed short canvas geometry");
  f.field(Tool::offset,L"unfinished offset");const auto raw=f.control(Tool::offset);SendMessageW(raw,EM_SETSEL,2,8);
  const auto retainedBefore=retained(f.tool.snapshot());const auto originalCaret=caret(raw);const auto status=f.tool.snapshot().at("status");
  const auto reads=f.reads;const auto button=f.control(Tool::pageDetails);focus(button);key(button,VK_RETURN);
  const auto details=f.control(Tool::detailsText);const auto style=GetWindowLongPtrW(details,GWL_STYLE);
  require(f.tool.snapshot().at("page")=="details"&&GetFocus()==details&&IsWindowVisible(details),"Details Enter did not focus the retained read-only text");
  require((style&(ES_READONLY|ES_MULTILINE|WS_VSCROLL))==(ES_READONLY|ES_MULTILINE|WS_VSCROLL),"Details is not readable/selectable/scrollable native text");
  const auto content=text(details);require(content.find(L"invalid raw draft")!=std::wstring::npos&&content.find(L"Allowed hex values: 37")!=std::wstring::npos&&content.find(L"Staged offset: 0.25 row = 0.0625 beat")!=std::wstring::npos,"Details omitted timing, raw validity or legal effect values");
  require(content.find(std::wstring(description.begin(),description.end()))!=std::wstring::npos,"Details discarded the full effect description");
  bounds(f);SendMessageW(details,EM_SETSEL,5,22);SendMessageW(details,EM_LINESCROLL,0,3);const auto selected=caret(details);const auto scroll=SendMessageW(details,EM_GETFIRSTVISIBLELINE,0,0);
  require(scroll>0,"Long Details fixture did not scroll");key(details,VK_RETURN);SendMessageW(details,WM_CHAR,L'x',0);key(details,VK_RIGHT);
  require(text(details)==content&&f.submissions.empty()&&f.reads==reads&&retained(f.tool.snapshot())==retainedBefore,"Read-only text input changed a draft or submitted Apply");
  SendMessageW(details,EM_SETSEL,selected.first,selected.second);const auto restoredScroll=SendMessageW(details,EM_GETFIRSTVISIBLELINE,0,0);
  f.tool.observeContext();f.tool.dockBounds(0,48,440,300);
  require(caret(details)==selected&&SendMessageW(details,EM_GETFIRSTVISIBLELINE,0,0)==restoredScroll&&GetFocus()==details,"Unchanged Details refresh reset selection/scroll/focus");
  f.tool.floatWindow();sizeClient(f.tool.window(),440,500);bounds(f);require(GetFocus()==details&&text(details)==content,"Floating recreated or replaced Details text");
  f.dock(owner.window);bounds(f);require(GetFocus()==details&&f.tool.snapshot().at("page")=="details","Short transition hid focused Details");
  key(details,VK_F6);require(f.tool.snapshot().at("page")=="timeline"&&GetFocus()==f.tool.window(),"Details F6 did not return to Timeline");
  key(f.tool.window(),VK_F6);require(f.tool.snapshot().at("page")=="hit"&&GetFocus()==f.control(Tool::list),"Timeline/Hit F6 changed");
  require(text(raw)==L"unfinished offset"&&caret(raw)==originalCaret&&retained(f.tool.snapshot())==retainedBefore&&f.tool.snapshot().at("status")==status,"Presentation navigation changed raw/caret/status/generation");
  Fixture apply(owner.window);apply.dock(owner.window);apply.field(Tool::velocity,L"91");apply.click(Tool::pageDetails);key(apply.control(Tool::detailsText),VK_RETURN,true);
  require(apply.submissions.size()==1&&apply.submissions[0].at("dryRun")==false&&apply.writes==1,"Explicit Ctrl+Enter from Details did not retain Apply");
}


void descriptionValidationBeforeAdoption(Owner &owner){
  const std::array<Json,5> wrongTypes={Json(nullptr),Json(false),Json(7),Json::array({"text"}),Json::object({{"text","wrong shape"}})};
  for(const auto &invalid:wrongTypes){
    Fixture f(owner.window);f.dock(owner.window);f.field(Tool::offset,L"retained invalid timing");
    const auto raw=f.control(Tool::offset);SendMessageW(raw,EM_SETSEL,2,7);const auto rawSelection=caret(raw);
    f.click(Tool::pageDetails);const auto details=f.control(Tool::detailsText);SendMessageW(details,EM_SETSEL,4,18);SendMessageW(details,EM_LINESCROLL,0,2);
    // A real captured Reload after an external revision must either admit the
    // whole candidate or retain the old owner. A new reply row makes partial
    // publication observable even though the worker request itself is read-only.
    f.current.revision="r1";f.tool.observeContext();const auto state=retained(f.tool.snapshot());
    const auto content=text(details);const auto selection=caret(details);const auto scroll=SendMessageW(details,EM_GETFIRSTVISIBLELINE,0,0);const auto reads=f.reads;
    f.beforeReply=[&](const std::string &method,Json &reply){if(method=="pattern.notes.get"){
      reply["effects"][0]["description"]=invalid;reply["events"][0]["velocity"]=23;}};
    rejected([&]{f.tool.reloadCaptured();},"Wrong-type optional effect description was published");
    require(f.reads==reads+1&&f.submissions.empty()&&f.writes==0,"Rejected description retried or issued a musical write");
    require(!f.tool.pending()&&retained(f.tool.snapshot())==state&&f.tool.snapshot().at("revision")=="r0","Invalid description changed capture/draft/generation before rejection");
    require(text(raw)==L"retained invalid timing"&&caret(raw)==rawSelection,"Rejected description replaced retained raw text/caret");
    require(GetFocus()==details&&text(details)==content&&caret(details)==selection&&SendMessageW(details,EM_GETFIRSTVISIBLELINE,0,0)==scroll,"Rejected description changed Details text/selection/scroll/focus");
    f.beforeReply={};f.tool.reloadCaptured();require(f.tool.capturedCurrent()&&f.reads==reads+2&&f.submissions.empty(),"A rejected description prevented a later explicit valid Reload");
  }
  for(const auto &description:std::array<std::optional<std::string>,3>{std::nullopt,std::string{},std::string("Unicode Ω / notes\r\n")+std::string(4096,'x')}){
    Fixture f(owner.window,false);f.beforeReply=[&](const std::string &method,Json &reply){if(method=="pattern.notes.get"){
      if(description)reply["effects"][0]["description"]=*description;else reply["effects"][0].erase("description");}};
    f.tool.initializeHidden();require(f.tool.capturedCurrent()&&!f.tool.visible()&&f.reads==1&&f.submissions.empty(),"Valid absent/empty/long Unicode description was rejected");
    if(description&&!description->empty())require(f.tool.snapshot().at("details").get<std::string>().find(*description)!=std::string::npos,"Valid full effect description was truncated in presentation");
  }
}

}
int wmain(int argc,wchar_t **argv){try{
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  ScreamSeq::Tests::runPrivateGuiProcess(L"ScreamSeqPreciseNoteOwner",argc,argv,[]{
    ScreamSeq::check(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED),"Initialize precise-note fixture COM");struct Com{~Com(){CoUninitialize();}} com;
    Owner owner;
    minimumPages(owner);std::cout<<"PASS precise-note minimum pages, actions and compact diagnostics\n";
    retainedRawCaretAndVirtualList(owner);std::cout<<"PASS precise-note retained HWND, raw text, caret and virtual-list scroll\n";
    hiddenInitializationAndStrictReplies(owner);std::cout<<"PASS precise-note hidden initialization and strict failed/malformed reads\n";
    stablePatternAndTrackIdentity(owner);std::cout<<"PASS precise-note stable pattern/track moves, deletion and explicit recapture\n";
    pumpedReadGuards(owner);std::cout<<"PASS precise-note pumped source/selection/raw generation guards\n";
    requestMergeAndCheckBoundary(owner);std::cout<<"PASS precise-note captured read/merge/write and dry-run boundary\n";
    pendingNewerRawAndFocus(owner);std::cout<<"PASS precise-note pending newer raw generation and native focus ownership\n";
    nativeTimelineKeysDragAndCancel(owner);std::cout<<"PASS precise-note DPI-scaled timeline drag/cancel and local keyboard ownership\n";
    focusedButtonEnterRunsItsOwnAction(owner);std::cout<<"PASS precise-note focused Button Enter and explicit Apply shortcut\n";
    musicalDetailsAndReadonlyKeys(owner);std::cout<<"PASS precise-note musical hit overview and retained read-only Details\n";
    descriptionValidationBeforeAdoption(owner);std::cout<<"PASS precise-note optional description validation before adoption\n";
    owner.close();
  });return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
