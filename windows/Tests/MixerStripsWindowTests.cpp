#include "common/stdafx.h"
#include "../App/MixerStripsWindow.hpp"
#include "PrivateGuiTest.hpp"

namespace {
using Json=ScreamSeq::Api::Json;
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
struct Owner {
  HWND window{};
  Owner(){WNDCLASSW type{};type.lpfnWndProc=DefWindowProcW;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"ScreamSeq.MixerStrips.TestOwner";RegisterClassW(&type);
    window=CreateWindowExW(0,type.lpszClassName,L"Owned mixer fixture",WS_OVERLAPPEDWINDOW|WS_VISIBLE|WS_CLIPCHILDREN,0,0,1000,700,nullptr,nullptr,type.hInstance,nullptr);
    check(window,"Create owned mixer host");ScreamSeq::Tests::ownGuiWindow(window);}
  ~Owner(){if(window)DestroyWindow(window);}
};
struct Fixture {
  Json data={{"active",true},{"buses",Json::array()}};
  std::string revision="r1";unsigned writes=0,previews=0;bool unknown=false;
  double audible=-6.123456789;
  ScreamSeq::MixerStripsWindow tool;
  explicit Fixture(HWND owner):tool(owner,
    [this](const auto &method,const auto &params){return read(method,params);},
    [this]{return std::pair(std::string("owned-song"),revision);},
    [this](const std::string &method,const Json &params,const auto &)->ScreamSeq::Api::CompletedCall {
      check(method=="mixer.bus.set","Unexpected durable mixer operation");check(params.at("expectedRevision")==revision,"Final lost its captured revision");
      ++writes;auto &bus=find(params.at("bus").get<std::string>());
      for(const auto &[key,value]:params.items())if(bus.contains(key)&&key!="id")bus[key]=value;
      audible=bus.at("gainDB").get<double>();revision="r"+std::to_string(writes+1);
      if(unknown)throw ScreamSeq::Api::ApiError(-32003,"Owned lost response",Tracker::WriteOutcome{});
      return {method,"owned-song",revision,{{"wouldChange",true}}};
    },[]{},[](const auto &){},[]{}) {
    for(unsigned i=0;i<12;++i)data["buses"].push_back({{"id","n"+std::to_string(i+1)},
      {"name",i==11?"Master":"Track "+std::to_string(i+1)},{"gainDB",i==0?audible:0},
      {"pan",0},{"mute",false},{"solo",false}});
    ScreamSeq::Tests::ownGuiWindow(tool.window());tool.dock(owner);tool.dockBounds(0,0,528,260);tool.show();tool.update();
  }
  Json &find(const std::string &id){for(auto &value:data["buses"])if(value["id"]==id)return value;throw std::runtime_error("Unknown captured mixer bus");}
  Json read(const std::string &method,const Json &params) {
    if(method=="mixer.get")return data;
    if(method=="synchronizeView")return Json::object();
    check(method=="mixer.bus.set"&&params.value("preview",false),"Unexpected preview operation");
    check(params.at("expectedRevision")==revision,"Preview/reset lost revision guard");++previews;
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
}
int main(){try {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  ScreamSeq::Tests::runPrivateGui(L"ScreamSeqMixerStrips",[]{Owner owner;interactions(owner.window);});
  std::cout<<"PASS native mixer gesture coalescing, exact no-op, stale cancel, raw retention, capture loss and uncertain result review\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
