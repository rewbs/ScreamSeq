#include "../App/SongTimingWindow.hpp"
#include "PrivateGuiTest.hpp"
#include <iostream>

namespace {
using Json=ScreamSeq::Api::Json;
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
Json context(const std::string &revision="r1",unsigned sequence=0,const std::string &document="owned-song"){
  return {{"documentId",document},{"revision",revision},{"editable",true},{"busy",false},{"sequence",sequence},{"sequenceName","Verse"}};
}
Json timing(unsigned sequence=0){return {{"mode","classic"},{"tempo",125.0},{"speed",6},{"rowsPerBeat",4},{"rowsPerMeasure",16},{"groove",Json::array()},
  {"sequence",sequence},{"patternOverrides",Json::array({1,4})},{"grooveActive",false}};}
struct Owner {
  HWND window{},edit{};
  Owner(){WNDCLASSW kind{};kind.lpfnWndProc=DefWindowProcW;kind.hInstance=GetModuleHandleW(nullptr);kind.lpszClassName=L"ScreamSeq.SongTiming.TestOwner";RegisterClassW(&kind);
    window=CreateWindowExW(0,kind.lpszClassName,L"Timing test owner",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,1100,800,nullptr,nullptr,kind.hInstance,nullptr);require(window,"Create timing test owner");ScreamSeq::Tests::ownGuiWindow(window);
    edit=CreateWindowExW(0,L"EDIT",L"An unrelated draft",WS_CHILD|WS_VISIBLE|WS_TABSTOP,10,10,220,26,window,nullptr,kind.hInstance,nullptr);require(edit,"Create unrelated focus target");ScreamSeq::Tests::ownGuiWindow(edit);}
  ~Owner(){if(window)DestroyWindow(window);}
};
struct Form {
  Json current=context(),saved=timing(),lastParams;
  unsigned loads=0,applies=0,returns=0,revision=1;
  std::function<Json()> onLoad;
  std::function<Json(const Json &)> onApply;
  ScreamSeq::SongTimingWindow tool;
  explicit Form(HWND owner):tool(owner,{[this]{++loads;return onLoad?onLoad():Json{{"context",current},{"timing",saved}};},
      [this](Json parameters){++applies;lastParams=parameters;return onApply?onApply(parameters):result(parameters);},[this]{++returns;}}){ScreamSeq::Tests::ownGuiWindow(tool.window());}
  HWND control(int id)const{const auto window=GetDlgItem(tool.window(),id);require(window,"Timing control missing");return window;}
  void click(int id){SendMessageW(tool.window(),WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),reinterpret_cast<LPARAM>(control(id)));}
  void write(int id,const wchar_t *value){require(SetWindowTextW(control(id),value),"Set timing draft");}
  void choose(int index){SendMessageW(control(7601),CB_SETCURSEL,index,0);SendMessageW(tool.window(),WM_COMMAND,MAKEWPARAM(7601,CBN_SELCHANGE),reinterpret_cast<LPARAM>(control(7601)));}
  void open(){tool.open(current);SetActiveWindow(tool.window());}
  Json result(const Json &parameters){
    auto after=saved;for(const auto *key:{"mode","tempo","speed","rowsPerBeat","rowsPerMeasure","groove"})after[key]=parameters.at(key);
    after["grooveActive"]=after["mode"]=="modern"&&!after["groove"].empty();const bool changed=after!=saved;const auto before=saved;const bool dry=parameters.at("dryRun").get<bool>();
    if(changed&&!dry){saved=after;current["revision"]="r"+std::to_string(++revision);}
    return {{"context",current},{"result",{{"before",before},{"after",after},{"wouldChange",changed},{"dryRun",dry}}}};
  }
  void key(int id,WPARAM key,bool ctrl=false,bool shift=false){
    BYTE previous[256]{},keys[256]{};GetKeyboardState(previous);std::copy(std::begin(previous),std::end(previous),std::begin(keys));
    keys[VK_CONTROL]=ctrl?0x80:0;keys[VK_SHIFT]=shift?0x80:0;keys[VK_MENU]=0;SetKeyboardState(keys);SetFocus(control(id));SendMessageW(control(id),WM_KEYDOWN,key,0);SetKeyboardState(previous);
  }
};
std::pair<DWORD,DWORD> selection(HWND window){DWORD start=0,end=0;SendMessageW(window,EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));return {start,end};}
void retainedDraftAndReload(Owner &owner){
  Form form(owner.window);form.open();require(form.loads==1&&form.tool.snapshot()["loaded"]==true,"Initial open did not load timing");require(GetFocus()==form.control(7601),"Initial open left focus on the tool shell");
  form.write(7602,L"130.");form.write(7605,L"unfinished");SetFocus(form.control(7602));SendMessageW(form.control(7602),EM_SETSEL,1,3);
  const auto raw=form.tool.snapshot().at("draft"),captured=form.tool.snapshot().at("captured");
  auto busy=form.current;busy["busy"]=true;form.tool.update(busy);require(form.tool.snapshot()["pending"]==true,"Busy context did not disable timing actions");
  form.tool.update(form.current);form.open();require(form.loads==1&&form.tool.snapshot()["draft"]==raw&&GetFocus()==form.control(7602)&&selection(form.control(7602))==std::pair<DWORD,DWORD>{1,3},"Polling or reopening replaced raw fields, focus or caret");
  form.current=context("r2",1,"other-song");form.saved=timing(1);form.tool.update(form.current);
  require(form.tool.snapshot()["stale"]==true&&form.tool.snapshot()["captured"]==captured&&form.tool.snapshot()["draft"]==raw&&!IsWindowEnabled(form.control(7611)),"New song silently rebound the draft");
  form.tool.hide();form.tool.update(form.current);require(!form.tool.visible(),"Context update reopened the hidden form");form.open();require(form.loads==1&&form.tool.snapshot()["draft"]==raw,"Hide/reopen discarded a stale draft");
  form.onLoad=[]{throw std::runtime_error("Cannot read this song yet");return Json();};form.click(7612);
  require(form.tool.snapshot()["error"]=="Cannot read this song yet"&&form.tool.snapshot()["draft"]==raw&&form.tool.snapshot()["pending"]==false,"Failed Reload lost the draft or stayed pending");
  form.onLoad={};form.click(7612);require(form.tool.snapshot()["captured"]==form.current&&form.tool.snapshot()["stale"]==false&&form.tool.snapshot()["dirty"]==false&&form.tool.snapshot()["draft"]["rowsPerMeasure"]=="16","Explicit Reload did not adopt the current song");
  SetActiveWindow(owner.window);SetFocus(owner.edit);form.tool.update(form.current);require(GetFocus()==owner.edit,"Polling stole another editor's focus");
  auto readonly=form.current;readonly["editable"]=false;form.tool.update(readonly);require(!IsWindowEnabled(form.control(7611)),"Read-only document permits Apply");
}
void previewValidationAndPresets(Owner &owner){
  Form form(owner.window);form.open();form.choose(2);form.write(7602,L"130.50");form.write(7606,L"1, 1, 1, 1");SetFocus(form.control(7602));SendMessageW(form.control(7602),EM_SETSEL,1,4);
  form.click(7610);auto snapshot=form.tool.snapshot();require(form.applies==1&&form.lastParams["dryRun"]==true&&form.lastParams["expectedRevision"]=="r1"&&snapshot["preview"]["wouldChange"]==true,"Preview did not send the captured revision");
  require(snapshot["draft"]["tempo"]=="130.50"&&snapshot["draft"]["groove"]=="1, 1, 1, 1"&&GetFocus()==form.control(7602)&&selection(form.control(7602))==std::pair<DWORD,DWORD>{1,4},"Preview canonicalized the raw draft or disturbed its caret");
  const std::array<std::pair<int,const wchar_t *>,7> invalid{{{7602,L"nan"},{7602,L"513"},{7603,L"1.5"},{7604,L"0"},{7605,L"129"},{7606,L"1, 1, 1,"},{7606,L"1, 0.2, 1, 1"}}};
  for(const auto &[id,text]:invalid){const auto before=form.applies;form.write(id,text);form.click(7610);require(form.applies==before&&!form.tool.snapshot()["error"].get<std::string>().empty(),"Invalid timing reached the host");form.click(7612);form.choose(2);}
  form.write(7607,L"87.5");form.click(7608);require(form.tool.snapshot()["draft"]["mode"]=="modern"&&form.tool.snapshot()["draft"]["groove"]=="1.75, 0.25, 1.75, 0.25","Swing upper boundary differs from Mac");
  form.write(7607,L"12.5");form.click(7608);require(form.tool.snapshot()["draft"]["groove"]=="0.25, 1.75, 0.25, 1.75","Swing lower boundary differs from Mac");
  form.write(7604,L"3");const auto groove=form.tool.snapshot()["draft"]["groove"];form.click(7608);require(form.tool.snapshot()["draft"]["groove"]==groove&&!form.tool.snapshot()["error"].get<std::string>().empty(),"Odd-row swing changed the draft");
  form.write(7604,L"4");form.click(7609);require(form.tool.snapshot()["draft"]["groove"]=="","Straight did not clear the local groove");
  form.write(7602,L"130.50");form.click(7611);snapshot=form.tool.snapshot();require(form.lastParams["dryRun"]==false&&snapshot["captured"]["revision"]=="r2"&&snapshot["draft"]["tempo"]=="130.5"&&snapshot["dirty"]==false,"Apply did not adopt canonical saved timing");
  require(snapshot["preview"].is_null()&&snapshot["status"].get<std::string>().find("Undo")!=std::string::npos,"Applied result retained a stale preview or omitted Undo");
  form.click(7611);require(form.tool.snapshot()["captured"]["revision"]=="r2"&&form.tool.snapshot()["status"].get<std::string>().find("playback is unchanged")!=std::string::npos,"No-op Apply misreported a change");
}
void pumpedRequests(Owner &owner){
  Form form(owner.window);form.open();form.write(7602,L"140");
  form.onApply=[&](const Json &parameters){require(form.tool.snapshot()["pending"]==true&&IsWindowEnabled(form.control(7602)),"Pending preview does not retain editable fields");const auto calls=form.applies;form.click(7611);form.click(7612);require(form.applies==calls&&form.loads==1,"Pending timing request reentered Apply or Reload");form.write(7602,L"141.");return form.result(parameters);};
  form.click(7610);require(form.tool.snapshot()["draft"]["tempo"]=="141."&&form.tool.snapshot()["preview"].is_null()&&form.tool.snapshot()["captured"]["revision"]=="r1","Older preview overwrote the pumped draft");
  form.onApply=[&](const Json &parameters){form.write(7602,L"142.");auto reply=form.result(parameters);form.tool.update(form.current);return reply;};
  form.click(7611);require(form.tool.snapshot()["draft"]["tempo"]=="142."&&form.tool.snapshot()["captured"]["revision"]=="r1"&&form.tool.snapshot()["context"]["revision"]=="r2"&&form.tool.snapshot()["stale"]==true,"Apply silently rebased a newer in-flight draft");
  form.onApply={};form.click(7612);form.write(7602,L"143");const auto captured=form.tool.snapshot()["captured"];
  form.onApply=[&](const Json &parameters){const auto reply=form.result(parameters);form.tool.update(context("different",1,"other-song"));SetActiveWindow(owner.window);SetFocus(owner.edit);return reply;};
  form.click(7611);require(form.tool.snapshot()["captured"]==captured&&form.tool.snapshot()["draft"]["tempo"]=="143"&&form.tool.snapshot()["context"]["documentId"]=="other-song"&&GetFocus()==owner.edit,"Obsolete Apply redirected song, draft or focus");
  form.current=context("other",1,"other-song");form.saved=timing(1);form.tool.update(form.current);form.onLoad=[&]{form.write(7602,L"still typing");return Json{{"context",form.current},{"timing",form.saved}};};
  form.click(7612);require(form.tool.snapshot()["draft"]["tempo"]=="still typing"&&form.tool.snapshot()["captured"]==captured,"Reload discarded a field edited while its callback pumped");
  form.onLoad={};form.click(7612);form.write(7602,L"150");form.onApply=[](const Json &){throw std::runtime_error("The song changed before Apply");return Json();};form.click(7611);
  require(form.tool.snapshot()["draft"]["tempo"]=="150"&&form.tool.snapshot()["pending"]==false&&form.tool.snapshot()["error"]=="The song changed before Apply","Rejected Apply lost its raw draft or pending guard");
}
void responseValidationAndKeyboard(Owner &owner){
  Form form(owner.window);form.open();const auto before=form.tool.snapshot();
  form.onLoad=[&]{auto broken=form.saved;broken["groove"]=Json::array({1.0});return Json{{"context",form.current},{"timing",broken}};};form.click(7612);
  require(form.tool.snapshot()["draft"]==before["draft"]&&form.tool.snapshot()["captured"]==before["captured"]&&!form.tool.snapshot()["error"].get<std::string>().empty(),"Malformed timing response partially replaced the form");
  form.onLoad={};form.click(7612);form.write(7602,L"160");form.key(7602,VK_RETURN);require(form.applies==0,"Plain Return in a numeric field applied timing");
  form.key(7602,VK_RETURN,true);require(form.applies==1&&form.tool.snapshot()["draft"]["tempo"]=="160","Ctrl+Enter did not apply timing");
  form.key(7611,VK_RETURN);require(form.applies==2&&GetFocus()==form.control(7611),"Completed Apply lost native button focus");
  form.key(7602,VK_F6);require(form.returns==1,"F6 did not return to the pattern");
  form.key(7613,VK_RETURN);require(form.returns==2,"Return to pattern button is not keyboard accessible");
  form.key(7602,VK_TAB);require(GetFocus()==form.control(7603),"Timing fields have an unexpected Tab order");
  SetFocus(form.tool.window());SendMessageW(form.tool.window(),WM_KEYDOWN,VK_TAB,0);require(GetFocus()==form.control(7601),"Tab could not leave the timing shell");
  form.write(7606,L"unfinished, ");form.key(7606,VK_ESCAPE);require(!form.tool.visible(),"Escape did not close timing tool");form.open();require(form.tool.snapshot()["draft"]["groove"]=="unfinished, ","Escape/reopen discarded raw groove text");
  SetFocus(form.control(7602));const auto beforeClose=form.returns;SendMessageW(form.tool.window(),WM_CLOSE,0,0);require(!form.tool.visible()&&form.returns==beforeClose+1,"Title-bar close did not return owned focus through the host");
  const auto returns=form.returns;SetFocus(owner.edit);form.tool.hide();SendMessageW(form.tool.window(),WM_KEYDOWN,VK_F6,0);require(form.returns==returns,"Hidden timing tool redirected unrelated focus");
}
void geometryAndPrecision(Owner &owner){
  Form form(owner.window);form.saved["mode"]="alternative";form.saved["groove"]=Json::array({1.2345678806304932,.7654321193695068,1.2345678806304932,.7654321193695068});form.open();form.click(7610);
  require(form.applies==1&&form.lastParams["groove"]==form.saved["groove"],"An unchanged legacy groove did not round-trip precisely");
  for(const auto size:std::array<std::pair<int,int>,2>{{{660,560},{920,720}}}){
    const auto window=form.tool.window();const auto dpi=GetDpiForWindow(window);RECT frame{0,0,MulDiv(size.first,dpi,96),MulDiv(size.second,dpi,96)};
    require(AdjustWindowRectExForDpi(&frame,DWORD(GetWindowLongPtrW(window,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(window,GWL_EXSTYLE)),dpi),"Calculate timing frame");
    require(SetWindowPos(window,nullptr,0,0,frame.right-frame.left,frame.bottom-frame.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE),"Resize timing tool");
    const auto snapshot=form.tool.snapshot();require(snapshot["client"]==Json::array({MulDiv(size.first,dpi,96),MulDiv(size.second,dpi,96)}),"Timing client minimum differs from requested DIP size");
    std::vector<RECT> boxes;for(const auto &control:snapshot["controls"]){const auto &b=control.at("bounds");RECT box{b[0].get<LONG>(),b[1].get<LONG>(),b[2].get<LONG>(),b[3].get<LONG>()};
      require(box.left>=0&&box.top>=0&&box.right<=snapshot["client"][0].get<LONG>()&&box.bottom<=snapshot["client"][1].get<LONG>()&&box.right>box.left&&box.bottom>box.top,"Timing control outside client bounds");
      for(const auto &other:boxes){RECT intersection{};require(!IntersectRect(&intersection,&box,&other),"Timing controls overlap");}boxes.push_back(box);
    }
    require(boxes.size()==26,"Timing snapshot omitted visible controls");std::cout<<"Timing bounds "<<size.first<<" x "<<size.second<<" DIPs at "<<dpi<<" DPI\n";
  }
}
void longPreviewRetention(Owner &owner){
  Form form(owner.window);form.saved["mode"]="modern";form.saved["rowsPerBeat"]=32;form.saved["rowsPerMeasure"]=64;form.saved["grooveActive"]=true;
  form.saved["groove"]=Json::array();for(unsigned row=0;row<32;++row)form.saved["groove"].push_back(row%2?.7654321193695068:1.2345678806304932);
  form.open();form.click(7610);const auto preview=form.control(7710);const auto style=GetWindowLongPtrW(preview,GWL_STYLE);
  require((style&(ES_MULTILINE|ES_READONLY|WS_VSCROLL))==(ES_MULTILINE|ES_READONLY|WS_VSCROLL),"Long timing preview is not a read-only scrollable text field");
  const auto length=GetWindowTextLengthW(preview);std::wstring text(size_t(length)+1,0);GetWindowTextW(preview,text.data(),int(text.size()));text.resize(size_t(length));
  require(length>650&&text.find(L"1.2345678806304932")!=std::wstring::npos&&text.find(L"0.76543211936950684")!=std::wstring::npos,"Preview lost normalized groove precision");
  const auto firstBreak=text.find(L"\r\n");require(firstBreak!=std::wstring::npos&&SendMessageW(preview,EM_LINEINDEX,1,0)==LRESULT(firstBreak+2),"Preview heading does not end with a native hard line break");
  for(size_t i=0;i<text.size();++i)if(text[i]==L'\n')require(i>0&&text[i-1]==L'\r',"Preview contains a bare LF instead of a native CRLF");
  require(SendMessageW(preview,EM_GETLINECOUNT,0,0)>4,"Long preview cannot scroll to all groove values");
  SetFocus(preview);SendMessageW(preview,EM_SETSEL,45,75);SendMessageW(preview,EM_LINESCROLL,0,4);const auto top=SendMessageW(preview,EM_GETFIRSTVISIBLELINE,0,0);const auto selected=selection(preview);
  const auto draft=form.tool.snapshot()["draft"];form.tool.update(form.current);SendMessageW(form.tool.window(),WM_SIZE,SIZE_RESTORED,0);
  require(GetFocus()==preview&&selection(preview)==selected&&SendMessageW(preview,EM_GETFIRSTVISIBLELINE,0,0)==top,"Polling or layout reset preview focus, selection or scroll");
  SendMessageW(preview,WM_CHAR,'X',0);require(GetWindowTextLengthW(preview)==length&&form.tool.snapshot()["draft"]==draft,"Read-only preview accepted text or changed the draft");
}
void initialPlacementAndReopen(Owner &owner){
  RECT original{};GetWindowRect(owner.window,&original);MONITORINFO monitor{sizeof(monitor)};require(GetMonitorInfoW(MonitorFromWindow(owner.window,MONITOR_DEFAULTTONEAREST),&monitor),"Read timing owner work area");
  require(SetWindowPos(owner.window,nullptr,monitor.rcWork.right-180,monitor.rcWork.bottom-120,180,120,SWP_NOZORDER|SWP_NOACTIVATE),"Position owned fixture near monitor edge");
  require(GetMonitorInfoW(MonitorFromWindow(owner.window,MONITOR_DEFAULTTONEAREST),&monitor),"Read relocated owner work area");const auto work=monitor.rcWork;
  {Form form(owner.window);form.onLoad=[]{throw std::runtime_error("Initial load unavailable");return Json();};form.open();RECT bounds{};GetWindowRect(form.tool.window(),&bounds);
    require(bounds.left>=work.left&&bounds.top>=work.top,"Initial timing tool started above or left of its owner work area");
    require(bounds.right-bounds.left>work.right-work.left?bounds.left==work.left:bounds.right<=work.right,"Initial timing tool did not clamp its right edge");
    require(bounds.bottom-bounds.top>work.bottom-work.top?bounds.top==work.top:bounds.bottom<=work.bottom,"Initial timing tool did not clamp its bottom edge");
    require(SetWindowPos(form.tool.window(),nullptr,work.right-160,work.top+30,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE),"Move retained timing tool");GetWindowRect(form.tool.window(),&bounds);
    form.tool.hide();form.open();RECT reopened{};GetWindowRect(form.tool.window(),&reopened);require(EqualRect(&bounds,&reopened),"Reopening after failed load discarded user placement");
    require(form.loads==1,"Reopening after failed load silently retried Reload");
  }
  require(SetWindowPos(owner.window,nullptr,original.left,original.top,original.right-original.left,original.bottom-original.top,SWP_NOZORDER|SWP_NOACTIVATE),"Restore owned fixture placement");
}
}
int main(){try{SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);ScreamSeq::Tests::runPrivateGui(L"ScreamSeqSongTimingTest",[]{Owner owner;retainedDraftAndReload(owner);previewValidationAndPresets(owner);pumpedRequests(owner);responseValidationAndKeyboard(owner);geometryAndPrecision(owner);longPreviewRetention(owner);initialPlacementAndReopen(owner);});
  std::cout<<"PASS timing tool: raw drafts, exact context, preview/apply, pumped edits, focus, keyboard and native bounds\n";return 0;}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
