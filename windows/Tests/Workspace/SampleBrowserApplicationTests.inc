void applicationSampleBrowserCompletionRetention(){
  RestoreFixture fixture;auto &app=*fixture.app;
  const auto samples=fixture.folder/L"samples";restoreCheck(std::filesystem::create_directory(samples),"Create sample browser fixture folder");
  auto source=Tracker::Document::demo();{std::ofstream file(samples/L"receipt.wav",std::ios::binary);restoreCheck(source->song().SaveWAVSample(1,file),"Write browser import fixture");}
  app.prepareSampleLibrary();
  app.independentOperation("sample.library.roots.set",{{"roots",RestoreJson::array({utf8Path(samples)})},{"expectedLibraryRevision",app.sampleLibrary->status().at("libraryRevision")}});
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  while(app.sampleLibrary->status().at("indexing").get<bool>()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(5));
  restoreCheck(!app.sampleLibrary->status().at("indexing").get<bool>(),"Bounded sample fixture indexing did not finish");
  app.openSampleBrowser();const auto window=app.sampleBrowser->window();ScreamSeq::Tests::ownGuiWindow(window);SendMessageW(window,WM_TIMER,1,0);
  const auto list=GetDlgItem(window,5404);restoreCheck(SendMessageW(list,LB_GETCOUNT,0,0)==1,"Expected one sample browser fixture");
  SendMessageW(list,LB_SETSEL,TRUE,0);SendMessageW(list,LB_SETCARETINDEX,0,FALSE);SendMessageW(window,WM_COMMAND,MAKEWPARAM(5404,LBN_SELCHANGE),reinterpret_cast<LPARAM>(list));
  app.workspaceState.show("samples",app.position(),1,true);app.layoutControls();app.sampleRange();
  rawDraftText(app.controls.at(sampleStartField),L"-");const auto rawID=app.sampleDraftId;const auto rawRevision=app.sampleDraftRevision;
  const auto before=app.view->samples.size();
  app.completionFault=[] {throw std::runtime_error("Drop browser import completion after real asset commit");};
  const auto import=[&]{SendMessageW(window,WM_COMMAND,5414,reinterpret_cast<LPARAM>(GetDlgItem(window,5414)));};
  import();
  restoreCheck(app.view->samples.size()==before+1&&!app.sampleBrowser->snapshot().at("importCompletion").is_null(),"Real browser import lost its completed receipt");
  const auto captured=app.documentDrafts.capture(app.documentId,app.view->session.revision);
  restoreCheck(app.documentDrafts.authorize(captured,true).status==Tracker::DocumentDeparture::Status::Uncertain,"Unresolved browser import allowed native discard");
  import();
  restoreCheck(app.sampleBrowser->snapshot().at("importCompletion").is_null()&&app.view->samples.size()==before+1,"Review repeated or failed to release real browser import");
  app.documentOperation("history.undo",{{"expectedRevision",app.view->session.revision},{"domain","all"}});
  restoreCheck(app.view->samples.size()==before,"Browser Review created an extra Undo entry");
  SetFocus(list);restoreCheck(GetFocus()==list,"Fixture did not focus the sample browser list");import();
  restoreCheck(app.view->samples.size()==before+1,"Successful import did not append exactly one sample");
  restoreCheck(app.sampleDraft,"Successful import discarded the sample range draft");
  restoreCheck(app.sampleDraftId==rawID,"Successful import changed the sample draft identity");
  restoreCheck(app.sampleDraftRevision==rawRevision,"Successful import rebased the sample draft revision");
  restoreCheck(restoreText(app.controls.at(sampleStartField))==L"-","Successful import replaced the raw sample draft text");
  restoreCheck(app.selectedSampleId()==rawID,"Successful import redirected the sample editor target");
  restoreCheck(GetFocus()==list,"Successful import stole browser focus");
  app.documentOperation("history.undo",{{"expectedRevision",app.view->session.revision},{"domain","all"}});
  restoreCheck(app.view->samples.size()==before,"Successful browser import did not have exactly one Undo");
  // A newer focus choice during the worker completion must win over the
  // browser's temporary focus parking, including another browser control.
  for(const auto target:{app.controls.at(sampleStartField),GetDlgItem(window,5412)}) {
    SetFocus(list);app.completionFault=[&]{SetFocus(target);};import();
    restoreCheck(GetFocus()==target,"Import completion overrode a newer focus choice");
    restoreCheck(app.view->samples.size()==before+1&&app.sampleDraft&&app.sampleDraftId==rawID&&
      app.sampleDraftRevision==rawRevision&&restoreText(app.controls.at(sampleStartField))==L"-",
      "Import with changed focus damaged the sample draft");
    app.documentOperation("history.undo",{{"expectedRevision",app.view->session.revision},{"domain","all"}});
    restoreCheck(app.view->samples.size()==before,"Import with changed focus created extra history");
  }
  fixture.close();
}
