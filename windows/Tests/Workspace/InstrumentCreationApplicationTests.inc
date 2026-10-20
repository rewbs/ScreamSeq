void applicationInstrumentCreationRecovery(){
  using Json=RestoreJson;
  for(bool imported:{true,false}){
    RestoreFixture fixture;auto &app=*fixture.app;createRestoreInstrument(app);const auto project=saveDepartureFixture(fixture);
    const auto path=fixture.folder/L"source.wav";{std::ofstream file(path,std::ios::binary);restoreCheck(app.controller->view()!=nullptr,"Instrument fixture has no worker view");auto demo=Tracker::Document::demo();restoreCheck(demo->song().SaveWAVSample(1,file),"Write instrument import fixture");}
    app.openInstrumentEnvelope();auto *tool=app.instrumentEnvelopeWindow.get();const auto window=tool->window();ScreamSeq::Tests::ownGuiWindow(window);
    const auto initial=tool->snapshot();const auto count=app.view->instruments;unsigned choices=0;
    const auto choose=[&]{++choices;const auto draft=tool->documentDraft();restoreCheck(draft&&draft->pending&&draft->document==app.documentId,"Instrument chooser did not register pending context");
      restoreRejected([&]{app.documentOperation("document.open",departureOpenRequest(app,project));});
      restoreRejected([&]{tool->importInstrument([]{return std::vector<std::filesystem::path>{};});});
      return std::vector{path};};
    app.completionFault=[&]{rawDraftText(GetDlgItem(window,4414),L"-.");SetFocus(app.window);throw std::runtime_error("Drop instrument completion after newer raw draft");};
    if(imported)restoreRejected([&]{tool->importInstrument(choose);});
    else SendMessageW(window,WM_COMMAND,4403,reinterpret_cast<LPARAM>(GetDlgItem(window,4403)));
    const auto held=tool->snapshot();
    restoreCheck(app.view->instruments==count+1&&!held.at("creationCompletion").is_null()&&held.at("creationCompletion").at("returned")==true&&
      held.at("fieldDraft")==true&&restoreText(GetDlgItem(window,4414))==L"-.","Instrument creation result or newer raw draft was lost");
    tool->hide();app.openInstrumentEnvelope();restoreCheck(app.instrumentEnvelopeWindow.get()==tool&&tool->snapshot().at("creationCompletion")==held.at("creationCompletion"),"Reopen replaced retained instrument owner");
    restoreRejected([&]{app.documentOperation("document.open",departureOpenRequest(app,project));});
    SetFocus(app.window);const auto current=app.view->session.revision;const auto typing=app.typingSoundId;
    tool->importInstrument([&]()->std::vector<std::filesystem::path>{throw std::runtime_error("Review reopened instrument picker");});
    const auto reviewed=tool->snapshot();
    restoreCheck(reviewed.at("creationCompletion").is_null()&&reviewed.at("creationReport").at("outcome")=="verified"&&app.view->instruments==count+1&&app.view->session.revision==current,
      "Instrument Review appended again or changed history");
    restoreCheck(reviewed.at("instrument")==initial.at("instrument")&&reviewed.at("expectedRevision")==initial.at("expectedRevision")&&
      reviewed.at("generation")==held.at("generation")&&restoreText(GetDlgItem(window,4414))==L"-."&&GetFocus()==app.window&&app.typingSoundId==typing,
      "Instrument Review redirected selection, rebased or erased a newer draft/focus");
    app.edit("history.undo",{{"domain","all"}});restoreCheck(app.view->instruments==count,"Creation Review added extra Undo");
    app.edit("history.redo",{{"domain","all"}});restoreCheck(app.view->instruments==count+1,"Created instrument did not survive Redo");
    fixture.close();std::cout<<"PASS instrument creation receipt "<<imported<<"\n";
  }
  {
    RestoreFixture fixture;auto &app=*fixture.app;createRestoreInstrument(app);app.openInstrumentEnvelope();auto *tool=app.instrumentEnvelopeWindow.get();ScreamSeq::Tests::ownGuiWindow(tool->window());
    const auto count=app.view->instruments;
    tool->importInstrument([]{return std::vector<std::filesystem::path>{};});restoreCheck(!tool->retainedDraft(),"Cancelled instrument picker retained work");
    restoreRejected([&]{tool->importInstrument([]()->std::vector<std::filesystem::path>{throw std::runtime_error("Picker failed");});});
    restoreCheck(!tool->retainedDraft(),"Failed instrument picker retained work");
    restoreRejected([&]{tool->importInstrument([&]{app.edit("document.patch",{{"title","During instrument picker"}});return std::vector{fixture.folder/L"unused.wav"};});});
    restoreCheck(app.view->instruments==count&&!tool->retainedDraft(),"Stale picker created an instrument");fixture.close();
  }
  // A separate native writer drops the real worker return without forwarding
  // its optional receipt. Production Application wiring is exercised above.
  for(bool imported:{true,false}){
    RestoreFixture fixture;auto &app=*fixture.app;createRestoreInstrument(app);
    const auto path=fixture.folder/L"unknown.wav";{auto demo=Tracker::Document::demo();std::ofstream file(path,std::ios::binary);restoreCheck(demo->song().SaveWAVSample(1,file),"Write unknown instrument fixture");}
    bool failRead=false,malformedRead=false;unsigned writes=0;
    auto context=[&]{return ScreamSeq::InstrumentEnvelopeWindow::Context{app.documentId,app.view->session.revision,1,1,app.view->session.document.at("instruments"),app.view->session.document.at("samples")};};
    auto tool=std::make_unique<ScreamSeq::InstrumentEnvelopeWindow>(app.window,[&](const auto &method,const auto &params){
      if(method=="document.get"){if(failRead)throw std::runtime_error("Readback failed");if(malformedRead)return Json{{"instruments",Json::object()},{"samples",Json::array()}};return app.view->session.document;}
      return app.documentOperation(method,params);},context,[](unsigned,const auto &,const auto &,const auto &){},[](unsigned,const auto &){},
      [&](const auto &method,const auto &params)->ScreamSeq::Api::CompletedCall{++writes;(void)app.documentOperationWithOutcome(method,params);throw std::runtime_error("Lost instrument creation response");});
    ScreamSeq::Tests::ownGuiWindow(tool->window());tool->openAt();tool->dock(app.window);tool->dockBounds(0,0,440,350);const auto count=app.view->instruments;
    if(imported)restoreRejected([&]{tool->importInstrument([&]{return std::vector{path};});});
    else SendMessageW(tool->window(),WM_COMMAND,4403,reinterpret_cast<LPARAM>(GetDlgItem(tool->window(),4403)));
    restoreCheck(app.view->instruments==count+1&&tool->snapshot().at("creationCompletion").at("returned")==false,"Unknown creation fixture retained an unintended receipt");
    restoreCheck(tool->snapshot().at("shortDock")==true&&restoreText(GetDlgItem(tool->window(),4451))==L"Review"&&restoreText(GetDlgItem(tool->window(),4452))==L"Accept", "Short dock lost compact recovery actions");
    failRead=true;restoreRejected([&]{tool->importInstrument();});failRead=false;
    malformedRead=true;restoreRejected([&]{tool->importInstrument();});malformedRead=false;
    restoreCheck(tool->documentDraft()->uncertain&&!tool->snapshot().at("creationNeedsAcknowledgement").get<bool>(),"Failed instrument readback discarded uncertainty");
    const auto project=saveDepartureFixture(fixture);const auto before=songState(app);tool->importInstrument();
    restoreCheck(tool->snapshot().at("creationNeedsAcknowledgement")==true&&tool->snapshot().at("creationReport").at("outcome")=="unverified"&&songState(app)==before,
      "Instrument readback replayed creation or claimed verified success");
    restoreRejected([&]{app.documentOperation("document.open",departureOpenRequest(app,project));});
    restoreCheck(!app.canClose(),"Unknown instrument result allowed Close");
    app.edit("document.patch",{{"title","Newer revision after readback"}});
    SendMessageW(tool->window(),WM_COMMAND,4452,reinterpret_cast<LPARAM>(GetDlgItem(tool->window(),4452)));
    restoreCheck(tool->documentDraft()->uncertain&&!tool->snapshot().at("creationNeedsAcknowledgement").get<bool>(),"Stale acknowledgement lost instrument result");
    tool->importInstrument();const auto observed=songState(app);
    SendMessageW(tool->window(),WM_COMMAND,4452,reinterpret_cast<LPARAM>(GetDlgItem(tool->window(),4452)));
    restoreCheck(tool->snapshot().at("creationCompletion").is_null()&&writes==1&&songState(app)==observed&&tool->snapshot().at("creationReport").at("outcome")=="unverified",
      "Acknowledgement replayed an import/create or changed observed state");
    tool.reset();fixture.close();
  }
}
