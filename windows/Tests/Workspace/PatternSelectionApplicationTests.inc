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
