void applicationKeyboardSettings() {
    RestoreFixture fixture;auto &app=*fixture.app;const auto original=songState(app);
    const auto press=[](HWND owner,int id){SendMessageW(owner,WM_COMMAND,id,reinterpret_cast<LPARAM>(GetDlgItem(owner,id)));};
    app.command(keyboardSettingsCommand);auto *tool=app.keyboardSettingsWindow.get();const auto owner=tool->window();ScreamSeq::Tests::ownGuiWindow(owner);
    rawDraftText(GetDlgItem(owner,8701),L"incomplete");press(owner,8704);
    restoreCheck(songState(app)==original&&app.workspaceShortcuts->noteKeys()==ScreamSeq::MusicalKeyMap{}&&restoreText(GetDlgItem(owner,8701))==L"incomplete","Invalid keyboard draft changed song/preferences or was overwritten");
    SetFocus(GetDlgItem(owner,8701));app.command(keyboardSettingsCommand);
    restoreCheck(GetFocus()==GetDlgItem(owner,8701)&&app.keyboardSettingsWindow.get()==tool&&restoreText(GetDlgItem(owner,8701))==L"incomplete","Reopening keyboard settings discarded its raw text/focus");
    rawDraftText(GetDlgItem(owner,8701),L"FSXDCVGBHNJM");const auto transport=GetDlgItem(owner,8703);SendMessageW(transport,CB_SETCURSEL,1,0);press(owner,8704);
    restoreCheck(songState(app)==original&&app.keyboardSettings().at("playStopKeys")==RestoreJson::array({"return"}),"Applying keyboard preferences changed musical state or failed to assign Return");
    const auto fKey=MapVirtualKeyExW(0x21,MAPVK_VSC_TO_VK_EX,GetKeyboardLayout(0));
    restoreCheck(fKey&&app.musicalOffset(fKey)==0,"Custom physical F did not map to lower C");
    auto position=app.position();position["row"]=7;position["channel"]=0;position["column"]=0;position["following"]=false;app.navigate(position);
    app.returnToSongPattern();const auto following=app.follow;const auto pitch=1+app.octave*12;
    app.handleKey(fKey);restoreCheck(app.view->cell(0,7,0).note==pitch&&app.follow==following&&app.typedNotes.contains(fKey),"Mapped F toggled Follow or failed to enter its note");
    const auto held=app.typedNotes.at(fKey).note;const auto beforeMapping=songState(app);
    const auto settings=app.keyboardSettings();app.setKeyboardSettings({{"expectedKeyboard",settings.at("revision")},{"lower","ASXDCVGBHNJM"},{"upper",settings.at("upper")},{"playStopKeys",settings.at("playStopKeys")}});
    restoreCheck(songState(app)==beforeMapping&&app.typedNotes.at(fKey).note==held&&app.releaseTypedKey(fKey)&&app.typedNotes.empty(),"Mapping change changed musical data or lost the original held-key release");
    app.openKeyboardSettings();rawDraftText(GetDlgItem(owner,8701),L"ZSXDCVGBHNJM");press(owner,8704);
    restoreCheck(app.workspaceShortcuts->noteKeys().lower=="ASXDCVGBHNJM"&&restoreText(GetDlgItem(owner,8701))==L"ZSXDCVGBHNJM","Stale keyboard form overwrote newer preferences or discarded text");
    press(owner,8705);restoreCheck(restoreText(GetDlgItem(owner,8701))==L"ASXDCVGBHNJM","Explicit preferences reload did not refresh fields");
    MINMAXINFO limits{};SendMessageW(owner,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&limits));SetWindowPos(owner,nullptr,0,0,limits.ptMinTrackSize.x,limits.ptMinTrackSize.y,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    RECT client{};GetClientRect(owner,&client);for(int id:{8701,8702,8703,8704,8705,8706,8707,8750,8751,8752,8753,8754,8755}){RECT bounds{};GetWindowRect(GetDlgItem(owner,id),&bounds);MapWindowPoints(nullptr,owner,reinterpret_cast<POINT *>(&bounds),2);
        restoreCheck(bounds.left>=0&&bounds.top>=0&&bounds.right<=client.right&&bounds.bottom<=client.bottom,"Keyboard settings control clips at minimum size");}
    // This is an application preference draft, not a song draft: it survives
    // replacement while the musical owners retire through their existing lease.
    rawDraftText(GetDlgItem(owner,8701),L"Retain this preference draft");tool->hide();
    app.documentOperation("document.new",{{"expectedRevision",app.view->session.revision},{"discard",true}});
    restoreCheck(app.keyboardSettingsWindow.get()==tool&&IsWindow(owner)&&restoreText(GetDlgItem(owner,8701))==L"Retain this preference draft","Song replacement discarded a global keyboard preference draft");

    std::vector<RestoreJson> calls;ScreamSeq::MusicalKeyMap mapping=ScreamSeq::MusicalKeyMap::validated("FSXDCVGBHNJM","Q2W3ER5T6Y7UI");
    // A deterministic native piano host records note-on/off parameters; this
    // qualifies input ownership, not the audio device or rendered voice.
    ScreamSeq::AuditionWindow piano(app.window,[&](const auto &,const RestoreJson &params){calls.push_back(params);return RestoreJson{{"epoch",1}};},
        [&]{return ScreamSeq::AuditionWindow::Context{app.documentId,app.view->session.revision,1,0,1,original.at("document").at("samples"),RestoreJson::array()};},[&]{return mapping;});
    piano.openAt(true);ScreamSeq::Tests::ownGuiWindow(piano.window());SetFocus(piano.window());SendMessageW(piano.window(),WM_KEYDOWN,fKey,0);
    mapping=ScreamSeq::MusicalKeyMap::validated("ASXDCVGBHNJM","Q2W3ER5T6Y7UI");piano.refreshKeyboard();SendMessageW(piano.window(),WM_KEYUP,fKey,0);
    restoreCheck(calls.size()==2&&calls[0].at("on")==true&&calls[1].at("on")==false&&calls[0].at("note")==calls[1].at("note")&&piano.snapshot().at("held").empty(),"Native audition used different mapping/release semantics from musical input");
}
