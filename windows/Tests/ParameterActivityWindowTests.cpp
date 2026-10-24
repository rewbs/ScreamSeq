#include "PrivateGuiTest.hpp"
#include "AccessibleControl.hpp"
#include "../App/ParameterActivityWindow.hpp"
#include <iostream>

namespace {
using Json=ScreamSeq::Api::Json;
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
void run(){
  auto owner=CreateWindowExW(0,L"STATIC",L"Activity test owner",WS_OVERLAPPEDWINDOW,0,0,1200,850,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
  require(owner!=nullptr,"Create test owner");ShowWindow(owner,SW_SHOW);
  struct OwnerCleanup {HWND &window;~OwnerCleanup(){if(window)DestroyWindow(window);}} ownerCleanup{owner};
  {
    ScreamSeq::ParameterActivityWindow::Context context{"song","r1",false};bool prepared=false,twoParameters=false,searchCatalogue=false,reverseCatalogue=false;uint32_t watchedParameter=1;unsigned edits=0,watches=0,requests=0;std::string token="1:1";Json navigation;
    Json processor={{"key","song/rack/a"},{"name","Gain copy"},{"plugin","a"},{"graph","n0"},{"node","n0"},{"target","n0"},{"instrument","n0"},{"role",0},{"channel",0},{"bypass",false},{"parameterCount",1}};
    std::function<void(const std::string &)> pump;std::string failRead;HWND toolWindow{};std::optional<double> constantTrace;std::optional<Json> incomingPage;
    Json lane=Json::array({{{"frame",24000},{"value",-.5}}});
    auto request=[&](const std::string &method,const Json &p)->Json {
      ++requests;
      if(pump)pump(method);if(method==failRead)throw std::runtime_error("Injected read failure");
      if(method=="parameter.activity.targets")return {{"targets",prepared?Json::array({processor}):Json::array()},{"active",prepared},{"engine",prepared?1:0}};
      if(method=="parameter.activity.parameters"){auto parameters=Json::array({{{"id",1},{"name","Gain"},{"min",-1},{"max",1},{"value",0},{"unitLabel","dB"}}});
        if(twoParameters)parameters.push_back({{"id",2},{"name","Balance"},{"min",-1},{"max",1},{"value",0},{"unitLabel","%"}});
        if(searchCatalogue){parameters.push_back({{"id",42},{"name","Gain"},{"unitLabel","dB"}});parameters.push_back({{"id",9001},{"name","Gain"},{"unitLabel","dB"}});}
        if(reverseCatalogue)std::reverse(parameters.begin(),parameters.end());
        return {{"target",processor},{"parameters",parameters}};}
      if(method=="parameter.activity.watch"){++watches;require(!p.contains("expectedRevision"),"Watch must be transient");watchedParameter=p.at("parameter");return {{"token",token},{"target",processor},{"parameter",watchedParameter}};}
      if(method=="parameter.activity.sources")return {{"sources",Json::array({{{"kind","baseline"},{"title","Saved manual value"},{"enabled",true}},{{"kind","recorded"},{"title","Recorded automation"},{"plugin","a"},{"parameter",1},{"enabled",true}}})},{"omitted",0},{"rule","Recorded values control the base."}};
      if(method=="parameter.activity.get"){Json result={{"token",token},{"target",processor},{"parameter",watchedParameter},{"points",p.at("after")==0?Json::array({
        {{"sequence",1},{"seconds",.5},{"value",-.5},{"minimum",-.5},{"maximum",-.5},{"pattern",0},{"order",0},{"position",0},{"source",{{"kind","recorded"}}}},
        {{"sequence",2},{"seconds",.6},{"value",.9},{"minimum",-.9},{"maximum",.9},{"pattern",0},{"order",0},{"position",128},{"source",{{"kind","graph-source"},{"id","n99"}}}},
        {{"sequence",3},{"seconds",.7},{"value",-.25},{"minimum",-.25},{"maximum",-.25},{"pattern",0},{"order",0},{"position",256},{"source",{{"kind","recorded"}}}}}):Json::array()},{"cursor",3},{"dropped",0},{"active",true}};
        if(constantTrace)for(auto &point:result["points"])for(const auto *key:{"value","minimum","maximum"})point[key]=*constantTrace;
        if(incomingPage){result["points"]=*incomingPage;result["cursor"]=incomingPage->empty()?p.at("after"):incomingPage->back().at("sequence");}
        return result;}
      if(method=="automation.recorded.get")return {{"points",lane},{"offset",0},{"total",lane.size()},{"sampleRate",48000}};
      if(method=="automation.recorded.edit"){require(p.at("expectedRevision")==context.revision,"Stale write reached callback");++edits;const auto original=p.at("frame");lane.erase(std::remove_if(lane.begin(),lane.end(),[&](const auto &v){return v.at("frame")==original;}),lane.end());if(!p.value("remove",false))lane.push_back({{"frame",p.value("newFrame",original)},{"value",p.at("value")}});std::sort(lane.begin(),lane.end(),[](const auto &a,const auto &b){return a.at("frame")<b.at("frame");});context.revision="r"+std::to_string(edits+2);return {{"wouldChange",true}};}
      throw std::runtime_error("Unexpected activity request");
    };
    Json navigationLane;
    ScreamSeq::ParameterActivityWindow tool(owner,request,[&]{return context;},[&](const Json &v){navigation=v;navigationLane=lane;});
    tool.openAt();auto window=tool.window();toolWindow=window;const auto click=[&](int id){SendMessageW(window,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(window,id)));};
    const auto choose=[&](int id,int index){SendMessageW(GetDlgItem(window,id),CB_SETCURSEL,index,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(id,CBN_SELCHANGE),reinterpret_cast<LPARAM>(GetDlgItem(window,id)));};
    require(tool.snapshot().at("targets").empty(),"Unprepared processors were fabricated");
    const auto refuses=[&](const std::string &plugin,uint32_t parameter){bool refused=false;try{tool.openSourceAt(plugin,parameter);}catch(const std::exception &){refused=true;}require(refused,"Explicit activity navigation accepted an unavailable or retained target");};
    const auto unprepared=tool.snapshot();refuses("a",1);require(tool.snapshot()==unprepared,"Unprepared activity fabricated or adopted a target");
    prepared=true;tool.openSourceAt("a",1);require(tool.snapshot().at("target")=="song/rack/a","Explicit navigation did not capture the prepared processor");
    const auto captured=tool.snapshot();refuses("missing",1);refuses("a",999);
    require(tool.snapshot()==captured,"Explicit activity fell back to a different processor or first parameter");
    {
      twoParameters=true;click(11002);
      const auto generation=tool.snapshot().at("generation").get<uint64_t>();
      pump=[&](const std::string &method){if(method=="parameter.activity.get")choose(11001,1);};
      SendMessageW(window,WM_TIMER,3,0);pump={};
      require(tool.snapshot().at("generation")==generation,"Selector reentered the worker during an observation read");
      context.busy=true;SendMessageW(window,WM_TIMER,4,0);context.busy=false;
      require(tool.snapshot().at("generation")==generation,"Deferred activity target bypassed the busy owner");
      SendMessageW(window,WM_TIMER,4,0);
      require(tool.snapshot().at("generation")==generation+1&&tool.snapshot().at("parameter")==2&&edits==0,
        "Pumped parameter selection was lost instead of adopting its stable target after the read");
      pump=[&](const std::string &method){if(method=="parameter.activity.get")choose(11000,0);};
      SendMessageW(window,WM_TIMER,3,0);pump={};context.revision="navigation-stale";
      SendMessageW(window,WM_TIMER,4,0);context.revision="r1";
      require(tool.snapshot().at("generation")==generation+1&&edits==0,"Deferred observer navigation crossed a document revision");
      click(11002);
      pump=[&](const std::string &method){if(method=="parameter.activity.get")choose(11001,0);};
      SendMessageW(window,WM_TIMER,3,0);pump={};
      SetWindowTextW(GetDlgItem(window,11016),L"unfinished");const auto rawGeneration=tool.snapshot().at("generation");
      SendMessageW(window,WM_TIMER,4,0);
      require(tool.snapshot().at("generation")==rawGeneration&&tool.snapshot().at("rawValue")=="unfinished"&&tool.snapshot().at("fieldDraft")==true&&edits==0,
        "Deferred target selection discarded newer raw point input");
      click(11002);
      pump=[&](const std::string &method){if(method=="parameter.activity.get")choose(11001,0);};
      SendMessageW(window,WM_TIMER,3,0);pump={};const auto beforeClose=tool.snapshot().at("generation");
      tool.hide();SendMessageW(window,WM_TIMER,4,0);
      require(tool.snapshot().at("generation")==beforeClose&&!tool.visible(),"Deferred target reopened a closed activity observer");
      tool.openAt("a",1);
      twoParameters=false;click(11002);
    }
    {
      // Refresh after external history can arrive inside a pumped observer
      // read. It must finish once when idle, not leave the old revision behind.
      choose(11006,2);SetFocus(GetDlgItem(window,11016));
      const auto beforeLane=lane;const auto beforeEdits=edits;
      context.revision="refresh-after-history";
      const auto beforeGeneration=tool.snapshot().at("generation").get<uint64_t>();
      const auto beforeWatches=watches;
      pump=[&](const std::string &method){if(method=="parameter.activity.get")click(11002);};
      SendMessageW(window,WM_TIMER,3,0);pump={};
      require(watches==beforeWatches&&tool.snapshot().at("generation")==beforeGeneration&&tool.snapshot().at("stale")==true,
        "Refresh reentered the observing worker or silently adopted stale point fields");
      context.busy=true;SendMessageW(window,WM_TIMER,4,0);context.busy=false;
      require(watches==beforeWatches,"Queued Refresh bypassed the busy document owner");
      SendMessageW(window,WM_TIMER,4,0);
      require(watches==beforeWatches+1&&tool.snapshot().at("generation")==beforeGeneration+1&&
        tool.snapshot().at("stale")==false&&tool.snapshot().at("recorded")==beforeLane&&
        tool.snapshot().at("selectedPoint")==0&&lane==beforeLane&&edits==beforeEdits,
        "Refresh during observation was lost or changed music instead of loading the current lane");
      SendMessageW(window,WM_TIMER,4,0);require(watches==beforeWatches+1,"Completed Refresh restarted observation twice");
      for(const bool changedDocument:{false,true}){
        pump=[&](const std::string &method){if(method=="parameter.activity.get")click(11002);};
        SendMessageW(window,WM_TIMER,3,0);pump={};
        const auto captured=tool.snapshot();const auto priorWatches=watches;
        if(changedDocument)context.revision="refresh-later-revision";
        else {SetWindowTextW(GetDlgItem(window,11016),L"unfinished");SendMessageW(GetDlgItem(window,11016),EM_SETSEL,2,5);}
        SendMessageW(window,WM_TIMER,4,0);
        DWORD first=0,last=0;SendMessageW(GetDlgItem(window,11016),EM_GETSEL,WPARAM(&first),LPARAM(&last));
        require(watches==priorWatches&&lane==beforeLane&&edits==beforeEdits&&
          (changedDocument?tool.snapshot().at("expectedRevision")==captured.at("expectedRevision"):
            tool.snapshot().at("rawValue")=="unfinished"&&first==2&&last==5&&GetFocus()==GetDlgItem(window,11016)),
          "Deferred Refresh discarded newer raw input/caret or crossed a later song revision");
        click(11002);
      }
      pump=[&](const std::string &method){if(method=="parameter.activity.get")click(11002);};
      SendMessageW(window,WM_TIMER,3,0);pump={};const auto priorWatches=watches;
      tool.hide();SendMessageW(window,WM_TIMER,4,0);
      require(watches==priorWatches&&!tool.visible(),"Deferred Refresh reopened a closed observer");
      tool.openAt("a",1);choose(11006,0);
    }
    {
      searchCatalogue=true;twoParameters=true;click(11002);
      const auto search=GetDlgItem(window,11021);require(search!=nullptr,"Missing Activity search edit");
      {
        RECT original{};require(GetWindowRect(window,&original),"Read Activity bounds");
        const auto dpi=GetDpiForWindow(window);const auto scale=dpi/96.;
        RECT minimum{0,0,LONG(std::lround(1020*scale)),LONG(std::lround(680*scale))};
        require(AdjustWindowRectExForDpi(&minimum,DWORD(GetWindowLongPtrW(window,GWL_STYLE)),FALSE,
          DWORD(GetWindowLongPtrW(window,GWL_EXSTYLE)),dpi),"Compute minimum Activity frame");
        require(SetWindowPos(window,nullptr,0,0,minimum.right-minimum.left,minimum.bottom-minimum.top,
          SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE),"Resize Activity to minimum client");
        RECT client{},query{},selector{},actions{};require(GetClientRect(window,&client)&&GetWindowRect(search,&query)&&
          GetWindowRect(GetDlgItem(window,11002),&actions),"Read Activity search layout");
        require(GetWindowRect(GetDlgItem(window,11001),&selector),"Read closed Activity selector");
        MapWindowPoints(nullptr,window,reinterpret_cast<POINT *>(&selector),2);
        MapWindowPoints(nullptr,window,reinterpret_cast<POINT *>(&query),2);
        MapWindowPoints(nullptr,window,reinterpret_cast<POINT *>(&actions),2);
        require(query.left>=0&&query.right<=client.right&&query.bottom<=selector.top&&selector.bottom<=actions.top,
          "Activity search or closed parameter selector overlaps or clips at minimum size");
        require(SetWindowPos(window,nullptr,original.left,original.top,original.right-original.left,original.bottom-original.top,
          SWP_NOZORDER|SWP_NOACTIVATE),"Restore Activity bounds");
      }
      const auto before=tool.snapshot();const auto oldRequests=requests,oldWatches=watches;
      SetFocus(search);SetWindowTextW(search,L"9001");SendMessageW(search,EM_SETSEL,1,3);
      DWORD first=0,last=0;SendMessageW(search,EM_GETSEL,WPARAM(&first),LPARAM(&last));
      require(tool.snapshot().at("parameterChoices")==Json::array({1,9001})&&tool.snapshot().at("searchMatches")==1&&
        tool.snapshot().at("parameter")==1&&tool.snapshot().at("generation")==before.at("generation")&&
        tool.snapshot().at("recorded")==before.at("recorded")&&GetFocus()==search&&first==1&&last==3&&
        requests==oldRequests&&watches==oldWatches&&edits==0,
        "Activity search touched the watch, song, draft, focus or stable result identities");
      choose(11001,1);
      require(tool.snapshot().at("parameter")==9001&&watchedParameter==9001&&watches==oldWatches+1&&
        tool.snapshot().at("parameterChoices")==Json::array({9001}),"Filtered row was treated as a catalogue index instead of stable ID");
      reverseCatalogue=true;click(11002);
      require(tool.snapshot().at("parameter")==9001&&tool.snapshot().at("parameterChoices")==Json::array({9001}),
        "Catalogue reorder changed the filtered observation target");
      SetWindowTextW(search,L"bAL");
      require(tool.snapshot().at("parameterChoices")==Json::array({9001,2})&&tool.snapshot().at("searchMatches")==1&&
        tool.snapshot().at("parameter")==9001,"Case-insensitive name filtering redirected the observation");
      context.busy=true;const auto busyRequests=requests;
      SetWindowTextW(search,L"%");require(tool.snapshot().at("searchMatches")==1&&requests==busyRequests,
        "Parameter unit filtering was blocked by or reentered the document owner");context.busy=false;
      SetWindowTextW(search,L"no such parameter");
      require(tool.snapshot().at("searchMatches")==0&&tool.snapshot().at("parameterChoices")==Json::array({9001})&&
        tool.snapshot().at("parameter")==9001,"Zero matches silently selected a replacement parameter");
      std::wstring selectedLabel(256,L'\0');SendMessageW(GetDlgItem(window,11001),CB_GETLBTEXT,0,LPARAM(selectedLabel.data()));
      require(selectedLabel.find(L"[observed; outside search]")!=std::wstring::npos,"Excluded observed parameter lacks a visible explanation");
      SetWindowTextW(search,L"");
      require(tool.snapshot().at("parameterChoices")==Json::array({9001,42,2,1})&&tool.snapshot().at("parameter")==9001,
        "Clearing search lost duplicate names or changed the stable observation");
      const auto beforePollWatches=watches;
      pump=[&](const std::string &method){if(method=="parameter.activity.get"){
        SetFocus(search);SetWindowTextW(search,L"42");SendMessageW(search,EM_SETSEL,1,2);
        require(tool.snapshot().at("searchPending")==true&&tool.snapshot().at("parameterChoices")==Json::array({9001,42,2,1}),
          "Pumped search rebuilt a selector while its observation was pending");
      }};
      SendMessageW(window,WM_TIMER,3,0);pump={};SendMessageW(window,WM_TIMER,5,0);
      SendMessageW(search,EM_GETSEL,WPARAM(&first),LPARAM(&last));
      require(tool.snapshot().at("searchPending")==false&&tool.snapshot().at("parameterChoices")==Json::array({9001,42})&&
        tool.snapshot().at("parameter")==9001&&watches==beforePollWatches&&GetFocus()==search&&first==1&&last==2,
        "Deferred search lost raw input, caret or stable observation after a pumped read");
      choose(11006,2);SetWindowTextW(GetDlgItem(window,11016),L"unfinished");const auto draft=tool.snapshot();
      const auto beforeDraftRequests=requests;SetFocus(search);SetWindowTextW(search,L"gain");
      require(tool.snapshot().at("rawValue")=="unfinished"&&tool.snapshot().at("fieldDraft")==true&&
        tool.snapshot().at("generation")==draft.at("generation")&&tool.snapshot().at("selectedPoint")==draft.at("selectedPoint")&&
        requests==beforeDraftRequests&&edits==0,"Search committed or discarded an invalid recorded-point draft");
      choose(11001,1);require(tool.snapshot().at("parameter")==9001&&tool.snapshot().at("rawValue")=="unfinished",
        "Explicit filtered selection redirected an unsaved point draft");
      SetWindowTextW(search,L"");reverseCatalogue=false;searchCatalogue=false;twoParameters=false;click(11002);choose(11006,0);
      require(tool.snapshot().at("parameter")==1&&edits==0,"Search fixture cleanup did not restore its original observer");
      // Reuse the same native registration for Ctrl+F and normal native Tab.
      struct RestoreKeys{BYTE state[256]{};RestoreKeys(){require(GetKeyboardState(state),"Read search keyboard state");}~RestoreKeys(){SetKeyboardState(state);}} restore;
      BYTE ctrl[256]{};std::copy(std::begin(restore.state),std::end(restore.state),std::begin(ctrl));ctrl[VK_CONTROL]|=0x80;
      require(SetKeyboardState(ctrl),"Set private Ctrl modifier");SetFocus(GetDlgItem(window,11001));SendMessageW(window,WM_KEYDOWN,'F',0);
      require(GetFocus()==search,"Activity Ctrl+F did not focus native search");
    }
    SendMessageW(window,WM_TIMER,3,0);require(tool.snapshot().at("pointCount")==3,"Live trace did not consume its cursor");SendMessageW(window,WM_TIMER,3,0);require(tool.snapshot().at("pointCount")==3,"Trace duplicated an old point");
    const auto trace=tool.snapshot().at("trace");require(trace.at("nativePoints")==2&&trace.at("normalizedContributions")==1&&trace.at("segments")==1&&trace.at("minimum")==-.5&&trace.at("maximum")==-.25,"Normalized graph contribution contaminated final native-unit trace range or segments");
    const auto traceLane=lane;const auto traceRevision=context.revision;
    for(const double held:{-1.,0.,1.}){
      constantTrace=held;click(11004);SendMessageW(window,WM_TIMER,3,0);const auto flat=tool.snapshot().at("trace");
      const auto plot=flat.at("plot"),point=flat.at("lastPoint");
      require(flat.at("minimum")==held&&flat.at("maximum")==held&&flat.at("segments")==1&&
        point[0].get<double>()>plot[0].get<double>()&&point[0].get<double>()<plot[0].get<double>()+plot[2].get<double>()&&
        std::abs(point[1].get<double>()-(plot[1].get<double>()+plot[3].get<double>()/2))<.01,
        "Constant activity trace is clipped or does not preserve its measured native value");
    }
    constantTrace.reset();click(11004);SendMessageW(window,WM_TIMER,3,0);
    require(lane==traceLane&&context.revision==traceRevision&&edits==0,"Transient trace presentation edited song data");
    {
      const auto p=tool.snapshot().at("trace").at("lastPoint");const auto scale=GetDpiForWindow(window)/96.;
      const auto at=MAKELPARAM(int(std::lround(p[0].get<double>()*scale)),int(std::lround(p[1].get<double>()*scale)));
      incomingPage=Json::array({{{"sequence",4},{"seconds",.8},{"value",.75},{"minimum",.75},{"maximum",.75},
        {"pattern",0},{"order",0},{"position",300},{"source",{{"kind","recorded"}}}}});
      pump=[&](const std::string &method){if(method=="parameter.activity.get"){SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,at);SendMessageW(window,WM_LBUTTONUP,0,at);}};
      SendMessageW(window,WM_TIMER,3,0);pump={};incomingPage.reset();
      require(tool.snapshot().at("frozen")==true&&tool.snapshot().at("trace").at("selectedSequence")==3&&GetFocus()==window,
        "Trace click did not freeze and select the observed native-value event");
      require(tool.snapshot().at("cursor")==3&&tool.snapshot().at("pointCount")==3&&!tool.snapshot().at("pending").get<bool>(),
        "Pumped observation replaced the displayed trace after its click froze the view");
      SendMessageW(window,WM_KEYDOWN,VK_LEFT,0);
      require(tool.snapshot().at("trace").at("selectedSequence")==1,"Trace keyboard navigation selected a normalized contribution or wrong neighbour");
      SendMessageW(window,WM_KEYDOWN,VK_RIGHT,0);require(tool.snapshot().at("trace").at("selectedSequence")==3,"Trace Right lost the next native event");
      SendMessageW(window,WM_KEYDOWN,VK_HOME,0);require(tool.snapshot().at("trace").at("selectedSequence")==1,"Trace Home lost the first event");
      SendMessageW(window,WM_KEYDOWN,VK_END,0);require(tool.snapshot().at("trace").at("selectedSequence")==3,"Trace End lost the last event");
      navigation=nullptr;SendMessageW(window,WM_KEYDOWN,VK_RETURN,0);
      require(navigation.at("kind")=="recorded"&&navigation.at("plugin")=="a"&&navigation.at("parameter")==1,"Trace Enter lost its stable source/plugin/parameter");
      SendMessageW(window,WM_KEYDOWN,VK_F6,0);require(GetFocus()==GetDlgItem(window,11007),"Trace F6 did not enter native details");
      SendMessageW(window,WM_KEYDOWN,VK_F6,0);require(GetFocus()==window,"Trace F6 did not return to the plot");
      SendMessageW(window,WM_KEYDOWN,VK_TAB,0);require(GetFocus()==GetDlgItem(window,11006),"Trace Tab trapped focus instead of entering the next visible control group");
      SendMessageW(GetFocus(),WM_KEYDOWN,VK_F6,0);require(GetFocus()==window,"Details F6 did not return to the plot");
      {
        struct RestoreKeys{BYTE state[256]{};RestoreKeys(){require(GetKeyboardState(state),"Read private keyboard state");}~RestoreKeys(){SetKeyboardState(state);}} restore;
        BYTE shifted[256]{};std::copy(std::begin(restore.state),std::end(restore.state),std::begin(shifted));shifted[VK_SHIFT]|=0x80;
        require(SetKeyboardState(shifted),"Set private Shift modifier");SendMessageW(window,WM_KEYDOWN,VK_TAB,0);
        require(GetFocus()==GetDlgItem(window,11005),"Trace Shift+Tab did not return to its time-range control");
      }
      SendMessageW(GetFocus(),WM_KEYDOWN,VK_F6,0);
      context.revision="changed-after-trace";navigation=nullptr;SendMessageW(window,WM_KEYDOWN,VK_RETURN,0);
      require(navigation.is_null()&&tool.snapshot().at("trace").at("selectedSequence")==3,"Trace source navigation crossed a captured revision boundary");
      context.revision=traceRevision;click(11002);SendMessageW(window,WM_TIMER,3,0);
      require(tool.snapshot().at("trace").at("selectedSequence").is_null()&&lane==traceLane&&edits==0,"Trace selection refresh wrote music or retained a previous watch selection");
      context.revision="source-read-revision";
      incomingPage=Json::array({{{"sequence",4},{"seconds",.8},{"value",.75},{"minimum",.75},{"maximum",.75},
        {"pattern",0},{"order",0},{"position",300},{"source",{{"kind","recorded"}}}}});
      pump=[&](const std::string &method){if(method=="parameter.activity.sources")SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,at);};
      SendMessageW(window,WM_TIMER,3,0);pump={};incomingPage.reset();
      require(tool.snapshot().at("frozen")==true&&tool.snapshot().at("trace").at("selectedSequence")==3&&
        tool.snapshot().at("cursor")==3&&tool.snapshot().at("pointCount")==3&&!tool.snapshot().at("pending").get<bool>(),
        "Pumped source read replaced the trace after selection froze it");
      context.revision=traceRevision;click(11002);SendMessageW(window,WM_TIMER,3,0);
    }
    {
      choose(11006,1);const auto recent=GetDlgItem(window,11007);
      const auto page=[](uint64_t first,uint64_t last){Json result=Json::array();for(auto sequence=first;sequence<=last;++sequence)
        result.push_back({{"sequence",sequence},{"seconds",double(sequence)/10},{"value",.5},{"minimum",.5},{"maximum",.5},
          {"pattern",0},{"order",0},{"position",sequence},{"source",{{"kind","recorded"}}}});return result;};
      incomingPage=page(4,512);SendMessageW(window,WM_TIMER,3,0);
      require(SendMessageW(recent,LB_GETCOUNT,0,0)==512,"Recent activity page did not retain its bounded rows");
      SendMessageW(recent,LB_SETCURSEL,400,0);SendMessageW(recent,LB_SETTOPINDEX,390,0);SetFocus(recent);
      unsigned resets=0;
      const SUBCLASSPROC observe=[](HWND h,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data)->LRESULT{
        if(message==LB_RESETCONTENT)++*reinterpret_cast<unsigned *>(data);return DefSubclassProc(h,message,w,l);};
      require(SetWindowSubclass(recent,observe,0x4154,reinterpret_cast<DWORD_PTR>(&resets))!=FALSE,"Observe native recent-list refresh");
      struct RemoveObserver{HWND window;SUBCLASSPROC callback;~RemoveObserver(){if(window)RemoveWindowSubclass(window,callback,0x4154);}}observer{recent,observe};
      incomingPage=Json::array();SendMessageW(window,WM_TIMER,3,0);
      require(resets==0&&SendMessageW(recent,LB_GETCURSEL,0,0)==400&&SendMessageW(recent,LB_GETTOPINDEX,0,0)==390&&GetFocus()==recent,
        "Unchanged observation rebuilt the list or lost native selection, scroll or focus");
      incomingPage=page(513,520);SendMessageW(window,WM_TIMER,3,0);
      require(SendMessageW(recent,LB_GETCOUNT,0,0)==512&&SendMessageW(recent,LB_GETCURSEL,0,0)==392&&
        SendMessageW(recent,LB_GETTOPINDEX,0,0)==382&&GetFocus()==recent,
        "Rolling recent activity redirected the selected event or its scroll position");
      navigation=nullptr;click(11008);require(navigation.at("kind")=="recorded"&&navigation.at("plugin")=="a","Retained recent selection lost source navigation");
      incomingPage=page(521,1032);SendMessageW(window,WM_TIMER,3,0);
      require(SendMessageW(recent,LB_GETCURSEL,0,0)==-1,"An evicted recent event silently selected a different source");
      navigation=nullptr;click(11008);require(navigation.is_null(),"Evicted activity selection navigated to a replacement event");
      require(RemoveWindowSubclass(recent,observe,0x4154)!=FALSE,"Release native recent-list observer");
      observer.window=nullptr;
      incomingPage.reset();choose(11006,0);click(11004);SendMessageW(window,WM_TIMER,3,0);
      require(lane==traceLane&&context.revision==traceRevision&&edits==0,"Recent trace navigation changed musical data");
    }
    SendMessageW(GetDlgItem(window,11007),LB_SETCURSEL,0,0);click(11008);require(navigation.at("kind")=="baseline"&&navigation.at("parameter")==1&&navigation.at("plugin")=="a","Baseline provenance lost watched plugin/parameter");
    {
      const auto originalRevision=context.revision;const auto beforeEdits=edits;
      for(const int button:{11008,11009}){
        navigation=nullptr;
        pump=[&](const std::string &method){if(method=="parameter.activity.get")click(button);};
        SendMessageW(window,WM_TIMER,3,0);pump={};
        require(navigation.is_null(),"Source navigation reentered an observation read");
        context.busy=true;SendMessageW(window,WM_TIMER,4,0);context.busy=false;
        require(navigation.is_null(),"Queued source navigation bypassed the busy document owner");
        SetFocus(GetDlgItem(window,11007));SendMessageW(window,WM_TIMER,4,0);
        require(navigation.at("kind")== (button==11008?"baseline":"recorded")&&
          navigation.at("plugin")=="a"&&navigation.at("parameter")==1&&edits==beforeEdits,
          "Source click during polling was lost or opened a different parameter");
        navigation=nullptr;SendMessageW(window,WM_TIMER,4,0);
        require(navigation.is_null(),"Queued source navigation repeated after completion");
      }
      pump=[&](const std::string &method){if(method=="parameter.activity.get")click(11008);};
      SendMessageW(window,WM_TIMER,3,0);pump={};context.revision="source-navigation-stale";
      SendMessageW(window,WM_TIMER,4,0);context.revision=originalRevision;
      require(navigation.is_null()&&edits==beforeEdits,"Queued source navigation crossed a changed song revision");
      click(11002);
    }
    choose(11006,2);require(tool.snapshot().at("recordedTotal")==1,"Recorded page missing");
    // The preceding explicit Refresh starts a fresh watch. Populate its trace
    // before the later retained-draft click test asks for a visible point.
    SendMessageW(window,WM_TIMER,3,0);
    require(tool.snapshot().at("trace").at("lastPoint").is_array()&&
      tool.snapshot().at("trace").at("lastPoint").size()==2,
      "Retained-draft trace fixture has no observed point to click");
    for(auto [id,name]:std::initializer_list<std::pair<int,const wchar_t *>>{
        {11000,L"Parameter activity processor copy"},{11021,L"Search activity parameters by name, unit or ID"},{11001,L"Parameter activity parameter"},
        {11005,L"Activity trace time range"},{11006,L"Parameter activity details page"},
        {11007,L"Parameter sources, changes or recorded points"},{11015,L"Recorded point time, seconds"},
        {11016,L"Recorded point value, native units"},{11010,L"Previous recorded page"},
        {11011,L"Next recorded page"},{11017,L"Zoom activity trace out"},{11018,L"Zoom activity trace in"}})
      require(ScreamSeq::Tests::accessibleName(GetDlgItem(window,id))==name,"Activity field lacks its stable native accessible name");
    const std::vector<int> activityTabs{11000,11021,11001,11002,11003,11004,11017,11019,11018,11005,
      11006,11008,11009,11007,11015,11016,11012,11013,11014,11010,11011,11020};
    for(size_t i=0;i<activityTabs.size();++i){const auto control=GetDlgItem(window,activityTabs[i]);
      require(GetNextDlgTabItem(window,control,FALSE)==GetDlgItem(window,activityTabs[(i+1)%activityTabs.size()])&&
        GetNextDlgTabItem(window,control,TRUE)==GetDlgItem(window,activityTabs[(i+activityTabs.size()-1)%activityTabs.size()]),
        "Activity Tab/Shift+Tab does not follow the visible control groups");}
    SetWindowTextW(GetDlgItem(window,11016),L"-0.25");require(tool.snapshot().at("fieldDraft")==true,"Raw value draft not retained");
    SetFocus(GetDlgItem(window,11016));const auto beforeTraceDraft=tool.snapshot();
    SendMessageW(GetDlgItem(window,11016),EM_SETSEL,1,4);
    for(const auto message:{WM_SETTINGCHANGE,WM_THEMECHANGED,WM_SYSCOLORCHANGE}){
      SendMessageW(window,message,0,0);DWORD first=0,last=0;
      SendMessageW(GetDlgItem(window,11016),EM_GETSEL,WPARAM(&first),LPARAM(&last));
      require(tool.snapshot()==beforeTraceDraft&&GetFocus()==GetDlgItem(window,11016)&&first==1&&last==4&&edits==0,
        "Native appearance refresh changed Activity target, raw point draft, caret or music");
    }
    const auto tracePoint=beforeTraceDraft.at("trace").at("lastPoint");const auto traceScale=GetDpiForWindow(window)/96.;
    SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(int(std::lround(tracePoint[0].get<double>()*traceScale)),int(std::lround(tracePoint[1].get<double>()*traceScale))));
    require(tool.snapshot().at("rawValue")=="-0.25"&&tool.snapshot().at("fieldDraft")==true&&GetFocus()==GetDlgItem(window,11016)&&
      tool.snapshot().at("trace").at("selectedSequence").is_null(),"Trace click discarded or hid a recorded field draft");
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
    lane=Json::array({{{"frame",24000},{"value",-.5}},{{"frame",36000},{"value",.625}},{{"frame",48000},{"value",.25}}});
    context.revision="three-points";click(11002);
    const auto itemList=GetDlgItem(window,11007);
    SendMessageW(itemList,LB_SETCURSEL,1,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(11007,LBN_SELCHANGE),LPARAM(itemList));
    const auto valueField=GetDlgItem(window,11016);SetActiveWindow(window);SetFocus(valueField);
    SetWindowTextW(valueField,L"0.75");SendMessageW(valueField,EM_SETSEL,1,3);
    pump=[&](const std::string &method){if(method=="parameter.activity.get")click(11013);};
    SendMessageW(window,WM_TIMER,3,0);pump={};
    require(edits==2&&tool.snapshot().at("fieldDraft")==true&&tool.queuedMusicalEdit(),"Polling submitted an edit reentrantly instead of retaining it");
    tool.flushForProjectSave();SendMessageW(window,WM_TIMER,0x5345,0);
    require(edits==3,"Save clicked during observation was lost or repeated");
    DWORD first=0,last=0;SendMessageW(valueField,EM_GETSEL,WPARAM(&first),LPARAM(&last));
    require(GetFocus()==valueField&&first==1&&last==3&&tool.snapshot().at("rawValue")=="0.75",
      "Deferred activity Save lost native field focus, raw input or selection");
    require(tool.snapshot().at("selectedPoint")==1&&SendMessageW(itemList,LB_GETCURSEL,0,0)==1&&
      lane==Json::array({{{"frame",24000},{"value",-.5}},{{"frame",36000},{"value",.75}},{{"frame",48000},{"value",.25}}}),
      "Activity value edit selected the first row or changed a different point");
    SetWindowTextW(GetDlgItem(window,11015),L"1.25");click(11013);
    require(tool.snapshot().at("selectedPoint")==2&&SendMessageW(itemList,LB_GETCURSEL,0,0)==2&&
      tool.snapshot().at("rawTime")=="1.25"&&tool.snapshot().at("rawValue")=="0.75"&&
      lane==Json::array({{{"frame",24000},{"value",-.5}},{{"frame",48000},{"value",.25}},{{"frame",60000},{"value",.75}}}),
      "Activity time edit lost its stable point identity after sorting");
    click(11014);
    require(tool.snapshot().at("selectedPoint")==-1&&SendMessageW(itemList,LB_GETCURSEL,0,0)==-1&&
      lane==Json::array({{{"frame",24000},{"value",-.5}},{{"frame",48000},{"value",.25}}}),
      "Activity deletion redirected selection or removed an unrelated point");
    SendMessageW(itemList,LB_SETCURSEL,0,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(11007,LBN_SELCHANGE),LPARAM(itemList));
    const auto beforeDeferred=lane;const auto beforeDeferredEdits=edits;
    SetWindowTextW(GetDlgItem(window,11016),L"0.125");
    pump=[&](const std::string &method){if(method=="parameter.activity.get")click(11013);};
    SendMessageW(window,WM_TIMER,3,0);pump={};
    SetWindowTextW(GetDlgItem(window,11016),L"0.375");SendMessageW(window,WM_TIMER,0x5345,0);
    require(edits==beforeDeferredEdits&&lane==beforeDeferred&&tool.snapshot().at("rawValue")=="0.375"&&
      tool.snapshot().at("fieldDraft")==true,"Deferred Save submitted changed raw input or lost its guarded draft");
    click(11002);
    // Observation does not own the point draft. Pumping text through either
    // observation read must leave its lane, selection, focus and caret intact,
    // keep consuming the trace, and never submit a musical write reentrantly.
    // The explicit typing intent saves later, once idle and still current.
    for(const auto *read:{"parameter.activity.get","parameter.activity.sources"}) {
      const auto beforeTyping=tool.snapshot();const auto beforeTypingLane=lane;const auto beforeTypingEdits=edits;
      const std::string rawTyped=std::string_view(read)=="parameter.activity.get"?"0.8125":"0.6875";
      const std::wstring typed(rawTyped.begin(),rawTyped.end());
      if(std::string_view(read)=="parameter.activity.sources")context.revision="observer-source-refresh";
      SetActiveWindow(window);SetFocus(valueField);
      pump=[&](const std::string &method){if(method==read){SetWindowTextW(valueField,typed.c_str());SendMessageW(valueField,EM_SETSEL,2,5);}};
      SendMessageW(window,WM_TIMER,3,0);pump={};
      const auto afterTyping=tool.snapshot();SendMessageW(valueField,EM_GETSEL,WPARAM(&first),LPARAM(&last));
      require(!afterTyping.at("frozen").get<bool>()&&!afterTyping.at("pending").get<bool>()&&
        afterTyping.at("rawValue")==rawTyped&&afterTyping.at("fieldDraft")==true&&
        GetFocus()==valueField&&first==2&&last==5,"Typing during observation froze the trace or lost native text focus/caret");
      for(const auto *key:{"recorded","selectedPoint","rawTime","expectedRevision","token"})
        require(afterTyping.at(key)==beforeTyping.at(key),"Observation adopted or redirected a recorded-point draft");
      require(lane==beforeTypingLane&&edits==beforeTypingEdits&&tool.queuedMusicalEdit(),"Typing during observation submitted reentrantly or lost its deferred edit");
      SendMessageW(window,WM_TIMER,3,0);
      require(!tool.snapshot().at("frozen").get<bool>()&&tool.snapshot().at("cursor")==3&&
        tool.snapshot().at("pointCount")==3,"Observer did not continue consuming its cursor after typing");
      context.busy=true;SendMessageW(window,WM_TIMER,0x5345,0);context.busy=false;
      require(lane==beforeTypingLane&&edits==beforeTypingEdits&&tool.queuedMusicalEdit(),
        "Automatic activity edit was lost or submitted while the owner was busy");
      SendMessageW(window,WM_TIMER,0x5345,0);SendMessageW(window,WM_TIMER,0x5345,0);
      if(std::string_view(read)=="parameter.activity.get"){
        auto expected=beforeTypingLane;expected[0]["value"]=.8125;
        require(lane==expected&&edits==beforeTypingEdits+1&&!tool.queuedMusicalEdit()&&
          !tool.snapshot().at("fieldDraft").get<bool>()&&tool.snapshot().at("selectedPoint")==0,
          "Automatic activity edit did not save exactly once at the selected point");
      }else require(lane==beforeTypingLane&&edits==beforeTypingEdits&&!tool.queuedMusicalEdit()&&
        tool.snapshot().at("fieldDraft")==true,"Automatic activity edit silently rebased stale raw fields");
      SendMessageW(valueField,EM_GETSEL,WPARAM(&first),LPARAM(&last));
      require(GetFocus()==valueField&&first==2&&last==5&&tool.snapshot().at("rawValue")==rawTyped,
        "Automatic activity completion lost typing focus, raw formatting or caret");
      click(11002);
    }
    {
      const auto beforeInvalid=lane;const auto beforeInvalidEdits=edits;
      SetWindowTextW(valueField,L"-");SendMessageW(valueField,EM_SETSEL,1,1);
      SendMessageW(window,WM_TIMER,0x5345,0);tool.flushForProjectSave();SendMessageW(window,WM_TIMER,0x5345,0);
      require(lane==beforeInvalid&&edits==beforeInvalidEdits&&!tool.queuedMusicalEdit()&&
        tool.snapshot().at("rawValue")=="-"&&tool.snapshot().at("fieldDraft")==true,
        "Invalid automatic point input was discarded, submitted or retried by project Save");
      SetWindowTextW(valueField,L"0.25");SetWindowTextW(valueField,L"0.5");
      tool.flushForProjectSave();
      auto expected=beforeInvalid;expected[0]["value"]=.5;
      require(lane==expected&&edits==beforeInvalidEdits+1&&!tool.queuedMusicalEdit(),
        "Latest valid point typing was not coalesced into one project-Save edit");
      SetWindowTextW(valueField,L"0.625");choose(11006,0);context.busy=true;
      SendMessageW(window,WM_TIMER,0x5345,0);context.busy=false;
      require(lane==expected&&tool.queuedMusicalEdit(),"Switching to sources lost an unsent busy point edit");
      tool.flushForProjectSave();expected[0]["value"]=.625;
      require(lane==expected&&edits==beforeInvalidEdits+2&&!tool.queuedMusicalEdit(),
        "Hidden recorded fields did not finish their original queued edit exactly once");choose(11006,2);
      click(11012);SetWindowTextW(GetDlgItem(window,11015),L"2");SetWindowTextW(valueField,L"0.75");
      SendMessageW(window,WM_TIMER,0x5345,0);
      require(lane==expected&&edits==beforeInvalidEdits+2&&!tool.queuedMusicalEdit()&&tool.snapshot().at("selectedPoint")==-1,
        "New-point fields created a partial point before explicit Add");click(11002);
    }
    {
      // Selecting a different row before debounce finishes saves the original
      // point once, then resolves the clicked frame in the sorted fresh page.
      const auto selectPoint=[&](int row){SendMessageW(itemList,LB_SETCURSEL,row,0);
        SendMessageW(window,WM_COMMAND,MAKEWPARAM(11007,LBN_SELCHANGE),LPARAM(itemList));};
      const auto initial=Json::array({{{"frame",24000},{"value",-.5}},{{"frame",36000},{"value",.625}},{{"frame",48000},{"value",.25}}});
      lane=initial;context.revision="idle-navigation";click(11002);selectPoint(1);
      const auto before=edits;
      SetWindowTextW(GetDlgItem(window,11015),L"1.25");selectPoint(2);
      const auto moved=Json::array({{{"frame",24000},{"value",-.5}},{{"frame",48000},{"value",.25}},{{"frame",60000},{"value",.625}}});
      require(lane==moved&&edits==before+1&&!tool.queuedMusicalEdit()&&
        tool.snapshot().at("selectedPoint")==1&&std::stod(tool.snapshot().at("rawTime").get<std::string>())==1&&
        SendMessageW(itemList,LB_GETCURSEL,0,0)==1,
        "Immediate row navigation lost the pending edit or selected the reordered index instead of its frame");
      SetWindowTextW(GetDlgItem(window,11015),L"1.5");selectPoint(1);
      auto movedAgain=Json::array({{{"frame",24000},{"value",-.5}},{{"frame",60000},{"value",.625}},{{"frame",72000},{"value",.25}}});
      require(lane==movedAgain&&edits==before+2&&tool.snapshot().at("selectedPoint")==2&&
        SendMessageW(itemList,LB_GETCURSEL,0,0)==2,
        "Selecting the edited row before debounce lost its accepted moved identity");
      // Invalid input blocks navigation without losing its text/caret. Once
      // consumed, that failed intent is not replayed by another selection.
      SetFocus(valueField);SetWindowTextW(valueField,L"-");SendMessageW(valueField,EM_SETSEL,1,1);
      selectPoint(0);selectPoint(1);SendMessageW(window,WM_TIMER,0x5345,0);
      SendMessageW(valueField,EM_GETSEL,WPARAM(&first),LPARAM(&last));
      require(lane==movedAgain&&edits==before+2&&!tool.queuedMusicalEdit()&&
        tool.snapshot().at("selectedPoint")==2&&tool.snapshot().at("rawValue")=="-"&&
        GetFocus()==valueField&&first==1&&last==1&&SendMessageW(itemList,LB_GETCURSEL,0,0)==2,
        "Invalid point navigation submitted, retried or discarded retained input/selection");
      SetWindowTextW(valueField,L"0.5");context.busy=true;selectPoint(0);context.busy=false;
      require(lane==movedAgain&&edits==before+2&&tool.queuedMusicalEdit()&&tool.snapshot().at("selectedPoint")==2,
        "Navigation bypassed the busy owner or lost its unsent automatic edit");
      selectPoint(0);movedAgain[2]["value"]=.5;
      require(lane==movedAgain&&edits==before+3&&!tool.queuedMusicalEdit()&&tool.snapshot().at("selectedPoint")==0,
        "Idle retry did not finish the original busy edit and choose the requested point exactly once");
      // A write that succeeds but whose following read fails is uncertain from
      // the editor's perspective. Navigation must retain, never resend it.
      SetWindowTextW(valueField,L"0.375");failRead="automation.recorded.get";selectPoint(1);failRead.clear();
      auto accepted=movedAgain;accepted[0]["value"]=.375;const auto uncertain=tool.snapshot();
      selectPoint(1);tool.flushForProjectSave();
      require(lane==accepted&&edits==before+4&&!tool.queuedMusicalEdit()&&
        tool.snapshot().at("selectedPoint")==uncertain.at("selectedPoint")&&
        tool.snapshot().at("rawValue")=="0.375"&&tool.snapshot().at("fieldDraft")==true,
        "Post-write read failure navigated or silently replayed an uncertain point edit");
      click(11002);
      SetWindowTextW(valueField,L"0.25");click(11012);accepted[0]["value"]=.25;
      require(lane==accepted&&edits==before+5&&tool.snapshot().at("selectedPoint")==-1&&
        !tool.snapshot().at("fieldDraft").get<bool>()&&!tool.queuedMusicalEdit(),
        "New point did not finish the existing requested edit first");
      SetWindowTextW(valueField,L"0.875");selectPoint(0);
      require(lane==accepted&&edits==before+5&&tool.snapshot().at("selectedPoint")==-1&&
        tool.snapshot().at("rawValue")=="0.875"&&tool.snapshot().at("fieldDraft")==true,
        "Navigation implicitly added or erased an unfinished new point");click(11002);
      twoParameters=true;click(11002);SetWindowTextW(valueField,L"0.125");choose(11001,1);
      accepted[0]["value"]=.125;
      require(lane==accepted&&edits==before+6&&tool.snapshot().at("parameter")==2&&
        !tool.queuedMusicalEdit()&&!tool.snapshot().at("fieldDraft").get<bool>(),
        "Parameter navigation failed to finish the old point before adopting the explicit stable target");
      choose(11001,0);twoParameters=false;click(11002);
      SetWindowTextW(valueField,L"0.25");
      pump=[&](const std::string &method){if(method=="automation.recorded.edit")SetWindowTextW(valueField,L"0.875");};
      selectPoint(1);pump={};accepted[0]["value"]=.25;
      SendMessageW(window,WM_TIMER,0x5345,0);
      require(lane==accepted&&edits==before+7&&tool.snapshot().at("selectedPoint")==0&&
        tool.snapshot().at("rawValue")=="0.875"&&tool.snapshot().at("fieldDraft")==true&&
        !tool.queuedMusicalEdit(),"Navigation silently rebased or hid newer input pumped during the original write");
      click(11002);
    }
    {
      const auto selectPoint=[&](int row){SendMessageW(itemList,LB_SETCURSEL,row,0);
        SendMessageW(window,WM_COMMAND,MAKEWPARAM(11007,LBN_SELCHANGE),LPARAM(itemList));};
      lane=Json::array({{{"frame",24000},{"value",-.5}},{{"frame",36000},{"value",.625}},{{"frame",48000},{"value",.25}}});
      context.revision="pumped-point-navigation";click(11002);selectPoint(1);
      auto before=edits;const auto initial=lane;
      SetWindowTextW(GetDlgItem(window,11015),L"1.25");
      pump=[&](const std::string &method){if(method=="parameter.activity.get")selectPoint(2);};
      SendMessageW(window,WM_TIMER,3,0);pump={};
      SetFocus(itemList);SendMessageW(window,WM_KEYDOWN,VK_DELETE,0);
      SendMessageW(window,WM_KEYDOWN,VK_BACK,0);
      require(lane==initial&&edits==before&&tool.snapshot().at("selectedPoint")==1&&tool.queuedMusicalEdit(),
        "Delete before a deferred selection completed removed the previously selected point");
      SendMessageW(window,WM_TIMER,0x5345,0);context.busy=true;SendMessageW(window,WM_TIMER,4,0);context.busy=false;
      require(lane==initial&&edits==before&&tool.queuedMusicalEdit()&&tool.snapshot().at("selectedPoint")==1,
        "Pumped point navigation or debounce reentered the worker or invalidated the queued destination");
      SendMessageW(window,WM_TIMER,4,0);SendMessageW(window,WM_TIMER,0x5345,0);
      auto afterDelete=Json::array({{{"frame",24000},{"value",-.5}},{{"frame",60000},{"value",.625}}});
      require(lane==afterDelete&&edits==before+2&&!tool.queuedMusicalEdit()&&
        tool.snapshot().at("selectedPoint")==-1&&GetFocus()==itemList,
        "Deferred Delete lost the chosen timestamp, removed the old edited point, repeated a key or lost focus");
      SendMessageW(window,WM_TIMER,4,0);SendMessageW(window,WM_TIMER,0x5345,0);
      require(lane==afterDelete&&edits==before+2,"Draining an already completed point Delete repeated the mutation");
      // Also retain the original selection-only debounce ordering contract.
      lane=initial;context.revision="pumped-selection-only";click(11002);selectPoint(1);before=edits;
      SetWindowTextW(GetDlgItem(window,11015),L"1.25");
      pump=[&](const std::string &method){if(method=="parameter.activity.get")selectPoint(2);};
      SendMessageW(window,WM_TIMER,3,0);pump={};
      SendMessageW(window,WM_TIMER,0x5345,0);SendMessageW(window,WM_TIMER,4,0);
      auto expected=Json::array({{{"frame",24000},{"value",-.5}},{{"frame",48000},{"value",.25}},{{"frame",60000},{"value",.625}}});
      require(lane==expected&&edits==before+1&&!tool.queuedMusicalEdit()&&
        tool.snapshot().at("selectedPoint")==1&&SendMessageW(itemList,LB_GETCURSEL,0,0)==1,
        "Debounce-first pumped selection failed to save once and resolve the clicked frame after sorting");
      SetWindowTextW(GetDlgItem(window,11015),L"1.5");
      pump=[&](const std::string &method){if(method=="parameter.activity.get")selectPoint(0);};
      SendMessageW(window,WM_TIMER,3,0);pump={};
      SendMessageW(window,WM_TIMER,4,0);SendMessageW(window,WM_TIMER,0x5345,0);
      expected=Json::array({{{"frame",24000},{"value",-.5}},{{"frame",60000},{"value",.625}},{{"frame",72000},{"value",.25}}});
      require(lane==expected&&edits==before+2&&!tool.queuedMusicalEdit()&&tool.snapshot().at("selectedPoint")==0,
        "Navigation-first timer ordering lost the edit or selected a different point");
      twoParameters=true;click(11002);SetWindowTextW(valueField,L"0.125");
      pump=[&](const std::string &method){if(method=="parameter.activity.get")choose(11001,1);};
      SendMessageW(window,WM_TIMER,3,0);pump={};SendMessageW(window,WM_TIMER,0x5345,0);
      require(lane==expected&&edits==before+2&&tool.snapshot().at("parameter")==1,
        "Debounce raced the guarded parameter navigation");
      SendMessageW(window,WM_TIMER,4,0);expected[0]["value"]=.125;
      require(lane==expected&&edits==before+3&&!tool.queuedMusicalEdit()&&tool.snapshot().at("parameter")==2,
        "Pumped parameter navigation lost its valid original point edit or stable target");
      choose(11001,0);twoParameters=false;click(11002);
      SetWindowTextW(valueField,L"0.25");
      pump=[&](const std::string &method){if(method=="parameter.activity.get")selectPoint(1);};
      SendMessageW(window,WM_TIMER,3,0);pump={};SetWindowTextW(valueField,L"0.375");
      SendMessageW(window,WM_TIMER,4,0);
      require(lane==expected&&edits==before+3&&tool.snapshot().at("selectedPoint")==0&&tool.snapshot().at("rawValue")=="0.375",
        "Deferred selection adopted a newer draft generation");
      SendMessageW(window,WM_TIMER,0x5345,0);expected[0]["value"]=.375;
      require(lane==expected&&edits==before+4&&!tool.queuedMusicalEdit()&&tool.snapshot().at("selectedPoint")==0,
        "Cancelled navigation stranded the newer automatic edit");
      click(11002);
    }
    {
      const auto selectPoint=[&](int row){SendMessageW(itemList,LB_SETCURSEL,row,0);
        SendMessageW(window,WM_COMMAND,MAKEWPARAM(11007,LBN_SELCHANGE),LPARAM(itemList));};
      // A clean selection received during polling also retains Delete, even
      // if Save/Close is requested before the navigation timer can run.
      for(const int boundary:{0,1,2}){
        const auto initial=lane;const auto beforeEdits=edits;
        context.revision="delete-boundary-"+std::to_string(boundary);click(11002);selectPoint(0);SetFocus(itemList);
        pump=[&](const std::string &method){if(method=="parameter.activity.get"){
          selectPoint(1);SendMessageW(window,WM_KEYDOWN,VK_BACK,0);
          SetFocus(GetDlgItem(window,11021));SetWindowTextW(GetDlgItem(window,11021),L"dB");
          SendMessageW(GetDlgItem(window,11021),EM_SETSEL,1,2);
        }};
        SendMessageW(window,WM_TIMER,3,0);pump={};
        require(lane==initial&&edits==beforeEdits&&tool.queuedMusicalEdit(),
          "Select then Backspace reentered observation, or search focus/filtering revoked the pending musical edit");
        SendMessageW(window,WM_TIMER,5,0);
        DWORD searchFirst=0,searchLast=0;SendMessageW(GetDlgItem(window,11021),EM_GETSEL,WPARAM(&searchFirst),LPARAM(&searchLast));
        require(tool.queuedMusicalEdit()&&tool.snapshot().at("search")=="dB"&&GetFocus()==GetDlgItem(window,11021)&&
          searchFirst==1&&searchLast==2,"Deferred filtering consumed the original point action or changed the search caret");
        context.busy=true;bool refused=false;try{tool.flushForProjectSave();}catch(const std::exception &){refused=true;}
        require(refused&&lane==initial&&edits==beforeEdits&&tool.queuedMusicalEdit(),
          "Save bypassed the busy owner or dropped a pending selected-point deletion");context.busy=false;
        if(boundary==2){
          context.revision="delete-boundary-stale";refused=false;
          try{tool.flushForProjectSave();}catch(const std::exception &){refused=true;}
          require(refused&&lane==initial&&edits==beforeEdits&&!tool.queuedMusicalEdit(),
            "Queued Delete silently rebased to changed music or remained replayable after rejection");
        }else{
          if(boundary==0)tool.flushForProjectSave();else tool.hide();
          auto expected=initial;expected.erase(expected.begin()+1);
          require(lane==expected&&edits==beforeEdits+1&&!tool.queuedMusicalEdit()&&tool.snapshot().at("selectedPoint")==-1,
            "Save/Close omitted or repeated the exact selected-point deletion");
          SendMessageW(window,WM_TIMER,4,0);require(lane==expected&&edits==beforeEdits+1,
            "A posted selection timer repeated the deletion after Save/Close");
          if(boundary==1)tool.openAt();
        }
        lane=initial;context.revision="delete-boundary-restored";click(11002);choose(11006,2);SetWindowTextW(GetDlgItem(window,11021),L"");
      }
      // A source handoff must display the point just typed, not the lane from
      // before its debounce. Capture the source before the save reorders rows.
      for(const int entry:{0,1,2}){
        const auto initial=lane;const auto beforeEdits=edits;twoParameters=true;
        context.revision="delete-reopen-"+std::to_string(entry);click(11002);selectPoint(0);SetFocus(itemList);
        bool refused=false;
        pump=[&](const std::string &method){if(method=="parameter.activity.get"){
          selectPoint(1);SendMessageW(window,WM_KEYDOWN,VK_BACK,0);
          if(entry==0)tool.openAt();
          else if(entry==1)tool.openSourceAt("a",1);
          else try{tool.openSourceAt("a",2);}catch(const std::exception &){refused=true;}
        }};
        SendMessageW(window,WM_TIMER,3,0);pump={};
        require(refused==(entry==2)&&lane==initial&&edits==beforeEdits&&tool.queuedMusicalEdit()&&
          tool.snapshot().at("parameter")==1,"Reopening Activity cancelled its queued point edit or entered the busy worker");
        if(entry==2)tool.openSourceAt("a",2);else SendMessageW(window,WM_TIMER,4,0);
        auto expected=initial;expected.erase(expected.begin()+1);
        require(lane==expected&&edits==beforeEdits+1&&!tool.queuedMusicalEdit()&&
          tool.snapshot().at("parameter")== (entry==2?2:1),
          "Connected Activity reopening failed to retain exact deletion before changing targets");
        SendMessageW(window,WM_TIMER,4,0);require(lane==expected&&edits==beforeEdits+1,
          "Reopening Activity left an already accepted deletion replayable");
        if(entry==2)tool.openSourceAt("a",1);
        twoParameters=false;lane=initial;context.revision="delete-reopen-restored";click(11002);
      }
      for(const bool pumped:{false,true})for(const int button:{11008,11009}){
        const auto initial=lane;click(11002);selectPoint(0);const auto beforeEdits=edits;
        navigation=nullptr;navigationLane=nullptr;SetWindowTextW(valueField,L"0.6875");
        if(pumped){
          pump=[&](const std::string &method){if(method=="parameter.activity.get")click(button);};
          SendMessageW(window,WM_TIMER,3,0);pump={};SendMessageW(window,WM_TIMER,0x5345,0);
          require(navigation.is_null()&&lane==initial&&edits==beforeEdits,
            "Source handoff bypassed the observation read or pending navigation ordering");
          SendMessageW(window,WM_TIMER,4,0);
        }else click(button);
        auto expected=initial;expected[0]["value"]=.6875;
        require(lane==expected&&navigationLane==expected&&edits==beforeEdits+1&&
          navigation.at("kind")=="recorded"&&navigation.at("plugin")=="a"&&navigation.at("parameter")==1&&
          !tool.queuedMusicalEdit()&&!tool.snapshot().at("fieldDraft").get<bool>(),
          "Source handoff opened before the accepted point edit or changed its stable target");
        navigation=nullptr;SetFocus(valueField);SetWindowTextW(valueField,L"-");SendMessageW(valueField,EM_SETSEL,1,1);
        click(button);DWORD start=0,end=0;SendMessageW(valueField,EM_GETSEL,WPARAM(&start),LPARAM(&end));
        require(navigation.is_null()&&lane==expected&&edits==beforeEdits+1&&
          tool.snapshot().at("rawValue")=="-"&&GetFocus()==valueField&&start==1&&end==1,
          "Source handoff hid invalid input, changed music or moved its caret");
        lane=initial;click(11002);
      }
      const auto before=lane;const auto beforeEdits=edits;
      selectPoint(1);SetFocus(itemList);SendMessageW(window,WM_KEYDOWN,VK_BACK,0);
      auto removed=before;removed.erase(removed.begin()+1);
      require(lane==removed&&edits==beforeEdits+1&&tool.snapshot().at("selectedPoint")==-1&&GetFocus()==itemList,
        "List Backspace failed to remove only the selected recorded point or lost list focus");
      SendMessageW(window,WM_KEYDOWN,VK_DELETE,0);
      require(lane==removed&&edits==beforeEdits+1,"Delete with no selected point removed a different point");
      selectPoint(0);SetFocus(valueField);SetWindowTextW(valueField,L"0.25");SendMessageW(valueField,EM_SETSEL,4,4);
      SendMessageW(valueField,WM_KEYDOWN,VK_BACK,0);SendMessageW(valueField,WM_CHAR,VK_BACK,0);
      require(lane==removed&&edits==beforeEdits+1&&tool.snapshot().at("rawValue")=="0.2"&&
        tool.snapshot().at("selectedPoint")==0&&GetFocus()==valueField,
        "Text Backspace deleted music instead of editing the native value field");
      tool.flushForProjectSave();removed[0]["value"]=.2;
      require(lane==removed&&edits==beforeEdits+2,"Text Backspace result did not use ordinary automatic point editing");
      choose(11006,0);SetFocus(itemList);SendMessageW(window,WM_KEYDOWN,VK_DELETE,0);
      require(lane==removed&&edits==beforeEdits+2,"Sources-list Delete mutated a recorded point");
      choose(11006,2);selectPoint(0);SetFocus(itemList);
      pump=[&](const std::string &method){if(method=="parameter.activity.get")SendMessageW(window,WM_KEYDOWN,VK_DELETE,0);};
      SendMessageW(window,WM_TIMER,3,0);pump={};
      require(lane==removed&&edits==beforeEdits+2&&tool.queuedMusicalEdit(),"List Delete reentered the live observation read");
      SendMessageW(window,WM_TIMER,0x5345,0);removed.erase(removed.begin());
      require(lane==removed&&edits==beforeEdits+3&&!tool.queuedMusicalEdit()&&tool.snapshot().at("selectedPoint")==-1,
        "List Delete received during observation was lost or repeated");click(11002);
    }
    {
      // Shared history changes the visible clean page without a manual Refresh.
      // A frozen trace, active field, or pumped input has a separate contract.
      lane=Json::array({{{"frame",24000},{"value",-.5}},{{"frame",48000},{"value",.5}}});
      context.revision="history-base";click(11002);SetFocus(itemList);
      const auto beforeEdits=edits;const auto beforeLane=lane;
      lane[0]["value"]=-.25;context.revision="history-value";SendMessageW(window,WM_TIMER,3,0);
      require(tool.snapshot().at("recorded")==lane&&tool.snapshot().at("selectedPoint")==0&&
        tool.snapshot().at("rawValue")=="-0.25"&&!tool.snapshot().at("stale").get<bool>()&&GetFocus()==itemList&&edits==beforeEdits,
        "Clean history refresh lost the selected frame, list focus or exact lane");
      click(11003);const auto frozenCursor=tool.snapshot().at("cursor");
      lane.erase(lane.begin());context.revision="history-delete";SendMessageW(window,WM_TIMER,3,0);
      require(tool.snapshot().at("recorded")==lane&&tool.snapshot().at("selectedPoint")==-1&&
        tool.snapshot().at("frozen")==true&&tool.snapshot().at("cursor")==frozenCursor,
        "History refresh resumed a frozen trace or selected a replacement point");
      lane=beforeLane;context.revision="history-undo";SendMessageW(window,WM_TIMER,3,0);
      require(tool.snapshot().at("recorded")==lane&&tool.snapshot().at("selectedPoint")==-1&&edits==beforeEdits,
        "History Undo invented a point selection or submitted an edit");
      SetFocus(valueField);SendMessageW(valueField,EM_SETSEL,0,1);
      const auto focused=tool.snapshot();lane[0]["value"]=-.375;context.revision="history-focused";
      SendMessageW(window,WM_TIMER,3,0);SendMessageW(valueField,EM_GETSEL,WPARAM(&first),LPARAM(&last));
      require(tool.snapshot().at("recorded")==focused.at("recorded")&&
        tool.snapshot().at("rawValue")==focused.at("rawValue")&&
        tool.snapshot().at("expectedRevision")==focused.at("expectedRevision")&&GetFocus()==valueField&&first==0&&last==1,
        "History refresh rebased focused text or changed its native caret");
      SetFocus(itemList);SendMessageW(window,WM_TIMER,3,0);
      require(tool.snapshot().at("recorded")==lane&&!tool.snapshot().at("stale").get<bool>(),
        "Clean page did not catch up after leaving the retained field");
      click(11003); // Resume only the trace; keep the current recorded target.
      const auto beforePump=tool.snapshot();lane[0]["value"]=-.625;context.revision="history-pumped";
      pump=[&](const std::string &method){if(method=="automation.recorded.get"){
        SetFocus(valueField);SetWindowTextW(valueField,L"unfinished");SendMessageW(valueField,EM_SETSEL,2,5);}};
      SendMessageW(window,WM_TIMER,3,0);pump={};SendMessageW(valueField,EM_GETSEL,WPARAM(&first),LPARAM(&last));
      require(tool.snapshot().at("recorded")==beforePump.at("recorded")&&
        tool.snapshot().at("expectedRevision")==beforePump.at("expectedRevision")&&
        tool.snapshot().at("rawValue")=="unfinished"&&tool.snapshot().at("fieldDraft")==true&&
        !tool.snapshot().at("frozen").get<bool>()&&GetFocus()==valueField&&first==2&&last==5&&edits==beforeEdits,
        "Pumped history read overwrote new input, rebased its guard or froze observation");
      click(11002);SetFocus(itemList);
      const auto beforeFailure=tool.snapshot();lane[0]["value"]=-.75;context.revision="history-read-failure";
      unsigned laneReads=0;pump=[&](const std::string &method){if(method=="automation.recorded.get")++laneReads;};
      failRead="automation.recorded.get";SendMessageW(window,WM_TIMER,3,0);SendMessageW(window,WM_TIMER,3,0);pump={};failRead.clear();
      require(laneReads==1&&tool.snapshot().at("recorded")==beforeFailure.at("recorded")&&
        tool.snapshot().at("expectedRevision")==beforeFailure.at("expectedRevision")&&
        !tool.snapshot().at("frozen").get<bool>()&&edits==beforeEdits,
        "Failed history read discarded fields, stopped observation or retried every tick");
      click(11002);require(tool.snapshot().at("recorded")==lane,"Explicit Refresh did not recover a failed history page");
      const auto beforeBusy=tool.snapshot();lane[0]["value"]=-.875;context.revision="history-busy";
      context.busy=true;const auto beforeRequests=requests;SendMessageW(window,WM_TIMER,3,0);context.busy=false;
      require(requests==beforeRequests&&tool.snapshot().at("recorded")==beforeBusy.at("recorded"),
        "History refresh bypassed the busy document owner");
      SendMessageW(window,WM_TIMER,3,0);require(tool.snapshot().at("recorded")==lane&&edits==beforeEdits,
        "History refresh did not resume after the document owner became idle");
      click(11002);
    }
    {
      unsigned histories=0;SetFocus(itemList);
      pump=[&](const std::string &method){if(method=="parameter.activity.get"){
        require(tool.deferHistoryIfObserving([&]{tool.flushForHistory();++histories;}),"Polling list history was not queued");}};
      SendMessageW(window,WM_TIMER,3,0);pump={};
      require(histories==0&&tool.queuedMusicalEdit(),"History reentered the observation read or lost its intent");
      bool secondRefused=false;try{tool.deferHistoryIfObserving([&]{histories+=100;});}catch(const std::exception &){secondRefused=true;}
      require(secondRefused&&histories==0,"A second history command overtook the queued command");
      context.busy=true;SendMessageW(window,WM_TIMER,6,0);context.busy=false;
      require(histories==0,"Queued history bypassed the document owner");
      SendMessageW(window,WM_TIMER,6,0);SendMessageW(window,WM_TIMER,6,0);
      require(histories==1&&!tool.queuedMusicalEdit()&&GetFocus()==itemList,"Queued history was lost, repeated or changed focus");
      pump=[&](const std::string &method){if(method=="parameter.activity.get")tool.deferHistoryIfObserving([&]{++histories;});};
      SendMessageW(window,WM_TIMER,3,0);pump={};SetFocus(valueField);SendMessageW(window,WM_TIMER,6,0);
      require(histories==1&&!tool.queuedMusicalEdit(),"Queued history crossed a newer field focus");
      SetFocus(itemList);pump=[&](const std::string &method){if(method=="parameter.activity.get")tool.deferHistoryIfObserving([&]{++histories;});};
      SendMessageW(window,WM_TIMER,3,0);pump={};context.revision="history-command-stale";SendMessageW(window,WM_TIMER,6,0);
      require(histories==1&&!tool.queuedMusicalEdit(),"Queued history crossed a changed document revision");click(11002);
    }
    // A real revision change during a read must still reject the snapshot.
    const auto beforeRevisionChange=tool.snapshot();
    pump=[&](const std::string &method){if(method=="parameter.activity.get")context.revision="changed-during-read";};
    SendMessageW(window,WM_TIMER,3,0);pump={};
    require(tool.snapshot().at("frozen")==true&&tool.snapshot().at("cursor")==beforeRevisionChange.at("cursor")&&
      tool.snapshot().at("recorded")==beforeRevisionChange.at("recorded"),"Observation bypassed its document revision guard");
    click(11002);
    context.document="other";SendMessageW(window,WM_TIMER,3,0);require(tool.snapshot().at("frozen")==true&&tool.snapshot().at("document")=="song","Changed document silently retargeted observer");
    navigation=nullptr;click(11009);require(navigation.is_null(),"Song automation action crossed a captured document boundary");
    MINMAXINFO limits{};SendMessageW(window,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&limits));SetWindowPos(window,nullptr,0,0,limits.ptMinTrackSize.x,limits.ptMinTrackSize.y,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    RECT client{};GetClientRect(window,&client);for(auto child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))if(IsWindowVisible(child)){RECT r{};GetWindowRect(child,&r);MapWindowPoints(nullptr,window,reinterpret_cast<POINT *>(&r),2);wchar_t type[32]{};GetClassNameW(child,type,32);require(r.left>=0&&r.top>=0&&r.right<=client.right&&(wcscmp(type,L"ComboBox")==0||r.bottom<=client.bottom),"Activity controls escaped minimum client bounds");}
  }
  require(DestroyWindow(owner)!=FALSE,"Destroy test owner");owner=nullptr;
}
}
int main(){try{SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);ScreamSeq::Tests::runPrivateGui(L"ScreamSeqActivityTests",[]{INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);run();});std::cout<<"Parameter activity retained-window tests passed\n";return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
