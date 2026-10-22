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
  rejected([&]{invoke(c,"automation.recording.start",{{"plugins",Json::array({id})}});});
  auto *playback=c.prepare(48000,Json::object(),true,true).get();active=true;
  uint64_t origin=hostTime100ns();std::array<float,8192> pcm{};
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
  rejected([&]{invoke(c,"automation.recording.capture",invalid);});
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
  const auto recovery=recoveryCapture(c);
  need(recovery->hasRecording&&recovery->needsProtection,"Worker parameter take was omitted from recovery");
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
  std::cout<<"PASS internal parameter recording lifecycle, native preview, recovery, guards, one Undo and save/reopen\n";
}
