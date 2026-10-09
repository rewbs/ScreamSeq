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
  fixture.close();
}
