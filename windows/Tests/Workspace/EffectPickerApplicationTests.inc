void applicationEffectPicker() {
  const auto press=[](HWND w,int id){SendMessageW(w,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),LPARAM(GetDlgItem(w,id)));};
  {
    RestoreFixture fixture;auto &app=*fixture.app;app.command(patternCommand);
    auto position=app.position();position["row"]=16;position["channel"]=0;position["column"]=3;position["following"]=false;app.navigate(position);
    const auto before=songState(app),selection=app.selection();SetFocus(app.window);
    restoreCheck(app.key(VK_F4)&&app.effectPickerWindow,"F4 did not expose the near-cursor picker");
    auto *picker=app.effectPickerWindow.get();const auto window=picker->window();ScreamSeq::Tests::ownGuiWindow(window);
    auto descriptor=RestoreJson::object();for(const auto &entry:app.view->commands.at("native"))if(entry.value("native",std::string())=="vibrato")descriptor=entry;
    restoreCheck(!descriptor.empty(),"Fixture has no native vibrato descriptor");
    const auto query=wide(descriptor.at("displayCode").get<std::string>()+"  "+descriptor.at("name").get<std::string>());
    rawDraftText(GetDlgItem(window,8201),query.c_str());
    restoreCheck(picker->snapshot().at("matches")==1&&picker->snapshot().at("current")==true&&songState(app)==before,
      "Native catalogue search lost its exact choice or changed music");
    press(window,8205);
    restoreCheck(!picker->visible()&&app.effectDraftExists&&app.effectDraftDirty&&app.effectDraftRow==16&&app.effectDraftChannel==0&&
      app.effectDraftColumn==0&&app.readEffectFields().at("native")=="vibrato"&&songState(app)==before&&app.position()==position&&app.selection()==selection,
      "Choosing an FX command failed to transfer one retained captured draft or changed music/cursor");
    const auto original=app.documentOperation("pattern.effects.get",{{"pattern",0}});app.command(effectApply);
    const auto applied=app.documentOperation("pattern.effects.get",{{"pattern",0}});
    restoreCheck(applied!=original,"Apply after the picker did not use the existing FX mutation path");
    app.edit("history.undo",{{"domain","all"}});restoreCheck(app.documentOperation("pattern.effects.get",{{"pattern",0}})==original,"Picker Apply was not one Undo");
    app.edit("history.redo",{{"domain","all"}});restoreCheck(app.documentOperation("pattern.effects.get",{{"pattern",0}})==applied,"Picker Apply Redo changed native fields");
  }
  {
    RestoreFixture fixture;auto &app=*fixture.app;app.command(patternCommand);app.openEffectPicker();
    auto *picker=app.effectPickerWindow.get();const auto window=picker->window();ScreamSeq::Tests::ownGuiWindow(window);
    rawDraftText(GetDlgItem(window,8201),L"nudge");const auto captured=picker->snapshot().at("captured");
    auto next=app.position();next["row"]=9;app.navigate(next);const auto song=songState(app);press(window,8205);
    restoreCheck(picker->visible()&&!app.effectDraftExists&&songState(app)==song&&picker->snapshot().at("captured")==captured,
      "Stale picker silently followed navigation or wrote music");
    press(window,8206);restoreCheck(picker->snapshot().at("current")==true&&picker->snapshot().at("search")=="nudge"&&
      picker->snapshot().at("captured").at("position").at("row")==9,"Explicit picker capture lost search or retained an obsolete cell");
    app.openEffectEditor();rawDraftText(app.controls.at(effectValue),L"invalid retained FX");app.effectFieldChanged();
    const auto generation=app.effectDraftGeneration;press(window,8205);app.openEffectPicker();
    restoreCheck(app.effectDraftGeneration==generation&&app.effectField(effectValue)==L"invalid retained FX",
      "Picker replaced a newer invalid inspector draft");
  }
  {
    RestoreFixture fixture;auto &app=*fixture.app;const auto path=saveDepartureFixture(fixture);app.command(patternCommand);app.openEffectPicker();
    const auto window=app.effectPickerWindow->window();ScreamSeq::Tests::ownGuiWindow(window);
    rawDraftText(GetDlgItem(window,8201),L"no-such-effect-phrase");const auto before=songState(app);press(window,8205);
    restoreCheck(app.effectPickerWindow->snapshot().at("matches")==0&&songState(app)==before,"Empty picker results selected an unrelated effect");
    MINMAXINFO limits{};SendMessageW(window,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&limits));
    SetWindowPos(window,nullptr,0,0,limits.ptMinTrackSize.x,limits.ptMinTrackSize.y,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    RECT client{};GetClientRect(window,&client);
    for(int id:{8200,8201,8202,8203,8204,8205,8206,8207}){RECT bounds{};GetWindowRect(GetDlgItem(window,id),&bounds);MapWindowPoints(nullptr,window,reinterpret_cast<POINT *>(&bounds),2);
      restoreCheck(bounds.left>=0&&bounds.top>=0&&bounds.right<=client.right&&bounds.bottom<=client.bottom,"Picker controls clip at minimum size");}
    app.documentOperation("document.open",departureOpenRequest(app,path));
    restoreCheck(!app.effectPickerWindow&&!IsWindow(window),"Document replacement retained a stale picker owner");
  }
}
