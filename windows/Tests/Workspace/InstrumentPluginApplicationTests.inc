void instrumentPluginRemovedTarget(){
  using Json=RestoreJson;RestoreFixture fixture;
  std::pair<std::string,std::string> context={"removed-target","r1"};
  Json instruments=Json::array({{{"id","n1"},{"index",1},{"name","Captured instrument"}}});
  const auto original=instruments;unsigned writes=0;bool failRead=false,changeDuringRead=false;
  ScreamSeq::InstrumentPluginWindow panel(fixture.root,context.first,"n1",
    [&](const std::string &method,const Json &)->Json{
      if(method=="synchronizeView")return Json::object();
      restoreCheck(method=="document.get","Removed-target review must only read the document");
      if(failRead)throw std::runtime_error("Controlled catalogue read failure");
      if(changeDuringRead)context.second+="x";
      return {{"instruments",instruments},{"nativePlugins",Json::array()}};
    },[&]{return context;},[&](const std::string &,const Json &)->ScreamSeq::Api::CompletedCall{
      ++writes;throw std::runtime_error("Removed-target review must never submit an assignment");
    },[](bool){});
  ScreamSeq::Tests::ownGuiWindow(panel.window());panel.show();
  const auto press=[&](int id){SendMessageW(panel.window(),WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(panel.window(),id)));};
  // An Undo can remove a clean inspected instrument before the next native
  // selection notification. That stale selection must remain reviewable.
  context.second="r2";instruments=Json::array();
  SendMessageW(panel.window(),WM_COMMAND,MAKEWPARAM(4801,CBN_SELCHANGE),reinterpret_cast<LPARAM>(GetDlgItem(panel.window(),4801)));
  restoreCheck(panel.retainedDraft(),"Stale assignment fixture did not retain its draft");press(4804);
  restoreCheck(panel.snapshot().at("removed")==true&&panel.retainedDraft(),"Missing instrument review silently discarded the assignment");
  const auto discard=GetDlgItem(panel.window(),4808);
  restoreCheck(discard&&IsWindowVisible(discard)&&IsWindowEnabled(discard),"Missing instrument has no explicit discard action");
  instruments=original;context.second="r3";press(4808);
  restoreCheck(panel.retainedDraft()&&panel.visible(),"Discard ignored an instrument restored after Review");
  instruments=Json::array();context.second="r4";press(4804);failRead=true;press(4808);failRead=false;
  restoreCheck(panel.retainedDraft(),"Failed catalogue read discarded the captured draft");
  changeDuringRead=true;press(4808);changeDuringRead=false;
  restoreCheck(panel.retainedDraft(),"Changed catalogue revision discarded the captured draft");
  press(4804);press(4808);
  restoreCheck(!panel.retainedDraft()&&!panel.visible()&&writes==0&&panel.snapshot().at("instrument")=="n1", "Explicit removed-target discard wrote, retargeted or retained the draft");
  std::cout<<"PASS removed instrument assignment: explicit discard, fresh identity check, failed/stale reads and no writes\n";
}

void applicationInstrumentPluginAssignment(){
  instrumentPluginRemovedTarget();
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
  choose(4801,1);restoreCheck(snapshot().at("plugin")==plugin&&!snapshot().at("dirty").get<bool>(),"Plugin selection did not save immediately");choose(4802,6);
  const auto target=snapshot().at("instrument");
  restoreCheck(!snapshot().at("dirty").get<bool>()&&snapshot().at("plugin")==plugin&&snapshot().at("channel")==7,"Native assignment failed");
  const auto assignment=app.documentOperation("plugin.instruments.get",{{"plugin",plugin}}).at("assignments");
  restoreCheck(assignment.size()==1&&assignment[0].at("instrument")==1&&assignment[0].at("channel")==7,"Wrong captured instrument or MIDI channel assigned");
  restoreCheck(app.documentOperation("instrument.get",{{"instrument",1}}).at("mapping")==mapping,"Assigning plugin erased sample mapping");
  write("history.undo",{{"domain","all"}});SendMessageW(window,WM_TIMER,1,0);restoreCheck(snapshot().at("plugin")==plugin&&snapshot().at("channel")==1,"One Undo did not restore the previous MIDI channel");
  write("history.redo",{{"domain","all"}});SendMessageW(window,WM_TIMER,1,0);restoreCheck(snapshot().at("plugin")==plugin,"Redo lost plugin identity");
  // A clean chooser intentionally follows adopted revisions on its 250 ms
  // timer. Establish that behavior separately from an edit arriving before
  // refresh; worker message pumping must not decide which case this tests.
  write("document.patch",{{"title","Clean chooser refresh"}});SendMessageW(window,WM_TIMER,1,0);
  restoreCheck(snapshot().at("revision")==app.view->session.revision&&!snapshot().at("dirty").get<bool>()&&
    snapshot().at("instrument")==target&&snapshot().at("plugin")==plugin&&snapshot().at("channel")==7,
    "Clean assignment refresh changed its captured instrument, plugin or MIDI channel");
  restoreCheck(KillTimer(window,1)!=FALSE,"Assignment fixture lost its automatic refresh timer");
  MSG queuedTimer{};while(PeekMessageW(&queuedTimer,window,WM_TIMER,WM_TIMER,PM_REMOVE))
    if(queuedTimer.wParam!=1)DispatchMessageW(&queuedTimer);
  const auto capturedRevision=snapshot().at("revision");
  // A stale draft must not jump to the newer revision or another instrument.
  write("document.patch",{{"title","Newer song edit"}});const auto newer=app.view->session.revision;
  restoreCheck(snapshot().at("revision")==capturedRevision&&capturedRevision!=newer&&snapshot().at("stale")==true,
    "Stale assignment fixture did not retain the pre-edit presentation revision");
  choose(4802,9);
  const auto refusedAssignment=snapshot();
  restoreCheck(refusedAssignment.at("dirty")==true&&refusedAssignment.at("stale")==true&&app.view->session.revision==newer&&refusedAssignment.at("instrument")==target,
    ("Stale assignment was replayed or redirected: "+refusedAssignment.dump()).c_str());
  press(4804);app.completionFault=[]{throw std::runtime_error("Lost assignment callback");};choose(4801,0);
  restoreCheck(!snapshot().at("completion").is_null(),"Lost assignment callback discarded its result");const auto assignedRevision=app.view->session.revision;press(4804);
  restoreCheck(snapshot().at("completion").is_null()&&snapshot().at("plugin")==""&&app.view->session.revision==assignedRevision,"Review repeated assignment or failed to read current state");
  write("history.undo",{{"domain","all"}});SendMessageW(window,WM_TIMER,1,0);restoreCheck(snapshot().at("plugin")==plugin,"Unassign recovery added another Undo");
  press(4807);const auto saved=saveDepartureFixture(fixture);app.documentOperation("document.open",departureOpenRequest(app,saved));
  restoreCheck(app.documentOperation("plugin.instruments.get",{{"plugin",plugin}}).at("assignments")==assignment&&app.documentOperation("instrument.get",{{"instrument",1}}).at("mapping")==mapping,"Save/reopen lost layered assignment, MIDI channel or sample map");
  // Rack creation retains its own receipt before submission. Reviewing a lost
  // callback must never create a second trigger or repeat the assignment.
  app.syncPluginRack();app.selectedPlugin=plugin;
  const auto beforeTriggers=app.view->session.document.at("instruments");
  app.completionFault=[]{throw std::runtime_error("Lost trigger creation callback");};
  restoreRejected([&]{app.pluginCommand(pluginNewInstrument);});
  restoreCheck(app.pluginTriggerCompletion.retained()&&app.view->session.document.at("instruments").size()==beforeTriggers.size()+1,"Lost trigger creation was not retained");
  const auto creationRevision=app.view->session.revision;
  app.pluginCommand(pluginNewInstrument);
  restoreCheck(!app.pluginTriggerCompletion.retained()&&app.pendingPluginTriggers.contains(plugin)&&app.view->session.revision==creationRevision,"Trigger Review repeated creation or assignment");
  const auto trigger=app.view->session.document.at("instruments").back();
  const auto triggerSlot=trigger.at("index").get<unsigned>();
  const auto triggerInfo=app.documentOperation("instrument.get",{{"instrument",triggerSlot}});
  restoreCheck(triggerInfo.at("name")=="Resonance Test Instrument","Generated trigger name was truncated instead of using the complete plugin name");
  bool failAssignment=true;
  app.sampleMutationCompletion=[&](const std::string &method){if(method=="instrument.plugin.set"&&std::exchange(failAssignment,false))throw std::runtime_error("Lost trigger assignment callback");};
  restoreRejected([&]{app.pluginCommand(pluginNewInstrument);});
  app.sampleMutationCompletion={};
  restoreCheck(app.pluginTriggerCompletion.retained(),"Lost assignment discarded the retained trigger receipt");
  const auto assignmentRevision=app.view->session.revision;app.pluginCommand(pluginNewInstrument);
  restoreCheck(!app.pluginTriggerCompletion.retained()&&!app.pendingPluginTriggers.contains(plugin)&&app.view->session.revision==assignmentRevision,"Assignment Review replayed the write");
  write("history.undo",{{"domain","all"}});
  restoreCheck(app.view->session.document.at("instruments").back()==trigger,"Assignment Undo removed or changed the created trigger");
  write("history.undo",{{"domain","all"}});
  restoreCheck(app.view->session.document.at("instruments")==beforeTriggers,"Creation Undo did not restore the original instrument catalogue");
  write("history.redo",{{"domain","all"}});write("history.redo",{{"domain","all"}});
  // Unknown completion uses read-only Review and explicit acknowledgement. A
  // newer revision invalidates acknowledgement rather than replaying input.
  restoreRejected([&]{app.pluginTriggerCompletion.submit([](const std::string &,const Json &)->ScreamSeq::Api::CompletedCall{throw std::runtime_error("Unknown trigger delivery");},
    "instrument.plugin.set",Json::object(),app.documentId,++app.pluginTriggerGeneration,app.pluginTriggerTarget);});
  const auto unknownRevision=app.view->session.revision;app.pluginCommand(pluginNewInstrument);
  restoreCheck(app.pluginTriggerReviewed&&app.pluginTriggerCompletion.retained()&&app.view->session.revision==unknownRevision,"Unknown trigger Review wrote or lost the outcome");
  write("document.patch",{{"title","Newer trigger review"}});restoreRejected([&]{app.pluginCommand(pluginNewInstrument);});
  restoreCheck(app.pluginTriggerCompletion.retained()&&!app.pluginTriggerReviewed,"Stale trigger acknowledgement discarded its outcome");
  app.pluginCommand(pluginNewInstrument);const auto reviewedRevision=app.view->session.revision;app.pluginCommand(pluginNewInstrument);
  restoreCheck(!app.pluginTriggerCompletion.retained()&&app.view->session.revision==reviewedRevision,"Acknowledging a trigger outcome repeated a write");
  const auto triggerSaved=saveDepartureFixture(fixture);app.documentOperation("document.open",departureOpenRequest(app,triggerSaved));
  restoreCheck(app.documentOperation("instrument.get",{{"instrument",triggerSlot}})==triggerInfo,"Trigger save/reopen lost the complete name or empty keymap");
  fixture.close();std::cout<<"PASS instrument-panel plugin assignment and retained rack trigger creation, lost callbacks, read-only Review, Undo/Redo and save/reopen\n";
}
