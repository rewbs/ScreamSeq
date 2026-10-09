void applicationSampleLibraryRecovery(){
  RestoreFixture fixture;auto &app=*fixture.app;const auto song=saveDepartureFixture(fixture);
  const auto folder=fixture.folder/L"pack";restoreCheck(std::filesystem::create_directory(folder),"Create owned sample library folder");
  app.prepareSampleLibrary();
  const auto ready=[&]{const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(app.sampleLibrary->status().at("indexing").get<bool>()&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(5));restoreCheck(!app.sampleLibrary->status().at("indexing").get<bool>(),"Sample library did not settle");};
  ready();app.independentOperation("sample.library.roots.set",{{"roots",RestoreJson::array({utf8Path(folder)})},{"expectedLibraryRevision",app.sampleLibrary->status().at("libraryRevision")}});ready();
  app.openSampleBrowser();auto *browser=app.sampleBrowser.get();const auto window=browser->window();ScreamSeq::Tests::ownGuiWindow(window);SendMessageW(window,WM_TIMER,1,0);
  const auto root=GetDlgItem(window,5402);SendMessageW(root,CB_SETCURSEL,1,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(5402,CBN_SELCHANGE),reinterpret_cast<LPARAM>(root));
  const auto click=[&](int id){SendMessageW(window,WM_COMMAND,id,reinterpret_cast<LPARAM>(GetDlgItem(window,id)));};
  const auto oldDocument=app.documentId;
  app.sampleLibraryCompletionFault=[] {throw std::runtime_error("Drop root-change native completion");};click(5406);
  restoreCheck(app.sampleLibrary->status().at("roots").empty()&&!browser->snapshot().at("libraryCompletion").is_null()&&browser->protectsClose(),"Committed root removal lost its native receipt");
  restoreCheck(!app.canClose(),"Close discarded unresolved sample-library work");
  app.documentOperation("document.open",departureOpenRequest(app,song));
  restoreCheck(app.documentId!=oldDocument&&app.sampleBrowser.get()==browser&&IsWindow(window),"Song replacement destroyed app-wide sample-library work");
  click(5407);restoreCheck(!browser->protectsClose()&&app.sampleLibraryCompletions==1,"Root Review repeated the operation");
  ready();SendMessageW(window,WM_TIMER,1,0);const auto before=songState(app);
  app.sampleLibraryCompletionFault=[] {throw std::runtime_error("Drop rescan native completion");};click(5407);
  restoreCheck(browser->protectsClose()&&!browser->snapshot().at("libraryCompletion").is_null()&&!app.canClose(),"Rescan completion was lost or allowed Close");
  click(5407);restoreCheck(!browser->protectsClose()&&app.sampleLibraryCompletions==2&&songState(app)==before,"Rescan Review rescanned or acquired musical history");
  restoreCheck(std::filesystem::is_directory(folder),"Root removal removed sample source files");fixture.close();
}
