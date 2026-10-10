void applicationNativeCommandRecovery(){
  withRestoreFixture([](RestoreApplication &app){
    app.command(patternCommand);app.command(mixerStripsCommand);
    restoreCheck(app.mixerEditorVisible()&&app.mixerStrips&&app.mixerStrips->visible()&&GetFocus()==app.mixerStrips->window(),
      "Strips command did not reveal the mixer and retain its keyboard focus");
    const auto before=songState(app);const auto target=app.mixerTarget;
    app.busy=true;app.command(mixerDetailsCommand);
    restoreCheck(app.deferredViews.size()==1&&app.deferredViews.front().command==mixerDetailsCommand&&app.mixerStripsMode,
      "Busy Details activation was lost or executed reentrantly");
    app.busy=false;app.drainViews();
    restoreCheck(app.deferredViews.empty()&&!app.mixerStripsMode&&app.mixerEditorVisible()&&GetFocus()==app.controls.at(mixerList)&&songState(app)==before,
      "Deferred Details opening changed music or lost its native focus");
    app.busy=true;app.command(mixerStripsCommand);app.mixerTarget="missing-captured-bus";
    app.busy=false;app.drainViews();
    restoreCheck(app.deferredViews.empty()&&!app.mixerStripsMode&&app.status.find(L"View target changed")!=std::wstring::npos,
      "Deferred mixer activation followed a changed target");
    app.mixerTarget=target;
  });
  {
    RestoreFixture fixture;auto &app=*fixture.app;
    app.documentOperation("mixer.enable",{{"expectedRevision",app.view->session.revision}});
    app.command(mixerDetailsCommand);
    const auto before=app.documentOperation("mixer.get",RestoreJson::object());
    app.mixerControlCommand(mixerAddReturn);
    const auto added=app.documentOperation("mixer.get",RestoreJson::object());
    const auto id=app.mixerTarget;
    auto bus=[&](const RestoreJson &data)->RestoreJson {
      for(const auto &entry:data.at("buses"))if(entry.at("id")==id)return entry;
      throw std::runtime_error("Created return identity is missing");
    };
    restoreCheck(added.at("buses").size()==before.at("buses").size()+1&&bus(added).at("kind")=="return"&&
      app.mixerEditorVisible()&&!app.mixerStripsMode&&GetFocus()==app.controls.at(mixerName),
      "Add Return did not expose the new stable bus in retained Details");
    for(size_t i=0;i<before.at("buses").size();++i)restoreCheck(added.at("buses")[i]==before.at("buses")[i],
      "Add Return changed an existing bus or created an unsolicited send");
    app.edit("history.undo",{{"domain","all"}});
    restoreCheck(app.documentOperation("mixer.get",RestoreJson::object()).at("buses")==before.at("buses"),"Add Return Undo changed existing routes");
    app.edit("history.redo",{{"domain","all"}});app.mixerTarget=id;app.refreshMixer();
    restoreCheck(app.documentOperation("mixer.get",RestoreJson::object()).at("buses")==added.at("buses"),"Add Return Redo changed its stable identity");
    rawDraftText(app.controls.at(mixerName),L"Texture return");rawDraftText(app.controls.at(mixerColor),L"12zz90");app.mixerFieldChanged();
    const auto generation=app.mixerGeneration;const auto revision=app.view->session.revision;
    for(const auto command:{mixerApply,mixerAdd,mixerAddReturn,mixerRemove,mixerAddEffect,mixerRouteInstrument})restoreRejected([&]{app.mixerControlCommand(command);});
    restoreCheck(app.mixerDirty&&app.mixerGeneration==generation&&app.view->session.revision==revision&&
      restoreText(app.controls.at(mixerColor))==L"12zz90"&&app.documentOperation("mixer.get",RestoreJson::object())==added,
      "Invalid color or structural command discarded a raw draft or changed the mixer");
    app.command(mixerStripsCommand);app.command(mixerDetailsCommand);
    restoreCheck(restoreText(app.controls.at(mixerColor))==L"12zz90"&&app.mixerTarget==id&&app.mixerGeneration==generation,
      "Switching mixer views discarded or retargeted the color/name draft");
    rawDraftText(app.controls.at(mixerColor),L"12AB90");app.mixerFieldChanged();app.mixerControlCommand(mixerApply);
    const auto named=bus(app.documentOperation("mixer.get",RestoreJson::object()));
    auto expected=bus(added);expected["name"]="Texture return";expected["color"]=0x12ab90;
    restoreCheck(named==expected&&!app.mixerDirty,"Applying name/color lost unrelated return settings");
    app.edit("history.undo",{{"domain","all"}});
    restoreCheck(bus(app.documentOperation("mixer.get",RestoreJson::object()))==bus(added),"Name and color did not Undo as one edit");
    app.edit("history.redo",{{"domain","all"}});
    restoreCheck(bus(app.documentOperation("mixer.get",RestoreJson::object()))==named,"Name/color Redo changed the stable return identity");
    const auto path=saveDepartureFixture(fixture);
    app.documentOperation("document.open",departureOpenRequest(app,path));
    restoreCheck(bus(app.documentOperation("mixer.get",RestoreJson::object()))==named,"Return name, color or stable identity did not survive native save/reopen");
  }
  withRestoreFixture([](RestoreApplication &app){
    app.documentOperation("mixer.enable",{{"expectedRevision",app.view->session.revision}});
    app.command(mixerDetailsCommand);unsigned writes=0;
    rawDraftText(app.controls.at(mixerName),L"Saved bus name");app.mixerFieldChanged();
    app.sampleMutationCompletion=[&](const std::string &method){if(method=="mixer.bus.set"){
      ++writes;rawDraftText(app.controls.at(mixerName),L"Later raw bus name");app.mixerFieldChanged();
      throw std::runtime_error("Owned mixer presentation failure");}};
    restoreRejected([&]{app.mixerControlCommand(mixerApply);});
    restoreCheck(writes==1&&app.nativeCommandCompletion.retained()&&app.nativeCommandCompletion.returned()&&app.mixerDirty,
      "Mixer Details lost its completed result or later raw draft");
    restoreRejected([&]{app.mixerControlCommand(mixerApply);});
    app.mixerControlCommand(mixerReload);
    restoreCheck(writes==1&&!app.nativeCommandCompletion.retained()&&app.mixerDirty&&restoreText(app.controls.at(mixerName))==L"Later raw bus name",
      "Mixer result Review replayed a write or discarded a newer name draft");
    app.sampleMutationCompletion={};
  });
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
  withRestoreFixture([](RestoreApplication &app){
    app.command(patternCommand);const auto original=app.documentOperation("track.get",RestoreJson::object());unsigned writes=0;
    app.sampleMutationCompletion=[&](const std::string &method){if(method=="track.column.set"){
      ++writes;auto position=app.position();position["channel"]=1;app.navigate(position);
      throw std::runtime_error("Owned track presentation failure");}};
    restoreRejected([&]{app.toggleNoteColumnMute();});
    restoreCheck(writes==1&&app.nativeCommandCompletion.retained()&&app.nativeCommandCompletion.returned()&&app.channel==1,
      "Track completion lost its original receipt or retargeted later navigation");
    restoreRejected([&]{app.toggleNoteColumnMute();});
    const auto muted=app.documentOperation("track.get",RestoreJson::object());
    restoreCheck(muted.at("columns")[0].at("mute")==true&&muted.at("columns")[1].at("mute")==false,
      "Repeated unresolved mute changed the newly selected column");
    app.reviewNativeCommand();
    restoreCheck(writes==1&&!app.nativeCommandCompletion.retained()&&app.channel==1&&app.documentOperation("track.get",RestoreJson::object())==muted,
      "Track Review replayed a write or lost current navigation");
    app.sampleMutationCompletion={};app.edit("history.undo",{{"domain","all"}});
    restoreCheck(app.documentOperation("track.get",RestoreJson::object())==original,"Track completion recovery added another history step");
  });
  // Unknown outcomes use actual side effects but deliberately drop every route
  // by which the fixture could return a receipt. This is not the ordinary API.
  for(const std::string domain:{"sample","midi","save","mixer","track"}){
    RestoreFixture fixture;auto &app=*fixture.app;const auto path=saveDepartureFixture(fixture);const auto doc=app.documentId;
    if(domain=="mixer")app.documentOperation("mixer.enable",{{"expectedRevision",app.view->session.revision}});
    const std::string method=domain=="sample"?"sample.clipboard.copy":domain=="midi"?"recording.start":domain=="mixer"?"mixer.bus.set":domain=="track"?"track.column.set":"document.save";
    RestoreJson params={{"expectedRevision",app.view->session.revision}},target=RestoreJson::object();
    if(domain=="sample"){params["sample"]=1;params["start"]=0;params["end"]=16;const auto &samples=app.view->session.document.at("samples");const auto found=std::find_if(samples.begin(),samples.end(),[](const auto &sample){return sample.at("index")==1;});restoreCheck(found!=samples.end(),"Clipboard fixture sample unavailable");target["id"]=found->at("id");}
    else if(domain=="midi"){params["channels"]=RestoreJson::array({0});params["instrument"]=1;}
    else if(domain=="mixer"){params["bus"]=app.documentOperation("mixer.get",RestoreJson::object()).at("buses")[0].at("id");params["gainDB"]=-12;}
    else if(domain=="track"){params["column"]=app.noteColumn(0).at("id");params["mute"]=true;}
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
    }else if(domain=="mixer"){
      app.duringCommandAcknowledgement=[&]{auto changed=params;changed["expectedRevision"]=app.view->session.revision;changed["gainDB"]=-6;app.documentOperation("mixer.bus.set",changed);};
      restoreRejected([&]{app.reviewNativeCommand();});restoreCheck(app.nativeCommandCompletion.retained(),"Changed mixer bypassed acknowledgement revision guard");
    }else if(domain=="track"){
      app.duringCommandAcknowledgement=[&]{auto changed=params;changed["expectedRevision"]=app.view->session.revision;changed["mute"]=false;app.documentOperation("track.column.set",changed);};
      restoreRejected([&]{app.reviewNativeCommand();});restoreCheck(app.nativeCommandCompletion.retained(),"Changed column mute bypassed acknowledgement revision guard");
    }
    app.reviewNativeCommand();restoreCheck(writes==1&&!app.nativeCommandCompletion.retained()&&app.nativeCommandReport.at("outcome")=="unverified","Unknown command recovery replayed or fabricated success");
    if(domain=="save")restoreCheck(!std::filesystem::exists(path),"Save acknowledgement rewrote the absent file");
    if(domain=="midi")restoreCheck(app.hasRecordingTake(),"MIDI acknowledgement consumed the newer take");
    if(domain=="track")restoreCheck(app.nativeCommandReport.at("observed").at("layout")==app.documentOperation("track.get",RestoreJson::object()),"Track Review did not retain the observed current layout");
  }
}
