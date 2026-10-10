void applicationPatternDisplay() {
  RestoreFixture fixture;auto &app=*fixture.app;app.command(patternCommand);
  const auto original=app.patternGridTiming();
  app.edit("document.timing.set",{{"rowsPerBeat",3},{"rowsPerMeasure",10},{"groove",RestoreJson::array()}});
  const RestoreJson timing={{"rowsPerBeat",3},{"rowsPerMeasure",10}};
  restoreCheck(app.patternGridTiming()==timing&&app.workspaceSnapshot().at("gridTiming")==timing&&
    app.graphLanesSnapshot().at("gridTiming")==timing,"Pattern and graph lanes did not publish the same effective beat/bar metrics");
  const auto song=songState(app),position=app.position();
  Tracker::SignalCommand command;command.kind=Tracker::SignalCommandKind::Start;command.amount=.7;
  restoreCheck(app.graphCommandText(&command)==L"S000 B2"&&command.amount==.7,"F04 Start display rounded or changed its stored value");
  command.kind=Tracker::SignalCommandKind::Amount;command.amount=.35;
  restoreCheck(app.graphCommandText(&command)==L"A000 59"&&command.amount==.35,"F04 Amount display rounded or changed its stored value");
  command.kind=Tracker::SignalCommandKind::Wet;command.wet=.35;command.amount=.7;
  restoreCheck(app.graphCommandText(&command)==L"W000 59"&&command.wet==.35&&command.amount==.7,
    "Wet display used Amount instead of the independent wet value");
  restoreCheck(songState(app)==song&&app.position()==position,"Display projection edited music or navigation");
  app.edit("history.undo",{{"domain","all"}});
  restoreCheck(app.patternGridTiming()==original,"Timing Undo left stale native grid metrics");
}
