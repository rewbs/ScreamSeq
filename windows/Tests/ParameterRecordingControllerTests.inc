// Worker lifecycle with a real native VST3 and a deterministic presentation
// clock. Runs in the existing private-desktop recovery-manual process.
void parameterRecordingControllerTests(const std::filesystem::path &scanner,const std::filesystem::path &cache,
    const std::filesystem::path &directory) {
  Tracker::WindowsVST3::configure(scanner.generic_string(),cache.generic_string());
  bool active=false;unsigned stops=0;
  PlaybackHooks hooks;hooks.feedback=[&]{PlaybackFeedback f;f.playing=f.audioActive=active;f.generation=41;return f;};
  DocumentController c({},"parameter-recording",[&]{active=false;++stops;},[](const auto &){},{},64u*1024u*1024u,{},hooks);
  Json descriptor;for(const auto &p:call(c,"plugin.discover",{{"format","VST3"}}))
    if(p.at("classID")=="5245534F4E414E434546464543540001")descriptor=p;
  need(!descriptor.is_null(),"Parameter recording fixture missing");
  invoke(c,"plugin.add",{{"descriptor",descriptor}});invoke(c,"plugin.editor.open",{{"slot",0}});
  call(c,"flushPluginEditors",{{"force",true}});
  const auto id=c.view()->session.document.at("nativePlugins")[0].at("instanceID");
  const auto baseline=call(c,"plugin.state.get",{{"plugin",id}});
  const auto baselineAutomation=call(c,"automation.get",Json::object());
  const auto module=GetModuleHandleW(std::filesystem::u8path(descriptor.at("path").get<std::string>()).c_str());
  const auto gesture=reinterpret_cast<int(*)(double)>(GetProcAddress(module,"ResonanceFixtureGesture"));
  need(gesture!=nullptr,"Parameter recording gesture export missing");
  const auto rejected=[&](auto work){bool refused=false;try{work();}catch(const std::exception &){refused=true;}need(refused,"Parameter recording allowed an unsafe lifecycle operation");};
  const auto refusedStart=[&](const char *message) {
    const auto revision=c.view()->session.revision;bool classified=false;std::string diagnostic="No refusal returned";
    try{invoke(c,"automation.recording.start",{{"plugins",Json::array({id})}});}
    catch(const Api::ApiError &error){diagnostic=error.what();classified=error.code==-32002&&error.outcome&&
      error.outcome->state==Tracker::CommitOutcome::NotCommitted&&!error.completed&&
      std::string(error.what()).find(message)!=std::string::npos;}
    if(!classified)throw std::runtime_error(std::string("Recording start refusal must be classified before intake; expected ")+message+", received "+diagnostic);
    need(c.view()->session.revision==revision&&
      call(c,"automation.recording.get",Json::object()).at("retained")==false&&
      call(c,"plugin.state.get",{{"plugin",id}})==baseline,
      "Refused recording start retained an uncertain write, take, or changed baseline/history");
  };
  refusedStart("Start song playback");
  auto *playback=c.prepare(48000,Json::object(),true,true).get();active=true;
  uint64_t origin=hostTime100ns();std::array<float,8192> pcm{};
  refusedStart("first timed buffer");
  // An unstable native presentation estimate is an unmapped interval, not a
  // proven device discontinuity. Keep those two admission contracts distinct.
  need(playback->render(pcm.data(),4096,RenderTime{origin,41,48000,true,false,false})&&
    playback->render(pcm.data(),4096,RenderTime{origin,41,48000,true,false,false}),
    "Presentation-estimate fixture failed to render");
  const auto timeline=playback->recordingTimeline();
  need(timeline->unmappedCorrelations.load()==1&&timeline->absoluteFrames.generation()==41&&
    timeline->absoluteFrames.failureDetails().reason==Tracker::RecordingFrameTimeline::Failure::None,
    "Unmapped presentation estimate invalidated otherwise valid recording coverage");
  // A real device discontinuity remains terminal even if the next native
  // notification carries a syntactically valid timestamp. Start must reject
  // before publishing any take/provider owner or changing musical history.
  need(playback->render(pcm.data(),4096,RenderTime{origin,42,48000,true,true,false}),
    "Discontinuous-clock fixture failed to render");
  need(timeline->absoluteFrames.generation()==0&&
    timeline->absoluteFrames.failureDetails().reason==Tracker::RecordingFrameTimeline::Failure::Invalidated,
    "True device discontinuity did not invalidate recording admission");
  refusedStart("playback-invalidated");
  active=false;playback=c.prepare(48000,Json::object(),true,true).get();active=true;origin=hostTime100ns();
  const auto renderAhead=[&]{
    const auto limit=hostTime100ns()+1000000;
    for(unsigned n=0;origin<=limit&&n<1024;++n) {
      need(playback->render(pcm.data(),4096,RenderTime{origin,41,48000,true,false,false}),"Parameter recording timed render failed");
      origin+=uint64_t(4096)*hostTicksPerSecond/48000;
    }
    need(origin>limit,"Parameter recording fixture could not cover its clock");
  };
  renderAhead();
  auto take=invoke(c,"automation.recording.start",{{"plugins",Json::array({id})}});
  need(take.at("capturing")==true&&take.at("retained")==true,"Worker did not retain an armed parameter take");
  const auto token=take.at("take");const auto beforeRevision=c.view()->session.revision;
  rejected([&]{invoke(c,"document.patch",{{"title","Must not change"}});});
  rejected([&]{c.prepare(48000,Json::object(),true,true).get();});
  Tracker::WindowsVST3::pluginMainCall([&]{need(gesture(.25)>0,"Parameter recording input failed");});
  const auto controlTime=hostTime100ns();
  const Json control={{"take",token},{"plugin",id},{"parameter",7},{"value",.75},{"timestamp",std::to_string(controlTime)}};
  auto invalid=control;invalid["value"]=true;
  rejected([&]{invoke(c,"automation.recording.capture",invalid);});
  invalid=control;invalid["value"]=2;
  const auto refusedBeforeIntake=[&](const Json &input){
    bool classified=false;
    try{invoke(c,"automation.recording.capture",input);}
    catch(const Api::ApiError &e){classified=e.code==-32602&&e.outcome&&
      e.outcome->state==Tracker::CommitOutcome::NotCommitted&&!e.completed;}
    need(classified&&c.view()->session.revision==beforeRevision,
      "Pure control validation was not classified as refused before intake");
  };
  refusedBeforeIntake(invalid);
  invalid=control;invalid["timestamp"]="1";
  refusedBeforeIntake(invalid);
  take=invoke(c,"automation.recording.capture",control);
  need(take.at("accepted")==true&&!take.contains("intakeError"),"Original-time native control was not retained after invalid-input refusal");
  renderAhead();take=call(c,"automation.recording.get",Json::object());
  need(take.at("eventCount")==2&&!take.contains("intakeError")&&c.view()->session.revision==beforeRevision,
    "Worker capture failed or wrote history before commit");
  const auto first=call(c,"automation.recording.events",{{"take",token},{"plugin",id},{"parameter",7},{"offset",0},{"limit",1}});
  const auto second=call(c,"automation.recording.events",{{"take",token},{"plugin",id},{"parameter",7},{"offset",1},{"limit",1}});
  need(first.at("total")==2&&first.at("events").size()==1&&second.at("events").size()==1&&
    first.at("events")[0].at("timestamp")==std::to_string(controlTime)&&first.at("events")[0].at("value")==.75&&
    second.at("events")[0].at("value")==.25&&!first.at("events")[0].at("frame").is_null(),
    "Paginated take review lost original event time, value or mapped frame");
  const Json timingRequest={{"take",token},{"plugin",id},{"parameter",7},{"limit",1},{"includeTiming",true}};
  auto inspected=call(c,"automation.recording.events",timingRequest);
  need(inspected.at("events")[0].at("timing").at("status")=="retained-frame",
    "Worker omitted the shared read-only timing projection");
  inspected["events"][0].erase("timing");
  need(inspected==first&&c.view()->session.revision==beforeRevision,"Timing inspection changed event/history");
  auto invalidTiming=timingRequest;invalidTiming["includeTiming"]=1;
  rejected([&]{call(c,"automation.recording.events",invalidTiming);});
  invalidTiming=timingRequest;invalidTiming["limit"]=17;
  rejected([&]{call(c,"automation.recording.events",invalidTiming);});
  const auto recovery=recoveryCapture(c);
  need(recovery->hasRecording&&recovery->needsProtection,"Worker parameter take was omitted from recovery");
  const Json delivery={{"version",1},{"reviewOnly",true},{"document",c.view()->session.documentId},{"revision",beforeRevision},
    {"take",token},{"accepted","1"},{"lost","2"},{"previewError",""},{"events",Json::array({
      {{"plugin",id},{"parameter",7},{"value",.75},{"hostTime",std::to_string(controlTime)},
       {"state","pending"},{"error","Original completion not observed"}}})}};
  auto protectedInput=recoveryWait(c,c.captureRecovery(beforeRevision,delivery));
  auto repeatedInput=recoveryWait(c,c.captureRecovery(beforeRevision,delivery));
  need(protectedInput->hasRecording&&protectedInput->needsProtection&&protectedInput->fingerprint==repeatedInput->fingerprint&&
    protectedInput->fingerprint!=recovery->fingerprint&&c.view()->session.revision==beforeRevision,
    "Native input recovery did not protect exact queued evidence independently of history");
  auto reopenedInput=Project::openNativeProjectBytes(*protectedInput->bytes);
  need(Tracker::readControlInputRecovery(reopenedInput.state.preserved.at(Tracker::controlInputRecoveryKey)[0])==delivery&&
    projectAbsoluteAutomation(reopenedInput.state)==projectAbsoluteAutomation(Project::openNativeProjectBytes(*recovery->bytes).state),
    "Recovery replayed pending input or lost exact native value/time/loss evidence on reopen");
  auto foreignInput=delivery;foreignInput["take"]="other-take";
  rejected([&]{recoveryWait(c,c.captureRecovery(beforeRevision,foreignInput));});
  foreignInput=delivery;foreignInput["document"]="other-song";
  rejected([&]{recoveryWait(c,c.captureRecovery(beforeRevision,foreignInput));});
  need(recoveryCapture(c)->fingerprint==recovery->fingerprint,"Native recovery input mutated the live project or consumed the worker take");
  need(call(c,"plugin.state.get",{{"plugin",id}})==baseline,"Worker capture changed the manual baseline");
  take=invoke(c,"automation.recording.stop",{{"take",token}});
  need(take.at("capturing")==false&&!take.contains("intakeError"),"Worker Stop failed its fixed native drain");
  const auto stopped=stops;
  take=invoke(c,"automation.recording.commit",{{"take",token},{"acknowledgeLoss",false}});
  need(take.at("completed")==true&&take.at("retained")==false&&take.at("changed")==true&&
    !take.contains("restorationError")&&stops==stopped&&active,"Worker parameter commit failed or stopped playback");
  const auto recorded=call(c,"automation.get",Json::object());
  need(recorded!=baselineAutomation&&call(c,"plugin.state.get",{{"plugin",id}})==baseline,
    "Worker parameter commit lost points or changed opaque manual state");
  rejected([&]{invoke(c,"automation.recording.commit",{{"take",token}});});
  renderAhead();take=invoke(c,"automation.recording.start",{{"plugins",Json::array({id})}});
  const auto discardedToken=take.at("take");const auto discardRevision=c.view()->session.revision;
  Tracker::WindowsVST3::pluginMainCall([&]{need(gesture(.75)>0,"Discard parameter input failed");});
  renderAhead();invoke(c,"automation.recording.stop",{{"take",discardedToken}});
  take=invoke(c,"automation.recording.discard",{{"take",discardedToken}});
  need(take.at("retained")==false&&c.view()->session.revision==discardRevision&&call(c,"automation.get",Json::object())==recorded&&
    call(c,"plugin.state.get",{{"plugin",id}})==baseline,"Parameter discard changed saved points or manual history");
  renderAhead();take=invoke(c,"automation.recording.start",{{"plugins",Json::array({id})}});
  const auto failedToken=take.at("take");
  Tracker::WindowsVST3::pluginMainCall([&]{need(gesture(.375)>0,"Failed take input failed");});
  need(playback->render(pcm.data(),4096,RenderTime{origin,42,48000,true,true,false}),"Discontinuous test render failed");
  take=call(c,"automation.recording.get",Json::object());
  need(take.at("retained")==true&&take.at("compatible")==false&&take.contains("intakeError"),
    "A changed playback clock lost the retained take or prevented review");
  active=false;playback->chain().applyPending();
  take=call(c,"automation.recording.get",Json::object());
  need(take.at("retained")==true&&take.at("transportPlaying")==false&&take.at("discardable")==true,
    "Stopped failed-clock take did not expose its guarded Discard path to the native review");
  invoke(c,"plugin.editor.close",{{"slot",0}});
  take=invoke(c,"automation.recording.discard",{{"take",failedToken}});
  need(take.at("retained")==false&&c.view()->session.revision==discardRevision&&call(c,"automation.get",Json::object())==recorded,
    "A failed clock could not explicitly discard its closed-editor take");
  invoke(c,"history.undo",{{"domain","plugins"}});
  need(call(c,"automation.get",Json::object())==baselineAutomation,"Worker parameter take needed more than one Undo");
  invoke(c,"history.redo",{{"domain","plugins"}});
  need(call(c,"automation.get",Json::object())==recorded,"Worker parameter take did not survive Redo");
  const auto path=directory/"parameter-recording-controller.screamseq";
  invoke(c,"document.save",{{"path",path.generic_string()},{"overwrite",true}});
  invoke(c,"document.open",{{"path",path.generic_string()},{"discard",true}});
  need(call(c,"automation.get",Json::object())==recorded&&call(c,"plugin.state.get",{{"plugin",id}})==baseline,
    "Worker parameter take failed native save/reopen");
  recoveryWait(c,c.recover(protectedInput->bytes,c.view()->session.revision,true));
  const auto imported=c.view();const auto importedMusic=call(c,"automation.get",Json::object());
  const auto catalogue=call(c,"automation.recovery.list",{{"offset",0},{"limit",2}});
  need(catalogue.at("total")==2&&catalogue.at("entries").size()==2&&catalogue.at("revision")==imported->session.revision,
    "Imported recording evidence was not catalogued with its current document revision");
  const auto nativeInput=invoke(c,"automation.recovery.get",{{"kind","nativeControls"},{"index",0},{"limit",1}});
  need(nativeInput.at("events").size()==1&&nativeInput.at("events")[0].at("hostTime")==std::to_string(controlTime)&&
    nativeInput.at("events")[0].at("value")==.75&&nativeInput.at("summary").at("document")==delivery.at("document")&&
    nativeInput.at("summary").at("lost")=="2"&&nativeInput.at("summary").at("take")==token,
    "Imported native input review remapped the old document, clock, value or loss evidence");
  const auto gestures=invoke(c,"automation.recovery.get",{{"kind","gestures"},{"index",0}});
  need(gestures.at("total")==2&&call(c,"automation.recording.get",Json::object()).at("retained")==false,
    "Detached gesture review rearmed a live take or lost captured input");
  rejected([&]{call(c,"automation.recovery.get",{{"expectedRevision",beforeRevision},{"kind","gestures"},{"index",0}});});
  rejected([&]{invoke(c,"automation.recovery.get",{{"kind","nativeControls"},{"index",true}});});
  rejected([&]{invoke(c,"automation.recovery.get",{{"kind","nativeControls"},{"index",0},{"limit",513}});});
  rejected([&]{call(c,"automation.recovery.list",{{"unexpected",true}});});
  need(c.view()->session.revision==imported->session.revision&&call(c,"automation.get",Json::object())==importedMusic,
    "Detached recovery review or refusal mutated accepted music/history");
  std::cout<<"PASS internal parameter recording lifecycle, native preview, recovery, guards, one Undo and save/reopen\n";
}
