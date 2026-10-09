// Real native owners and worker calls, on the existing private fixture desktop.
void applicationSampleMutationRecovery(){
  using Json=RestoreJson;
  const auto press=[](HWND window,int id){SendMessageW(window,WM_COMMAND,id,reinterpret_cast<LPARAM>(GetDlgItem(window,id)));};
  {
    RestoreFixture fixture;auto &app=*fixture.app;
    const auto path=fixture.folder/L"replacement.wav";{auto demo=Tracker::Document::demo();std::ofstream file(path,std::ios::binary);restoreCheck(demo->song().SaveWAVSample(1,file),"Write replacement fixture");}
    app.edit("sample.process",{{"sample",1},{"operation","invert"}});
    const auto original=app.documentOperation("sample.pcm.get",{{"sample",1}}),other=app.documentOperation("sample.get",{{"sample",2}});
    const auto project=saveDepartureFixture(fixture);app.openSampleDetail();auto *tool=app.sampleDetailWindow.get();const auto window=tool->window();ScreamSeq::Tests::ownGuiWindow(window);
    const auto initial=tool->snapshot();unsigned choices=0;
    tool->replaceAudioFromFile([]{return std::vector<std::filesystem::path>{};});restoreCheck(!tool->documentDraft()->retained(),"Cancelled sample picker retained work");
    restoreRejected([&]{tool->replaceAudioFromFile([]()->std::vector<std::filesystem::path>{throw std::runtime_error("Picker failure");});});
    restoreCheck(!tool->documentDraft()->retained(),"Failed sample picker retained work");
    const auto choose=[&]{++choices;const auto draft=tool->documentDraft();restoreCheck(draft&&draft->pending,"Sample chooser did not register pending work");
      restoreRejected([&]{app.documentOperation("document.open",departureOpenRequest(app,project));});
      restoreRejected([&]{tool->replaceAudioFromFile([]{return std::vector<std::filesystem::path>{};});});return std::vector{path};};
    app.completionFault=[&]{rawDraftText(GetDlgItem(window,4856),L"Newer sample name");SetFocus(app.window);throw std::runtime_error("Lost replacement completion");};
    restoreRejected([&]{tool->replaceAudioFromFile(choose);});const auto held=tool->snapshot();
    restoreCheck(held.at("mutationCompletion").at("returned")==true&&held.at("id")==initial.at("id")&&held.at("fieldDraft")==true,
      "Replacement lost exact receipt, stable target or newer draft");
    const auto replaced=app.documentOperation("sample.pcm.get",{{"sample",1}});restoreCheck(replaced!=original,"Replacement fixture did not change PCM");
    restoreCheck(app.documentOperation("sample.get",{{"sample",2}})==other,"Replacement changed another sample");
    tool->hide();app.openSampleDetail();restoreCheck(app.sampleDetailWindow.get()==tool,"Reopen replaced sample owner");
    restoreRejected([&]{app.documentOperation("document.open",departureOpenRequest(app,project));});
    const auto before=songState(app);SetFocus(app.window);tool->replaceAudioFromFile(choose);
    restoreCheck(choices==1&&songState(app)==before&&tool->snapshot().at("mutationCompletion").is_null()&&tool->snapshot().at("mutationReport").at("outcome")=="verified",
      "Sample Review repeated chooser/write or failed to resolve known result");
    restoreCheck(restoreText(GetDlgItem(window,4856))==L"Newer sample name"&&tool->snapshot().at("expectedRevision")==initial.at("expectedRevision")&&GetFocus()==app.window,
      "Sample Review erased/rebased newer text or stole focus");
    app.edit("history.undo",{{"domain","all"}});restoreCheck(app.documentOperation("sample.pcm.get",{{"sample",1}})==original,"Replacement did not undo in one step");
    app.edit("history.redo",{{"domain","all"}});restoreCheck(app.documentOperation("sample.pcm.get",{{"sample",1}})==replaced,"Replacement did not redo");
    app.sampleDetailWindow.reset();fixture.close();std::cout<<"PASS sample replacement receipt, chooser ownership, newer draft and one Undo\n";
  }
  // Failure after the write, in either refresh read, must leave drawing and
  // raw loop text intact even though the PCM commit has already happened.
  for(const std::string read:{"sample.get","sample.waveform.get"}){
    RestoreFixture fixture;auto &app=*fixture.app;app.openSampleDetail();auto *tool=app.sampleDetailWindow.get();const auto window=tool->window();ScreamSeq::Tests::ownGuiWindow(window);
    const auto original=app.documentOperation("sample.pcm.get",{{"sample",1}});
    rawDraftText(GetDlgItem(window,4822),L"0");rawDraftText(GetDlgItem(window,4823),L"0.5");press(window,4824);
    const auto staged=tool->snapshot();restoreCheck(staged.at("points").size()==1,"Drawing fixture failed to stage");
    bool injected=false;app.sampleMutationCompletion=[&](const auto &method){if(method==read&&!injected){injected=true;rawDraftText(GetDlgItem(window,5007),L"-.");SetFocus(app.window);throw std::runtime_error("Refresh failed after newer loop text");}};
    press(window,4825);app.sampleMutationCompletion={};const auto held=tool->snapshot();
    restoreCheck(injected&&held.at("mutationCompletion").at("returned")==true&&held.at("points")==staged.at("points")&&restoreText(GetDlgItem(window,5007))==L"-.",
      "Refresh failure erased drawing, raw loop draft or receipt");
    const auto changed=app.documentOperation("sample.pcm.get",{{"sample",1}});restoreCheck(changed!=original,"Drawing fixture did not change PCM");
    const auto before=songState(app);press(window,4803);
    restoreCheck(songState(app)==before&&tool->snapshot().at("mutationCompletion").is_null()&&tool->snapshot().at("points")==staged.at("points")&&restoreText(GetDlgItem(window,5007))==L"-.",
      "Review replayed drawing or discarded newer draft");
    app.edit("history.undo",{{"domain","all"}});restoreCheck(app.documentOperation("sample.pcm.get",{{"sample",1}})==original,"Drawing refresh failure added history");
    app.sampleDetailWindow.reset();fixture.close();std::cout<<"PASS sample staged refresh "<<read<<"\n";
  }
  // Drop the optional ticket only in this fixture to exercise genuine unknown
  // outcomes. Production Application receipt wiring is covered above.
  for(const bool instrument:{false,true}){
    RestoreFixture fixture;auto &app=*fixture.app;if(instrument)createRestoreInstrument(app);
    bool failRead=false,malformed=false;unsigned writes=0;std::vector<std::string> reads;
    auto tool=std::make_unique<ScreamSeq::SampleDetailWindow>(app.window,[&](const auto &method,const auto &params){
      reads.push_back(method);
      if((method=="document.get"&&instrument)||(method=="sample.clipboard.get"&&!instrument)){
        if(failRead)throw std::runtime_error("Unknown sample readback failure");
        if(malformed)return Json::object();
      }
      if(method=="document.get")return app.view->session.document;return app.documentOperation(method,params);
    },[&](bool full){return ScreamSeq::SampleDetailWindow::Context{app.documentId,app.view->session.revision,1,full?app.view->session.document.at("samples"):Json::array()};},
    [](unsigned,const auto &,const auto &,const auto &){},[]{},[&](const auto &method,const auto &params)->ScreamSeq::Api::CompletedCall{
      ++writes;(void)app.documentOperationWithOutcome(method,params);throw std::runtime_error("Lost sample result without receipt");});
    const auto window=tool->window();ScreamSeq::Tests::ownGuiWindow(window);tool->openAt();const auto revision=app.view->session.revision;const auto instruments=app.view->instruments;
    if(!instrument)press(window,5004);press(window,instrument?4863:4849);
    restoreCheck(tool->snapshot().at("mutationCompletion").at("returned")==false&&writes==1,"Unknown sample fixture unexpectedly retained a ticket");
    if(instrument)restoreCheck(app.view->instruments==instruments+1,"Unknown creation fixture did not create instrument");
    else restoreCheck(app.view->session.revision==revision,"Clipboard copy unexpectedly changed song revision");
    failRead=true;press(window,4803);failRead=false;malformed=true;press(window,4803);malformed=false;
    restoreCheck(tool->documentDraft()->uncertain&&!tool->snapshot().at("mutationNeedsAcknowledgement").get<bool>(),"Failed/malformed readback discarded sample uncertainty");
    const auto project=saveDepartureFixture(fixture);const auto before=songState(app);reads.clear();press(window,4803);
    const auto report=tool->snapshot().at("mutationReport");
    restoreCheck(report.at("outcome")=="unverified"&&tool->snapshot().at("mutationNeedsAcknowledgement")==true&&songState(app)==before&&writes==1,
      "Unknown sample readback replayed mutation or claimed verified success");
    restoreCheck(report.at("observed").contains(instrument?"document":"clipboard"),"Unknown sample review omitted affected domain");
    restoreRejected([&]{app.documentOperation("document.open",departureOpenRequest(app,project));});restoreCheck(!app.canClose(),"Unknown sample outcome allowed Close");
    app.edit("document.patch",{{"title","Changed after sample readback"}});press(window,4802);
    restoreCheck(tool->documentDraft()->uncertain&&!tool->snapshot().at("mutationNeedsAcknowledgement").get<bool>(),"Stale acknowledgement discarded result");
    press(window,4803);const auto observed=songState(app);press(window,4802);
    restoreCheck(tool->snapshot().at("mutationCompletion").is_null()&&songState(app)==observed&&writes==1&&tool->snapshot().at("mutationReport").at("outcome")=="unverified",
      "Sample acknowledgement repeated write or changed observed state");
    tool.reset();fixture.close();std::cout<<"PASS unknown sample result domain "<<instrument<<"\n";
  }
}
