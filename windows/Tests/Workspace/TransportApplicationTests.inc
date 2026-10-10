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
