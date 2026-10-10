void applicationInstrumentPluginAssignment(){
  using Json=RestoreJson;RestoreFixture fixture;auto &app=*fixture.app;
  auto env=[](const wchar_t *key){wchar_t value[32768]{};const auto size=GetEnvironmentVariableW(key,value,DWORD(std::size(value)));restoreCheck(size&&size<std::size(value),"Missing fixture environment");return std::filesystem::path(value);};
  Tracker::WindowsVST3::configure(utf8Path(env(L"SCREAMSEQ_WORKSPACE_VST3_SCANNER")),utf8Path(fixture.folder/L"instrument-cache.json"));
  Tracker::WindowsVST3::rescan(utf8Path(env(L"SCREAMSEQ_WORKSPACE_VST3_FIXTURE")));
  createRestoreInstrument(app);const auto write=[&](const std::string &method,Json params){params["expectedRevision"]=app.view->session.revision;return app.documentOperation(method,params);};
  const auto mapping=app.documentOperation("instrument.get",{{"instrument",1}}).at("mapping");
  app.openInstrumentEnvelope();auto *panel=app.instrumentEnvelopeWindow.get();ScreamSeq::Tests::ownGuiWindow(panel->window());
  const auto source=GetDlgItem(panel->window(),4690);restoreCheck(IsWindowVisible(source)&&IsWindowEnabled(source),"Instrument panel lacks a visible Plugin action");
  SendMessageW(panel->window(),WM_COMMAND,MAKEWPARAM(4690,BN_CLICKED),reinterpret_cast<LPARAM>(source));
  HWND window=nullptr;EnumThreadWindows(GetCurrentThreadId(),[](HWND h,LPARAM out)->BOOL{wchar_t name[96]{};GetClassNameW(h,name,96);if(!wcscmp(name,L"ScreamSeq.InstrumentPlugin")){*reinterpret_cast<HWND *>(out)=h;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&window));
  restoreCheck(window&&IsWindowVisible(window),"Plugin action did not open native instrument assignment");ScreamSeq::Tests::ownGuiWindow(window);
  const auto snapshot=[&]{return panel->snapshot().at("pluginAssignment");};
  const auto press=[&](int id){SendMessageW(window,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(window,id)));};
  const auto choose=[&](int id,int index){SendMessageW(GetDlgItem(window,id),CB_SETCURSEL,index,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(id,CBN_SELCHANGE),reinterpret_cast<LPARAM>(GetDlgItem(window,id)));};
  // Actual browser and rack navigation are reachable from this captured owner.
  press(4805);restoreCheck(app.pluginLibraryWindow&&app.pluginLibraryWindow->visible()&&app.pluginLibraryWindow->snapshot().at("destination").at("bus")=="","Add plugin did not open the rack library");
  restoreCheck(SendMessageW(GetDlgItem(app.pluginLibraryWindow->window(),3202),CB_GETCURSEL,0,0)==2,"Instrument browse did not filter instruments");
  app.pluginLibraryWindow->hide();press(4806);restoreCheck(app.pluginEditorVisible(),"Instrument plugin did not reveal the rack");
  const auto catalog=app.documentOperation("plugin.discover",{{"format","VST3"}});auto descriptor=std::find_if(catalog.begin(),catalog.end(),[](const auto &p){return p.at("name")=="Resonance Test Instrument";});restoreCheck(descriptor!=catalog.end(),"Instrument fixture missing");
  write("plugin.add",{{"descriptor",*descriptor}});const auto plugin=app.view->session.document.at("nativePlugins").back().at("instanceID").get<std::string>();
  SendMessageW(window,WM_TIMER,1,0);restoreCheck(snapshot().at("plugins").size()==1,"New rack plugin did not appear in captured instrument chooser");
  choose(4801,1);choose(4802,6);restoreCheck(snapshot().at("dirty")==true&&panel->retainedDraft(),"Assignment draft is absent from retained owner census");
  const auto target=snapshot().at("instrument");press(4803);
  restoreCheck(!snapshot().at("dirty").get<bool>()&&snapshot().at("plugin")==plugin&&snapshot().at("channel")==7,"Native assignment failed");
  const auto assignment=app.documentOperation("plugin.instruments.get",{{"plugin",plugin}}).at("assignments");
  restoreCheck(assignment.size()==1&&assignment[0].at("instrument")==1&&assignment[0].at("channel")==7,"Wrong captured instrument or MIDI channel assigned");
  restoreCheck(app.documentOperation("instrument.get",{{"instrument",1}}).at("mapping")==mapping,"Assigning plugin erased sample mapping");
  write("history.undo",{{"domain","all"}});SendMessageW(window,WM_TIMER,1,0);restoreCheck(snapshot().at("plugin")=="","One Undo did not remove assignment");
  write("history.redo",{{"domain","all"}});SendMessageW(window,WM_TIMER,1,0);restoreCheck(snapshot().at("plugin")==plugin,"Redo lost plugin identity");
  // A stale draft must not jump to the newer revision or another instrument.
  choose(4802,9);write("document.patch",{{"title","Newer song edit"}});const auto newer=app.view->session.revision;press(4803);
  restoreCheck(snapshot().at("dirty")==true&&snapshot().at("stale")==true&&app.view->session.revision==newer&&snapshot().at("instrument")==target,"Stale assignment was replayed or redirected");
  press(4804);choose(4801,0);app.completionFault=[]{throw std::runtime_error("Lost assignment callback");};press(4803);
  restoreCheck(!snapshot().at("completion").is_null(),"Lost assignment callback discarded its result");const auto assignedRevision=app.view->session.revision;press(4804);
  restoreCheck(snapshot().at("completion").is_null()&&snapshot().at("plugin")==""&&app.view->session.revision==assignedRevision,"Review repeated assignment or failed to read current state");
  write("history.undo",{{"domain","all"}});SendMessageW(window,WM_TIMER,1,0);restoreCheck(snapshot().at("plugin")==plugin,"Unassign recovery added another Undo");
  press(4807);const auto saved=saveDepartureFixture(fixture);app.documentOperation("document.open",departureOpenRequest(app,saved));
  restoreCheck(app.documentOperation("plugin.instruments.get",{{"plugin",plugin}}).at("assignments")==assignment&&app.documentOperation("instrument.get",{{"instrument",1}}).at("mapping")==mapping,"Save/reopen lost layered assignment, MIDI channel or sample map");
  fixture.close();std::cout<<"PASS instrument-panel plugin assignment, browse/rack, captured drafts, lost receipt, Undo/Redo and save/reopen\n";
}
