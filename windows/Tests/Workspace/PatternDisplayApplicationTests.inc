void applicationPatternRowHeight() {
  RestoreFixture fixture;auto &app=*fixture.app;app.command(patternCommand);
  app.selectWholePattern();app.openPatternTools();auto *tool=app.patternToolsWindow.get();
  const auto field=GetDlgItem(tool->window(),8006);ScreamSeq::Tests::ownGuiWindow(tool->window());
  rawDraftText(field,L"-");SetFocus(field);
  const auto draft=tool->snapshot().at("draft"),captured=tool->snapshot().at("captured");
  const auto song=songState(app),position=app.position(),selection=app.selection(),viewport=app.workspaceSnapshot().at("viewport");
  const auto context=app.contextRevision;const auto rows=app.visibleRows();
  for(unsigned height:{22u,26u,30u,34u,34u}) {
    app.runWorkspaceShortcut(patternRowsLargerCommand);
    restoreCheck(app.rowHeight==height&&app.workspaceSnapshot().at("geometry").at("pattern").at("rowHeight")==height,
      "Native increase-row-height command or public geometry diverged");
  }
  restoreCheck(app.visibleRows()<rows&&songState(app)==song&&app.position()==position&&app.selection()==selection&&app.contextRevision==context&&
    app.workspaceSnapshot().at("viewport")==viewport&&GetFocus()==field&&tool->snapshot().at("draft")==draft&&tool->snapshot().at("captured")==captured,
    "Row height changed music, input context, viewport, focus or retained raw draft");
  restoreCheck(app.savedLayouts.load(fixture.folder/L"row-height-layouts.json"),"Load isolated row-height layout storage");
  app.workspace("workspace.layout",{{"name","Save custom"},{"savedName","Readable"}});
  for(unsigned height:{30u,26u,22u,18u,18u}) {
    app.runWorkspaceShortcut(patternRowsSmallerCommand);restoreCheck(app.rowHeight==height,"Row-height lower bound failed");
  }
  restoreCheck(app.savedLayouts.reload(),"Reopen saved row-height layout from disk");
  app.workspace("workspace.layout",{{"name","Restore custom"},{"savedName","Readable"}});
  restoreCheck(app.rowHeight==34&&GetFocus()==field&&tool->snapshot().at("draft")==draft&&tool->snapshot().at("captured")==captured,
    "Saved row height did not restore without redirecting the retained inspector");
  const auto valid=app.layoutConfiguration();
  for(const auto &value:{RestoreJson(true),RestoreJson(17),RestoreJson(20),RestoreJson(35),RestoreJson(22.5),RestoreJson("22")}) {
    auto invalid=valid;invalid["patternRowHeight"]=value;bool refused=false;
    try{app.restoreLayoutConfiguration(invalid);}catch(const ScreamSeq::Api::ApiError &error){refused=error.code==-32602;}
    restoreCheck(refused&&app.layoutConfiguration()==valid,"Invalid row-height layout partially applied");
  }
  auto legacy=valid;legacy.erase("patternRowHeight");app.restoreLayoutConfiguration(legacy);
  restoreCheck(app.rowHeight==18,"Legacy layout did not restore its historical row height");
  for(const float height:{18.f,22.f,26.f,30.f,34.f}) {
    app.setPatternRowHeight(height);const auto grid=app.patternGridRect();
    const auto expected=app.firstRow+2;
    restoreCheck(app.gridClick(grid.x+app.gutter+4,grid.y+app.gridHeader+2.5f*height,false)&&app.row==expected,
      "Pattern drawing and native hit-testing use different row heights");
  }
  const auto unchanged=app.rowHeight;SetCapture(app.window);bool refused=false;
  try{app.setPatternRowHeight(18);}catch(const ScreamSeq::Api::ApiError &error){refused=error.code==-32002;}
  ReleaseCapture();restoreCheck(refused&&app.rowHeight==unchanged,"Row resize changed coordinates during mouse capture");
  const auto roots=app.departureRoots();app.departureInput=std::make_unique<ScreamSeq::NativeInputGate>(roots);
  requireDepartureRefusal([&]{app.setPatternRowHeight(18);});app.departureInput.reset();
  restoreCheck(app.rowHeight==unchanged&&songState(app)==song,"Refused density change altered row height or song");
}

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

void applicationPatternRuler() {
  RestoreFixture fixture;auto &app=*fixture.app;app.command(patternCommand);
  const auto song=songState(app),position=app.position(),viewport=app.workspaceSnapshot().at("viewport");
  const auto focus=GetFocus();
  app.workspace("workspace.ruler",{{"mode","beats"}});
  restoreCheck(app.positionMode=="beats"&&app.gutter==72&&app.rulerRowText(4,false)==L"1.000","Beat ruler did not use effective pattern timing");
  restoreCheck(songState(app)==song&&app.position()==position&&GetFocus()==focus&&app.workspaceSnapshot().at("viewport")==viewport,
    "Ruler mode changed song, selection, focus or viewport");
  const auto button=app.controls.at(positionRulerCommand);SetFocus(button);
  app.command(positionRulerCommand);
  restoreCheck(app.positionMode=="patternTime"&&GetFocus()==button&&app.gutter==104,"Native ruler button lost keyboard focus or time width");
  const auto settle=[&]{
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    do {
      app.controller->service();app.servicePatternRuler();
      if(app.rulerSnapshot().at("ready")==true)return;
      if(app.rulerFuture.valid())app.rulerFuture.wait_for(std::chrono::milliseconds(1));
    }while(std::chrono::steady_clock::now()<end);
    throw std::runtime_error("Native ruler did not publish its bounded worker query");
  };
  settle();restoreCheck(app.rulerRowText(0,true)==L"00:00.000","Reached pattern origin did not display zero time");
  app.setPositionMode("songTime");app.servicePatternRuler();
  restoreCheck(app.rulerFuture.valid(),"Time query was not queued asynchronously");
  const auto prior=app.rulerRequestedKey;
  app.edit("document.timing.set",{{"speed",3}});
  app.servicePatternRuler();
  restoreCheck(app.currentRulerKey()!=prior&&app.rulerReadyKey!=prior&&app.rulerSnapshot().at("ready")==false,
    "Late time result from the prior document revision was displayed");
  settle();
  app.edit("order.edit",{{"order",0},{"operation","after"},{"pattern",0}});
  const auto second=app.view->session.document.at("orderMetadata")[1].at("id").get<std::string>();
  app.selectArrangementOrder(second);settle();
  restoreCheck(app.rulerSnapshot().at("order")==1&&app.rulerRowText(0,true)!=L"00:00.000", "Song ruler ignored selected repeated occurrence");
  const auto captured=app.position();app.selectWholePattern();const auto selected=app.selection();
  app.setPositionMode("rows");
  restoreCheck(app.gutter==38&&app.rulerRowText(7,false)==L"007"&&app.position()==captured&&app.selection()==selected,
    "Returning to rows changed captured edit position or selection");
  for(const auto &params:{RestoreJson::object(),RestoreJson{{"mode",1}},RestoreJson{{"mode","seconds"}},RestoreJson{{"mode","rows"},{"extra",true}}}) {
    bool refused=false;try{app.workspace("workspace.ruler",params);}catch(const ScreamSeq::Api::ApiError &error){refused=error.code==-32602;}
    restoreCheck(refused&&app.positionMode=="rows","Invalid ruler mode was not rejected atomically");
  }
  const auto roots=app.departureRoots();app.departureInput=std::make_unique<ScreamSeq::NativeInputGate>(roots);bool refused=false;
  try{app.setPositionMode("beats");}catch(const std::exception &){refused=true;}
  app.departureInput.reset();
  restoreCheck(refused&&app.positionMode=="rows","Departure lease did not protect ruler input");
}
