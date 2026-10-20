void applicationLibraryCompletionRetention(){
  RestoreFixture fixture;auto &app=*fixture.app;const auto path=saveDepartureFixture(fixture);
  app.openPluginLibrary();auto *library=app.pluginLibraryWindow.get();const auto window=library->window();ScreamSeq::Tests::ownGuiWindow(window);
  const auto click=[&](int id){SendMessageW(window,WM_COMMAND,id,reinterpret_cast<LPARAM>(GetDlgItem(window,id)));};
  const auto choose=[&]{
    const auto entries=library->snapshot().at("plugins");int index=-1;
    for(size_t i=0;i<entries.size();++i)if(entries[i].at("format")=="Built-in"){index=int(i);break;}
    restoreCheck(index>=0,"Missing built-in library fixture");
    SendMessageW(GetDlgItem(window,3207),LB_SETCURSEL,index,0);
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(3207,LBN_SELCHANGE),reinterpret_cast<LPARAM>(GetDlgItem(window,3207)));
  };
  click(3213);choose();const auto originalDocument=app.documentId;
  app.completionFault=[&]{rawDraftText(GetDlgItem(window,3210),L"Later category draft");throw std::runtime_error("Drop preference completion after worker success");};
  click(3208);
  restoreCheck(!library->snapshot().at("completion").is_null()&&!app.canClose()&&library->visible(),"Lost library result or raw draft allowed Close");
  const auto savedPreferences=app.documentOperation("plugin.library.get",{{"includeHidden",true},{"rescan",false}});
  app.documentOperation("document.open",departureOpenRequest(app,path));
  restoreCheck(app.documentId!=originalDocument&&app.pluginLibraryWindow.get()==library&&library->window()==window&&IsWindow(window),"Song replacement destroyed app-wide library work");
  click(3213);
  restoreCheck(library->snapshot().at("completion").is_null()&&library->snapshot().at("report").at("outcome")=="returned"&&
    restoreText(GetDlgItem(window,3210))==L"Later category draft", "Library Review across song replacement lost its receipt or newer raw field");
  restoreCheck(app.documentOperation("plugin.library.get",{{"includeHidden",true},{"rescan",false}})==savedPreferences,"Review repeated preference write or changed its revision");
  click(3213);choose();
  const auto beforePlugins=app.view->session.document.at("nativePlugins");
  app.completionFault=[] {throw std::runtime_error("Drop rack insertion completion after worker success");};click(3212);
  restoreCheck(!library->snapshot().at("completion").is_null()&&app.view->session.document.at("nativePlugins").size()==beforePlugins.size()+1,"Rack add lost exact completion");
  restoreRejected([&]{app.documentOperation("document.open",departureOpenRequest(app,path));});
  click(3213);restoreCheck(library->snapshot().at("completion").is_null(),"Review did not release attributed rack insertion");
  app.documentOperation("history.undo",{{"expectedRevision",app.view->session.revision},{"domain","all"}});
  restoreCheck(app.view->session.document.at("nativePlugins")==beforePlugins,"Rack Review added a duplicate plugin or Undo entry");
  app.edit("mixer.enable",RestoreJson::object());app.command(mixerDetailsCommand);app.mixerControlCommand(mixerAddReturn);
  const auto busID=app.mixerTarget;const auto beforeBusInsert=app.documentOperation("mixer.get",RestoreJson::object());
  const auto rackBeforeBusInsert=app.view->session.document.at("nativePlugins");
  app.command(mixerAddEffect);
  restoreCheck(library->snapshot().at("destination").at("bus")==busID&&library->snapshot().at("destination").at("document")==app.documentId&&
    (GetFocus()==window||IsChild(window,GetFocus())),"Mixer effect browser lost captured destination or native focus");
  click(3213);choose();
  const auto effectEntries=library->snapshot().at("plugins");
  for(const auto &entry:effectEntries)restoreCheck(!entry.at("isInstrument").get<bool>(),"Bus effect destination offered an instrument");
  app.sampleMutationCompletion=[](const std::string &method){if(method=="plugin.add")throw std::runtime_error("Drop bus insertion completion after atomic success");};
  click(3212);app.sampleMutationCompletion={};
  const auto afterBusInsert=app.documentOperation("mixer.get",RestoreJson::object());
  const auto rackAfterBusInsert=app.view->session.document.at("nativePlugins");
  restoreCheck(rackAfterBusInsert.size()==rackBeforeBusInsert.size()+1&&!library->snapshot().at("completion").is_null(),"Bus insertion was not committed with a retained receipt");
  const auto insertedID=rackAfterBusInsert.back().at("instanceID");
  auto expectedMixer=beforeBusInsert;
  bool foundDestination=false;for(auto &bus:expectedMixer.at("buses"))if(bus.at("id")==busID){bus.at("inserts").push_back(insertedID);foundDestination=true;}
  // Mixer reads also contain revision/runtime information; compare the stored buses.
  restoreCheck(foundDestination&&afterBusInsert.at("buses")==expectedMixer.at("buses"),"Adding an effect changed another bus or failed to append to the captured insert chain");
  restoreRejected([&]{library->setDestination(app.documentId,"n99999","Other bus");});
  click(3213);restoreCheck(library->snapshot().at("completion").is_null(),"Exact bus insertion Review did not finish");
  app.edit("history.undo",{{"domain","all"}});
  restoreCheck(app.view->session.document.at("nativePlugins")==rackBeforeBusInsert&&app.documentOperation("mixer.get",RestoreJson::object()).at("buses")==beforeBusInsert.at("buses"),
    "A single Undo did not remove the new effect and its bus insertion atomically");
  app.edit("history.redo",{{"domain","all"}});
  restoreCheck(app.view->session.document.at("nativePlugins")==rackAfterBusInsert&&app.documentOperation("mixer.get",RestoreJson::object()).at("buses")==afterBusInsert.at("buses"),
    "Bus insertion Redo changed stable identities or another route");
  const auto busProject=saveDepartureFixture(fixture);app.documentOperation("document.open",departureOpenRequest(app,busProject));
  restoreCheck(app.view->session.document.at("nativePlugins")==rackAfterBusInsert&&app.documentOperation("mixer.get",RestoreJson::object()).at("buses")==afterBusInsert.at("buses"),
    "Bus effect and its stable insertion destination did not survive save/reopen");
  const auto reopenedRevision=app.view->session.revision;click(3212);
  restoreCheck(app.view->session.revision==reopenedRevision&&library->snapshot().at("completion").is_null(),"Library silently followed a destination into a replacement document");
  library->setDestination(app.documentId,busID,"Return");click(3213);choose();
  app.edit("mixer.bus.remove",{{"bus",busID}});const auto deletedRevision=app.view->session.revision;
  click(3212);restoreCheck(app.view->session.revision==deletedRevision&&library->snapshot().at("completion").is_null(),"Missing bus inserted an effect elsewhere or created an uncertain write");
  click(3216);restoreCheck(library->snapshot().at("destination").at("bus")=="","Explicit Rack destination did not clear the unavailable bus");
  fixture.close();
}
