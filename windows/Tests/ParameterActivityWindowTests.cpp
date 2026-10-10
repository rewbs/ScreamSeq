#include "PrivateGuiTest.hpp"
#include "../App/ParameterActivityWindow.hpp"
#include <iostream>

namespace {
using Json=ScreamSeq::Api::Json;
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
void run(){
  auto owner=CreateWindowExW(0,L"STATIC",L"Activity test owner",WS_OVERLAPPEDWINDOW,0,0,1200,850,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
  require(owner!=nullptr,"Create test owner");ShowWindow(owner,SW_SHOW);
  {
    ScreamSeq::ParameterActivityWindow::Context context{"song","r1",false};bool prepared=false;unsigned edits=0;std::string token="1:1";Json navigation;
    Json processor={{"key","song/rack/a"},{"name","Gain copy"},{"plugin","a"},{"graph","n0"},{"node","n0"},{"target","n0"},{"instrument","n0"},{"role",0},{"channel",0},{"bypass",false},{"parameterCount",1}};
    std::function<void(const std::string &)> pump;std::string failRead;HWND toolWindow{};
    Json lane=Json::array({{{"frame",24000},{"value",-.5}}});
    auto request=[&](const std::string &method,const Json &p)->Json {
      if(pump)pump(method);if(method==failRead)throw std::runtime_error("Injected read failure");
      if(method=="parameter.activity.targets")return {{"targets",prepared?Json::array({processor}):Json::array()},{"active",prepared},{"engine",prepared?1:0}};
      if(method=="parameter.activity.parameters")return {{"target",processor},{"parameters",Json::array({{{"id",1},{"name","Gain"},{"min",-1},{"max",1},{"value",0},{"unitLabel","dB"}}})}};
      if(method=="parameter.activity.watch"){require(!p.contains("expectedRevision"),"Watch must be transient");return {{"token",token},{"target",processor},{"parameter",1}};}
      if(method=="parameter.activity.sources")return {{"sources",Json::array({{{"kind","baseline"},{"title","Saved manual value"},{"enabled",true}},{{"kind","recorded"},{"title","Recorded automation"},{"plugin","a"},{"parameter",1},{"enabled",true}}})},{"omitted",0},{"rule","Recorded values control the base."}};
      if(method=="parameter.activity.get")return {{"token",token},{"target",processor},{"parameter",1},{"points",p.at("after")==0?Json::array({
        {{"sequence",1},{"seconds",.5},{"value",-.5},{"minimum",-.5},{"maximum",-.5},{"pattern",0},{"order",0},{"position",0},{"source",{{"kind","recorded"}}}},
        {{"sequence",2},{"seconds",.6},{"value",.9},{"minimum",-.9},{"maximum",.9},{"pattern",0},{"order",0},{"position",128},{"source",{{"kind","graph-source"},{"id","n99"}}}},
        {{"sequence",3},{"seconds",.7},{"value",-.25},{"minimum",-.25},{"maximum",-.25},{"pattern",0},{"order",0},{"position",256},{"source",{{"kind","recorded"}}}}}):Json::array()},{"cursor",3},{"dropped",0},{"active",true}};
      if(method=="automation.recorded.get")return {{"points",lane},{"offset",0},{"total",lane.size()},{"sampleRate",48000}};
      if(method=="automation.recorded.edit"){require(p.at("expectedRevision")==context.revision,"Stale write reached callback");++edits;const auto original=p.at("frame");lane.erase(std::remove_if(lane.begin(),lane.end(),[&](const auto &v){return v.at("frame")==original;}),lane.end());if(!p.value("remove",false))lane.push_back({{"frame",p.value("newFrame",original)},{"value",p.at("value")}});context.revision="r"+std::to_string(edits+2);return {{"wouldChange",true}};}
      throw std::runtime_error("Unexpected activity request");
    };
    ScreamSeq::ParameterActivityWindow tool(owner,request,[&]{return context;},[&](const Json &v){navigation=v;});
    tool.openAt();auto window=tool.window();toolWindow=window;const auto click=[&](int id){SendMessageW(window,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(window,id)));};
    const auto choose=[&](int id,int index){SendMessageW(GetDlgItem(window,id),CB_SETCURSEL,index,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(id,CBN_SELCHANGE),reinterpret_cast<LPARAM>(GetDlgItem(window,id)));};
    require(tool.snapshot().at("targets").empty(),"Unprepared processors were fabricated");
    const auto refuses=[&](const std::string &plugin,uint32_t parameter){bool refused=false;try{tool.openSourceAt(plugin,parameter);}catch(const std::exception &){refused=true;}require(refused,"Explicit activity navigation accepted an unavailable or retained target");};
    const auto unprepared=tool.snapshot();refuses("a",1);require(tool.snapshot()==unprepared,"Unprepared activity fabricated or adopted a target");
    prepared=true;tool.openSourceAt("a",1);require(tool.snapshot().at("target")=="song/rack/a","Explicit navigation did not capture the prepared processor");
    const auto captured=tool.snapshot();refuses("missing",1);refuses("a",999);
    require(tool.snapshot()==captured,"Explicit activity fell back to a different processor or first parameter");
    SendMessageW(window,WM_TIMER,3,0);require(tool.snapshot().at("pointCount")==3,"Live trace did not consume its cursor");SendMessageW(window,WM_TIMER,3,0);require(tool.snapshot().at("pointCount")==3,"Trace duplicated an old point");
    const auto trace=tool.snapshot().at("trace");require(trace.at("nativePoints")==2&&trace.at("normalizedContributions")==1&&trace.at("segments")==1&&trace.at("minimum")==-.5&&trace.at("maximum")==-.25,"Normalized graph contribution contaminated final native-unit trace range or segments");
    SendMessageW(GetDlgItem(window,11007),LB_SETCURSEL,0,0);click(11008);require(navigation.at("kind")=="baseline"&&navigation.at("parameter")==1&&navigation.at("plugin")=="a","Baseline provenance lost watched plugin/parameter");
    choose(11006,2);require(tool.snapshot().at("recordedTotal")==1,"Recorded page missing");
    SetWindowTextW(GetDlgItem(window,11016),L"-0.25");require(tool.snapshot().at("fieldDraft")==true,"Raw value draft not retained");
    const auto retained=tool.snapshot();refuses("a",2);tool.openSourceAt("a",1);
    require(tool.snapshot()==retained,"Explicit activity navigation erased a recorded-point draft");
    context.revision="r2";click(11013);require(edits==0&&tool.snapshot().at("fieldDraft")==true,"Stale recorded fields were committed or discarded");
    tool.hide();tool.openAt("different",9);require(tool.snapshot().at("target")=="song/rack/a"&&tool.snapshot().at("fieldDraft")==true,"Reopen retargeted a retained draft");
    click(11002);SetWindowTextW(GetDlgItem(window,11015),L"0.75");SetWindowTextW(GetDlgItem(window,11016),L"-0.25");click(11013);
    require(edits==1&&lane.at(0).at("frame")==36000&&lane.at(0).at("value")==-.25,"Seconds/native-value point move failed");require(tool.snapshot().at("fieldDraft")==false,"Saved fields remained dirty");
    click(11009);require(navigation.at("kind")=="recorded"&&navigation.at("plugin")=="a","Open lane lost stable plugin identity");
    const auto currentRevision=context.revision;navigation=nullptr;context.revision="stale-source";click(11009);
    require(navigation.is_null(),"Stale Song automation action followed a captured plugin into a new revision");context.revision=currentRevision;
    token="2:1";SendMessageW(window,WM_TIMER,3,0);require(tool.snapshot().at("frozen")==true&&tool.snapshot().at("token")=="1:1","External watch silently retargeted observer");
    const auto frozen=tool.snapshot();refuses("a",2);tool.openSourceAt("a",1);require(tool.snapshot()==frozen,"Parameter action erased a frozen activity trace");
    click(11002);require(tool.snapshot().at("token")=="2:1","Explicit refresh did not resume new engine");
    // Every read is staged: failure after the target/catalog reads leaves the
    // previous lane, watch and raw fields intact, including pumped native input.
    choose(11006,2);SetWindowTextW(GetDlgItem(window,11016),L"0.125");const auto beforeFailure=tool.snapshot();
    failRead="automation.recorded.get";click(11002);failRead.clear();auto failed=tool.snapshot();
    for(const auto *key:{"document","expectedRevision","target","parameter","token","recorded","recordedTotal","recordedOffset","rawTime","rawValue","fieldDraft"})require(failed.at(key)==beforeFailure.at(key),"Failed refresh partially adopted a target or discarded fields");
    pump=[&](const std::string &method){if(method=="parameter.activity.sources")SetWindowTextW(GetDlgItem(toolWindow,11016),L"0.375");};click(11002);pump={};
    require(tool.snapshot().at("rawValue")=="0.375"&&tool.snapshot().at("fieldDraft")==true&&tool.snapshot().at("token")==beforeFailure.at("token"),"Pumped refresh discarded newer recorded fields");
    click(11002);choose(11006,2);const auto beforePendingSelection=tool.snapshot();
    pump=[&](const std::string &method){if(method=="parameter.activity.get"){choose(11006,0);choose(11001,-1);SendMessageW(GetDlgItem(window,11007),LB_SETCURSEL,-1,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(11007,LBN_SELCHANGE),reinterpret_cast<LPARAM>(GetDlgItem(window,11007)));}};
    SendMessageW(window,WM_TIMER,3,0);pump={};require(tool.snapshot().at("page")==2&&SendMessageW(GetDlgItem(window,11006),CB_GETCURSEL,0,0)==2&&SendMessageW(GetDlgItem(window,11001),CB_GETCURSEL,0,0)==0&&SendMessageW(GetDlgItem(window,11007),LB_GETCURSEL,0,0)==beforePendingSelection.at("selectedPoint").get<int>(),"Pumped selection redirected displayed activity target or recorded point");
    SetWindowTextW(GetDlgItem(window,11016),L"0.5");const auto commitRevision=tool.snapshot().at("expectedRevision");
    pump=[&](const std::string &method){if(method=="automation.recorded.edit")SetWindowTextW(GetDlgItem(toolWindow,11016),L"0.625");};click(11013);pump={};
    require(edits==2&&tool.snapshot().at("rawValue")=="0.625"&&tool.snapshot().at("fieldDraft")==true&&tool.snapshot().at("expectedRevision")==commitRevision,"Write completion erased newer raw fields or silently rebased them");
    click(11002);
    context.document="other";SendMessageW(window,WM_TIMER,3,0);require(tool.snapshot().at("frozen")==true&&tool.snapshot().at("document")=="song","Changed document silently retargeted observer");
    navigation=nullptr;click(11009);require(navigation.is_null(),"Song automation action crossed a captured document boundary");
    MINMAXINFO limits{};SendMessageW(window,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&limits));SetWindowPos(window,nullptr,0,0,limits.ptMinTrackSize.x,limits.ptMinTrackSize.y,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    RECT client{};GetClientRect(window,&client);for(auto child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))if(IsWindowVisible(child)){RECT r{};GetWindowRect(child,&r);MapWindowPoints(nullptr,window,reinterpret_cast<POINT *>(&r),2);wchar_t type[32]{};GetClassNameW(child,type,32);require(r.left>=0&&r.top>=0&&r.right<=client.right&&(wcscmp(type,L"ComboBox")==0||r.bottom<=client.bottom),"Activity controls escaped minimum client bounds");}
  }
  require(DestroyWindow(owner)!=FALSE,"Destroy test owner");
}
}
int main(){try{SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);ScreamSeq::Tests::runPrivateGui(L"ScreamSeqActivityTests",[]{INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);run();});std::cout<<"Parameter activity retained-window tests passed\n";return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
