#include "common/stdafx.h"
#include "../App/MixerStripsWindow.hpp"
#include "PrivateGuiTest.hpp"
#include <set>

namespace {
using Json=ScreamSeq::Api::Json;
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
std::wstring text(HWND control){std::wstring value(size_t(GetWindowTextLengthW(control))+1,0);GetWindowTextW(control,value.data(),int(value.size()));value.resize(wcslen(value.c_str()));return value;}
RECT bounds(HWND control,HWND parent){RECT value{};check(GetWindowRect(control,&value),"Read mixer control bounds");MapWindowPoints(nullptr,parent,reinterpret_cast<POINT *>(&value),2);return value;}
struct Owner {
  HWND window{};
  Owner(){WNDCLASSW type{};type.lpfnWndProc=DefWindowProcW;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"ScreamSeq.MixerStrips.TestOwner";RegisterClassW(&type);
    window=CreateWindowExW(0,type.lpszClassName,L"Owned mixer fixture",WS_OVERLAPPEDWINDOW|WS_VISIBLE|WS_CLIPCHILDREN,0,0,1000,700,nullptr,nullptr,type.hInstance,nullptr);
    check(window,"Create owned mixer host");ScreamSeq::Tests::ownGuiWindow(window);}
  ~Owner(){if(window)DestroyWindow(window);}
};
struct Fixture {
  Json data={{"active",true},{"buses",Json::array()}};
  std::string revision="r1";unsigned writes=0,previews=0;bool unknown=false;Json lastWrite,lastPreview;
  double audible=-6.123456789;
  std::function<void()> duringWrite;
  std::function<void(const std::string &)> duringRead;
  ScreamSeq::MixerStripsWindow tool;
  explicit Fixture(HWND owner,unsigned busCount=12):tool(owner,
    [this](const auto &method,const auto &params){return read(method,params);},
    [this]{return std::pair(std::string("owned-song"),revision);},
    [this](const std::string &method,const Json &params,const auto &)->ScreamSeq::Api::CompletedCall {
      check(method=="mixer.bus.set","Unexpected durable mixer operation");check(params.at("expectedRevision")==revision,"Final lost its captured revision");
      ++writes;lastWrite=params;auto &bus=find(params.at("bus").get<std::string>());
      for(const auto &[key,value]:params.items())if(bus.contains(key)&&key!="id")bus[key]=value;
      audible=bus.at("gainDB").get<double>();revision="r"+std::to_string(writes+1);
      if(duringWrite){auto callback=std::exchange(duringWrite,{});callback();}
      if(unknown)throw ScreamSeq::Api::ApiError(-32003,"Owned lost response",Tracker::WriteOutcome{});
      return {method,"owned-song",revision,{{"wouldChange",true}}};
    },[]{},[](const auto &){},[]{}) {
    for(unsigned i=0;i<busCount;++i)data["buses"].push_back({{"id","n"+std::to_string(i+1)},
      {"name",i+1==busCount?"Master":"Track "+std::to_string(i+1)},{"gainDB",i==0?audible:0},
      {"pan",0},{"preGainDB",0},{"prePan",0},{"width",1},{"mute",false},{"solo",false}});
    ScreamSeq::Tests::ownGuiWindow(tool.window());tool.dock(owner);tool.dockBounds(0,0,528,260);tool.show();tool.update();
  }
  Json &find(const std::string &id){for(auto &value:data["buses"])if(value["id"]==id)return value;throw std::runtime_error("Unknown captured mixer bus");}
  Json read(const std::string &method,const Json &params) {
    if(duringRead)duringRead(method);
    if(method=="mixer.get")return data;
    if(method=="synchronizeView")return Json::object();
    check(method=="mixer.bus.set"&&params.value("preview",false),"Unexpected preview operation");
    check(params.at("expectedRevision")==revision,"Preview/reset lost revision guard");++previews;lastPreview=params;
    if(params.contains("gainDB"))audible=params.at("gainDB").get<double>();return Json::object();
  }
  HWND control(int id){auto value=GetDlgItem(tool.window(),id);check(value,"Missing native mixer control");return value;}
  void press(int id){check(IsWindowEnabled(control(id)),"Native action is disabled");SendMessageW(tool.window(),WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),reinterpret_cast<LPARAM>(control(id)));}
  void slide(int position,WORD notification=TB_THUMBTRACK){auto h=control(101);SendMessageW(h,TBM_SETPOS,TRUE,position);SendMessageW(tool.window(),WM_VSCROLL,MAKEWPARAM(notification,position),reinterpret_cast<LPARAM>(h));}
};
void interactions(HWND owner) {
  Fixture f(owner);
  const auto rounded=int(SendMessageW(f.control(101),TBM_GETPOS,0,0));
  f.slide(rounded,TB_ENDTRACK);check(f.writes==0&&!f.tool.hasGesture(),"Releasing an untouched rounded slider committed a value");
  f.slide(330);f.slide(360);check(f.previews==0,"Pointer events did not coalesce before the UI service tick");
  f.tool.update();check(f.previews==1&&f.audible==-12&&f.find("n1")["gainDB"]==-6.123456789,"Preview changed saved state or lost latest value");
  f.slide(360,TB_ENDTRACK);check(f.writes==1&&f.find("n1")["gainDB"]==-12&&!f.tool.hasGesture(),"Drag must finish in one durable operation");
  f.slide(420);f.tool.update();f.find("n1")["gainDB"]=-2;f.revision="external";
  f.press(12);check(f.audible==-2&&f.writes==1&&!f.tool.hasGesture(),"Cancel overwrote a newer accepted value");
  SetFocus(f.control(102));SetWindowTextW(f.control(102),L"--");
  const auto draft=f.tool.documentDraft();check(draft&&draft->dirty&&draft->target=="n1","Invalid raw gain was not retained against its bus");
  f.tool.hide();f.tool.show();f.tool.update();wchar_t raw[16]{};GetWindowTextW(f.control(102),raw,16);
  check(std::wstring(raw)==L"--"&&f.tool.documentDraft()->generation==draft->generation,"Show/refresh discarded or reformatted raw text");
  f.press(12);check(!f.tool.hasGesture()&&f.audible==-2,"Cancel failed to restore saved controls while not playing");
  f.slide(400);SendMessageW(f.control(101),WM_CAPTURECHANGED,0,0);f.tool.update();
  check(!f.tool.hasGesture()&&f.writes==1&&f.audible==-2,"Capture loss committed an unfinished drag");
  f.unknown=true;f.slide(390);f.slide(390,TB_ENDTRACK);
  check(f.writes==2&&f.tool.documentDraft()->uncertain,"Lost final response did not retain reconciliation state");
  f.press(13);check(f.writes==2&&f.tool.documentDraft()->uncertain,"Review repeated a durable mixer edit");
  f.press(14);check(f.writes==2&&!f.tool.hasGesture()&&f.audible==-15,"Accept current repeated or lost the saved result");
}
void layoutAndIdentity(HWND owner) {
  Fixture f(owner);
  f.tool.dockBounds(0,0,528,226);f.tool.update();
  const auto balance=bounds(f.control(103),f.tool.window()),mute=bounds(f.control(104),f.tool.window());
  check(balance.bottom<=mute.top,"Short mixer dock overlaps balance with Mute/Solo");
  RECT client{};GetClientRect(f.tool.window(),&client);
  check(bounds(f.control(106),f.tool.window()).bottom<=client.bottom,"Default short dock clips Details");
  SetFocus(f.control(102));SendMessageW(f.control(102),EM_SETSEL,1,3);
  const auto first=text(f.control(100)),second=text(f.control(116));
  std::swap(f.data["buses"][0],f.data["buses"][1]);f.revision="reordered";f.tool.update();
  DWORD start=0,end=0;SendMessageW(f.control(102),EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));
  check(text(f.control(100))==first&&text(f.control(116))==second&&GetFocus()==f.control(102)&&start==1&&end==3,
    "External reorder rebound a focused strip, duplicated a neighbor, or moved its caret");
  // Shrinking a retained page must keep the focused late strip on screen.
  SetFocus(f.control(118));f.tool.dockBounds(0,0,280,150);f.tool.update();
  GetClientRect(f.tool.window(),&client);const auto focused=bounds(f.control(118),f.tool.window());
  check(IsWindowVisible(f.control(118))&&focused.left>=0&&focused.right<=client.right,
    "Narrow dock clipped the focused strip instead of retaining its identity in view");
  SetFocus(f.control(122));const auto detail=bounds(f.control(122),f.tool.window());
  check(detail.top>=0&&detail.bottom<=client.bottom,"Tab focus did not scroll Details into a short viewport");
  SendMessageW(f.tool.window(),WM_VSCROLL,SB_TOP,0);
  check(bounds(f.control(10),f.tool.window()).top>=0,"Native scrollbar cannot reach mixer navigation");
  // Removed focused identities stay visibly unavailable until focus leaves;
  // they must never become controls for the replacement at the same index.
  f.data["buses"].erase(f.data["buses"].begin());f.revision="deleted";f.tool.update();
  check(text(f.control(116))==L"Bus unavailable"&&!IsWindowEnabled(f.control(117))&&!IsWindowEnabled(f.control(122)),
    "Deleted focused bus silently rebound or remained writable");
  SetFocus(f.tool.window());f.tool.update();
  check(text(f.control(100))==first,"Leaving retained focus did not adopt the current ordered page");
  f.unknown=true;f.slide(390);f.slide(390,TB_ENDTRACK);
  SetFocus(f.control(13));f.tool.update();
  GetClientRect(f.tool.window(),&client);
  for(int id:{13,14}) {const auto r=bounds(f.control(id),f.tool.window());
    check(IsWindowVisible(f.control(id))&&r.left>=0&&r.right<=client.right&&r.top>=0&&r.bottom<=client.bottom,
      "Narrow uncertain-result actions are not both reachable");}
}
void inputAndWidth(HWND owner) {
  Fixture f(owner);f.tool.dockBounds(0,0,280,150);
  auto adjust=[&](int id,int value,WORD notification){auto control=f.control(id);SetFocus(control);
    SendMessageW(control,TBM_SETPOS,TRUE,value);SendMessageW(f.tool.window(),WM_HSCROLL,MAKEWPARAM(notification,value),reinterpret_cast<LPARAM>(control));};
  adjust(108,40,TB_THUMBTRACK);f.tool.update();
  check(f.lastPreview.at("prePan")==-.6&&!f.lastPreview.contains("pan")&&f.find("n1")["prePan"]==0,
    "Pre-balance preview changed post balance or persisted before release");
  adjust(108,40,TB_ENDTRACK);
  check(f.writes==1&&f.lastWrite.at("prePan")==-.6&&f.find("n1")["pan"]==0,"Pre-balance final lost its independent control");
  adjust(109,150,TB_THUMBTRACK);f.tool.update();adjust(109,150,TB_ENDTRACK);
  check(f.writes==2&&f.lastWrite.at("width")==1.5&&f.find("n1")["width"]==1.5,"Width did not commit its musical ratio once");
  SetFocus(f.control(107));SetWindowTextW(f.control(107),L"-9.123456789");
  SendMessageW(f.control(107),WM_KEYDOWN,VK_RETURN,0);
  check(f.writes==3&&f.lastWrite.at("preGainDB")==-9.123456789&&!f.lastWrite.contains("gainDB"),
    "Typed pre gain was rounded, retargeted, or omitted from the guarded final");
  SetWindowTextW(f.control(107),L"100");SendMessageW(f.control(107),WM_KEYDOWN,VK_RETURN,0);
  check(f.writes==3&&f.tool.hasGesture()&&text(f.control(107))==L"100","Out-of-range pre gain was submitted or discarded");
  SendMessageW(f.control(107),WM_KEYDOWN,VK_ESCAPE,0);
  check(!f.tool.hasGesture()&&f.find("n1")["preGainDB"]==-9.123456789&&text(f.control(107))==L"-9.123456789",
    "Pre-gain cancel lost the exact saved baseline or left rejected text in the focused field");
  for(int id:{107,108,109}) {SetFocus(f.control(id));RECT client{};GetClientRect(f.tool.window(),&client);const auto r=bounds(f.control(id),f.tool.window());
    check(r.top>=0&&r.bottom<=client.bottom,"Input/width control cannot be reached by keyboard in a short dock");}
  SetFocus(f.tool.window());while(IsWindowEnabled(f.control(11)))f.press(11);
  bool master=false;for(int i=0;i<16;++i){auto title=GetDlgItem(f.tool.window(),100+i*16);if(title&&IsWindowVisible(title)&&text(title)==L"Master")master=true;}
  check(master,"Late Master is unreachable through native mixer navigation");
}
void pendingSliderInput(HWND owner) {
  for(bool keyboard:{false,true}) {
    Fixture f(owner);bool injected=false;f.slide(330);
    f.duringRead=[&](const auto &method){if(method=="mixer.bus.set"&&!injected){
      injected=true;f.slide(360);
      if(keyboard)SendMessageW(f.control(101),WM_KEYUP,VK_DOWN,0);else f.slide(360,TB_ENDTRACK);
      check(f.writes==0,"A pending preview recursively submitted a durable write");
    }};
    f.tool.update();
    check(injected&&f.previews==1&&f.writes==1&&f.lastWrite.at("gainDB")==-12&&
      f.find("n1")["gainDB"]==-12&&!f.tool.hasGesture(),
      "Release or key-up during preview lost the latest slider value or its one final commit");
  }
  Fixture f(owner);f.slide(360);
  f.duringWrite=[&]{f.slide(420);f.slide(420,TB_ENDTRACK);};
  f.slide(360,TB_ENDTRACK);
  check(f.writes==1&&f.find("n1")["gainDB"]==-12&&f.tool.hasGesture()&&
    text(f.control(102))==L"-18"&&!f.tool.documentDraft()->uncertain,
    "Final completion lost a newer slider gesture or recursively committed it");
  f.tool.update();check(f.writes==1&&f.previews==0,"Completed write replayed a newer gesture against its stale revision");
  SendMessageW(f.control(101),WM_CAPTURECHANGED,0,0);f.tool.hide();f.tool.show();f.tool.update();
  check(f.tool.hasGesture()&&text(f.control(102))==L"-18"&&f.writes==1&&f.previews==0,
    "Hiding or capture loss discarded newer slider input after an earlier final completed");
  f.press(12);check(f.writes==1&&!f.tool.hasGesture()&&f.audible==-12,"Cancel failed to reset the newer slider to current saved state");
}
void pendingTextRetention(HWND owner) {
  for(unsigned scenario=0;scenario<3;++scenario) {
    Fixture f(owner);SetFocus(f.control(102));SetWindowTextW(f.control(102),L"-9");
    const auto generation=f.tool.documentDraft()->generation;
    const auto newer=[&](const wchar_t *raw){SetWindowTextW(f.control(102),raw);SendMessageW(f.control(102),EM_SETSEL,1,2);};
    if(scenario==0)f.duringWrite=[&]{newer(L"--");};
    if(scenario==1)f.duringRead=[&](const auto &method){if(method=="mixer.get")newer(L"--");};
    if(scenario==2){f.unknown=true;f.duringWrite=[&]{newer(L"--");};}
    SendMessageW(f.control(102),WM_KEYDOWN,VK_RETURN,0);
    DWORD start=0,end=0;SendMessageW(f.control(102),EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));
    check(f.writes==1&&f.find("n1")["gainDB"]==-9&&text(f.control(102))==L"--"&&start==1&&end==2&&GetFocus()==f.control(102)&&
      f.tool.documentDraft()->dirty&&f.tool.documentDraft()->generation>generation,
      "Final completion lost newer native raw text, caret, generation or original committed value");
    if(scenario!=0) {
      check(f.tool.documentDraft()->uncertain,"Failed readback or unknown completion did not retain its original outcome");
      f.duringRead=[&](const auto &method){if(method=="mixer.get")newer(L"-.");};
      f.press(13);
      check(f.writes==1&&f.tool.documentDraft()->uncertain&&text(f.control(102))==L"-.",
        "Review discarded newer input or repeated the original write");
      f.duringRead={};f.press(13);
      if(scenario==2) {
        check(f.tool.documentDraft()->uncertain,"Unknown observation was promoted to a verified write");
        f.press(14);check(f.writes==1&&!f.tool.hasGesture()&&f.audible==-9,"Explicit current acknowledgement repeated the write");
        continue;
      }
    }
    check(!f.tool.documentDraft()->uncertain&&f.tool.snapshot().at("report").at("outcome")=="returned",
      "Exact receipt was lost when newer input survived its completion");
    SendMessageW(f.control(102),WM_KEYDOWN,VK_RETURN,0);
    check(f.writes==1&&f.tool.hasGesture(),"Newer input silently rebased itself onto the earlier completion");
    f.duringRead=[&](const auto &method){if(method=="mixer.bus.set")newer(L"newer");};
    f.press(12);
    check(f.tool.hasGesture()&&text(f.control(102))==L"newer"&&f.writes==1,
      "Cancel's pumped reset discarded newer raw input");
    f.duringRead={};f.press(12);
    check(!f.tool.hasGesture()&&text(f.control(102))==L"-9"&&f.audible==-9&&f.writes==1,
      "Explicit Cancel failed to restore the current saved value without a second write");
  }
}
void viewportPool(HWND owner) {
  Fixture f(owner,240);
  auto bound=[&](const std::string &bus) {
    const auto snapshot=f.tool.snapshot();
    for(const auto &strip:snapshot.at("strips"))if(strip.at("bus")==bus)return strip;
    throw std::runtime_error("Expected bus is absent from the native strip pool: "+bus);
  };
  const auto capacity=f.tool.snapshot().at("visibleCapacity").get<size_t>();
  SetFocus(f.tool.window());SendMessageW(f.tool.window(),WM_KEYDOWN,VK_TAB,0);
  check(GetFocus()!=f.tool.window()&&IsChild(f.tool.window(),GetFocus()),"Keyboard cannot enter strips after the palette focuses their root");
  SetFocus(f.tool.window());
  const int neighbor=bound("n2").at("controlBase");
  SendMessageW(f.tool.window(),WM_HSCROLL,SB_LINERIGHT,0);
  check(bound("n2").at("controlBase")==neighbor,"Scrolling recycled an unchanged neighboring HWND");
  SendMessageW(f.tool.window(),WM_HSCROLL,SB_LEFT,0);
  const int capturedBase=bound("n1").at("controlBase");
  const auto captured=f.control(capturedBase+2);
  SetFocus(captured);SetWindowTextW(captured,L"--");SendMessageW(captured,EM_SETSEL,1,2);
  const auto draft=f.tool.documentDraft();
  check(draft&&draft->dirty&&draft->target=="n1","Pool fixture did not capture its invalid raw draft");
  SendMessageW(f.tool.window(),WM_HSCROLL,SB_RIGHT,0);
  auto snapshot=f.tool.snapshot();
  check(snapshot.at("firstBus")==240-capacity&&bound("n240").at("inViewport")==true,
    "Native horizontal scrollbar did not expose the late Master");
  DWORD start=0,end=0;SendMessageW(captured,EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));
  check(GetFocus()==captured&&text(captured)==L"--"&&start==1&&end==2&&
    bound("n1").at("controlBase")==capturedBase&&f.tool.documentDraft()->generation==draft->generation,
    "Scrolling retargeted a captured HWND or changed raw text, caret or generation");
  auto bounded=[&] {
    const auto state=f.tool.snapshot();std::set<std::string> identities;std::set<size_t> positions;
    for(const auto &strip:state.at("strips")) {
      check(identities.insert(strip.at("bus").get<std::string>()).second,"Pool duplicated a bus identity");
      check(positions.insert(strip.at("position").get<size_t>()).second,"Pool duplicated a bus position");
    }
    check(state.at("allocatedStrips").get<size_t>()<=capacity+3,
      "Native HWND allocation grew with song length instead of visible neighbors and the captured owner");
  };
  bounded();
  for(unsigned i=0;i<240;++i){SendMessageW(f.tool.window(),WM_HSCROLL,SB_LINELEFT,0);bounded();}
  for(unsigned i=0;i<240;++i){SendMessageW(f.tool.window(),WM_HSCROLL,SB_LINERIGHT,0);bounded();}
  check(f.writes==0&&f.previews==0&&text(captured)==L"--","Viewport navigation performed or discarded an edit");
  // A key on an offscreen focused field reveals that same control before input.
  SendMessageW(captured,WM_KEYDOWN,VK_F1,0);
  RECT client{};GetClientRect(f.tool.window(),&client);const auto r=bounds(captured,f.tool.window());
  check(r.left>=0&&r.right<=client.right&&GetFocus()==captured,"Keyboard input failed to reveal the captured bus");
  SendMessageW(captured,WM_KEYDOWN,VK_ESCAPE,0);SetFocus(f.tool.window());f.tool.update();
  SendMessageW(f.tool.window(),WM_HSCROLL,SB_RIGHT,0);
  const int beforeMaster=bound("n239").at("controlBase"),master=bound("n240").at("controlBase");
  check(GetNextDlgTabItem(f.tool.window(),f.control(beforeMaster+9),FALSE)==f.control(master+1)&&
    GetNextDlgTabItem(f.tool.window(),f.control(master+1),TRUE)==f.control(beforeMaster+9),
    "Recycled HWND creation order replaced musical left-to-right Tab order");
  SetFocus(f.control(beforeMaster+9));SendMessageW(f.control(beforeMaster+9),WM_KEYDOWN,VK_TAB,0);
  check(GetFocus()==f.control(master+1),"Tab did not reach the adjacent late Master fader");
  SendMessageW(f.tool.window(),WM_HSCROLL,SB_LEFT,0);SetFocus(f.tool.window());f.tool.update();
  const auto first=bound("n1").at("controlBase").get<int>();
  const auto position=SendMessageW(f.control(first+1),TBM_GETPOS,0,0);
  const auto previews=f.previews;
  SendMessageW(f.control(first+1),WM_MOUSEHWHEEL,MAKEWPARAM(0,WHEEL_DELTA/2),0);
  check(f.tool.snapshot().at("firstBus")==0,"Partial wheel delta moved too far");
  SendMessageW(f.control(first+1),WM_MOUSEHWHEEL,MAKEWPARAM(0,WHEEL_DELTA/2),0);
  check(f.tool.snapshot().at("firstBus")==1&&SendMessageW(f.control(first+1),TBM_GETPOS,0,0)==position&&
    !f.tool.hasGesture()&&f.writes==0&&f.previews==previews,
    "Wheel over a strip changed its musical value or failed to navigate the viewport");
}
}
int main(){try {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  ScreamSeq::Tests::runPrivateGui(L"ScreamSeqMixerStrips",[]{Owner owner;interactions(owner.window);layoutAndIdentity(owner.window);inputAndWidth(owner.window);pendingSliderInput(owner.window);pendingTextRetention(owner.window);viewportPool(owner.window);});
  std::cout<<"PASS native mixer gesture coalescing, exact no-op, stale cancel, raw retention, capture loss and uncertain result review\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
