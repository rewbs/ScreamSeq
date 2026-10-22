void applicationPluginRecoveryReview() {
    using Json=RestoreJson;
    RestoreFixture fixture;auto &app=*fixture.app;const auto music=songState(app);
    auto context=std::pair(app.documentId,app.view->session.revision);
    Json original={{"version",1},{"reviewOnly",true},{"document","previous-process"},{"revision","previous-revision"},
      {"take","original-take"},{"accepted","9007199254740993"},{"lost","2"},{"previewError",""},{"events",Json::array()}};
    for(size_t i=0;i<130;++i)original["events"].push_back({{"plugin","retired-plugin"},{"parameter",17},{"value",.125+double(i)/1000.},
      {"hostTime",std::to_string(9007199254740993ull+i)},{"state",i%2?"unknown":"refused"},{"error","Original input retained"}});
    auto future=original;future["version"]=2;
    Json preserved={{Tracker::gestureRecoveryKey,Json::object()},{Tracker::controlInputRecoveryKey,Json::array({future,original})}};
    const auto initial=preserved;bool failRead=false;std::function<void()> afterPage;
    std::vector<std::pair<std::string,Json>> reads;
    const auto read=[&](const std::string &method,const Json &params)->Json {
      reads.emplace_back(method,params);Json result;
      if(method=="automation.recovery.list")result=Tracker::pluginInputRecoveryList(preserved,params.at("offset").get<size_t>(),params.at("limit").get<size_t>());
      else {
        restoreCheck(method=="automation.recovery.get"&&params.at("expectedRevision")==context.second,"Native recovery review omitted its revision or attempted a mutation");
        if(failRead)throw std::runtime_error("Owned review read failure");
        result=Tracker::pluginInputRecoveryPage(preserved,params.at("kind").get<std::string>(),params.at("index").get<size_t>(),params.at("offset").get<size_t>(),params.at("limit").get<size_t>());
      }
      result["revision"]=context.second;if(method=="automation.recovery.get")if(auto action=std::exchange(afterPage,{}))action();return result;
    };
    ScreamSeq::PluginRecoveryReviewWindow tool(app.window,read,[&]{return context;});tool.show();
    const auto window=tool.window();ScreamSeq::Tests::ownGuiWindow(window);
    const auto control=[&](int id){return GetDlgItem(window,id);};
    const auto click=[&](int id){SendMessageW(window,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),reinterpret_cast<LPARAM>(control(id)));};
    const auto choose=[&](int row){SendMessageW(control(9781),CB_SETCURSEL,row,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(9781,CBN_SELCHANGE),reinterpret_cast<LPARAM>(control(9781)));};
    restoreCheck(tool.snapshot().at("archiveTotal")==3&&!tool.snapshot().at("error").get<std::string>().empty()&&!tool.snapshot().at("pending").get<bool>(),
      "Unsupported first collection prevented the native review window from opening");
    choose(1);restoreCheck(tool.snapshot().at("events").empty()&&!tool.snapshot().at("error").get<std::string>().empty(),"Future archive was rendered as known input");
    choose(2);restoreCheck(tool.snapshot().at("events").size()==128&&tool.snapshot().at("selected").at("index")==1&&
      tool.snapshot().at("events")[0].at("hostTime")=="9007199254740993"&&tool.snapshot().at("error")=="",
      "Independent valid native archive was hidden, narrowed or resolved by the wrong index");
    restoreCheck((GetWindowLongPtrW(control(9790),GWL_STYLE)&ES_READONLY)!=0&&
      ScreamSeq::Tests::accessibleName(control(9790))==L"Exact original input data, read only","Exact recovery detail is editable or unnamed");
    click(9788);restoreCheck(tool.snapshot().at("offset")==128&&tool.snapshot().at("events").size()==2,"Recovered input page reused earlier events");
    ScreamSeq::NativeReportList::setItemState(control(9786),-1,0,LVIS_SELECTED|LVIS_FOCUSED);
    ScreamSeq::NativeReportList::setItemState(control(9786),1,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    SetFocus(control(9790));SendMessageW(control(9790),EM_SETSEL,2,8);const auto detail=controlState(control(9790));
    click(9784);restoreCheck(controlState(control(9790))==detail&&GetFocus()==control(9790)&&ListView_GetNextItem(control(9786),-1,LVNI_SELECTED)==1,
      "Refreshing identical recovery input reset selection, exact-text caret or keyboard focus");
    SendMessageW(control(9790),WM_CHAR,VK_BACK,0);restoreCheck(controlState(control(9790))==detail,"Backspace changed detached original evidence");
    tool.hide();tool.show();restoreCheck(GetFocus()==control(9790)&&tool.snapshot().at("offset")==128,"Reopening the retained review lost focus or its page");
    failRead=true;click(9784);restoreCheck(tool.snapshot().at("events").empty()&&tool.snapshot().at("error")=="Owned review read failure"&&
      !tool.snapshot().at("pending").get<bool>(),"Failed review kept unrelated rows or left input pending");
    failRead=false;click(9784);restoreCheck(tool.snapshot().at("events").size()==2&&preserved==initial,"Review retry changed original evidence or failed to resume");
    click(9793);restoreCheck(GetFocus()==control(9790)&&restoreText(control(9790)).find(L"9007199254740993")!=std::wstring::npos&&
      restoreText(control(9790)).find(L"previous-process")!=std::wstring::npos,"Archive details hid exact counters or original document provenance");
    SendMessageW(control(9790),EM_SETSEL,2,8);const auto archiveText=controlState(control(9790));click(9784);
    restoreCheck(controlState(control(9790))==archiveText,"Refresh reset the selected archive provenance text");
    context.second="later-view-revision";afterPage=[&]{context.first="replaced-song";};click(9784);
    restoreCheck(tool.snapshot().at("events").empty()&&!tool.snapshot().at("error").get<std::string>().empty()&&preserved==initial,
      "A pumped document replacement redirected recovery input to another song");context.first=app.documentId;click(9784);
    for(unsigned i=0;i<65;++i){auto another=original;another["take"]="other-take-"+std::to_string(i);preserved[Tracker::controlInputRecoveryKey].push_back(std::move(another));}
    click(9784);click(9783);restoreCheck(tool.snapshot().at("archiveOffset")==64&&tool.snapshot().at("archiveTotal")==68&&
      tool.snapshot().at("entries").size()==4&&tool.snapshot().at("selected").at("index")==63,
      "Recovered archive pagination hid later takes or confused global and per-kind indices");
    restoreCheck(songState(app)==music,"Read-only recovered input review changed music or history");
}
