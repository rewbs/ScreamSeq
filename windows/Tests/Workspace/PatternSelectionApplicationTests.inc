void applicationWholePatternSelection() {
  RestoreFixture fixture;auto &app=*fixture.app;
  app.command(patternCommand);auto cursor=app.position();cursor["row"]=8;cursor["channel"]=1;cursor["column"]=0;cursor["following"]=false;app.navigate(cursor);
  const auto before=songState(app),position=app.position();const auto firstRow=app.firstRow;const auto scroll=app.horizontalScroll;
  app.runWorkspaceShortcut(selectAllFocusedCommand);
  const RestoreJson expected={{"startRow",0},{"endRow",app.patternRows()-1},{"startChannel",0},{"endChannel",app.view->channels-1}};
  restoreCheck(app.selection()==expected&&songState(app)==before&&app.position()==position&&app.firstRow==firstRow&&app.horizontalScroll==scroll,
    "Select All changed song/cursor/viewport or failed to include every row and column");
  const auto revision=app.contextRevision;app.selectWholePattern();
  restoreCheck(app.contextRevision==revision,"Repeated Select All changed context identity");
  const auto selected=app.noteTrackContext().at("channels");
  restoreCheck(selected.size()==app.view->channels&&selected.front()==0&&selected.back()==app.view->channels-1,
    "Track grouping used the cursor instead of the independent selection endpoint");
  app.edit("track.create",{{"columns",2},{"name","Added after selection"}});
  restoreCheck(app.selection()==expected&&app.position()==position,"Appending columns silently expanded or moved a captured selection");
  app.selectWholePattern();restoreCheck(app.selection().at("endChannel")==app.view->channels-1,"Explicit new Select All missed appended columns");
  const auto beforeShrink=app.contextRevision;app.edit("history.undo",{{"domain","all"}});
  restoreCheck(app.selection()==expected&&app.contextRevision!=beforeShrink,"Shrinking channels did not clamp the selection or revise its context guard");
  cursor=app.position();cursor["row"]=9;app.navigate(cursor);
  restoreCheck(!app.selecting&&!app.explicitSelectionEnd&&app.selection()==RestoreJson{{"startRow",9},{"endRow",9},{"startChannel",1},{"endChannel",1}},
    "Cursor navigation retained the previous whole-pattern selection");
  app.selectWholePattern();const auto grid=app.patternGridRect();
  const auto x=grid.x+app.gutter+app.channelStart(app.channel)-app.horizontalScroll+4;
  const auto y=grid.y+app.gridHeader+(app.row-app.firstRow)*app.rowHeight+4;
  const auto beforeClick=app.contextRevision;
  restoreCheck(app.gridClick(x,y,false)&&!app.selecting&&!app.explicitSelectionEnd&&app.contextRevision!=beforeClick,
    "Clicking the existing cursor failed to clear selection or revise its context guard");
  app.selectWholePattern();const auto roots=app.departureRoots();
  app.departureInput=std::make_unique<ScreamSeq::NativeInputGate>(roots);
  requireDepartureRefusal([&]{app.selectWholePattern();});app.departureInput.reset();
  restoreCheck(app.selection()==expected,"Refused selection changed its captured bounds");
  const auto pattern=app.patternIndex,lastRow=app.patternRows()-1,lastChannel=app.view->channels-1;
  app.edit("pattern.apply",{{"cells",RestoreJson::array({
    {{"pattern",pattern},{"row",0},{"channel",0},{"note",61}},
    {{"pattern",pattern},{"row",lastRow},{"channel",lastChannel},{"note",67}}})}});
  const auto cells=app.pattern(pattern).cells;const auto editPosition=app.position();
  app.command(clearCommand);
  const auto cleared=app.pattern(pattern).cells;
  restoreCheck(cleared.front().at(0)==0&&cleared.back().at(0)==0&&app.position()==editPosition&&app.selection()==expected,
    "Delete after Select All missed an edge, moved the cursor or lost its selection");
  app.edit("history.undo",{{"domain","all"}});
  restoreCheck(app.pattern(pattern).cells==cells,"Whole-pattern Delete did not restore with one Undo");
}

void applicationTypingNavigation() {
  RestoreFixture fixture;auto &app=*fixture.app;app.command(patternCommand);
  app.edit("pattern.apply",{{"cells",RestoreJson::array({{{"pattern",0},{"row",12},{"channel",0},{"instrument",3}},
    {{"pattern",0},{"row",13},{"channel",0},{"instrument",0}}})}});
  app.edit("pattern.notes.set",{{"pattern",0},{"events",RestoreJson::array({{{"channel",0},{"position",13*65536+123},{"note",61},{"instrument",7}}})}});
  auto cursor=app.position();cursor["row"]=12;cursor["channel"]=0;cursor["column"]=1;cursor["following"]=false;app.navigate(cursor);
  SetFocus(app.window);restoreCheck(app.key(VK_RETURN)&&app.inputInstrumentOverride==3,"Enter in an instrument cell did not select its sound for future notes");
  app.setPatternSelection(true,10,0,std::pair{14u,1u});
  app.openPatternTools();auto *tool=app.patternToolsWindow.get();const auto edit=GetDlgItem(tool->window(),8006);
  ScreamSeq::Tests::ownGuiWindow(tool->window());rawDraftText(edit,L"-");SetFocus(edit);
  const auto draft=tool->snapshot().at("draft"),target=tool->snapshot().at("captured");
  const auto before=songState(app),position=app.position(),selection=app.selection();const auto focus=GetFocus();
  app.inputInstrumentOverride=255;app.refreshTypingSounds();const auto edge=app.contextRevision;
  app.command(inputInstrumentNextCommand);restoreCheck(app.contextRevision==edge,"Instrument upper boundary changed context");
  app.command(inputInstrumentPreviousCommand);restoreCheck(app.inputInstrumentOverride==254,"Previous input instrument skipped the empty slot");
  app.command(inputInstrumentAtCursorCommand);restoreCheck(app.inputInstrumentOverride==3,"Use instrument at cursor ignored the module cell");
  app.octave=9;const auto upper=app.contextRevision;app.command(inputOctaveUpCommand);
  restoreCheck(app.octave==9&&app.contextRevision==upper,"Octave upper boundary changed context");
  app.command(inputOctaveDownCommand);restoreCheck(app.octave==8,"Lower input octave did not update");
  restoreCheck(songState(app)==before&&app.position()==position&&app.selection()==selection&&GetFocus()==focus&&
    tool->snapshot().at("draft")==draft&&tool->snapshot().at("captured")==target,"Typing navigation moved the edit context, stole focus or changed a retained draft/song");
  app.busy=true;app.command(inputInstrumentNextCommand);app.command(inputOctaveDownCommand);app.busy=false;
  restoreCheck(app.octave==8&&app.inputInstrumentOverride==3,"Busy input navigation partially applied");
  app.departureInput=std::make_unique<ScreamSeq::NativeInputGate>(app.departureRoots());
  requireDepartureRefusal([&]{app.typingInputCommand(inputOctaveDownCommand);});app.departureInput.reset();
  cursor["row"]=13;app.navigate(cursor);app.command(inputInstrumentAtCursorCommand);
  restoreCheck(app.inputInstrumentOverride==7,"Use instrument at cursor did not find the precise-note instrument");
  cursor["row"]=14;app.navigate(cursor);app.edit("pattern.apply",{{"cells",RestoreJson::array({{{"pattern",0},{"row",14},{"channel",0},{"instrument",0}}})}});
  const auto empty=app.contextRevision;app.command(inputInstrumentAtCursorCommand);
  restoreCheck(app.inputInstrumentOverride==7&&app.contextRevision==empty,"Empty instrument cell changed future input");
  app.octave=0;const auto lower=app.contextRevision;app.command(inputOctaveDownCommand);
  restoreCheck(app.octave==0&&app.contextRevision==lower,"Octave lower boundary changed context");
}

void applicationQueuedPatternPaste() {
  RestoreFixture fixture;auto &app=*fixture.app;app.command(patternCommand);SetFocus(app.window);
  app.edit("pattern.apply",{{"cells",RestoreJson::array({{{"pattern",0},{"row",0},{"channel",0},{"note",61}},{{"pattern",0},{"row",8},{"channel",0},{"note",0}}})}});
  auto cursor=app.position();cursor["pattern"]=0;cursor["row"]=0;cursor["channel"]=0;cursor["column"]=0;cursor["following"]=false;app.navigate(cursor);
  restoreCheck(app.copyCells(),"Prepare private paste clipboard");cursor["row"]=8;app.navigate(cursor);
  const auto note=[&](unsigned row){const auto pattern=app.pattern(0);return unsigned(pattern.cells.at(size_t(row)*pattern.channels).at(0));};
  // Exercise input inside the genuine busy wait, not a timing-dependent sleep.
  std::promise<RestoreJson> gate;app.inputCompletion=&gate;
  app.postedInput=[&]{restoreCheck(app.busy,"Paste fixture did not enter a worker wait");app.command(pasteCommand);restoreCheck(app.pendingPatternPaste.has_value(),"Busy paste was dropped instead of retaining its intent");};
  restoreCheck(PostMessageW(app.window,restoreInputMessage,0,0)!=FALSE,"Queue paste during worker wait");
  (void)app.await(gate.get_future());app.inputCompletion=nullptr;app.drainPatternPaste();
  restoreCheck(!app.pendingPatternPaste&&note(8)==61,"Queued paste did not apply at its captured target");
  app.edit("history.undo",{{"domain","all"}});restoreCheck(note(8)==0,"Queued paste was not one undoable edit");
  app.edit("history.redo",{{"domain","all"}});restoreCheck(note(8)==61,"Queued paste Redo lost its content");
  app.edit("history.undo",{{"domain","all"}});
  // Clipboard changes do not replace an already captured paste payload.
  app.busy=true;app.pasteCells();app.busy=false;const auto clipboard=app.inspectionClipboard;app.inspectionClipboard="invalid replacement";
  app.drainPatternPaste();restoreCheck(note(8)==61,"Queued paste read a later clipboard instead of its captured payload");
  app.inspectionClipboard=clipboard;app.edit("history.undo",{{"domain","all"}});
  // A busy Copy completes first; the queued Paste may consume only that copy.
  app.copyingPatternClipboard=true;app.busy=true;app.pasteCells();app.busy=false;app.copyingPatternClipboard=false;++app.patternClipboardSerial;
  app.drainPatternPaste();restoreCheck(note(8)==61,"Paste queued behind Copy missed its completed clipboard");
  app.edit("history.undo",{{"domain","all"}});
  app.copyingPatternClipboard=true;app.busy=true;app.pasteCells();app.busy=false;app.copyingPatternClipboard=false;
  app.drainPatternPaste();restoreCheck(note(8)==0&&app.status.find(L"Pending copy")!=std::wstring::npos,"Failed pending Copy pasted older data");
  // Navigation and edits invalidate queued intent instead of retargeting it.
  app.busy=true;app.pasteCells();app.busy=false;cursor["row"]=9;app.navigate(cursor);const auto navigated=songState(app);app.drainPatternPaste();
  restoreCheck(songState(app)==navigated&&app.status.find(L"Paste target changed")!=std::wstring::npos,"Queued paste followed a newer cursor");
  app.busy=true;app.pasteCells();app.busy=false;app.edit("document.patch",{{"title","Newer song revision"}});const auto edited=songState(app);app.drainPatternPaste();
  restoreCheck(songState(app)==edited,"Queued paste bypassed the revision guard");
  app.busy=true;app.pasteCells();app.busy=false;app.edit("document.new",{{"discard",true}});const auto replaced=songState(app);app.drainPatternPaste();
  restoreCheck(songState(app)==replaced,"Queued paste crossed document identity");
}
