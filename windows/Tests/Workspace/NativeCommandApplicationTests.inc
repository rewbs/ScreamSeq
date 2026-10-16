void applicationNativeCommandRecovery(){
  withRestoreFixture([](RestoreApplication &app){
    app.documentOperation("recording.start",{{"expectedRevision",app.view->session.revision},{"channels",RestoreJson::array({0})},{"instrument",1}});unsigned stops=0;
    app.sampleMutationCompletion=[&](const std::string &method){if(method=="recording.stop"){++stops;throw std::runtime_error("Owned transport Stop completion failure");}};
    app.stop();restoreCheck(stops==1&&app.nativeCommandCompletion.retained()&&app.nativeCommandCompletion.returned()->method=="recording.stop","Transport Stop lost its MIDI result");
    app.stop();app.serviceMidi();restoreCheck(stops==1&&app.nativeCommandCompletion.retained(),"Repeated transport Stop replayed an unresolved MIDI operation");
    app.reviewNativeCommand();app.serviceMidi();restoreCheck(stops==1&&!app.nativeCommandCompletion.retained()&&app.hasRecordingTake(),"Transport Stop Review discarded the take or repeated Stop");app.sampleMutationCompletion={};
  });
  {
    RestoreFixture fixture;auto &app=*fixture.app;const auto path=saveDepartureFixture(fixture);app.command(samplesCommand);app.sampleCommand(sampleAllCommand);
    const auto sample=app.selectedSample();const auto identity=app.selectedSampleId();const auto before=app.documentOperation("sample.pcm.get",{{"sample",sample}});unsigned writes=0;
    app.sampleMutationCompletion=[&](const std::string &method){if(method=="sample.process"){++writes;rawDraftText(app.controls.at(sampleStartField),L"-");throw std::runtime_error("Owned main-sample native completion failure");}};
    restoreRejected([&]{app.sampleCommand(sampleReverseCommand);});
    restoreCheck(writes==1&&app.nativeCommandCompletion.retained()&&app.nativeCommandCompletion.returned()&&app.sampleDraft,"Main sample completion lost the receipt or later raw range");
    const auto raw=restoreText(app.controls.at(sampleStartField));const auto after=app.documentOperation("sample.pcm.get",{{"sample",sample}});restoreCheck(after!=before,"Main reverse did not change the fixture PCM");
    restoreRejected([&]{app.documentOperation("document.open",departureOpenRequest(app,path));});
    app.reviewNativeCommand();restoreCheck(!app.nativeCommandCompletion.retained()&&writes==1&&app.selectedSampleId()==identity&&restoreText(app.controls.at(sampleStartField))==raw&&app.sampleDraft,"Main sample Review replayed or replaced newer range work");
    app.sampleMutationCompletion={};app.edit("history.undo",{{"domain","all"}});restoreCheck(app.documentOperation("sample.pcm.get",{{"sample",sample}})==before,"Main sample recovery added history or lost exact PCM Undo");
  }
  {
    RestoreFixture fixture;auto &app=*fixture.app;const auto path=saveDepartureFixture(fixture);app.configureRecovery(fixture.folder/L"recovery");
    app.edit("document.patch",{{"title","Saved before failed cleanup"}});app.recoveryOperation("recovery.save",{{"expectedRevision",app.view->session.revision}});
    const auto copies=app.recoveryCopies();restoreCheck(!copies.empty(),"Save cleanup fixture lacks a retained recovery copy");app.failSaveBeforeCleanup=true;
    restoreRejected([&]{app.saveFile();});const auto bytes=ScreamSeq::Project::readProjectBytes(path);const auto receipt=app.nativeCommandCompletion.returned();
    restoreCheck(receipt&&receipt->method=="document.save"&&!app.view->dirty&&app.saveCleanupFailures==1,"Saved file lost its exact result after failed cleanup scheduling");
    app.reviewNativeCommand();restoreCheck(!app.nativeCommandCompletion.retained()&&app.saveCleanupFailures==1&&ScreamSeq::Project::readProjectBytes(path)==bytes&&app.recoveryCopies()==copies,"Save Review overwrote the file or repeated recovery cleanup");
    app.failSaveBeforeCleanup=false;
  }
  for(const std::string failing:{"recording.stop","recording.commit","recording.discard"}){
    withRestoreFixture([&](RestoreApplication &app){
      const auto take=app.documentOperation("recording.start",{{"expectedRevision",app.view->session.revision},{"channels",RestoreJson::array({0})},{"instrument",1}}).at("take").get<std::string>();unsigned calls=0;
      app.sampleMutationCompletion=[&](const std::string &method){if(method==failing){++calls;throw std::runtime_error("Owned MIDI native completion failure");}};
      if(failing=="recording.discard")restoreRejected([&]{app.discardRecordingTake(take);});else restoreRejected([&]{app.finishRecordingTake(take);});
      restoreCheck(calls==1&&app.nativeCommandCompletion.retained()&&app.nativeCommandCompletion.returned()->method==failing,"MIDI failure lost its own exact operation");
      auto state=app.documentOperation("recording.get",RestoreJson::object());app.reviewNativeCommand();
      auto after=app.documentOperation("recording.get",RestoreJson::object());
      // hostTime is sampled on every read, including a stopped/absent take.
      // All take identity, events, loss counters and state still compare exactly.
      state.erase("hostTime");after.erase("hostTime");
      restoreCheck(!app.nativeCommandCompletion.retained()&&calls==1&&after==state,"MIDI Review repeated Stop/commit/discard or changed the retained take");app.sampleMutationCompletion={};
    });
  }
  // Unknown outcomes use actual side effects but deliberately drop every route
  // by which the fixture could return a receipt. This is not the ordinary API.
  for(const std::string domain:{"sample","midi","save"}){
    RestoreFixture fixture;auto &app=*fixture.app;const auto path=saveDepartureFixture(fixture);const auto doc=app.documentId;
    const std::string method=domain=="sample"?"sample.clipboard.copy":domain=="midi"?"recording.start":"document.save";
    RestoreJson params={{"expectedRevision",app.view->session.revision}},target=RestoreJson::object();
    if(domain=="sample"){params["sample"]=1;params["start"]=0;params["end"]=16;const auto &samples=app.view->session.document.at("samples");const auto found=std::find_if(samples.begin(),samples.end(),[](const auto &sample){return sample.at("index")==1;});restoreCheck(found!=samples.end(),"Clipboard fixture sample unavailable");target["id"]=found->at("id");}
    else if(domain=="midi"){params["channels"]=RestoreJson::array({0});params["instrument"]=1;}
    else{params["path"]=utf8Path(path);params["overwrite"]=true;}
    app.nativeCommandTarget={{"domain",domain},{"method",method},{"documentId",doc},{"revision",app.view->session.revision},{"params",params},{"target",target}};
    unsigned writes=0;restoreRejected([&]{app.nativeCommandCompletion.submit([&](const auto &name,const auto &values,const auto &)->ScreamSeq::Api::CompletedCall{++writes;(void)app.documentOperation(name,values);throw std::runtime_error("Owned unknown command result");},method,params,doc,++app.nativeCommandGeneration,app.nativeCommandTarget);});
    app.nativeCommandChoice=IDYES;
    if(domain=="save"){
      app.duringCommandAcknowledgement=[&]{restoreCheck(path.parent_path()==fixture.folder,"Owned Save fixture path escaped its directory");std::filesystem::remove(path);};
      restoreRejected([&]{app.reviewNativeCommand();});restoreCheck(app.nativeCommandCompletion.retained(),"Changed saved file bypassed acknowledgement guard");
    }else if(domain=="sample"){
      app.duringCommandAcknowledgement=[&]{auto changed=params;changed["end"]=8;app.documentOperation("sample.clipboard.copy",changed);};
      restoreRejected([&]{app.reviewNativeCommand();});restoreCheck(app.nativeCommandCompletion.retained(),"Changed clipboard bypassed acknowledgement guard");
    }else if(domain=="midi"){
      app.duringCommandAcknowledgement=[&]{const auto take=app.view->recording.at("take");app.documentOperation("recording.discard",{{"expectedRevision",app.view->session.revision},{"take",take}});app.documentOperation("recording.start",params);};
      restoreRejected([&]{app.reviewNativeCommand();});restoreCheck(app.nativeCommandCompletion.retained(),"New MIDI take bypassed acknowledgement guard");
    }
    app.reviewNativeCommand();restoreCheck(writes==1&&!app.nativeCommandCompletion.retained()&&app.nativeCommandReport.at("outcome")=="unverified","Unknown command recovery replayed or fabricated success");
    if(domain=="save")restoreCheck(!std::filesystem::exists(path),"Save acknowledgement rewrote the absent file");
    if(domain=="midi")restoreCheck(app.hasRecordingTake(),"MIDI acknowledgement consumed the newer take");
  }
}
