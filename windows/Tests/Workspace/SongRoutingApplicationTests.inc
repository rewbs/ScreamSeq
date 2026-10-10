void applicationSongRoutingPorts(){
  RestoreFixture fixture;auto &app=*fixture.app;const auto plugin=addRestoreGain(app);
  app.edit("mixer.enable",RestoreJson::object());
  auto mixer=app.documentOperation("mixer.get",RestoreJson::object());
  const auto source=mixer.at("buses")[0].at("id").get<std::string>(),other=mixer.at("buses")[1].at("id").get<std::string>();
  app.edit("mixer.bus.set",{{"bus",other},{"inserts",RestoreJson::array({plugin})}});
  const auto path=saveDepartureFixture(fixture);app.openSongRouting();auto *tool=app.songRoutingWindow.get();const auto window=tool->window();ScreamSeq::Tests::ownGuiWindow(window);
  const auto click=[&](int id){SendMessageW(window,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),LPARAM(GetDlgItem(window,id)));};
  const auto select=[&](int id,int index){SendMessageW(GetDlgItem(window,id),CB_SETCURSEL,index,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(id,CBN_SELCHANGE),LPARAM(GetDlgItem(window,id)));};
  const auto node=[&](int id,const std::string &identity){const auto nodes=tool->snapshot().at("canvas").at("nodes");for(size_t i=0;i<nodes.size();++i)if(nodes[i].at("id")==identity){select(id,int(i));return;}throw std::runtime_error("Missing routing fixture node");};
  select(3804,4);node(3805,source);node(3806,"plugin:"+plugin);
  const auto catalog=tool->snapshot().at("inputPorts");
  restoreCheck(catalog.at("plugin")==plugin&&!catalog.at("ports").empty()&&catalog.at("indices")==RestoreJson::array({0}),"Routing did not read the actual plugin input catalog");
  const auto originalRevision=app.view->session.revision;click(3812);
  restoreCheck(app.view->session.revision==originalRevision&&tool->snapshot().at("completion").is_null(),"Routing guessed a plugin input before explicit selection");
  select(3837,1);SetWindowTextW(GetDlgItem(window,3807),L"-6");
  const auto before=app.documentOperation("mixer.get",RestoreJson::object());
  app.sampleMutationCompletion=[&](const std::string &method){if(method=="mixer.sidechains.set"){
    SetWindowTextW(GetDlgItem(window,3807),L"-.");throw std::runtime_error("Drop routing completion after newer raw gain");
  }};click(3812);app.sampleMutationCompletion={};
  const auto after=app.documentOperation("mixer.get",RestoreJson::object());
  restoreCheck(after.at("sidechains")==RestoreJson::array({{{"source",source},{"plugin",plugin},{"input",0},{"gainDB",-6.0},{"preFader",false},{"enabled",true}}})&&
    !tool->snapshot().at("completion").is_null()&&restoreText(GetDlgItem(window,3807))==L"-.","Sidechain insertion lost its exact result or newer raw draft");
  restoreRejected([&]{app.documentOperation("document.open",departureOpenRequest(app,path));});
  for(int id:{3812,3813,3814,3836})click(id);
  restoreCheck(app.documentOperation("mixer.get",RestoreJson::object()).at("sidechains")==after.at("sidechains"),"Unresolved routing operation was submitted again");
  click(3815);
  restoreCheck(tool->snapshot().at("completion").is_null()&&tool->snapshot().at("report").at("outcome")=="returned"&&restoreText(GetDlgItem(window,3807))==L"-.","Routing Review lost exact attribution or discarded newer raw input");
  click(3815);app.edit("history.undo",{{"domain","all"}});
  restoreCheck(app.documentOperation("mixer.get",RestoreJson::object()).at("sidechains")==before.at("sidechains"),"Sidechain insertion needed more than one Undo");
  app.edit("history.redo",{{"domain","all"}});click(3815);
  restoreCheck(app.documentOperation("mixer.get",RestoreJson::object()).at("sidechains")==after.at("sidechains"),"Sidechain Redo changed the captured endpoints");
  saveDepartureFixture(fixture);app.documentOperation("document.open",departureOpenRequest(app,path));
  restoreCheck(app.documentOperation("mixer.get",RestoreJson::object()).at("sidechains")==after.at("sidechains"),"Declared sidechain route did not survive save/reopen");
  app.openSongRouting();restoreCheck(app.songRoutingWindow&&!app.songRoutingWindow->retired()&&app.songRoutingWindow->visible(),"Retired routing owner could not be reopened after document replacement");
  fixture.close();
}

void applicationSongRoutingUnknownResult(){
  RestoreFixture fixture;auto &app=*fixture.app;app.edit("mixer.enable",RestoreJson::object());saveDepartureFixture(fixture);
  unsigned writes=0;bool failRead=false,malformed=false,newRaw=false;
  ScreamSeq::SongRoutingWindow *owner=nullptr;
  ScreamSeq::SongRoutingWindow tool(app.window,[&](const std::string &method,const RestoreJson &params){
    if(method=="graph.get"&&failRead)throw std::runtime_error("Routing observation unavailable");
    auto result=app.documentOperation(method,params);
    if(method=="graph.get"&&malformed)result["mixer"]["buses"]=nullptr;
    if(method=="graph.get"&&newRaw){newRaw=false;SetWindowTextW(GetDlgItem(owner->window(),3807),L"New raw text during Review");}
    return result;
  },[&]{return std::pair(app.documentId,app.view->session.revision);},[](const auto &){},
    [&](const auto &,const auto &,const auto &)->ScreamSeq::Api::CompletedCall{++writes;throw std::runtime_error("Unknown routing delivery outcome");});
  owner=&tool;ScreamSeq::Tests::ownGuiWindow(tool.window());
  const auto click=[&](int id){SendMessageW(tool.window(),WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),LPARAM(GetDlgItem(tool.window(),id)));};
  const auto select=[&](int id,int index){SendMessageW(GetDlgItem(tool.window(),id),CB_SETCURSEL,index,0);SendMessageW(tool.window(),WM_COMMAND,MAKEWPARAM(id,CBN_SELCHANGE),LPARAM(GetDlgItem(tool.window(),id)));};
  select(3804,1);select(3805,0);select(3806,1);click(3812);
  restoreCheck(writes==1&&!tool.snapshot().at("completion").is_null(),"Unknown routing operation lost its captured write");
  const auto submission=tool.snapshot().at("completion");
  SetWindowTextW(GetDlgItem(tool.window(),3807),L"Later raw gain");
  for(int id:{3812,3813,3814,3836})click(id);
  failRead=true;click(3815);failRead=false;restoreCheck(tool.snapshot().at("completion")==submission,"Failed observation discarded uncertain routing");
  malformed=true;click(3815);malformed=false;restoreCheck(!tool.snapshot().at("completion").is_null(),"Malformed graph observation resolved uncertain routing");
  newRaw=true;click(3815);restoreCheck(!tool.snapshot().at("completion").is_null()&&restoreText(GetDlgItem(tool.window(),3807))==L"New raw text during Review","Review ignored a newer draft generation");
  click(3815);
  restoreCheck(writes==1&&tool.snapshot().at("completion").is_null()&&tool.snapshot().at("report").at("outcome")=="unverified"&&tool.snapshot().at("readbackNeedsReload")==true,
    "Routing observation repeated the write or claimed success without a receipt");
  click(3812);restoreCheck(writes==1,"Unverified routing observation enabled a write before Reload");
  click(3815);restoreCheck(tool.snapshot().at("readbackNeedsReload")==false&&!tool.snapshot().at("draft").get<bool>(),"Explicit Reload failed to adopt reviewed routing");
}

void applicationSongRoutingCatalogRefusal(){
  for(unsigned scenario=0;scenario<4;++scenario){
    RestoreFixture fixture;auto &app=*fixture.app;const auto plugin=addRestoreGain(app);app.edit("mixer.enable",RestoreJson::object());
    unsigned writes=0;ScreamSeq::SongRoutingWindow *owner=nullptr;
    ScreamSeq::SongRoutingWindow tool(app.window,[&](const std::string &method,const RestoreJson &params){
      if(method=="plugin.buses.get"&&scenario==0)throw std::runtime_error("Plugin unavailable");
      auto result=app.documentOperation(method,params);
      if(method=="plugin.buses.get"){
        if(scenario==1)for(auto &port:result.at("buses"))port["supported"]=false;
        if(scenario==2)result["buses"]=RestoreJson::array();
        if(scenario==3)SetWindowTextW(GetDlgItem(owner->window(),3807),L"New draft during port read");
      }
      return result;
    },[&]{return std::pair(app.documentId,app.view->session.revision);},[](const auto &){},
      [&](const auto &,const auto &,const auto &)->ScreamSeq::Api::CompletedCall{++writes;throw std::runtime_error("Unavailable port must not submit");});
    owner=&tool;ScreamSeq::Tests::ownGuiWindow(tool.window());
    const auto select=[&](int id,int index){SendMessageW(GetDlgItem(tool.window(),id),CB_SETCURSEL,index,0);SendMessageW(tool.window(),WM_COMMAND,MAKEWPARAM(id,CBN_SELCHANGE),LPARAM(GetDlgItem(tool.window(),id)));};
    select(3804,4);select(3805,0);
    const auto nodes=tool.snapshot().at("canvas").at("nodes");int target=-1;
    for(size_t i=0;i<nodes.size();++i)if(nodes[i].at("id")=="plugin:"+plugin)target=int(i);
    restoreCheck(target>=0,"Missing plugin catalog fixture node");select(3806,target);
    if(scenario==1)select(3837,1);
    SendMessageW(tool.window(),WM_COMMAND,MAKEWPARAM(3812,BN_CLICKED),LPARAM(GetDlgItem(tool.window(),3812)));
    const auto state=tool.snapshot();restoreCheck(writes==0&&state.at("completion").is_null()&&!state.at("pending").get<bool>(),"Unavailable or changed port catalog admitted a connection");
    if(scenario==3)restoreCheck(restoreText(GetDlgItem(tool.window(),3807))==L"New draft during port read"&&!state.at("inputPorts").at("error").get<std::string>().empty(),"Port discovery overwrote a newer raw draft");
    if(scenario==2)restoreCheck(state.at("inputPorts").at("indices").empty(),"Empty plugin catalog fabricated a sidechain input");
  }
}
