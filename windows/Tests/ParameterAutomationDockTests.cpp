#include "../App/ParameterAutomationWindow.hpp"
#include "PrivateGuiTest.hpp"
#include <iostream>
#include <set>

namespace {
using Json=ScreamSeq::Api::Json;
using Tool=ScreamSeq::ParameterAutomationWindow;
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class Action>void rejected(Action action,const char *message){try{action();}catch(const std::exception &){return;}throw std::runtime_error(message);}
struct Owner {
  HWND window{},edit{};
  Owner(){
    WNDCLASSW type{};type.lpfnWndProc=DefWindowProcW;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"ScreamSeq.ParameterDock.TestOwner";RegisterClassW(&type);
    window=CreateWindowExW(0,type.lpszClassName,L"Automation dock test",WS_OVERLAPPEDWINDOW|WS_VISIBLE|WS_CLIPCHILDREN,0,0,1200,900,nullptr,nullptr,type.hInstance,nullptr);
    require(window,"Create automation dock owner");ScreamSeq::Tests::ownGuiWindow(window);
    edit=CreateWindowExW(0,L"EDIT",L"Unrelated retained text",WS_CHILD|WS_VISIBLE|WS_TABSTOP,10,10,200,26,window,nullptr,type.hInstance,nullptr);
    require(edit,"Create unrelated field");ScreamSeq::Tests::ownGuiWindow(edit);
  }
  ~Owner(){if(window)DestroyWindow(window);}
  void close(){const auto parent=window,child=edit;require(DestroyWindow(parent),"Destroy automation dock owner");window=edit=nullptr;require(!IsWindow(parent)&&!IsWindow(child),"Automation dock owner survived destruction");}
};
void sizeClient(HWND window,int width,int height){
  ScreamSeq::Tests::sizeOwnedGuiClient(window,width,height);
}
std::wstring text(HWND window){std::wstring result(size_t(GetWindowTextLengthW(window))+1,0);GetWindowTextW(window,result.data(),int(result.size()));result.resize(wcslen(result.c_str()));return result;}
std::pair<DWORD,DWORD> selection(HWND window){DWORD first=0,last=0;SendMessageW(window,EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));return {first,last};}
void key(HWND window,WPARAM code,bool ctrl=false){
  struct Restore {BYTE state[256]{};Restore(){require(GetKeyboardState(state),"Read test keyboard state");}~Restore(){SetKeyboardState(state);}}restore;
  BYTE modified[256]{};std::copy(std::begin(restore.state),std::end(restore.state),std::begin(modified));
  for(int id:{VK_CONTROL,VK_LCONTROL,VK_RCONTROL,VK_SHIFT,VK_LSHIFT,VK_RSHIFT,VK_MENU,VK_LMENU,VK_RMENU})modified[id]&=0x7f;
  if(ctrl)modified[VK_CONTROL]|=0x80;require(SetKeyboardState(modified),"Set test keyboard modifiers");SendMessageW(window,WM_KEYDOWN,code,0);
}
Json retained(const Json &value){Json result;for(const auto field:{"generation","document","expectedRevision","patternID","plugin","parameter","lane","dirty","fieldDraft","toolFieldDraft","points","selectedPoint","start","end","valueLow","valueHigh"})result[field]=value.at(field);return result;}
struct Fixture {
  Tool::Cursor current{"document-a","r0",0,Json::array({{{"id","pattern-a"},{"index",0},{"name","Captured pattern"}}}),Json::array({{{"instanceID","plugin-a"},{"name","Captured plugin"}}})};
  Json saved=Json::array({{{"position",2048},{"value",.25},{"curve","linear"}},{{"position",8192},{"value",.75},{"curve","smooth"}}});
  std::vector<std::pair<std::string,Json>> writes;
  unsigned reads=0,previews=0;
  std::function<void(const std::string &)> beforeRequest;
  Tool tool;
  explicit Fixture(HWND owner,bool open=true):tool(owner,[this](const std::string &method,const Json &params){return request(method,params);},[this]{return current;},[](const std::string &,uint32_t){},[](const std::string &,uint32_t){}){
    ScreamSeq::Tests::ownGuiWindow(tool.window());if(open){tool.openAt("plugin-a",7);sizeClient(tool.window(),1100,760);}KillTimer(tool.window(),3);
  }
  Json request(const std::string &method,const Json &params){
    if(beforeRequest)beforeRequest(method);
    if(method=="automation.pattern.get"){++reads;return {{"patternID","pattern-a"},{"rows",64},{"rowsPerBeat",4},{"lanes",Json::array({{{"id","lane-a"},{"plugin","plugin-a"},{"parameter",7},{"enabled",true},{"points",saved}}})}};}
    if(method=="plugin.parameters.get")return Json::array({{{"id",7},{"name","Filter resonance"}},{{"id",8},{"name","A second selectable parameter"}}});
    if(method=="automation.formula.preview"){++previews;Json values=Json::array();for(const auto &point:params.at("points"))values.push_back(Json::array({point.at("position"),point.at("value")}));return {{"values",values}};}
    if(method=="automation.pattern.set"){
      require(params.at("expectedRevision")==current.revision,"Apply lost captured revision");writes.emplace_back(method,params);
      if(!params.at("dryRun").get<bool>()){saved=params.at("points");current.revision="r"+std::to_string(writes.size());}return {{"wouldChange",true}};
    }
    throw std::runtime_error("Unexpected automation dock fixture request: "+method);
  }
  HWND control(int id)const{const auto result=GetDlgItem(tool.window(),id);require(result,"Missing automation control");return result;}
  void click(int id){require(IsWindowVisible(control(id)),"Fixture tried to click an unavailable page action");SendMessageW(control(id),BM_CLICK,0,0);}
  void choose(int id,int index){require(IsWindowVisible(control(id)),"Fixture selected hidden combo");SendMessageW(control(id),CB_SETCURSEL,index,0);SendMessageW(tool.window(),WM_COMMAND,MAKEWPARAM(id,CBN_SELCHANGE),reinterpret_cast<LPARAM>(control(id)));}
  void focus(int id){SetActiveWindow(GetAncestor(tool.window(),GA_ROOT));SetFocus(control(id));require(GetFocus()==control(id),"Focus automation native field");}
  void dock(HWND parent,int height=300){tool.dock(parent);tool.dockBounds(0,48,Tool::dockMinimumWidth,float(height));}
};
void checkBounds(Fixture &f,std::set<int> *available=nullptr){
  RECT client{};GetClientRect(f.tool.window(),&client);std::vector<std::pair<int,RECT>> boxes;
  for(HWND child=GetWindow(f.tool.window(),GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){
    if(!IsWindowVisible(child))continue;const auto id=GetDlgCtrlID(child);RECT box{};GetWindowRect(child,&box);MapWindowPoints(nullptr,f.tool.window(),reinterpret_cast<POINT *>(&box),2);
    if(box.left<0||box.top<0||box.right>client.right||box.bottom>client.bottom||box.right<=box.left||box.bottom<=box.top)throw std::runtime_error("Automation control escapes client: "+std::to_string(id));
    for(const auto &[otherID,other]:boxes){RECT intersection{};if(IntersectRect(&intersection,&box,&other))throw std::runtime_error("Overlapping automation controls: "+std::to_string(id)+" and "+std::to_string(otherID));}
    boxes.emplace_back(id,box);if(available)available->insert(id);
  }
  const auto view=f.tool.snapshot();if(view.at("canvasVisible").get<bool>()){
    const auto &canvas=view.at("canvas");const float scale=GetDpiForWindow(f.tool.window())/96.f;
    // Include the existing 22-DIP axis ruler in the curve's reserved rectangle.
    RECT box{LONG(std::lround(canvas[0].get<float>()*scale)),LONG(std::lround((canvas[1].get<float>()-22)*scale)),LONG(std::lround((canvas[0].get<float>()+canvas[2].get<float>())*scale)),LONG(std::lround((canvas[1].get<float>()+canvas[3].get<float>())*scale))};
    for(const auto &[id,other]:boxes){RECT intersection{};if(IntersectRect(&intersection,&box,&other))throw std::runtime_error("Automation control overlaps curve/ruler: "+std::to_string(id));}
  }
}
void minimumBoundsAndActions(Owner &owner){
  Fixture f(owner.window);const auto window=f.tool.window();f.dock(owner.window);std::set<int> available;
  for(const auto height:{300,310}){
    f.tool.dockBounds(0,48,440,float(height));
    for(const auto page:{4241,4242,4243,4244}){
      f.click(page);if(page==4244)f.choose(4230,5);checkBounds(f,&available);
      const auto view=f.tool.snapshot();require(view.at("shortDock").get<bool>()&&view.at("compact").get<bool>(),"Minimum dock did not use short pages");
      for(const auto action:{4215,4216,4218,4310})require(IsWindowVisible(f.control(action)),"Short page hid persistent Apply/Verify/Reload/status");
      if(page==4242){const auto height=view.at("canvas")[3].get<float>();require(height>=100&&height<=120,"Short dock curve is not 100–120 DIPs tall");}
    }
  }
  for(int id=4201;id<=4244;++id)require(available.contains(id),"An original automation action/field is unreachable from all short pages");
  require(f.tool.window()==window&&f.current.revision=="r0"&&f.writes.empty(),"Reflow recreated the editor or changed song state");
  f.tool.floatWindow();sizeClient(window,440,500);require(!f.tool.snapshot().at("shortDock").get<bool>(),"Floating minimum incorrectly uses dock reflow");
  MINMAXINFO minimum{};SendMessageW(window,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&minimum));
  RECT expected{0,0,MulDiv(440,GetDpiForWindow(window),96),MulDiv(500,GetDpiForWindow(window),96)};
  require(AdjustWindowRectExForDpi(&expected,DWORD(GetWindowLongPtrW(window,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(window,GWL_EXSTYLE)),GetDpiForWindow(window)),"Calculate floating minimum frame");
  require(minimum.ptMinTrackSize.x==expected.right-expected.left&&minimum.ptMinTrackSize.y==expected.bottom-expected.top,"Short dock changed floating 440×500 minimum");
  for(const auto page:{4241,4242,4243,4244}){f.click(page);checkBounds(f);}
  sizeClient(window,1100,760);require(!f.tool.snapshot().at("compact").get<bool>(),"Wide automation layout was lost");checkBounds(f);
}
void pointDraftAndAxes(Owner &owner){
  Fixture f(owner.window);f.click(4227);const auto beforeZoom=f.tool.snapshot();require(beforeZoom.at("start").get<double>()>0,"Axis retention fixture did not zoom");
  f.focus(4207);SetWindowTextW(f.control(4207),L"00--.125");SendMessageW(f.control(4207),EM_SETSEL,2,5);
  const auto field=f.control(4207),window=f.tool.window();const auto before=retained(f.tool.snapshot());const auto reads=f.reads;
  f.dock(owner.window);require(f.tool.snapshot().at("page")=="curve","Shrink did not retain visible point field page");
  require(f.control(4207)==field&&GetFocus()==field&&selection(field)==std::pair<DWORD,DWORD>{2,5}&&text(field)==L"00--.125","Dock reflow lost raw point text/caret/HWND/focus");
  f.tool.dockBounds(0,48,460,310);f.tool.floatWindow();sizeClient(window,440,500);
  require(GetFocus()==field&&selection(field)==std::pair<DWORD,DWORD>{2,5}&&text(field)==L"00--.125"&&retained(f.tool.snapshot())==before,"Float/resize changed point draft, captured context or viewport axes");
  f.dock(owner.window);f.tool.hide();f.tool.show();require(retained(f.tool.snapshot())==before&&text(field)==L"00--.125"&&selection(field)==std::pair<DWORD,DWORD>{2,5},"Hide/show reconstructed a point draft");
  SetActiveWindow(owner.window);SetFocus(owner.edit);f.tool.dockBounds(0,48,440,310);
  require(GetFocus()==owner.edit&&f.reads==reads&&f.writes.empty(),"Layout stole unrelated focus or reloaded/submitted the draft");
}
void formulaAndToolDrafts(Owner &owner){
  Fixture f(owner.window);f.choose(4205,8);f.focus(4209);SetWindowTextW(f.control(4209),L"mix(start, end, t) + -");SendMessageW(f.control(4209),EM_SETSEL,4,9);
  const auto formula=f.control(4209);const auto before=retained(f.tool.snapshot());f.dock(owner.window);
  require(f.tool.snapshot().at("page")=="formula"&&GetFocus()==formula&&selection(formula)==std::pair<DWORD,DWORD>{4,9}&&retained(f.tool.snapshot())==before,"Short dock hid or reconstructed focused formula draft");
  f.tool.floatWindow();sizeClient(f.tool.window(),1100,760);require(GetFocus()==formula&&text(formula)==L"mix(start, end, t) + -"&&selection(formula)==std::pair<DWORD,DWORD>{4,9},"Float lost formula caret/raw text");
  f.choose(4230,5);f.focus(4233);SetWindowTextW(f.control(4233),L"-001.25");SendMessageW(f.control(4233),EM_SETSEL,1,4);SetWindowTextW(f.control(4231),L"-");
  const auto option=f.control(4233);const auto toolBefore=retained(f.tool.snapshot());f.dock(owner.window);
  require(f.tool.snapshot().at("page")=="tools"&&GetFocus()==option&&selection(option)==std::pair<DWORD,DWORD>{1,4}&&retained(f.tool.snapshot())==toolBefore,"Short dock lost independent tool draft or focus");
  f.tool.dockBounds(0,48,440,310);f.click(4242);f.click(4244);
  require(text(option)==L"-001.25"&&text(f.control(4231))==L"-"&&text(formula)==L"mix(start, end, t) + -"&&selection(option)==std::pair<DWORD,DWORD>{1,4}&&retained(f.tool.snapshot())==toolBefore,"Page/resize changed hidden formula or tool fields");
  f.current.revision="external";const auto writes=f.writes.size();f.focus(4233);key(option,VK_RETURN,true);
  require(f.writes.size()==writes&&f.tool.snapshot().at("stale").get<bool>()&&text(option)==L"-001.25"&&text(formula)==L"mix(start, end, t) + -","Short dock shortcut rebased or submitted stale retained fields");
}
void curveMappingAndKeyboard(Owner &owner){
  Fixture f(owner.window);f.dock(owner.window);f.click(4242);f.click(4227);
  auto before=f.tool.snapshot();const auto &canvas=before.at("canvas");
  const double x=canvas[0].get<double>()+canvas[2].get<double>()*.25,y=canvas[1].get<double>()+canvas[3].get<double>()*.5;
  const double position=before.at("start").get<double>()+(before.at("end").get<double>()-before.at("start").get<double>())*.25;
  const auto scale=GetDpiForWindow(f.tool.window())/96.;const auto point=MAKELPARAM(int(std::lround(x*scale)),int(std::lround(y*scale)));
  SendMessageW(f.tool.window(),WM_LBUTTONDOWN,MK_LBUTTON,point);SendMessageW(f.tool.window(),WM_LBUTTONUP,0,point);
  const auto edited=f.tool.snapshot();const auto selected=edited.at("selectedPoint").get<size_t>();
  require(edited.at("points")[selected].at("position")==uint32_t(std::round(position/256)*256)&&std::abs(edited.at("points")[selected].at("value").get<double>()-.5)<.01,"Short curve click no longer maps through retained row/value axes");
  require(f.current.revision=="r0"&&f.writes.empty()&&edited.at("dirty").get<bool>(),"Canvas click committed instead of staging");
  key(f.tool.window(),VK_F6);require(GetFocus()==f.control(4207),"F6 did not reach point field from short canvas");
  SetWindowTextW(f.control(4207),L"24.5");SetWindowTextW(f.control(4208),L"62.5");key(f.control(4207),VK_RETURN);
  require(!f.tool.snapshot().at("fieldDraft").get<bool>()&&f.tool.snapshot().at("points")[selected].at("position")==6272,"Enter did not stage exact point fields");
  key(f.control(4207),VK_RETURN,true);require(f.writes.size()==1&&f.writes.front().second.at("expectedRevision")=="r0"&&f.writes.front().second.at("points")==f.saved,"Ctrl+Enter did not apply exactly the captured curve once");
  key(f.control(4207),VK_NEXT,true);require(f.tool.snapshot().at("page")=="formula","Ctrl+PageDown no longer reaches Formula");
  key(GetFocus(),VK_PRIOR,true);require(f.tool.snapshot().at("page")=="curve","Ctrl+PageUp no longer returns to Curve");
  f.current.revision="outside";const auto retainedBefore=retained(f.tool.snapshot());key(GetFocus(),VK_RETURN,true);
  require(f.writes.size()==1&&retained(f.tool.snapshot())==retainedBefore&&f.tool.snapshot().at("stale").get<bool>(),"Stale Apply changed song, draft or captured revision");
}
void hiddenInitialization(Owner &owner){
  SetActiveWindow(owner.window);SetFocus(owner.edit);const auto outside=GetFocus();
  {
    Fixture staged(owner.window,false);const auto window=staged.tool.window();unsigned placements=0;staged.tool.placementChanged([&]{++placements;});
    staged.tool.initializeHidden("plugin-a",7);const auto ready=staged.tool.snapshot();
    require(!staged.tool.visible()&&!staged.tool.docked()&&GetFocus()==outside&&placements==0,"Hidden initialization changed visibility, placement or external focus");
    require(ready.at("document")=="document-a"&&ready.at("expectedRevision")=="r0"&&ready.at("patternID")=="pattern-a"&&ready.at("plugin")=="plugin-a"&&ready.at("parameter")==7&&ready.at("points")==staged.saved&&!ready.at("stale").get<bool>()&&!ready.at("pending").get<bool>(),"Hidden initialization did not prepare the captured target");
    const auto state=retained(ready);const auto reads=staged.reads;
    rejected([&]{staged.tool.initializeHidden("plugin-a",8);},"Repeated hidden initialization was accepted");
    require(staged.tool.window()==window&&retained(staged.tool.snapshot())==state&&staged.reads==reads&&GetFocus()==outside&&placements==0,"Rejected initialization reloaded or changed the prepared target");
  }
  {
    Fixture visible(owner.window,false);visible.tool.show();const auto reads=visible.reads;
    rejected([&]{visible.tool.initializeHidden();},"Visible initialization was accepted");require(visible.reads==reads,"Visible initialization reached the API");
  }
  {
    Fixture docked(owner.window,false);docked.tool.dock(owner.window);
    rejected([&]{docked.tool.initializeHidden();},"Docked initialization was accepted");require(docked.reads==0,"Docked initialization reached the API");
  }
  {
    Fixture drafted(owner.window,false);SetWindowTextW(drafted.control(4207),L"-");
    require(drafted.tool.retainedDraft(),"Nonfresh initialization fixture did not stage raw text");
    rejected([&]{drafted.tool.initializeHidden();},"Raw draft initialization was accepted");require(drafted.reads==0&&text(drafted.control(4207))==L"-","Rejected initialization erased unbound raw text");
  }
  Fixture existing(owner.window);existing.focus(4207);SetWindowTextW(existing.control(4207),L"00--.25");SendMessageW(existing.control(4207),EM_SETSEL,2,5);
  const auto existingWindow=existing.tool.window(),focused=GetFocus();const auto before=retained(existing.tool.snapshot());HWND abandoned{};
  {
    Fixture staged(owner.window,false);abandoned=staged.tool.window();staged.beforeRequest=[](const std::string &method){if(method=="automation.pattern.get")throw std::runtime_error("Injected initial automation read failure");};
    rejected([&]{staged.tool.initializeHidden();},"Initial read failure did not propagate");
    require(!staged.tool.visible()&&!staged.tool.docked()&&staged.tool.snapshot().at("document")==""&&!staged.tool.snapshot().at("pending").get<bool>(),"Failed hidden stage was shown or captured");
  }
  require(!IsWindow(abandoned)&&existing.tool.window()==existingWindow&&existing.tool.visible()&&GetFocus()==focused&&retained(existing.tool.snapshot())==before&&text(focused)==L"00--.25"&&selection(focused)==std::pair<DWORD,DWORD>{2,5},"Failed staged window changed an existing editor or survived cleanup");
  {
    Fixture staged(owner.window,false);staged.beforeRequest=[](const std::string &method){if(method=="plugin.parameters.get")throw std::runtime_error("Unavailable parameter catalog");};
    staged.tool.initializeHidden("plugin-a",7);
    require(!staged.tool.visible()&&staged.tool.snapshot().at("lane")=="lane-a"&&staged.tool.snapshot().at("parameter")==7&&staged.tool.snapshot().at("status")=="Unavailable parameter catalog"&&GetFocus()==focused,"Hidden staging changed the existing nonfatal parameter catalog fallback");
  }
}
template<class Ready>void pumpOrdinaryTimers(HWND window,Ready ready){
  const auto deadline=GetTickCount64()+3000;
  while(!ready()&&GetTickCount64()<deadline){
    MSG message{};while(PeekMessageW(&message,window,WM_TIMER,WM_TIMER,PM_REMOVE))DispatchMessageW(&message);
    if(!ready())MsgWaitForMultipleObjectsEx(0,nullptr,20,QS_TIMER,MWMO_INPUTAVAILABLE);
  }
  require(ready(),"Ordinary automation presentation timer did not complete within three seconds");
}
void hiddenPreviewResume(Owner &owner){
  SetActiveWindow(owner.window);SetFocus(owner.edit);
  Fixture staged(owner.window,false);staged.tool.initializeHidden("plugin-a",7);
  require(staged.previews==0&&staged.tool.snapshot().at("previewSamples")==0,"Hidden preparation requested a visible curve preview");
  ScreamSeq::NativeToolWindow *shell=&staged.tool;
  shell->dock(owner.window);shell->dockBounds(0,48,440,300);
  const auto captured=retained(staged.tool.snapshot());const auto reads=staged.reads;
  shell->show();require(GetFocus()==owner.edit,"Showing a prepared dock stole external focus");
  pumpOrdinaryTimers(shell->window(),[&]{return staged.previews==1;});
  require(staged.tool.snapshot().at("previewSamples")==staged.saved.size()&&retained(staged.tool.snapshot())==captured&&staged.reads==reads&&staged.writes.empty()&&GetFocus()==owner.edit,"Base-pointer show failed to render the prepared curve without reload, mutation or focus change");

  staged.click(4227);staged.focus(4207);SetWindowTextW(staged.control(4207),L"00--.125");SendMessageW(staged.control(4207),EM_SETSEL,2,5);
  const auto draft=retained(staged.tool.snapshot());shell->hide();SetFocus(owner.edit);
  // Let the actual outstanding preview timer expire while hidden. Its handler
  // keeps the pending preview but removes the timer; showing must resume it.
  bool hiddenTimer=false;const auto deadline=GetTickCount64()+3000;
  while(!hiddenTimer&&GetTickCount64()<deadline){
    MSG message{};while(PeekMessageW(&message,shell->window(),WM_TIMER,WM_TIMER,PM_REMOVE)){hiddenTimer|=message.wParam==3;DispatchMessageW(&message);}
    if(!hiddenTimer)MsgWaitForMultipleObjectsEx(0,nullptr,20,QS_TIMER,MWMO_INPUTAVAILABLE);
  }
  require(hiddenTimer&&staged.previews==1&&staged.reads==reads,"Hidden timer unexpectedly read preview or target data");
  shell->show();pumpOrdinaryTimers(shell->window(),[&]{return staged.previews==2;});
  require(retained(staged.tool.snapshot())==draft&&text(staged.control(4207))==L"00--.125"&&selection(staged.control(4207))==std::pair<DWORD,DWORD>{2,5}&&staged.reads==reads&&staged.writes.empty()&&GetFocus()==owner.edit,"Hide/show preview restart lost a raw field, caret, target, revision or external focus");
}
}
int main(){try{
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  ScreamSeq::Tests::runPrivateGui(L"ScreamSeqParameterDock",[]{
    // Like the existing retained-tool fixture, this uses intrinsic User32
    // controls; every real HWND is checked after creation below.
    Owner owner;minimumBoundsAndActions(owner);pointDraftAndAxes(owner);formulaAndToolDrafts(owner);curveMappingAndKeyboard(owner);hiddenInitialization(owner);hiddenPreviewResume(owner);owner.close();
  });
  std::cout<<"PASS automation short dock: 440x300/310 page bounds, complete actions, unchanged floating minimum, retained HWND/text/caret/axes/context, curve hit mapping, guarded keyboard Apply, pure hidden initialization and ordinary preview timer resumption\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
