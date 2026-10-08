#include "GraphWorkflowWindow.hpp"
#include "GraphCanvas.hpp"
#include "PrivateGuiProcessTest.hpp"
#include <iostream>

namespace {
using Tool=ScreamSeq::GraphWorkflowWindow;using Json=ScreamSeq::Api::Json;
void require(bool yes,const char *why){if(!yes)throw std::runtime_error(why);}
struct Owner {
  HWND window{},edit{};
  Owner(){WNDCLASSW type{};type.lpfnWndProc=DefWindowProcW;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"ScreamSeq.GraphWorkflow.Test";RegisterClassW(&type);
    window=CreateWindowExW(0,type.lpszClassName,L"Graph workflow fixture",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,1200,900,nullptr,nullptr,type.hInstance,nullptr);require(window,"Create fixture owner");ScreamSeq::Tests::ownGuiWindow(window);
    edit=CreateWindowExW(0,L"EDIT",L"Unrelated text",WS_CHILD|WS_VISIBLE,8,8,200,26,window,nullptr,type.hInstance,nullptr);require(edit,"Create foreign edit");}
  ~Owner(){if(window)DestroyWindow(window);}
  void close(){auto h=window;require(DestroyWindow(h)!=FALSE,"Destroy fixture owner");window=nullptr;require(!IsWindow(h)&&!IsWindow(edit),"Owned HWND survived teardown");}
};
struct Fixture {
  Json context={{"documentId","doc"},{"revision","r1"},{"busy",false},{"fieldDraft",false},{"graph","n20"},{"node","n22"}};
  Json catalog={{"library",Json::array({{{"id","n20"},{"name","Recipe"},{"nodes",Json::array({{{"id","n21"},{"name","Input"},{"kind","input"}},{{"id","n22"},{"name","Effect"},{"kind","plugin"},{"plugin",{{"bypass",false}}}},{{"id","n23"},{"name","Output"},{"kind","output"}}})},{"audio",Json::array({{{"source","n21"},{"target","n22"}},{{"source","n22"},{"target","n23"}}})},{"modulation",Json::array()},{"groups",Json::array()},{"presentation",{{"regions",Json::array()},{"cables",Json::array()},{"collapsedNodes",Json::array()}}}}})},
    {"groups",Json::array()},{"songSources",Json::array()},{"songModulation",Json::array()},{"plugins",Json::array({{{"id","p1"},{"slot",0},{"name","Gain"},{"isInstrument",false}}})},{"mixer",{{"buses",Json::array({{{"id","n1"},{"name","Master"}}})}}},{"instruments",Json::array()}};
  Json pluginParameters=Json::array({{{"id",1},{"name","Gain"},{"value",0},{"manualValue",0},{"min",-96},{"max",24},{"writable",true}}});
  std::vector<std::pair<std::string,Json>> writes;std::function<void()> pump;unsigned revision=1;
  Tool tool;
  Fixture(Owner &owner):tool(owner.window,{[this]{return context;},[this](const auto &method,const auto &p){return request(method,p);},{},{},{}}){tool.open();}
  Json request(const std::string &method,const Json &p){
    if(method=="graph.get"){if(pump){auto action=std::exchange(pump,{});action();}return catalog;}
    if(method=="graph.signal.get")return {{"ports",Json::array()},{"listen",{{"port",nullptr}}}};
    if(method=="graph.scope.get")return {{"fresh",false},{"waveform",Json::array()},{"spectrum",Json::array()}};
    if(method=="plugin.parameters.get")return Json::array({{{"id",1},{"name","Gain"}}});
    if(method=="graph.plugin.get"){if(pump){auto action=std::exchange(pump,{});action();}return {{"parameters",pluginParameters}};}
    require(p.at("expectedRevision")==context.at("revision"),"Wrong captured write revision");writes.emplace_back(method,p);
    // The fixture accepts the saved payload without a revision change, matching
    // the document APIs' no-op contract; the tests below inspect that exact payload.
    if(method=="graph.presentation.set"&&p.at("presentation")==catalog.at("library")[0].at("presentation"))return {{"changed",false}};
    if(method=="graph.plugin.set"&&p.at("parameters").size()==1)for(const auto &parameter:pluginParameters)if(parameter.at("id")==p.at("parameters")[0].at("id")&&parameter.value("manualValue",parameter.at("value"))==p.at("parameters")[0].at("value"))return {{"changed",false}};
    if(!p.value("dryRun",false))context["revision"]="r"+std::to_string(++revision);return {{"changed",true}};
  }
  HWND control(int id){auto h=GetDlgItem(tool.window(),id);require(h,"Missing workflow control");return h;}
  void button(int id){require(IsWindowVisible(control(id))&&IsWindowEnabled(control(id)),"Fixture action is hidden or disabled");SendMessageW(control(id),BM_CLICK,0,0);}
  void choose(int id,unsigned i){SendMessageW(control(id),CB_SETCURSEL,i,0);SendMessageW(tool.window(),WM_COMMAND,MAKEWPARAM(id,CBN_SELCHANGE),reinterpret_cast<LPARAM>(control(id)));}
  void node(unsigned i){SendMessageW(control(Tool::nodes),LB_SETSEL,FALSE,-1);SendMessageW(control(Tool::nodes),LB_SETSEL,TRUE,i);SendMessageW(tool.window(),WM_COMMAND,MAKEWPARAM(Tool::nodes,LBN_SELCHANGE),reinterpret_cast<LPARAM>(control(Tool::nodes)));}
  void text(int id,const wchar_t *value){SetWindowTextW(control(id),value);}
};
void geometryAndRetention(Owner &owner){Fixture f(owner);const auto hwnd=f.tool.window();f.text(Tool::name,L"Retained raw group name");SetFocus(f.control(Tool::name));SendMessageW(f.control(Tool::name),EM_SETSEL,3,11);
  RECT r{0,0,780,620};const auto dpi=GetDpiForWindow(hwnd);r.right=MulDiv(r.right,dpi,96);r.bottom=MulDiv(r.bottom,dpi,96);AdjustWindowRectExForDpi(&r,DWORD(GetWindowLongPtrW(hwnd,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(hwnd,GWL_EXSTYLE)),dpi);SetWindowPos(hwnd,nullptr,0,0,r.right-r.left,r.bottom-r.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
  require(GetFocus()==f.control(Tool::name),"Resize stole retained text focus");DWORD a=0,b=0;SendMessageW(f.control(Tool::name),EM_GETSEL,reinterpret_cast<WPARAM>(&a),reinterpret_cast<LPARAM>(&b));require(a==3&&b==11,"Resize reset caret");
  for(unsigned page=0;page<8;++page){f.choose(Tool::page,page);RECT client{};GetClientRect(hwnd,&client);const auto scale=GetDpiForWindow(hwnd)/96.f;for(const auto &c:f.tool.snapshot().at("controls"))if(c.at("visible")==true){const auto &bounds=c.at("bounds");require(bounds[0]>=0&&bounds[1]>=0&&bounds[2].get<float>()<=client.right/scale+1&&bounds[3].get<float>()<=client.bottom/scale+1,"Visible native control is outside minimum client");}}
  f.choose(Tool::page,0);f.tool.hide();f.tool.open();require(f.tool.window()==hwnd&&f.tool.snapshot().at("fields").at("10102")=="Retained raw group name","Hide/page/show replaced raw fields or HWND");
}
void staleAndPumpedReads(Owner &owner){Fixture f(owner);f.text(Tool::name,L"Uncommitted");const auto fields=f.tool.snapshot().at("fields");f.context["revision"]="external";f.button(Tool::groupCreate);require(f.writes.empty()&&f.tool.snapshot().at("fields")==fields,"Stale group action mutated or replaced raw fields");
  f.pump=[&]{f.text(Tool::name,L"Newer while reading");};f.button(Tool::reload);require(f.tool.snapshot().at("fields").at("10102")=="Newer while reading"&&f.writes.empty(),"Pumped read erased newer input");
  f.button(Tool::reload);f.choose(Tool::page,4);f.choose(Tool::regionCollapsed,0);require(f.tool.snapshot().at("dirty")==true,"Raw boolean selection did not retain a draft");
  f.pump=[&]{f.choose(Tool::regionCollapsed,1);f.text(Tool::regionTitle,L"Typed during pending read");};f.button(Tool::reload);
  require(SendMessageW(f.control(Tool::regionCollapsed),CB_GETCURSEL,0,0)==0&&f.tool.snapshot().at("fields").at("10502")=="Typed during pending read","Pumped combo selection changed accepted draft identity or hid newer text");
  f.button(Tool::reload);f.choose(Tool::page,0);f.context["fieldDraft"]=true;f.button(Tool::groupCreate);require(f.writes.empty(),"Workflow overwrote main graph draft");
}
void exactActionsAndPresentation(Owner &owner){Fixture f(owner);SendMessageW(f.control(Tool::nodes),LB_SETSEL,FALSE,-1);SendMessageW(f.control(Tool::nodes),LB_SETSEL,TRUE,1);f.text(Tool::name,L"My group");f.button(Tool::groupCreate);require(f.writes.size()==1&&f.writes[0].first=="graph.group.create"&&f.writes[0].second.at("nodes")==Json::array({"n22"}),"Group action did not use selected stable identities");
  f.choose(Tool::page,4);SendMessageW(f.control(Tool::nodes),LB_SETSEL,TRUE,1);f.text(Tool::regionTitle,L"Read me");f.button(Tool::regionAdd);require(f.writes.size()==2&&f.writes.back().first=="graph.presentation.set","Annotation did not use presentation API");const auto &p=f.writes.back().second;require(p.at("presentation").at("regions").size()==1&&p.at("presentation").at("cables").empty()&&p.at("graph")=="n20","Annotation replacement lost scope or unrelated collection");
  f.choose(Tool::page,1);SendMessageW(f.control(Tool::nodes),LB_SETSEL,FALSE,-1);SendMessageW(f.control(Tool::nodes),LB_SETSEL,TRUE,1);f.button(Tool::detach);require(f.writes.back().first=="graph.nodes.detach"&&f.writes.back().second.at("remove")==false,"Detach must heal and retain the selected processor");
}
void capturedSongNavigation(Owner &owner){Fixture f(owner);f.catalog["songSources"]=Json::array({{{"id","n50"},{"name","Song LFO"},{"kind","lfo"}}});f.catalog["songModulation"]=Json::array({{{"source","n50"},{"plugin","p1"},{"parameter",1},{"minimum",-.2},{"maximum",.3},{"enabled",true},{"quantized",false}}});
  f.tool.openSongSource("n50","p1",1);auto state=f.tool.snapshot();require(state.at("graph").is_null()&&state.at("page")==3&&state.at("fields").at("10401")=="n50"&&state.at("fields").at("10402")=="p1"&&state.at("fields").at("10403")==1,"Source navigation failed to resolve captured song modulation target");
  f.catalog["plugins"].push_back({{"id","p2"},{"slot",1},{"name","Second gain"},{"isInstrument",false}});f.catalog["songModulation"].push_back({{"source","n50"},{"plugin","p2"},{"parameter",1},{"minimum",-.4},{"maximum",.4},{"enabled",true},{"quantized",false}});f.button(Tool::reload);f.choose(Tool::connection,2);require(f.tool.snapshot().at("fields").at("10402")=="p2","Saved connection did not select its destination");
  f.text(Tool::minimum,L"-");f.choose(Tool::modPlugin,0);require(f.tool.snapshot().at("fields").at("10402")=="p2","Rejected destination change did not restore the last accepted saved connection");bool refused=false;try{f.tool.openSongSource("n50","p1",1);}catch(const std::exception &){refused=true;}require(refused&&f.tool.snapshot().at("fields").at("10404")=="-","Explicit source navigation replaced retained raw fields");
}

void dependentSavedValues(Owner &owner){Fixture f(owner);
  auto &visual=f.catalog["library"][0]["presentation"];
  visual["cables"]=Json::array({{{"source","n21"},{"target","n22"},{"output",0},{"input",0},{"modulation",false},{"points",Json::array({Json::array({490,70}),Json::array({610,120})})}}});
  const auto savedPresentation=visual;f.button(Tool::reload);f.choose(Tool::page,5);f.choose(Tool::cable,0);
  auto fields=[&]{return f.tool.snapshot().at("fields");};
  require(fields().at("10603")==0&&fields().at("10601")=="490"&&fields().at("10602")=="70"&&!f.tool.snapshot().at("dirty").get<bool>(),"Cable selection did not hydrate the first saved reroute coordinates");
  const auto revision=f.context.at("revision");f.button(Tool::rerouteMove);
  require(f.writes.size()==1&&f.writes.back().second.at("presentation")==savedPresentation&&f.context.at("revision")==revision,"Immediate Move changed the saved reroute instead of sending an exact no-op");
  f.choose(Tool::cable,0);f.text(Tool::rerouteX,L"-");f.choose(Tool::reroutePoint,1);
  require(fields().at("10603")==0&&fields().at("10601")=="-"&&fields().at("10602")=="70","Point selection overwrote a retained coordinate draft");
  f.context["revision"]="external";f.button(Tool::rerouteMove);require(f.writes.size()==1&&fields().at("10601")=="-","Stale reroute applied or erased raw coordinates");
  f.button(Tool::reload);f.choose(Tool::cable,0);f.choose(Tool::reroutePoint,1);require(fields().at("10601")=="610"&&fields().at("10602")=="120","Explicit clean point selection did not hydrate its own saved coordinates");

  f.pluginParameters=Json::array({{{"id",1},{"name","Gain"},{"manualValue",-9},{"value",-3},{"min",-96},{"max",24},{"writable",true}},{{"id",2},{"name","Other"},{"manualValue",2},{"value",4},{"min",-96},{"max",24},{"writable",true}}});
  f.choose(Tool::page,1);f.node(1);
  require(fields().at("10202").at("id")==1&&fields().at("10203")=="-9"&&!f.tool.snapshot().at("dirty").get<bool>(),"Node selection did not hydrate the saved manual baseline instead of the effective value");
  const auto beforeSet=f.context.at("revision");f.button(Tool::setParameter);
  require(f.writes.size()==2&&f.writes.back().first=="graph.plugin.set"&&f.writes.back().second.at("node")=="n22"&&f.writes.back().second.at("parameters")==Json::array({{{"id",1},{"value",-9}}})&&f.context.at("revision")==beforeSet,"Immediate Set parameter changed the manual baseline instead of sending an exact no-op");
  f.node(1);f.text(Tool::pluginValue,L"-");f.choose(Tool::pluginParameter,1);f.node(2);
  require(fields().at("10202").at("id")==1&&fields().at("10203")=="-"&&f.tool.snapshot().at("selectedNodes")==Json::array({"n22"}),"Dependent parameter or node selection overwrote the retained manual value draft");
  f.context["revision"]="external2";f.button(Tool::setParameter);require(f.writes.size()==2&&fields().at("10203")=="-","Stale parameter action applied or erased raw input");
  f.button(Tool::reload);f.pump=[&]{f.text(Tool::pluginValue,L"Newer during parameter read");};f.node(1);
  require(fields().at("10203")=="Newer during parameter read"&&f.tool.snapshot().at("dirty")==true&&f.writes.size()==2,"Dependent hydration erased input received during the parameter read");
  f.button(Tool::reload);f.pluginParameters[0].erase("manualValue");f.node(1);require(fields().at("10203")=="-3","Parameter without a separate manual baseline did not use its saved value");
  f.catalog["library"][0]["presentation"]["regions"]=Json::array({{{"id","frame"},{"kind","frame"},{"title","Saved frame"},{"text",""},{"x",20},{"y",40},{"width",320},{"height",180},{"color",0x658b82},{"scope",""},{"collapsed",false},{"nodes",Json::array({"n21","n23"})}}});
  f.button(Tool::reload);f.choose(Tool::page,4);f.choose(Tool::region,1);f.text(Tool::regionTitle,L"Retained frame title");f.choose(Tool::page,1);f.node(1);
  require(f.tool.snapshot().at("selectedNodes")==Json::array({"n21","n23"})&&fields().at("10502")=="Retained frame title","Rejected processor selection restored an older node set instead of the saved frame selection");
}

void presentationGeometry(){
  using Canvas=ScreamSeq::GraphCanvas;Canvas canvas;canvas.viewport={12,20,700,400};canvas.zoom=1.25f;canvas.panX=18;canvas.panY=-7;
  Json definition={{"nodes",Json::array({{{"id","n1"},{"name","Input"},{"kind","input"},{"x",0},{"y",80}},{{"id","n2"},{"name","Effect"},{"kind","plugin"},{"x",220},{"y",80},{"plugin",Json::object()}},{{"id","n3"},{"name","Output"},{"kind","output"},{"x",480},{"y",80}}})},{"audio",Json::array({{{"source","n1"},{"target","n2"}},{{"source","n2"},{"target","n3"}}})},{"modulation",Json::array()},
    {"groups",Json::array({{{"id","n4"},{"name","One effect"},{"parent",nullptr},{"nodes",Json::array({"n2"})}}})},
    {"presentation",{{"regions",Json::array()},{"collapsedNodes",Json::array({"n2"})},{"cables",Json::array({{{"source","n1"},{"target","n2"},{"output",0},{"input",0},{"modulation",false},{"points",Json::array({Json::array({170,240})})}}})}}}};
  const auto before=definition;canvas.rebuild(definition);require(definition==before,"Canvas mutated musical or presentation data");require(canvas.regions.size()==1&&canvas.nodes[1].compact,"Processing boundary or collapsed node is missing");
  const auto anchor=canvas.screen(170,240);require(canvas.wires[0].points.size()==65&&std::hypot(canvas.wires[0].points[32].x-anchor.x,canvas.wires[0].points[32].y-anchor.y)<.01f,"Saved reroute did not pass through its exact transformed anchor");require(canvas.wireAt(anchor.x,anchor.y)==0,"Rerouted cable drawing and hit test diverged");
  definition["presentation"]["regions"]=Json::array({{{"id","frame"},{"kind","frame"},{"title","Collapsed effects"},{"text",""},{"x",210},{"y",40},{"width",200},{"height",160},{"color",0x658b82},{"collapsed",true},{"nodes",Json::array({"n2"})}}});
  canvas.rebuild(definition);require(canvas.nodes.size()==2&&canvas.wires.size()==2,"Collapsed frame removed external audio cables or kept hidden member cards");
  unsigned endpoints=0;for(const auto &socket:canvas.sockets)if(socket.node=="n2"){++endpoints;require(canvas.socketAt(socket.at.x,socket.at.y)>=0,"Frame boundary socket is not hittable");}require(endpoints==2,"Frame proxies lost real stable endpoint IDs");
}

}
int wmain(int argc,wchar_t **argv){try{SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);ScreamSeq::Tests::runPrivateGuiProcess(L"ScreamSeqGraphWorkflow",argc,argv,[]{ScreamSeq::check(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED),"Initialize GUI COM");struct Com{~Com(){CoUninitialize();}}com;Owner owner;geometryAndRetention(owner);std::cout<<"PASS graph workflow minimum geometry and retained native draft\n";staleAndPumpedReads(owner);std::cout<<"PASS graph workflow stale and pumped-read guards\n";exactActionsAndPresentation(owner);std::cout<<"PASS graph workflow stable group, annotation and heal requests\n";capturedSongNavigation(owner);std::cout<<"PASS captured song modulation navigation and retained draft guard\n";dependentSavedValues(owner);std::cout<<"PASS dependent saved coordinates and manual values with raw/stale/pumped guards\n";presentationGeometry();std::cout<<"PASS graph presentation reroutes and collapsed real endpoints\n";owner.close();});return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
