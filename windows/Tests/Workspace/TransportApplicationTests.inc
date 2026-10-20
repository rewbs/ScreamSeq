void applicationPlaybackLoop() {
  RestoreFixture fixture;auto &app=*fixture.app;
  app.openNoteTrackEditor(true);const auto tool=app.createNoteTrackWindow.get();
  const auto window=tool->window(),field=GetDlgItem(window,7902);ScreamSeq::Tests::ownGuiWindow(window);
  rawDraftText(field,L"-");SetFocus(field);
  const auto draft=tool->snapshot(),song=songState(app),position=app.position();
  const auto generation=app.stopGeneration;const auto initial=app.playbackLoop;
  app.runWorkspaceShortcut(playbackLoopCommand);
  restoreCheck(restoreText(app.controls.at(playbackLoopCommand))==(!initial?L"Playback loop: on":L"Playback loop: off"),
    "Native loop control did not expose its state in text");
  restoreCheck(app.playbackLoop!=initial&&GetFocus()==field&&tool->snapshot()==draft&&songState(app)==song&&
    app.position()==position&&app.stopGeneration==generation,"Loop shortcut changed a retained draft, focus, song or transport generation");
  app.setPlaybackLoop(initial);
  restoreCheck(app.playbackLoop==initial&&songState(app)==song,"Restoring the stopped loop setting created document history");
  app.busy=true;bool refused=false;
  try{app.setPlaybackLoop(!initial);}catch(const ScreamSeq::Api::ApiError &error){
    refused=error.code==-32002&&error.outcome&&error.outcome->state==Tracker::CommitOutcome::NotCommitted;
  }
  app.busy=false;
  restoreCheck(refused&&app.playbackLoop==initial&&app.stopGeneration==generation,"Busy loop update changed playback or lacked a noncommit outcome");
  const auto roots=app.departureRoots();app.departureInput=std::make_unique<ScreamSeq::NativeInputGate>(roots);
  requireDepartureRefusal([&]{app.setPlaybackLoop(!initial);});app.departureInput.reset();
  restoreCheck(app.playbackLoop==initial&&tool->snapshot()==draft,"Document replacement admitted a loop update or lost the retained draft");
  app.edit("recording.start",{{"channels",RestoreJson::array({0})},{"instrument",1}});
  const auto take=app.view->recording.at("take");const auto captured=app.view->recording.at("capturing");
  app.setPlaybackLoop(!initial);
  restoreCheck(app.view->recording.at("take")==take&&app.view->recording.at("capturing")==captured&&
    tool->snapshot()==draft&&app.stopGeneration==generation,"Loop preference discarded capture ownership or a raw draft");
  app.edit("recording.stop",{{"take",take}});app.edit("recording.discard",{{"take",take}});
}

void applicationPlaybackRegions() {
  RestoreFixture fixture;auto &app=*fixture.app;
  const auto other=app.edit("pattern.create",{{"rows",32}}).at("pattern").get<unsigned>();
  app.edit("order.edit",{{"order",0},{"operation","after"},{"pattern",0}});
  const auto occurrence=app.view->session.document.at("orderMetadata").at(1).at("id").get<std::string>();
  app.selectArrangementOrder(occurrence);auto cursor=app.position();cursor["row"]=16;cursor["following"]=false;app.navigate(cursor);
  app.setPlaybackLoop(true);const auto song=songState(app),position=app.position();
  const auto settings=[&](bool fromCursor,bool bounded){return app.resolveWorkspacePlayback(app.captureWorkspacePlayback(fromCursor,bounded));};
  restoreCheck(settings(false,false)==RestoreJson{{"order",1},{"cursorRow",0},{"loop",true}},"Play lost the selected repeated occurrence");
  restoreCheck(settings(true,false)==RestoreJson{{"order",1},{"cursorRow",16},{"loop",true}},"Cursor Play did not start at the detached edit cursor");
  restoreCheck(settings(false,true)==RestoreJson{{"order",1},{"cursorRow",0},{"loop",true},{"pattern",0},{"startRow",0},{"endRow",app.patternRows()}},
    "Unselected bounded Play did not use the complete pattern");
  app.setPatternSelection(true,8,0,std::pair{23u,1u});
  const RestoreJson range={{"order",1},{"cursorRow",8},{"loop",true},{"pattern",0},{"startRow",8},{"endRow",24}};
  restoreCheck(settings(false,true)==range&&settings(true,true).at("cursorRow")==16,"Selection range did not use exclusive end or the inside cursor");
  const auto captured=app.captureWorkspacePlayback(true,true);
  cursor["row"]=4;app.navigate(cursor);app.setPatternSelection(true,8,0,std::pair{23u,1u});
  restoreCheck(settings(true,true)==range,"Outside cursor did not fall back to the selection start");
  restoreCheck(app.resolveWorkspacePlayback(captured).at("cursorRow")==16&&songState(app)==song,
    "Later navigation redirected captured playback or settings edited the song");
  app.edit("order.edit",{{"order",1},{"operation","down"}});
  const auto moved=app.orderForIdentity(occurrence);
  restoreCheck(moved!=1&&app.resolveWorkspacePlayback(captured).at("order")==moved,"Order reorder retargeted a captured repeated occurrence");
  app.edit("order.edit",{{"order",moved},{"operation","assign"},{"pattern",other}});
  restoreCheck(restoreRejected([&]{app.resolveWorkspacePlayback(captured);}).find("reassigned")!=std::string::npos,
    "Reassigning a captured occurrence silently changed playback's pattern");
  app.edit("history.undo",{{"domain","all"}});
  app.edit("order.edit",{{"order",app.orderForIdentity(occurrence)},{"operation","remove"}});
  restoreCheck(restoreRejected([&]{app.resolveWorkspacePlayback(captured);}).find("removed")!=std::string::npos,
    "Removing a captured occurrence silently chose another repetition");
  app.edit("history.undo",{{"domain","all"}});
  app.selectArrangementOrder(occurrence);app.navigate(position);
  bool changedDuringFlush=false;uint64_t generationAfterEdit=0;
  app.sampleMutationCompletion=[&](const std::string &method){if(method=="flushPluginEditors"&&!changedDuringFlush){
    changedDuringFlush=true;app.edit("order.edit",{{"order",app.orderForIdentity(occurrence)},{"operation","assign"},{"pattern",other}});
    generationAfterEdit=app.stopGeneration;
  }};
  const auto error=restoreRejected([&]{app.playWorkspaceRegion(true,false);});app.sampleMutationCompletion={};
  restoreCheck(changedDuringFlush&&error.find("reassigned")!=std::string::npos&&app.stopGeneration==generationAfterEdit&&!app.device.running(),
    "Native Play did not resolve its captured target after the actual plugin-flush boundary and before device work");
  while(true) {
    const auto &orders=app.view->session.document.at("orders");const auto found=std::find(orders.begin(),orders.end(),RestoreJson(0));
    if(found==orders.end())break;
    app.edit("order.edit",{{"order",std::distance(orders.begin(),found)},{"operation","remove"}});
  }
  app.navigate(position);const auto beforeUnarranged=songState(app);
  restoreCheck(restoreRejected([&]{app.captureWorkspacePlayback(true,false);}).find("not in the arrangement")!=std::string::npos,
    "Unarranged cursor Play silently started another pattern");
  const auto unarranged=settings(false,true);
  restoreCheck(unarranged.at("pattern")==0&&unarranged.at("startRow")==0&&unarranged.at("endRow")==app.patternRows()&&songState(app)==beforeUnarranged,
    "An unarranged pattern could not use bounded playback without changing music");
  auto oversized=app.captureWorkspacePlayback(false,true);++oversized.endRow;
  restoreCheck(restoreRejected([&]{app.resolveWorkspacePlayback(oversized);}).find("no longer fits")!=std::string::npos,
    "Playback accepted an out-of-range captured end");
  const auto roots=app.departureRoots();app.departureInput=std::make_unique<ScreamSeq::NativeInputGate>(roots);
  requireDepartureRefusal([&]{app.playWorkspaceRegion(false,true);});app.departureInput.reset();
}
