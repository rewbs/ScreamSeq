void applicationPatternTools() {
  const auto press=[](HWND window,int id){SendMessageW(window,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),LPARAM(GetDlgItem(window,id)));};
  const auto choose=[](HWND window,int id,int index){const auto control=GetDlgItem(window,id);SendMessageW(control,CB_SETCURSEL,index,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(id,CBN_SELCHANGE),LPARAM(control));};
  const auto checkBox=[](HWND window,int id,bool value){SendMessageW(GetDlgItem(window,id),BM_SETCHECK,value?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),LPARAM(GetDlgItem(window,id)));};
  {
    RestoreFixture fixture;auto &app=*fixture.app;app.command(patternCommand);
    const auto path=saveDepartureFixture(fixture);
    app.edit("pattern.apply",{{"cells",RestoreJson::array({{{"pattern",0},{"row",8},{"channel",0},{"note",61},{"instrument",1}},
      {{"pattern",0},{"row",23},{"channel",0},{"note",65},{"instrument",1}}})}});
    auto position=app.position();position["row"]=23;position["channel"]=0;app.navigate(position);app.setPatternSelection(true,8,0);
    app.command(patternToolsCommand);auto *tool=app.patternToolsWindow.get();restoreCheck(tool!=nullptr,"Pattern tools command is unreachable");
    const auto window=tool->window();ScreamSeq::Tests::ownGuiWindow(window);
    const std::array<const wchar_t *,9> fieldNames{L"Pattern operation",L"Apply to",L"Value to transform",L"Interpolation curve",L"Cells to affect",L"From value",L"To value",L"Amount",L"Random seed"};
    for(size_t i=0;i<fieldNames.size();++i)restoreCheck(ScreamSeq::Tests::accessibleName(GetDlgItem(window,8001+int(i)))==fieldNames[i],"Pattern Tools provider omitted an explicit field name");
    restoreCheck(ScreamSeq::Tests::accessibleName(GetDlgItem(window,8022))==L"Pattern operation preview","Pattern preview lacks a native accessible name");
    const auto before=app.pattern(0).cells;const auto song=songState(app);const auto captured=tool->snapshot().at("captured");
    press(window,8018);restoreCheck(app.pattern(0).cells==before,"Apply without a preview edited music");
    choose(window,8005,2); // All cells, so interpolation fills the captured 8..23 rows.
    press(window,8017);restoreCheck(songState(app)==song&&tool->snapshot().at("applyEnabled")==true,"Transform preview mutated music or remained unavailable");
    const auto expected=app.documentOperation("pattern.transform",tool->snapshot().at("prepared"));
    restoreCheck(tool->snapshot().at("preview")==expected,"Native preview differs from the actual API for identical arguments");
    position["row"]=32;position["channel"]=3;app.navigate(position);tool->hide();app.openPatternTools();
    restoreCheck(app.patternToolsWindow.get()==tool&&tool->snapshot().at("captured")==captured,"Reopening or navigation retargeted prepared transform");
    restoreRejected([&]{app.documentOperation("document.open",departureOpenRequest(app,path));});
    press(window,8018);
    const auto transformed=app.pattern(0).cells;
    restoreCheck(transformed[8*app.view->channels].at(3)==4&&transformed[23*app.view->channels].at(3)==64&&
      app.row==32&&app.channel==3&&tool->snapshot().at("completed")==true,"Interpolation missed endpoints or moved the detached cursor");
    app.edit("history.undo",{{"domain","all"}});restoreCheck(app.pattern(0).cells==before,"Pattern workbench needed more than one Undo");
    app.edit("history.redo",{{"domain","all"}});restoreCheck(app.pattern(0).cells==transformed,"Pattern workbench Redo differs");
    press(window,8016);rawDraftText(GetDlgItem(window,8006),L"-");press(window,8017);
    restoreCheck(tool->snapshot().at("preview").is_null()&&restoreText(GetDlgItem(window,8006))==L"-","Invalid draft was discarded or accepted");
    rawDraftText(GetDlgItem(window,8006),L"4");press(window,8017);app.edit("document.patch",{{"title","Intervening edit"}});
    const auto stale=songState(app);press(window,8018);restoreCheck(songState(app)==stale&&tool->snapshot().at("stale")==true,"Stale preview rebased its revision");
    app.departureChoice=1002;restoreCheck(app.reviewNativeDeparture(),"Pattern tools were not registered for departure");
    app.documentOperation("document.open",departureOpenRequest(app,path));restoreCheck(!app.patternToolsWindow&&!IsWindow(window),"Retired workbench survived document replacement");
  }
  {
    RestoreFixture fixture;auto &app=*fixture.app;app.command(patternCommand);app.setPatternSelection(true,8,0,std::pair{23u,0u});app.openPatternTools();
    auto *tool=app.patternToolsWindow.get();const auto window=tool->window();ScreamSeq::Tests::ownGuiWindow(window);
    choose(window,8005,2);checkBox(window,8015,true); // Explicitly permit structural loss in this catalogue exercise.
    const auto before=songState(app);
    for(unsigned operation=0;operation<SCREAMSEQ_PATTERN_TOOL_COUNT;++operation) {
      choose(window,8001,int(operation));rawDraftText(GetDlgItem(window,8006),operation==6?L"1":L"4");rawDraftText(GetDlgItem(window,8007),operation==6?L"2":L"64");
      rawDraftText(GetDlgItem(window,8008),L"2");press(window,8017);
      restoreCheck(tool->snapshot().at("preview").is_object(),"A catalogue operation produced an invalid native request");
      restoreCheck(songState(app)==before,"Catalogue Preview changed song or history");
    }
    app.edit("track.group",{{"channels",{0,1}},{"name","Tools scope"}});press(window,8016);choose(window,8001,0);
    for(int scope=0;scope<5;++scope){choose(window,8002,scope);press(window,8017);restoreCheck(tool->snapshot().at("preview").is_object(),"A native transform scope failed validation");}
    restoreCheck(tool->snapshot().at("prepared").at("track")==app.noteColumn(0).at("track"),"Note-track scope lost captured stable identity");
    choose(window,8002,0);choose(window,8001,7);checkBox(window,8010,false);checkBox(window,8011,false);checkBox(window,8012,true);checkBox(window,8013,false);press(window,8017);
    restoreCheck(tool->snapshot().at("prepared").at("fields")==RestoreJson::array({"volume"}),"Field mask did not preserve unchecked layers");
    app.sampleMutationCompletion=[&](const std::string &method){if(method=="pattern.transform")rawDraftText(GetDlgItem(window,8008),L"newer incomplete");};
    press(window,8017);app.sampleMutationCompletion={};
    restoreCheck(tool->snapshot().at("preview").is_null()&&restoreText(GetDlgItem(window,8008))==L"newer incomplete","Preview completion overwrote newer settings");
    MINMAXINFO minimum{};SendMessageW(window,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&minimum));
    restoreCheck(SetWindowPos(window,nullptr,0,0,minimum.ptMinTrackSize.x,minimum.ptMinTrackSize.y,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE),"Resize Pattern tools");
    RECT client{};GetClientRect(window,&client);
    for(int id:{8001,8002,8012,8016,8017,8018,8020,8021,8022,8108}){RECT rect{};GetWindowRect(GetDlgItem(window,id),&rect);MapWindowPoints(nullptr,window,reinterpret_cast<POINT *>(&rect),2);
      restoreCheck(rect.left>=0&&rect.top>=0&&rect.right<=client.right&&rect.bottom<=client.bottom,"Pattern tools control clipped at minimum size");}
  }
  {
    RestoreFixture fixture;auto &app=*fixture.app;app.openPatternTools();auto *tool=app.patternToolsWindow.get();const auto window=tool->window();ScreamSeq::Tests::ownGuiWindow(window);
    choose(window,8005,2);press(window,8017);unsigned writes=0;
    app.sampleMutationCompletion=[&](const std::string &method){if(method=="pattern.transform"){
      ++writes;rawDraftText(GetDlgItem(window,8006),L"Newer raw setting");throw std::runtime_error("Owned pattern transform completion failure");}};
    press(window,8018);restoreCheck(writes==1&&tool->snapshot().at("completion").at("returned")==true,"Committed transform result was not retained");
    app.sampleMutationCompletion={};press(window,8018);
    restoreCheck(writes==1&&tool->snapshot().at("completion").is_null()&&tool->snapshot().at("dirty")==true&&restoreText(GetDlgItem(window,8006))==L"Newer raw setting",
      "Review repeated the transform or discarded later text");
  }
  {
    RestoreFixture fixture;auto &app=*fixture.app;app.command(patternCommand);
    app.edit("pattern.apply",{{"cells",RestoreJson::array({{{"pattern",0},{"row",31},{"channel",3},{"note",0}},
      {{"pattern",0},{"row",32},{"channel",3},{"note",0}}})}});
    app.edit("pattern.notes.set",{{"pattern",0},{"events",RestoreJson::array({{{"channel",3},{"position",31*65536+123},{"note",61}}})}});
    auto position=app.position();position["row"]=31;position["channel"]=3;app.navigate(position);app.setPatternSelection(true,31,3,std::pair{32u,3u});
    app.openPatternTools();auto *tool=app.patternToolsWindow.get();const auto window=tool->window();ScreamSeq::Tests::ownGuiWindow(window);
    const auto before=app.documentOperation("pattern.notes.get",{{"pattern",0}});const auto cells=app.pattern(0).cells;
    choose(window,8001,7);checkBox(window,8011,false);checkBox(window,8012,false);checkBox(window,8013,false);press(window,8017);
    restoreCheck(tool->snapshot().at("preview").at("changedCells")==0&&tool->snapshot().at("preview").at("effectsChanged")==true&&
      tool->snapshot().at("applyEnabled")==true,"Native-only transform Preview left Apply disabled");
    press(window,8018);const auto after=app.documentOperation("pattern.notes.get",{{"pattern",0}});
    restoreCheck(after.at("events").at(0).at("note")==61&&after.at("events").at(0).at("position")==32*65536+123&&
      app.pattern(0).cells==cells,"Native-only Apply missed precise notes or changed module cells");
    app.edit("history.undo",{{"domain","all"}});restoreCheck(app.documentOperation("pattern.notes.get",{{"pattern",0}})==before,"Native-only workbench Undo lost precise notes");
    app.edit("history.redo",{{"domain","all"}});restoreCheck(app.documentOperation("pattern.notes.get",{{"pattern",0}})==after,"Native-only workbench Redo lost precise notes");
  }
  {
    RestoreFixture fixture;auto &app=*fixture.app;unsigned writes=0;
    ScreamSeq::PatternToolsWindow tool(app.window,{[&]{return app.patternToolsContext();},
      [&](const auto &method,const auto &params){return app.documentOperation(method,params);},
      [&](const auto &method,const auto &params,const auto &)->ScreamSeq::Api::CompletedCall{++writes;(void)app.documentOperation(method,params);throw std::runtime_error("Owned unknown transform outcome");},[]{} });
    tool.open();const auto window=tool.window();ScreamSeq::Tests::ownGuiWindow(window);choose(window,8005,2);press(window,8017);press(window,8018);
    restoreCheck(writes==1&&tool.snapshot().at("completion").at("returned")==false,"Unknown transform fixture retained unexpected completion evidence");
    press(window,8018);restoreCheck(writes==1&&tool.snapshot().at("observation").is_object(),"Unknown transform Review repeated the write");
    app.edit("document.patch",{{"title","Changed since Review"}});press(window,8019);restoreCheck(!tool.snapshot().at("completion").is_null(),"Changed song bypassed Review admission");
    press(window,8018);press(window,8019);restoreCheck(writes==1&&tool.snapshot().at("completion").is_null()&&tool.snapshot().at("completed")==true,"Accepting observed state replayed the transform");
  }
}
