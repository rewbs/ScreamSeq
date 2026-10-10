// Native ownership and retention tests on a process-owned private desktop.
#include "GraphCurveWindow.hpp"
#include "PrivateGuiProcessTest.hpp"
#include "AccessibleControl.hpp"
#include <iostream>
#include <map>
#include <set>

namespace {
using Json=ScreamSeq::Api::Json;
using Tool=ScreamSeq::GraphCurveWindow;
void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
template<class Action>void rejected(Action action,const char *message){try{action();}catch(const std::exception &){return;}throw std::runtime_error(message);}
struct Owner {
  HWND window{},edit{};
  Owner(){
    WNDCLASSW type{};type.lpfnWndProc=DefWindowProcW;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"ScreamSeq.GraphCurveOwner.Test";RegisterClassW(&type);
    window=CreateWindowExW(0,type.lpszClassName,L"Owned graph curve fixture",WS_OVERLAPPEDWINDOW|WS_VISIBLE|WS_CLIPCHILDREN,0,0,1200,900,nullptr,nullptr,type.hInstance,nullptr);
    require(window,"Create graph curve fixture owner");ScreamSeq::Tests::ownGuiWindow(window);
    edit=CreateWindowExW(0,L"EDIT",L"Unrelated retained text",WS_CHILD|WS_VISIBLE|WS_TABSTOP,8,8,250,26,window,nullptr,type.hInstance,nullptr);
    require(edit,"Create unrelated owner edit");ScreamSeq::Tests::ownGuiWindow(edit);
  }
  ~Owner(){if(window)DestroyWindow(window);}
  void focus(){SetActiveWindow(window);SetFocus(edit);require(GetFocus()==edit,"Focus unrelated owner field");}
  void close(){const auto parent=window,child=edit;require(DestroyWindow(window),"Destroy graph curve fixture owner");window=edit=nullptr;require(!IsWindow(parent)&&!IsWindow(child),"Graph curve fixture owner survived cleanup");}
};
void sizeClient(HWND window,int width,int height){
  const auto dpi=GetDpiForWindow(window);RECT frame{0,0,MulDiv(width,dpi,96),MulDiv(height,dpi,96)};
  require(AdjustWindowRectExForDpi(&frame,DWORD(GetWindowLongPtrW(window,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(window,GWL_EXSTYLE)),dpi),"Calculate graph curve native frame");
  require(SetWindowPos(window,nullptr,0,0,frame.right-frame.left,frame.bottom-frame.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE),"Resize graph curve client");
}
std::wstring text(HWND window){std::wstring value(size_t(GetWindowTextLengthW(window))+1,0);GetWindowTextW(window,value.data(),int(value.size()));value.resize(wcslen(value.c_str()));return value;}
std::pair<DWORD,DWORD> caret(HWND window){DWORD first=0,last=0;SendMessageW(window,EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));return {first,last};}
void focus(HWND window){SetActiveWindow(GetAncestor(window,GA_ROOT));SetFocus(window);require(GetFocus()==window,"Focus owned curve control");}
void type(HWND window,const wchar_t *value){
  require(IsWindowVisible(window)&&IsWindowEnabled(window),"Raw edit fixture is unavailable");focus(window);
  SendMessageW(window,EM_SETSEL,0,-1);SendMessageW(window,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(value));
}
void press(HWND window){require(IsWindowVisible(window)&&IsWindowEnabled(window),"Action fixture is unavailable");SendMessageW(window,BM_CLICK,0,0);}
void key(HWND window,WPARAM code,bool ctrl=false,bool shift=false){
  struct Restore {BYTE state[256]{};Restore(){require(GetKeyboardState(state),"Read test keyboard state");}~Restore(){SetKeyboardState(state);}} restore;
  BYTE state[256]{};std::copy(std::begin(restore.state),std::end(restore.state),std::begin(state));
  for(int id:{VK_CONTROL,VK_LCONTROL,VK_RCONTROL,VK_SHIFT,VK_LSHIFT,VK_RSHIFT,VK_MENU,VK_LMENU,VK_RMENU})state[id]&=0x7f;
  if(ctrl)state[VK_CONTROL]|=0x80;if(shift)state[VK_SHIFT]|=0x80;
  require(SetKeyboardState(state),"Set test-local keyboard modifiers");SendMessageW(window,WM_KEYDOWN,code,0);
}
HWND childTool(HWND owner,const wchar_t *name){
  // CreateWindowEx promotes a WS_CHILD owner to its top-level parent. This
  // helper runs at child creation; later placement keeps the exact HWND.
  struct Search {HWND owner{},found{};const wchar_t *name{};unsigned count=0;} search{GetAncestor(owner,GA_ROOT),nullptr,name};
  EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM parameter)->BOOL{
    auto &search=*reinterpret_cast<Search *>(parameter);wchar_t kind[128]{};GetClassNameW(window,kind,128);
    if(GetWindow(window,GW_OWNER)==search.owner&&std::wstring_view(kind)==search.name){search.found=window;++search.count;}return TRUE;
  },reinterpret_cast<LPARAM>(&search));
  require(search.found&&search.count==1,"Missing or ambiguous formula/bank child HWND");return ScreamSeq::Tests::ownGuiWindow(search.found);
}
Json retained(const Json &value){
  Json result;for(const auto name:{"initialized","document","expectedRevision","graph","node","patternID","pattern","generation","dirty","fieldDraft","points","enabled","selectedPoint","start","end","raw"})result[name]=value.at(name);return result;
}
Json defaultPoints(){return Json::array({{{"position",2048},{"value",.25},{"curve","linear"}},{{"position",8192},{"value",.75},{"curve","smooth"}}});}
struct Fixture {
  Tool::Context current{"document-a","r0",Json::array({{{"id","pattern-a"},{"index",0},{"name","Source A"}},{{"id","pattern-b"},{"index",1},{"name","Source B"}}}),Tool::Target{"graph-a","node-a","pattern-a",0},L"Graph A / source A",1,false};
  std::map<std::string,Json> saved{{"pattern-a",defaultPoints()},{"pattern-b",Json::array({{{"position",4096},{"value",.625},{"curve","step"}}})}};
  std::vector<std::pair<std::string,Json>> calls;
  std::vector<Json> writes;
  std::function<void(const std::string &,Json &)> beforeReply;
  unsigned reads=0,previews=0,returns=0,revision=0;
  Tool tool;
  explicit Fixture(HWND owner,bool open=true):tool(owner,[this](const auto &method,const auto &params){return request(method,params);},[this]{return current;},[this]{++returns;}){
    ScreamSeq::Tests::ownGuiWindow(tool.window());if(open)tool.openAt();KillTimer(tool.window(),3);
  }
  Json request(const std::string &method,const Json &params){
    calls.emplace_back(method,params);Json result;
    if(method=="graph.automation.get"){
      ++reads;const auto found=std::find_if(current.patterns.begin(),current.patterns.end(),[&](const auto &item){return item.at("index")==params.at("pattern");});
      require(found!=current.patterns.end(),"Fixture graph get requested missing pattern index");const auto id=found->at("id").get<std::string>();
      require(saved.contains(id),"Fixture graph get requested an unknown stable identity");
      result={{"graph",params.at("graph")},{"node",params.at("node")},{"patternID",id},{"pattern",params.at("pattern")},{"rows",64},{"rowsPerBeat",4},{"unitsPerRow",256},{"enabled",true},{"points",saved.at(id)}};
    }else if(method=="graph.automation.set"){
      require(params.at("expectedRevision")==current.revision,"Apply did not use captured revision");
      const auto found=std::find_if(current.patterns.begin(),current.patterns.end(),[&](const auto &item){return item.at("index")==params.at("pattern");});
      require(found!=current.patterns.end(),"Apply requested deleted pattern");saved.at(found->at("id").get<std::string>())=params.at("points");writes.push_back(params);current.revision="own-"+std::to_string(++revision);result={{"changed",true}};
    }else if(method=="automation.formula.preview"){
      ++previews;Json values=Json::array();for(const auto &point:params.at("points"))values.push_back(Json::array({point.at("position"),point.at("value")}));result={{"values",std::move(values)}};
    }else if(method=="automation.formula.reference")result={{"symbols",Json::array({{{"name","mix"},{"insert","mix(start,end,t)"},{"description","Interpolate endpoint values"},{"category","function"}}})},{"notes","Owned reference fixture"}};
    else if(method=="envelope.bank.list")result={{"entries",Json::array({{{"id","template-a"},{"name","Owned template"},{"shape",{{"span",16384},{"rowsPerBeat",4},{"points",defaultPoints()}}}}})}};
    else if(method=="envelope.catalogue.list")result={{"entries",Json::array()},{"revision","catalogue-a"}};
    else throw std::runtime_error("Unexpected graph curve fixture request: "+method);
    if(beforeReply)beforeReply(method,result);return result;
  }
  HWND control(int id)const{const auto window=GetDlgItem(tool.window(),id);require(window,"Missing GraphCurve native control");return window;}
  void click(int id){press(control(id));}
  void page(int page){click(Tool::pageCurve+page);}
  void choose(int id,int index){require(IsWindowVisible(control(id))&&IsWindowEnabled(control(id)),"Combo fixture unavailable");SendMessageW(control(id),CB_SETCURSEL,index,0);SendMessageW(tool.window(),WM_COMMAND,MAKEWPARAM(id,CBN_SELCHANGE),reinterpret_cast<LPARAM>(control(id)));}
  void field(int id,const wchar_t *value){const auto before=tool.snapshot().at("generation");type(control(id),value);require(tool.snapshot().at("generation")>before&&tool.snapshot().at("fieldDraft").get<bool>(),"Raw curve edit failed to register a draft");}
  void dock(HWND parent,int height=300){tool.dock(parent);tool.dockBounds(0,48,440,float(height));}
  void selectB(){current.selected=Tool::Target{"graph-a","node-a","pattern-b",1};current.selectedTitle=L"Graph A / pattern B";++current.selectionGeneration;}
};
void bounds(Fixture &f,std::set<int> *available=nullptr){
  RECT client{};GetClientRect(f.tool.window(),&client);std::vector<std::pair<int,RECT>> boxes;
  for(HWND child=GetWindow(f.tool.window(),GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){
    if(!IsWindowVisible(child))continue;const auto id=GetDlgCtrlID(child);RECT box{};GetWindowRect(child,&box);MapWindowPoints(nullptr,f.tool.window(),reinterpret_cast<POINT *>(&box),2);
    if(box.left<0||box.top<0||box.right>client.right||box.bottom>client.bottom||box.right<=box.left||box.bottom<=box.top)throw std::runtime_error("GraphCurve control outside client: "+std::to_string(id));
    for(const auto &[otherID,other]:boxes){RECT overlap{};if(IntersectRect(&overlap,&box,&other))throw std::runtime_error("GraphCurve native control overlap: "+std::to_string(id)+" / "+std::to_string(otherID));}
    boxes.emplace_back(id,box);if(available)available->insert(id);
  }
  const auto state=f.tool.snapshot();if(state.at("canvasVisible").get<bool>()){
    const auto &r=state.at("canvas");const auto scale=GetDpiForWindow(f.tool.window())/96.;
    RECT canvas{LONG(std::lround((r.at("x").get<double>()-38)*scale)),LONG(std::lround((r.at("y").get<double>()-17)*scale)),LONG(std::lround((r.at("x").get<double>()+r.at("width").get<double>())*scale)),LONG(std::lround((r.at("y").get<double>()+r.at("height").get<double>())*scale))};
    require(canvas.left>=0&&canvas.top>=0&&canvas.right<=client.right&&canvas.bottom<=client.bottom,"Curve axes escape native client");
    for(const auto &[id,box]:boxes){RECT overlap{};if(IntersectRect(&overlap,&canvas,&box)){
      std::cerr<<"GraphCurve geometry: dpi="<<GetDpiForWindow(f.tool.window())<<" client="<<client.right<<'x'<<client.bottom
        <<" control="<<id<<" ["<<box.left<<','<<box.top<<','<<box.right<<','<<box.bottom<<"] axes=["
        <<canvas.left<<','<<canvas.top<<','<<canvas.right<<','<<canvas.bottom<<"]\n";
      throw std::runtime_error("GraphCurve control overlaps canvas/axes: "+std::to_string(id));
    }}
  }
}
void automaticCurve(Owner &owner){
  Fixture f(owner.window);f.click(Tool::ramp);require(f.writes.empty(),"Curve button wrote before its edit settled");
  SendMessageW(f.tool.window(),WM_TIMER,0x5345,0);require(f.writes.size()==1&&!f.tool.snapshot().at("dirty").get<bool>(),"Curve edit still requires Apply");
  f.field(Tool::pointValue,L"35.00");const auto field=f.control(Tool::pointValue);SendMessageW(field,EM_SETSEL,1,4);
  SendMessageW(f.tool.window(),WM_TIMER,0x5345,0);
  require(f.writes.size()==2&&f.saved.at("pattern-a")[0].at("value")==.35,"Existing point fields did not save directly");
  require(GetFocus()==field&&text(field)==L"35.00"&&caret(field)==std::pair<DWORD,DWORD>{1,4},"Automatic curve write lost text/caret/focus");
  f.field(Tool::pointValue,L"-.");SendMessageW(f.tool.window(),WM_TIMER,0x5345,0);SendMessageW(f.tool.window(),WM_TIMER,0x5345,0);
  require(f.writes.size()==2&&text(field)==L"-."&&IsWindowVisible(f.control(Tool::apply)),"Invalid point was saved/retried or lacks recovery action");
}
void minimumPages(Owner &owner){
  Fixture f(owner.window);const auto window=f.tool.window();f.dock(owner.window);std::set<int> reachable;const auto reads=f.reads;
  for(int height:{300,310}){f.tool.dockBounds(0,48,440,float(height));for(int page:{0,1,2}){
    f.page(page);bounds(f,&reachable);for(int id:{Tool::preview,Tool::reload,Tool::statusLabel})require(IsWindowVisible(f.control(id)),"A short page hid fixed curve actions/status");
    if(page==0){const auto h=f.tool.snapshot().at("canvas").at("height").get<double>();require(h>=100&&h<=120,"Short curve viewport is not 100–120 DIPs high");}
  }}
  require(!IsWindowVisible(f.control(Tool::apply)),"Clean curve still requires an Apply action");
  for(int id=Tool::pattern;id<=Tool::reference;++id)if(id!=Tool::apply)require(reachable.contains(id),"An original curve action is unreachable across short pages");
  require(f.tool.window()==window&&f.reads==reads&&f.writes.empty()&&f.current.revision=="r0","Layout recreated/reloaded/applied curve");
  f.tool.floatWindow();sizeClient(window,440,500);for(int page:{0,1,2}){f.page(page);bounds(f);}
  f.page(0);require(f.tool.snapshot().at("canvas").at("height")==272,"Floating minimum lost proposed 272-DIP curve height");
  MINMAXINFO min{};SendMessageW(window,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&min));const auto dpi=GetDpiForWindow(window);RECT expected{0,0,MulDiv(440,dpi,96),MulDiv(500,dpi,96)};
  require(AdjustWindowRectExForDpi(&expected,DWORD(GetWindowLongPtrW(window,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(window,GWL_EXSTYLE)),dpi),"Calculate expected curve minimum");
  require(min.ptMinTrackSize.x==expected.right-expected.left&&min.ptMinTrackSize.y==expected.bottom-expected.top,"GraphCurve floating minimum changed");
}
void retainedFieldsAndFocus(Owner &owner){
  Fixture f(owner.window);f.click(Tool::zoomIn);f.field(Tool::pointRow,L"00--.125");SendMessageW(f.control(Tool::pointRow),EM_SETSEL,2,5);
  const auto window=f.tool.window(),field=f.control(Tool::pointRow);const auto before=retained(f.tool.snapshot());const auto reads=f.reads;
  f.dock(owner.window);f.tool.dockBounds(0,48,460,310);f.tool.floatWindow();sizeClient(window,440,500);
  require(f.tool.window()==window&&f.control(Tool::pointRow)==field&&text(field)==L"00--.125"&&caret(field)==std::pair<DWORD,DWORD>{2,5}&&GetFocus()==field,"Placement lost point field HWND/raw text/caret/focus");
  require(retained(f.tool.snapshot())==before&&f.reads==reads&&f.writes.empty(),"Placement changed captured curve or axes");
  f.dock(owner.window);f.tool.hide();owner.focus();f.tool.show();
  require(GetFocus()==owner.edit&&retained(f.tool.snapshot())==before&&text(field)==L"00--.125"&&caret(field)==std::pair<DWORD,DWORD>{2,5},"Base show stole focus or replaced point draft");
  f.page(1);f.choose(Tool::kind,8);f.field(Tool::formula,L"mix(start, end, t) + -");SendMessageW(f.control(Tool::formula),EM_SETSEL,4,9);
  const auto formula=f.control(Tool::formula);const auto formulaState=retained(f.tool.snapshot());f.tool.floatWindow();sizeClient(window,440,500);f.dock(owner.window);
  require(f.tool.snapshot().at("page")=="formula"&&GetFocus()==formula&&caret(formula)==std::pair<DWORD,DWORD>{4,9}&&retained(f.tool.snapshot())==formulaState,"Short Formula page replaced raw draft or caret");
  f.page(2);f.page(0);f.page(1);require(text(formula)==L"mix(start, end, t) + -"&&retained(f.tool.snapshot())==formulaState,"Page navigation overwrote hidden raw formula");
  f.tool.floatWindow();f.page(0);focus(f.control(Tool::pattern));const auto pattern=f.control(Tool::pattern);const auto captured=retained(f.tool.snapshot());f.dock(owner.window);
  require(f.tool.snapshot().at("page")=="tools"&&GetFocus()==pattern&&f.control(Tool::pattern)==pattern&&retained(f.tool.snapshot())==captured,"Short entry did not preserve focused common Pattern chooser");
}
void stableIdentityAndExplicitRecapture(Owner &owner){
  Fixture f(owner.window);f.current.patterns[0]["index"]=3;f.current.selected->pattern=3;f.current.revision="moved";++f.current.selectionGeneration;
  f.tool.reloadCaptured();require(f.tool.snapshot().at("patternID")=="pattern-a"&&f.tool.snapshot().at("pattern")==3&&f.calls.back().second.at("pattern")==3,"Reload used stale numeric slot instead of stable pattern identity");
  f.field(Tool::pointRow,L"-1 retained");const auto raw=retained(f.tool.snapshot());const auto reads=f.reads;
  f.current.patterns[0]={{"id","replacement-c"},{"index",3},{"name","Reused slot"}};f.saved["replacement-c"]=Json::array();f.current.revision="deleted";f.selectB();
  key(f.control(Tool::pointRow),VK_RETURN,true);require(f.writes.empty()&&retained(f.tool.snapshot())==raw,"Stale Ctrl+Enter submitted or rebased deleted-source draft");
  rejected([&]{f.tool.reloadCaptured();},"Deleted identity reload silently adopted reused slot");
  require(f.reads==reads&&retained(f.tool.snapshot())==raw&&!f.tool.followSelection(),"Deleted target reload/automatic Follow changed retained draft");
  f.page(2);f.beforeReply=[](const std::string &method,Json &){if(method=="graph.automation.get")throw std::runtime_error("Injected selected-source read failure");};f.click(Tool::follow);
  require(retained(f.tool.snapshot())==raw&&f.writes.empty(),"Failed explicit Load selection erased local curve");
  f.beforeReply={};f.click(Tool::follow);const auto loaded=f.tool.snapshot();
  require(loaded.at("patternID")=="pattern-b"&&loaded.at("pattern")==1&&loaded.at("expectedRevision")=="deleted"&&loaded.at("points")==f.saved.at("pattern-b")&&!loaded.at("fieldDraft").get<bool>()&&!loaded.at("dirty").get<bool>(),"Explicit Load selection did not adopt complete selected source after successful read");
  require(f.writes.empty(),"Explicit Load selection changed song data");
}
void pumpedReadGuards(Owner &owner){
  for(int scenario=0;scenario<4;++scenario){
    Fixture f(owner.window);f.field(Tool::pointRow,L"old raw -");f.page(2);f.selectB();const auto before=retained(f.tool.snapshot());
    f.beforeReply=[&](const std::string &method,Json &result){if(method!="graph.automation.get")return;
      if(scenario==0){f.current.selected=Tool::Target{"graph-a","node-a","pattern-a",0};++f.current.selectionGeneration;}
      if(scenario==1){f.current.document="replacement-song";f.current.revision="replacement-revision";}
      if(scenario==2)result["patternID"]="wrong-response-target";
      if(scenario==3){f.current.patterns[1]["id"]="replacement-pattern";}
    };
    f.click(Tool::follow);require(retained(f.tool.snapshot())==before&&!f.tool.pending()&&f.writes.empty(),"Pumped read overwrote prior target/raw draft");
  }
  Fixture f(owner.window);f.field(Tool::pointRow,L"old raw");f.page(2);f.selectB();const auto old=f.tool.snapshot();
  f.beforeReply=[&](const std::string &method,Json &){if(method=="graph.automation.get")SetWindowTextW(f.control(Tool::pointRow),L"newer pumped raw");};
  f.click(Tool::follow);const auto after=f.tool.snapshot();
  require(after.at("generation")>old.at("generation")&&after.at("patternID")==old.at("patternID")&&after.at("points")==old.at("points")&&text(f.control(Tool::pointRow))==L"newer pumped raw"&&!f.tool.pending(),"Pumped newer field generation was overwritten by read completion");
}
void hiddenInitialization(Owner &owner){
  owner.focus();const auto outside=GetFocus();
  {
    Fixture f(owner.window,false);unsigned placements=0;f.tool.placementChanged([&]{++placements;});f.tool.initializeHidden();const auto before=retained(f.tool.snapshot());const auto reads=f.reads;
    require(!f.tool.visible()&&!f.tool.docked()&&f.tool.capturedCurrent()&&GetFocus()==outside&&placements==0&&before.at("initialized").get<bool>(),"Hidden curve initialization changed visibility/focus or failed capture");
    rejected([&]{f.tool.initializeHidden();},"Repeated hidden initialization accepted");require(f.reads==reads&&retained(f.tool.snapshot())==before,"Repeated initialization changed owner");
  }
  {
    Fixture f(owner.window,false);f.tool.show();rejected([&]{f.tool.initializeHidden();},"Visible hidden initialization accepted");require(f.reads==0,"Visible hidden initialization called worker");
  }
  {
    Fixture f(owner.window,false);f.tool.dock(owner.window);rejected([&]{f.tool.initializeHidden();},"Docked hidden initialization accepted");require(f.reads==0,"Docked initialization called worker");
  }
  {
    Fixture f(owner.window,false);f.current.selected.reset();f.tool.initializeHidden();require(f.tool.operationGuard().at("initialized").get<bool>()&&!f.tool.capturedTarget()&&f.reads==0,"Empty fresh initialization fabricated a source");
    f.current.selected=Tool::Target{"graph-a","node-a","pattern-a",0};++f.current.selectionGeneration;require(f.tool.followSelection()&&f.tool.capturedCurrent()&&!f.tool.visible(),"Fresh empty owner could not later follow without showing");
  }
  Fixture existing(owner.window);existing.field(Tool::pointRow,L"retained -001");SendMessageW(existing.control(Tool::pointRow),EM_SETSEL,2,6);
  const auto field=GetFocus(),window=existing.tool.window();const auto before=retained(existing.tool.snapshot());HWND abandoned{};
  {
    Fixture staged(owner.window,false);abandoned=staged.tool.window();staged.beforeReply=[](const std::string &method,Json &){if(method=="graph.automation.get")throw std::runtime_error("Initial curve read failure");};
    rejected([&]{staged.tool.initializeHidden();},"Initial read failure did not propagate");require(!staged.tool.visible()&&!staged.tool.capturedTarget()&&!staged.tool.pending(),"Failed hidden candidate adopted partial source");
  }
  require(!IsWindow(abandoned)&&existing.tool.window()==window&&retained(existing.tool.snapshot())==before&&GetFocus()==field&&caret(field)==std::pair<DWORD,DWORD>{2,6}&&text(field)==L"retained -001","Failed staging disturbed existing HWND/draft/caret/focus");
}
template<class Ready>void pumpTimers(HWND window,Ready ready){
  const auto deadline=GetTickCount64()+3000;
  while(!ready()&&GetTickCount64()<deadline){MSG message{};while(PeekMessageW(&message,window,WM_TIMER,WM_TIMER,PM_REMOVE))DispatchMessageW(&message);if(!ready())MsgWaitForMultipleObjectsEx(0,nullptr,20,QS_TIMER,MWMO_INPUTAVAILABLE);}
  require(ready(),"Ordinary curve timer failed to complete within three seconds");
}
void hiddenPreviewAndGuideOnly(Owner &owner){
  {
    owner.focus();Fixture f(owner.window,false);f.tool.initializeHidden();require(f.previews==0&&f.tool.snapshot().at("previewSamples")==0,"Hidden initialization executed preview");
    ScreamSeq::NativeToolWindow *shell=&f.tool;shell->dock(owner.window);shell->dockBounds(0,48,440,300);const auto before=retained(f.tool.snapshot());const auto reads=f.reads;
    shell->show();pumpTimers(shell->window(),[&]{return f.previews==1;});require(GetFocus()==owner.edit&&f.reads==reads&&retained(f.tool.snapshot())==before&&f.tool.snapshot().at("previewSamples")==f.saved.at("pattern-a").size(),"Base-pointer show failed preview or reloaded/rebased capture");
    f.click(Tool::zoomIn);f.field(Tool::pointRow,L"unfinished -");const auto raw=retained(f.tool.snapshot());shell->hide();owner.focus();shell->show();pumpTimers(shell->window(),[&]{return f.previews==2;});
    require(GetFocus()==owner.edit&&f.reads==reads&&retained(f.tool.snapshot())==raw&&text(f.control(Tool::pointRow))==L"unfinished -"&&f.writes.empty(),"Hide/show preview changed retained raw field or focus");
  }
  HWND guide{};
  {
    Fixture f(owner.window,false);f.current.selected.reset();f.tool.openFormulaReference();guide=childTool(f.tool.window(),L"ScreamSeq.FormulaReference");const auto search=GetDlgItem(guide,2002);type(search,L"mix");SendMessageW(search,EM_SETSEL,1,2);
    require(!f.tool.operationGuard().at("initialized").get<bool>()&&!f.tool.capturedTarget()&&f.reads==0,"Guide-only owner unexpectedly captured a source");
    f.tool.dock(owner.window);f.tool.dockBounds(0,48,440,300);focus(search);ScreamSeq::NativeToolWindow *shell=&f.tool;shell->show();
    require(GetFocus()==search&&IsWindow(guide)&&text(search)==L"mix"&&caret(search)==std::pair<DWORD,DWORD>{1,2}&&f.reads==0&&!f.tool.operationGuard().at("initialized").get<bool>(),"Showing existing Guide-only owner read data or replaced Guide text/focus");
    rejected([&]{f.tool.initializeHidden();},"Existing Guide-only owner was treated as a fresh staged editor");
    f.current.selected=Tool::Target{"graph-a","node-a","pattern-a",0};++f.current.selectionGeneration;f.tool.openAt();
    require(f.tool.capturedCurrent()&&f.tool.operationGuard().at("initialized").get<bool>()&&f.reads==1&&IsWindow(guide)&&text(search)==L"mix"&&caret(search)==std::pair<DWORD,DWORD>{1,2},"Explicit open did not initialize already-visible Guide owner in place");
    require(IsWindowVisible(guide)&&!f.tool.retainedDraft(),"Visible Guide incorrectly blocks a clean owner");
    f.selectB();f.page(2);f.click(Tool::follow);
    require(f.tool.snapshot().at("patternID")=="pattern-b"&&f.reads==2&&IsWindowVisible(guide)&&text(search)==L"mix"&&caret(search)==std::pair<DWORD,DWORD>{1,2},"Explicit Load selection replaced or lost visible Guide-only child state");
  }
  require(!IsWindow(guide),"Formula Guide outlived sole curve owner");
}
void guideCloseFocusFallback(Owner &owner){
  owner.focus();Fixture f(owner.window,false);f.current.selected.reset();
  f.tool.openFormulaReference();const auto guide=childTool(f.tool.window(),L"ScreamSeq.FormulaReference");
  const auto search=GetDlgItem(guide,2002);type(search,L"mix");SendMessageW(search,EM_SETSEL,1,2);
  const auto before=retained(f.tool.snapshot());const auto context=f.current;
  require(!f.tool.visible()&&!f.tool.capturedTarget()&&f.reads==0,"Guide-close fixture unexpectedly opened a curve");
  const auto report=[&](const char *stage){
    const auto describe=[](HWND window)->Json{return {{"hwnd",reinterpret_cast<uintptr_t>(window)},{"valid",bool(IsWindow(window))},
      {"visible",bool(IsWindowVisible(window))},{"enabled",bool(IsWindowEnabled(window))},{"style",uint32_t(GetWindowLongPtrW(window,GWL_STYLE))},
      {"parent",reinterpret_cast<uintptr_t>(GetParent(window))},{"owner",reinterpret_cast<uintptr_t>(GetWindow(window,GW_OWNER))},
      {"root",reinterpret_cast<uintptr_t>(GetAncestor(window,GA_ROOT))}};};
    std::cout<<"GUIDE_FOCUS "<<Json{{"stage",stage},{"focus",reinterpret_cast<uintptr_t>(GetFocus())},
      {"active",reinterpret_cast<uintptr_t>(GetActiveWindow())},{"main",describe(owner.window)},
      {"curve",describe(f.tool.window())},{"guide",describe(guide)},{"search",describe(search)}}.dump()<<'\n';
  };
  report("before-close");
  require(IsWindowVisible(owner.window)&&IsWindowEnabled(owner.window),"Guide-close fixture Main is not a visible enabled owner");
  require(!(GetWindowLongPtrW(f.tool.window(),GWL_STYLE)&(WS_CHILD|WS_POPUP))&&GetWindow(f.tool.window(),GW_OWNER)==owner.window,
      "Guide-close fixture does not exercise the hidden overlapped-tool ownership chain");
  press(GetDlgItem(guide,2008));
  report("after-close");
  require(GetFocus()==owner.window&&IsWindowVisible(GetFocus())&&IsWindowEnabled(GetFocus()),"Closing Guide-only child focused hidden owner instead of Main");
  require(!IsWindowVisible(guide)&&!f.tool.visible()&&IsWindow(guide)&&retained(f.tool.snapshot())==before&&f.reads==0&&f.writes.empty(),"Guide-only close changed capture, visibility or child lifetime");
  require(f.current.document==context.document&&f.current.revision==context.revision&&f.current.selected==context.selected&&f.current.selectionGeneration==context.selectionGeneration,"Guide-only close changed document or source context");
  f.tool.openFormulaReference();require(childTool(f.tool.window(),L"ScreamSeq.FormulaReference")==guide&&text(search)==L"mix"&&caret(search)==std::pair<DWORD,DWORD>{1,2},"Guide-only close/reopen lost HWND, search or caret");
  f.tool.show();focus(search);press(GetDlgItem(guide,2008));
  require(GetFocus()==f.tool.window()&&f.tool.visible()&&retained(f.tool.snapshot())==before,"Visible owner lost its existing direct focus-return behavior");
  f.tool.openFormulaReference();owner.focus();SendMessageW(guide,WM_CLOSE,0,0);
  require(GetFocus()==owner.edit&&!IsWindowVisible(guide)&&retained(f.tool.snapshot())==before&&f.reads==0&&f.writes.empty(),"Closing an unfocused Guide stole unrelated retained focus");
}
void pendingCompletionFocus(Owner &owner){
  for(bool chooseUnrelated:{false,true}){
    Fixture f(owner.window);f.dock(owner.window);f.click(Tool::zoomIn);f.field(Tool::pointRow,L"unfinished -001");SendMessageW(f.control(Tool::pointRow),EM_SETSEL,3,8);
    const auto field=f.control(Tool::pointRow);const auto before=retained(f.tool.snapshot());bool observedPending=false;
    f.beforeReply=[&](const std::string &method,Json &){if(method!="automation.formula.preview")return;
      observedPending=true;require(f.tool.pending()&&!IsWindowEnabled(field)&&GetFocus()==nullptr,"Preview did not expose expected disabled-field/null-focus interval");
      if(chooseUnrelated)owner.focus();
    };
    // This case deliberately delivers the normal timer handler while the raw
    // point field is unfinished. Preview uses saved staged points, not fields.
    SendMessageW(f.tool.window(),WM_TIMER,3,0);
    require(observedPending&&!f.tool.pending()&&f.previews==1&&f.writes.empty()&&retained(f.tool.snapshot())==before&&text(field)==L"unfinished -001"&&caret(field)==std::pair<DWORD,DWORD>{3,8},"Pending preview completion changed raw draft/caret/capture");
    require(GetFocus()==(chooseUnrelated?owner.edit:field),"Pending preview stole unrelated valid focus or failed to restore its transient-null field");
  }
}
// Test-only access to the real shell's existing typography-refresh path.
// A qualified protected-member pointer is used on the actual base subobject;
// there is no object cast, alternate Formula implementation or production hook.
struct FontRefreshAccess : ScreamSeq::NativeToolWindow {
  static void refresh(ScreamSeq::NativeToolWindow &tool){
    const auto cachedDpi=&FontRefreshAccess::fontDpi_;
    const auto relayout=&FontRefreshAccess::layoutAll;
    tool.*cachedDpi=0;(tool.*relayout)();
  }
};
void formulaCodeColorAndUndo(Owner &owner){
  using Formula=ScreamSeq::FormulaWorkbenchWindow;
  const Json points=Json::array({{{"position",0},{"value",.5},{"curve","scripted"},{"formula","mix(start,end,t)"}}});
  unsigned writes=0;
  Formula tool(owner.window,L"Owned formula typography", "mix(start,end,t)",{{"points",points},{"rows",64},{"rowsPerBeat",4}},0,
    [](const std::string &method,const Json &)->Json{
      if(method=="automation.formula.reference")return {{"symbols",Json::array()},{"notes","Owned typography fixture"}};
      if(method=="automation.formula.preview")return {{"values",Json::array({Json::array({0,.5}),Json::array({16384,.5})})}};
      throw std::runtime_error("Unexpected typography fixture request");
    },[]{return true;},[&](const std::string &){++writes;return true;});
  ScreamSeq::Tests::ownGuiWindow(tool.window());tool.show();KillTimer(tool.window(),3);KillTimer(tool.window(),4);
  const auto code=GetDlgItem(tool.window(),2001);require(code&&IsWindowVisible(code),"Missing Formula RichEdit");
  require(ScreamSeq::Tests::accessibleName(code)==L"Formula source","Formula RichEdit lacks a native accessible name");
  auto appearance=[&](bool contrast=ScreamSeq::NativeControls::highContrast()){
    CHARRANGE retained{};SendMessageW(code,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&retained));
    POINT scroll{};SendMessageW(code,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));
    auto verify=[&](WPARAM scope){CHARFORMAT2W format{};format.cbSize=sizeof(format);
      SendMessageW(code,EM_GETCHARFORMAT,scope,reinterpret_cast<LPARAM>(&format));
      require((format.dwMask&CFM_COLOR)&&!(format.dwEffects&CFE_AUTOCOLOR)&&format.crTextColor==(contrast?GetSysColor(COLOR_WINDOWTEXT):RGB(218,232,241)),"Formula RichEdit lost explicit readable foreground");
      require(std::wstring_view(format.szFaceName)==L"Consolas","Formula RichEdit lost its code typeface");};
    verify(SCF_DEFAULT);verify(SCF_SELECTION);
    CHARRANGE all{0,-1};SendMessageW(code,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&all));verify(SCF_SELECTION);
    SendMessageW(code,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&retained));SendMessageW(code,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));
    const auto expectedBackground=contrast?GetSysColor(COLOR_WINDOW):RGB(16,23,31);
    require(COLORREF(SendMessageW(code,EM_SETBKGNDCOLOR,0,expectedBackground))==expectedBackground,"Formula RichEdit background does not match its foreground policy");
  };
  appearance();require(!SendMessageW(code,EM_CANUNDO,0,0),"Initial Formula formatting created an Undo item");
  const auto initial=tool.snapshot().at("source");type(code,L"mix(start,end,t) * .75");
  SendMessageW(code,EM_STOPGROUPTYPING,0,0);KillTimer(tool.window(),3);KillTimer(tool.window(),4);
  CHARRANGE selected{4,9};SendMessageW(code,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selected));
  POINT scroll{};SendMessageW(code,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));
  const auto retained=tool.snapshot();const auto focused=GetFocus();appearance();
  for(unsigned pass=0;pass<3;++pass){
    FontRefreshAccess::refresh(tool);appearance();
    // Exercise the system palette without changing the user's OS settings,
    // then let the real retained child theme path restore the actual policy.
    require(ScreamSeq::NativeRichText::applyPlainTextColors(code,true),"Apply plain-text system colors");appearance(true);
    const std::array<UINT,3> messages={WM_THEMECHANGED,WM_SYSCOLORCHANGE,WM_SETTINGCHANGE};
    SendMessageW(tool.window(),messages[pass],0,0);appearance();
    CHARRANGE after{};SendMessageW(code,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after));
    POINT afterScroll{};SendMessageW(code,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&afterScroll));
    require(tool.snapshot()==retained&&after.cpMin==4&&after.cpMax==9&&afterScroll.x==scroll.x&&afterScroll.y==scroll.y&&GetFocus()==focused,"Typography/theme refresh changed draft, caret, scroll or focus");
  }
  require(SendMessageW(code,EM_UNDO,0,0)&&tool.snapshot().at("source")==initial,"Font refresh consumed or damaged the sole raw-text Undo");
  require(!SendMessageW(code,EM_CANUNDO,0,0),"Font refresh added unexpected Undo history");
  require(SendMessageW(code,EM_REDO,0,0)&&tool.snapshot().at("source")==retained.at("source"),"Font refresh damaged raw-text Redo");
  appearance();require(writes==0,"Typography edited the captured point");
}
void childDraftsBecomeStaleWithoutReplacement(Owner &owner){
  Fixture f(owner.window,false);f.saved.at("pattern-a")[0]["curve"]="scripted";f.saved.at("pattern-a")[0]["formula"]="mix(start,end,t)";f.tool.openAt();KillTimer(f.tool.window(),3);f.dock(owner.window);
  const auto handle=f.tool.snapshot().at("handles")[0];const auto scale=GetDpiForWindow(f.tool.window())/96.;const auto position=MAKELPARAM(int(std::lround(handle.at("x").get<double>()*scale)),int(std::lround(handle.at("y").get<double>()*scale)));
  SendMessageW(f.tool.window(),WM_LBUTTONDOWN,MK_LBUTTON,position);SendMessageW(f.tool.window(),WM_LBUTTONUP,0,position);f.page(1);f.click(Tool::expand);
  const auto formula=childTool(f.tool.window(),L"ScreamSeq.FormulaWorkbench"),code=GetDlgItem(formula,2001);type(code,L"mix(start,end,t) * .75");
  CHARRANGE range{4,9};SendMessageW(code,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&range));const auto source=f.tool.formulaWorkbenchSnapshot().at("source");
  f.page(2);f.click(Tool::bank);const auto bank=childTool(f.tool.window(),L"ScreamSeq.EnvelopeBank"),name=GetDlgItem(bank,1003);type(name,L"Retained template draft");
  require(f.tool.formulaWorkbenchSnapshot().at("sourceCurrent").get<bool>()&&f.tool.envelopeBankSnapshot().at("sourceCurrent").get<bool>(),"Child fixture was stale before recapture");
  require(!f.tool.followSelection(),"Automatic follow replaced retained child drafts");f.selectB();f.click(Tool::follow);
  const auto captured=retained(f.tool.snapshot());const auto work=f.tool.formulaWorkbenchSnapshot(),savedBank=f.tool.envelopeBankSnapshot();
  require(captured.at("patternID")=="pattern-b"&&IsWindow(formula)&&IsWindow(bank)&&work.at("source")==source&&work.at("dirty").get<bool>()&&!work.at("sourceCurrent").get<bool>()&&savedBank.at("dirty").get<bool>()&&!savedBank.at("sourceCurrent").get<bool>()&&text(name)==L"Retained template draft","Successful explicit recapture replaced child text or kept old source writable");
  CHARRANGE selected{};SendMessageW(code,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selected));require(selected.cpMin==4&&selected.cpMax==9,"Recapture changed Formula caret");
  press(GetDlgItem(formula,2006));require(f.tool.formulaWorkbenchSnapshot().at("valid").get<bool>(),"Stale-use fixture did not first validate child text");
  require(!IsWindowEnabled(GetDlgItem(formula,2007))&&text(GetDlgItem(formula,2012)).find(L"Source changed")!=std::wstring::npos,
      "Stale Formula still advertises a usable captured source");
  for(int id:{1005,1007,1008,1009})require(!IsWindowEnabled(GetDlgItem(bank,id)),"Stale Bank source action remains enabled");
  require(text(GetDlgItem(bank,1100)).find(L"Source changed")!=std::wstring::npos&&IsWindowEnabled(name)&&IsWindowEnabled(GetDlgItem(bank,1006)),
      "Stale source presentation hid the Bank warning or disabled its independent master draft");
  // A forged command must still reach the existing guarded rejection. Disabled
  // controls alone never constitute the captured-source mutation guard.
  SendMessageW(formula,WM_COMMAND,MAKEWPARAM(2007,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(formula,2007)));
  require(f.tool.formulaWorkbenchSnapshot().at("visible").get<bool>()&&f.tool.formulaWorkbenchSnapshot().at("source")==source&&retained(f.tool.snapshot())==captured&&f.writes.empty(),"Stale Formula Use changed new captured curve or lost child text");
}
}
int wmain(int argc,wchar_t **argv){try{
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  ScreamSeq::Tests::runPrivateGuiProcess(L"ScreamSeqGraphCurveOwner",argc,argv,[]{
    const auto result=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);ScreamSeq::check(result,"Initialize graph curve test COM");struct Com{~Com(){CoUninitialize();}} com;
    Owner owner;
    automaticCurve(owner);minimumPages(owner);std::cout<<"PASS graph curve minimum pages and reachable actions\n";
    retainedFieldsAndFocus(owner);std::cout<<"PASS retained HWND/raw fields/caret/axes and focused short transition\n";
    stableIdentityAndExplicitRecapture(owner);std::cout<<"PASS stable pattern identity, deleted target and explicit successful recapture\n";
    pumpedReadGuards(owner);std::cout<<"PASS pumped read identity/generation guards preserve newer state\n";
    hiddenInitialization(owner);std::cout<<"PASS pure hidden initialization and existing-window failure isolation\n";
    hiddenPreviewAndGuideOnly(owner);std::cout<<"PASS ordinary hidden preview resumption and Guide-only owner adoption\n";
    guideCloseFocusFallback(owner);std::cout<<"PASS Guide-only close returns to visible owner without replacing retained child\n";
    pendingCompletionFocus(owner);std::cout<<"PASS pending preview transient-null restoration and unrelated valid focus survival\n";
    childDraftsBecomeStaleWithoutReplacement(owner);std::cout<<"PASS Formula/Bank draft ownership and stale-source rejection\n";
    formulaCodeColorAndUndo(owner);std::cout<<"PASS Formula explicit native color, typography refresh, caret and raw-text Undo/Redo\n";
    owner.close();
    // Exercise 96-DPI native control metrics even on a high-DPI developer
    // display. Only newly created fixture HWNDs use this thread-local context.
    const auto oldDpi=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE);
    require(oldDpi!=nullptr,"Select fixture-local 96-DPI context");
    struct Dpi {DPI_AWARENESS_CONTEXT previous;~Dpi(){SetThreadDpiAwarenessContext(previous);}} dpi{oldDpi};
    Owner unscaled;require(GetDpiForWindow(unscaled.window)==96,"96-DPI fixture was not established");
    minimumPages(unscaled);unscaled.close();std::cout<<"PASS graph curve 96-DPI minimum pages and reachable actions\n";
  });return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
