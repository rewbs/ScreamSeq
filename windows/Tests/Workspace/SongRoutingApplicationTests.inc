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

// Graph-to-browser insertion exercises real native controls and the shared
// atomic plugin.add path, including publication, history and persistence.
void applicationMixingGraphUsability(){
  RestoreFixture fixture;auto &app=*fixture.app;app.edit("mixer.enable",RestoreJson::object());
  app.openSongRouting();auto *tool=app.songRoutingWindow.get();const auto window=tool->window();ScreamSeq::Tests::ownGuiWindow(window);
  const auto click=[&](int id){SendMessageW(window,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),LPARAM(GetDlgItem(window,id)));};
  const auto select=[&](const std::string &identity){const auto nodes=tool->snapshot().at("canvas").at("nodes");for(size_t i=0;i<nodes.size();++i)if(nodes[i].at("id")==identity){SendMessageW(GetDlgItem(window,3802),CB_SETCURSEL,i,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(3802,CBN_SELCHANGE),LPARAM(GetDlgItem(window,3802)));return;}throw std::runtime_error("Missing graph usability node");};
  const auto bus=app.documentOperation("mixer.get",RestoreJson::object()).at("buses")[0].at("id").get<std::string>();select(bus);
  restoreCheck(IsWindowEnabled(GetDlgItem(window,3839)),"Selected bus cannot browse effects");click(3839);
  auto *library=app.pluginLibraryWindow.get();restoreCheck(library&&library->visible()&&library->snapshot().at("destination").at("bus")==bus,"Graph Add effect did not capture its bus");
  const auto browser=library->window();ScreamSeq::Tests::ownGuiWindow(browser);SendMessageW(browser,WM_TIMER,1,0);
  const auto search=GetDlgItem(browser,3201);SetFocus(search);SetWindowTextW(search,L"Gain");
  SendMessageW(search,WM_KEYDOWN,VK_DOWN,1);
  restoreCheck(GetFocus()==search&&!library->snapshot().at("selected").get<std::string>().empty(),"Plugin search arrows lost focus or did not choose a filtered result");
  const auto before=app.documentOperation("mixer.get",RestoreJson::object()).at("buses");const auto rack=app.view->session.document.at("nativePlugins");
  SendMessageW(search,WM_KEYDOWN,VK_RETURN,1);
  restoreCheck(app.view->session.document.at("nativePlugins").size()==rack.size()+1,"Enter in plugin search did not add the chosen effect");
  const auto plugin=app.view->session.document.at("nativePlugins").back().at("instanceID").get<std::string>();
  auto expected=before;for(auto &entry:expected)if(entry.at("id")==bus)entry.at("inserts").push_back(plugin);
  restoreCheck(app.documentOperation("mixer.get",RestoreJson::object()).at("buses")==expected,"Graph insertion changed an unrelated bus or missed the captured destination");
  SendMessageW(window,WM_TIMER,7,0);auto nodes=tool->snapshot().at("canvas").at("nodes");
  restoreCheck(std::any_of(nodes.begin(),nodes.end(),[&](const auto &n){return n.at("id")=="plugin:"+plugin;})&&GetFocus()==search,"Graph refresh did not reveal the new effect or stole browser focus");
  app.edit("history.undo",{{"domain","all"}});restoreCheck(app.view->session.document.at("nativePlugins")==rack&&app.documentOperation("mixer.get",RestoreJson::object()).at("buses")==before,"Graph effect insertion did not have atomic Undo");
  app.edit("history.redo",{{"domain","all"}});restoreCheck(app.documentOperation("mixer.get",RestoreJson::object()).at("buses")==expected,"Graph effect insertion Redo changed routing");
  SendMessageW(window,WM_TIMER,7,0);library->hide();tool->show();
  // Add group/return are native graph actions, with stable new-node selection.
  click(3840);auto group=tool->snapshot().at("selected").get<std::string>();restoreCheck(group!=bus&&!group.empty(),"New group was not selected in graph");
  auto mixer=app.documentOperation("mixer.get",RestoreJson::object());restoreCheck(std::any_of(mixer.at("buses").begin(),mixer.at("buses").end(),[&](const auto &b){return b.at("id")==group&&b.at("kind")=="group";}),"Add group did not create a group bus");
  click(3841);const auto ret=tool->snapshot().at("selected").get<std::string>();restoreCheck(ret!=group&&!ret.empty(),"New return was not selected in graph");
  select(bus);click(3842);restoreCheck(tool->snapshot().at("page")==1,"Context insert-chain action did not select its inspector");
  // A native context menu must enter USER32's menu loop and cancel cleanly.
  const auto thread=GetWindowThreadProcessId(window,nullptr);
  auto menu=std::async(std::launch::async,[=]{bool entered=false;const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(std::chrono::steady_clock::now()<until){GUITHREADINFO info{sizeof(info)};if(GetGUIThreadInfo(thread,&info)&&(info.flags&GUI_INMENUMODE)){entered=true;break;}std::this_thread::sleep_for(std::chrono::milliseconds(5));}PostMessageW(window,WM_CANCELMODE,0,0);return entered;});
  SendMessageW(window,WM_CONTEXTMENU,WPARAM(window),MAKELPARAM(-1,-1));restoreCheck(menu.get(),"Graph context menu never entered the native menu loop");
  // Dirty fields remain captured after an external document change.
  SetWindowTextW(GetDlgItem(window,3807),L"-.");const auto captured=tool->snapshot().at("expectedRevision");app.edit("mixer.bus.set",{{"bus",bus},{"gainDB",-3.0}});SendMessageW(window,WM_TIMER,7,0);
  restoreCheck(tool->snapshot().at("expectedRevision")==captured&&tool->snapshot().at("draft")==true&&restoreText(GetDlgItem(window,3807))==L"-.","Automatic graph refresh discarded or rebased raw input");
  click(3839);restoreCheck(!library->visible(),"Dirty graph opened an effect browser against another revision");click(3815);
  select(bus);const auto canvas=tool->snapshot().at("canvas");const auto node=*std::find_if(canvas.at("nodes").begin(),canvas.at("nodes").end(),[&](const auto &n){return n.at("id")==bus;});
  const auto rect=node.at("rect");const auto scale=GetDpiForWindow(window)/96.f;const auto point=MAKELPARAM(int((rect[0].get<float>()+rect[2].get<float>()/2)*scale),int((rect[1].get<float>()+rect[3].get<float>()/2)*scale));
  restoreCheck(GetClassLongPtrW(window,GCL_STYLE)&CS_DBLCLKS,"Graph window did not register double-click input");SendMessageW(window,WM_LBUTTONDBLCLK,MK_LBUTTON,point);
  restoreCheck(app.mixerTarget==bus&&!app.mixerStripsMode,"Graph double-click did not open the selected bus inspector");
  const auto saved=app.documentOperation("mixer.get",RestoreJson::object()).at("buses");const auto path=saveDepartureFixture(fixture);app.documentOperation("document.open",departureOpenRequest(app,path));
  restoreCheck(app.documentOperation("mixer.get",RestoreJson::object()).at("buses")==saved&&app.view->session.document.at("nativePlugins").back().at("instanceID")==plugin,"Graph changes lost routing or plugin identity on save/reopen");
  fixture.close();
}

void applicationGraphParameterInspector(){
  RestoreFixture fixture;auto &app=*fixture.app;app.edit("mixer.enable",RestoreJson::object());const auto plugin=addRestoreGain(app),other=addRestoreGain(app);
  app.openSongRouting();auto *graph=app.songRoutingWindow.get();ScreamSeq::Tests::ownGuiWindow(graph->window());
  const auto select=[&](const std::string &id){const auto nodes=graph->snapshot().at("canvas").at("nodes");for(size_t i=0;i<nodes.size();++i)if(nodes[i].at("id")==id){SendMessageW(GetDlgItem(graph->window(),3802),CB_SETCURSEL,i,0);SendMessageW(graph->window(),WM_COMMAND,MAKEWPARAM(3802,CBN_SELCHANGE),LPARAM(GetDlgItem(graph->window(),3802)));return;}throw std::runtime_error("Missing parameter graph node");};
  select("plugin:"+plugin);SendMessageW(graph->window(),WM_COMMAND,3821,LPARAM(GetDlgItem(graph->window(),3821)));
  auto *editor=app.graphPluginParameters.at(plugin).get();const auto window=editor->window();ScreamSeq::Tests::ownGuiWindow(window);
  restoreCheck(editor->visible()&&graph->visible(),"Parameter inspector hid the mixing graph");
  const auto click=[&](int id){SendMessageW(window,WM_COMMAND,id,LPARAM(GetDlgItem(window,id)));};
  const auto original=app.documentOperation("plugin.parameters.get",{{"plugin",plugin}});const auto untouched=app.documentOperation("plugin.parameters.get",{{"plugin",other}});
  click(3953);restoreCheck(app.documentOperation("plugin.parameters.get",{{"plugin",plugin}})[0].at("value")!=original[0].at("value"),"Native parameter toggle did not apply");
  app.edit("history.undo",{{"domain","all"}});restoreCheck(app.documentOperation("plugin.parameters.get",{{"plugin",plugin}})==original,"Parameter toggle needed more than one Undo");click(3955);
  const auto found=std::find_if(original.begin(),original.end(),[](const auto &p){const auto f=ScreamSeq::PluginParameterField::read(p);return f.editable()&&f.kind==ScreamSeq::PluginParameterField::Kind::Number&&f.maximum>f.minimum;});
  restoreCheck(found!=original.end(),"Gain fixture has no numeric parameter");const auto index=size_t(found-original.begin());const auto field=ScreamSeq::PluginParameterField::read(*found);
  SendMessageW(GetDlgItem(window,3950),CB_SETCURSEL,index,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(3950,CBN_SELCHANGE),LPARAM(GetDlgItem(window,3950)));
  const double candidate=field.minimum+(field.maximum-field.minimum)*.25;
  SetWindowTextW(GetDlgItem(window,3951),L"-.");select("plugin:"+other);
  restoreCheck(graph->snapshot().at("selected")=="plugin:"+other&&editor->snapshot().at("plugin")==plugin&&restoreText(GetDlgItem(window,3951))==L"-.","Graph navigation retargeted or discarded a parameter draft");
  click(3957);restoreCheck(restoreText(GetDlgItem(window,3951))==L"-.","Parameter step overwrote partial text");
  click(3954);restoreCheck(app.documentOperation("plugin.parameters.get",{{"plugin",plugin}})==original,"Invalid parameter text mutated the plugin");
  SetWindowTextW(GetDlgItem(window,3951),std::to_wstring(candidate).c_str());click(3954);
  const auto after=app.documentOperation("plugin.parameters.get",{{"plugin",plugin}});restoreCheck(after[index].at("value")!=original[index].at("value")&&app.documentOperation("plugin.parameters.get",{{"plugin",other}})==untouched,"Parameter editor changed the graph's new selection instead of its captured plugin");
  app.edit("history.undo",{{"domain","all"}});restoreCheck(app.documentOperation("plugin.parameters.get",{{"plugin",plugin}})==original,"Parameter edit did not have one Undo");
  app.edit("history.redo",{{"domain","all"}});restoreCheck(app.documentOperation("plugin.parameters.get",{{"plugin",plugin}})==after,"Parameter Redo changed the value");click(3955);
  // Unrelated graph edits must not silently rebase a typed parameter draft.
  SetWindowTextW(GetDlgItem(window,3951),L"-.");const auto rev=editor->snapshot().at("revision");app.edit("document.patch",{{"title","Parameter draft survives graph work"}});SendMessageW(window,WM_TIMER,7,0);
  restoreCheck(editor->snapshot().at("revision")==rev&&restoreText(GetDlgItem(window,3951))==L"-.","Parameter timer rebased or overwrote raw text");SetWindowTextW(GetDlgItem(window,3951),std::to_wstring(field.value).c_str());click(3954);
  restoreCheck(app.documentOperation("plugin.parameters.get",{{"plugin",plugin}})==after,"Stale parameter edit was applied");
  const auto path=saveDepartureFixture(fixture);restoreRejected([&]{app.documentOperation("document.open",departureOpenRequest(app,path));});
  click(3955);SetWindowTextW(GetDlgItem(window,3951),std::to_wstring(field.value).c_str());
  app.sampleMutationCompletion=[&](const std::string &method){if(method=="plugin.parameters.set"){SetWindowTextW(GetDlgItem(window,3951),L"Newer text");throw std::runtime_error("Drop parameter completion after commit");}};
  click(3954);app.sampleMutationCompletion={};restoreCheck(!editor->snapshot().at("completion").is_null(),"Parameter completion was not retained");
  const auto committed=app.documentOperation("plugin.parameters.get",{{"plugin",plugin}});click(3954);click(3955);
  restoreCheck(editor->snapshot().at("completion").is_null()&&restoreText(GetDlgItem(window,3951))==L"Newer text"&&app.documentOperation("plugin.parameters.get",{{"plugin",plugin}})==committed,"Parameter Review replayed a write or discarded newer text");
  click(3955);saveDepartureFixture(fixture);app.documentOperation("document.open",departureOpenRequest(app,path));
  restoreCheck(editor->retired()&&app.documentOperation("plugin.parameters.get",{{"plugin",plugin}})==committed&&app.documentOperation("plugin.parameters.get",{{"plugin",other}})==untouched,"Parameter identity/value failed save/reopen or stale editor remained live");fixture.close();
}
