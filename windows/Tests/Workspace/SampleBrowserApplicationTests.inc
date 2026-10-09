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

void applicationDirectSampleImportRetention(){
  RestoreFixture fixture;auto &app=*fixture.app;const auto project=saveDepartureFixture(fixture);
  const auto file=fixture.folder/L"direct.wav";auto demo=Tracker::Document::demo();
  {std::ofstream stream(file,std::ios::binary);restoreCheck(demo->song().SaveWAVSample(1,stream),"Write direct import fixture");}
  const auto originalDocument=app.documentId;const auto before=app.view->samples.size();unsigned choices=0;
  app.sampleImportChooser=[&](bool){
    ++choices;restoreCheck(app.sampleBrowser&&!app.sampleBrowser->visible(),"Direct chooser unnecessarily opened the browser");
    restoreCheck(app.documentDrafts.authorize(census(app),true).status==Tracker::DocumentDeparture::Status::Pending,"File chooser escaped departure census");
    restoreRejected([&]{app.documentOperation("document.open",departureOpenRequest(app,project));});
    restoreCheck(app.documentId==originalDocument,"Song changed beneath the native file chooser");
    restoreRejected([&]{app.importSample(false);});
    return std::vector<std::filesystem::path>{};
  };
  app.command(sampleImportRawCommand);
  restoreCheck(choices==1&&app.view->samples.size()==before&&census(app).drafts.empty(),"Cancelled picker imported, nested or retained phantom work");
  app.sampleImportChooser=[](bool)->std::vector<std::filesystem::path>{throw std::runtime_error("Owned file chooser failure");};
  restoreRejected([&]{app.importSample();});
  restoreCheck(census(app).drafts.empty()&&!app.sampleBrowser->snapshot().at("pending").get<bool>(),"Failed picker left pending work");
  app.sampleImportChooser=[&](bool){app.edit("sample.patch",{{"sample",1},{"values",{{"name","Changed during chooser"}}}});return std::vector{file};};
  const auto stale=restoreRejected([&]{app.importSample(false);});
  restoreCheck(stale.find("Song changed")!=std::string::npos&&app.view->samples.size()==before&&census(app).drafts.empty(),"Stale picker imported or retained a false uncertain result");
  app.documentOperation("history.undo",{{"expectedRevision",app.view->session.revision},{"domain","all"}});

  app.workspaceState.show("samples",app.position(),1,true);app.layoutControls();app.sampleRange();
  rawDraftText(app.controls.at(sampleStartField),L"-");const auto rawID=app.sampleDraftId,rawRevision=app.sampleDraftRevision;
  for(const auto [command,mapped]:std::array<std::pair<int,bool>,3>{{{sampleImportRawCommand,false},{sampleImportInstrumentsCommand,true},{sampleImportCommand,app.view->instruments!=0}}}) {
    const auto instruments=app.view->instruments;const auto browserMapping=app.sampleBrowser->snapshot().at("createInstruments");
    app.sampleImportChooser=[&](bool actual){++choices;restoreCheck(actual==mapped,"Direct command selected the wrong import mapping");return std::vector{file};};
    SetFocus(app.controls.at(sampleStartField));app.command(command);
    // Entering instrument mode preserves existing sample-only notes by
    // mapping the original sample slots before appending the imported sound.
    const auto expectedInstruments=mapped?(instruments?instruments:before)+1:instruments;
    restoreCheck(app.view->samples.size()==before+1&&app.view->instruments==expectedInstruments,"Direct command did not import the requested sample/instrument");
    if(mapped){
      const auto first=instruments?expectedInstruments:1;
      for(size_t slot=first;slot<=expectedInstruments;++slot){
        const auto mapping=app.documentOperation("instrument.get",{{"instrument",slot}}).at("mapping").get<std::vector<unsigned>>();
        const auto sample=slot==expectedInstruments?before+1:slot;
        restoreCheck(!mapping.empty()&&std::all_of(mapping.begin(),mapping.end(),[&](unsigned value){return value==sample;}),"Entering instrument mode changed an original sample mapping or misrouted the import");
      }
    }
    restoreCheck(app.sampleBrowser->snapshot().at("createInstruments")==browserMapping,"Direct import changed the browser's independent mapping choice");
    restoreCheck(app.sampleDraft&&app.sampleDraftId==rawID&&app.sampleDraftRevision==rawRevision&&restoreText(app.controls.at(sampleStartField))==L"-"&&GetFocus()==app.controls.at(sampleStartField),"Direct command discarded/rebased raw sample text or stole focus");
    app.documentOperation("history.undo",{{"expectedRevision",app.view->session.revision},{"domain","all"}});
    restoreCheck(app.view->samples.size()==before&&app.view->instruments==instruments,"Direct import did not have one Undo");
  }
  app.sampleImportChooser=[&](bool){++choices;return std::vector{file};};
  app.completionFault=[] {throw std::runtime_error("Drop direct import completion after asset commit");};
  app.command(sampleImportRawCommand);const auto retained=app.sampleBrowser->snapshot().at("importCompletion");
  restoreCheck(!retained.is_null()&&app.view->samples.size()==before+1,"Direct command lost committed import result");
  const auto chosen=choices;app.command(sampleImportInstrumentsCommand);
  restoreCheck(choices==chosen&&app.sampleBrowser->visible()&&app.sampleBrowser->snapshot().at("importCompletion")==retained,"Repeated direct command chose/imported again or lost Review");
  restoreRejected([&]{app.documentOperation("document.open",departureOpenRequest(app,project));});
  const auto window=app.sampleBrowser->window();ScreamSeq::Tests::ownGuiWindow(window);
  SendMessageW(window,WM_COMMAND,5414,reinterpret_cast<LPARAM>(GetDlgItem(window,5414)));
  restoreCheck(app.sampleBrowser->snapshot().at("importCompletion").is_null()&&app.view->samples.size()==before+1,"Direct command Review repeated import");
  app.documentOperation("history.undo",{{"expectedRevision",app.view->session.revision},{"domain","all"}});
  restoreCheck(app.view->samples.size()==before&&app.sampleDraft&&app.sampleDraftId==rawID&&restoreText(app.controls.at(sampleStartField))==L"-","Reviewed direct import lost draft or one Undo");
  fixture.close();
}
