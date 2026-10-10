// Exercise automatic editing through the real Application and worker APIs.
void applicationAutomaticEdits(){
  using Json=RestoreJson;
  const auto settle=[](HWND window){SendMessageW(window,WM_TIMER,0x5345,0);};
  const auto press=[](HWND window,int id){SendMessageW(window,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),LPARAM(GetDlgItem(window,id)));};
  {
    RestoreFixture fixture;auto &app=*fixture.app;app.openSampleDetail();auto *tool=app.sampleDetailWindow.get();const auto window=tool->window();ScreamSeq::Tests::ownGuiWindow(window);
    const auto before=app.documentOperation("sample.get",{{"sample",1}});const auto pcm=app.documentOperation("sample.pcm.get",{{"sample",1}});
    const auto field=GetDlgItem(window,4857);SetActiveWindow(GetAncestor(window,GA_ROOT));SetFocus(field);
    SetWindowTextW(field,L"48001");SendMessageW(field,EM_SETSEL,2,4);
    restoreCheck(app.documentOperation("sample.get",{{"sample",1}})==before,"Typing changed settings before debounce");settle(window);
    restoreCheck(app.documentOperation("sample.get",{{"sample",1}}).at("rate")==48001,"Sample rate did not save without Apply");
    DWORD first=0,last=0;SendMessageW(field,EM_GETSEL,WPARAM(&first),LPARAM(&last));
    restoreCheck(GetFocus()==field&&first==2&&last==4&&restoreText(field)==L"48001","Automatic sample edit lost raw text, caret or focus");
    app.edit("history.undo",{{"domain","all"}});restoreCheck(app.documentOperation("sample.get",{{"sample",1}})==before,"Automatic sample settings needed more than one Undo");press(window,4803);
    press(window,5003);SetWindowTextW(GetDlgItem(window,5007),L"1");SetWindowTextW(GetDlgItem(window,5008),L"8");press(window,5006);settle(window);
    const auto loop=app.documentOperation("sample.get",{{"sample",1}});restoreCheck(loop.at("loop")==true&&loop.at("loopStart")==1&&loop.at("loopEnd")==8,"Loop controls required a second Apply");
    restoreCheck(app.documentOperation("sample.pcm.get",{{"sample",1}})==pcm,"Loop edit changed PCM");
    app.edit("history.undo",{{"domain","all"}});restoreCheck(app.documentOperation("sample.get",{{"sample",1}})==before,"Automatic loop pair did not Undo atomically");press(window,4803);
    SetWindowTextW(field,L"-.");const auto rev=app.view->session.revision;settle(window);settle(window);
    restoreCheck(app.view->session.revision==rev&&restoreText(field)==L"-.","Invalid sample number changed the song or was retried");fixture.close();
  }
  {
    RestoreFixture fixture;auto &app=*fixture.app;createRestoreInstrument(app);app.openInstrumentEnvelope();auto *tool=app.instrumentEnvelopeWindow.get();const auto window=tool->window();ScreamSeq::Tests::ownGuiWindow(window);
    const auto before=app.documentOperation("instrument.get",{{"instrument",1}});
    SetWindowTextW(GetDlgItem(window,4439),L"Automatic instrument");settle(window);
    restoreCheck(app.documentOperation("instrument.get",{{"instrument",1}}).at("name")=="Automatic instrument"&&!tool->snapshot().at("dirty").get<bool>(),"Instrument property still requires Apply");
    app.edit("history.undo",{{"domain","all"}});restoreCheck(app.documentOperation("instrument.get",{{"instrument",1}})==before,"Instrument property Undo failed");press(window,4451);
    SetWindowTextW(GetDlgItem(window,4439),L"Stale instrument");app.edit("document.patch",{{"title","External edit"}});const auto rev=app.view->session.revision;settle(window);settle(window);
    restoreCheck(app.view->session.revision==rev&&tool->snapshot().at("dirty")==true&&restoreText(GetDlgItem(window,4439))==L"Stale instrument","Automatic property edit rebased a stale draft");fixture.close();
  }
  {
    RestoreFixture fixture;auto &app=*fixture.app;app.edit("mixer.enable",Json::object());app.command(mixerDetailsCommand);
    const auto before=app.documentOperation("mixer.get",Json::object());const auto revision=app.view->session.revision;
    SetWindowTextW(app.controls.at(mixerGain),L"-3.00");app.mixerEditDue=0;app.mixerTimer();
    restoreCheck(!app.mixerDirty&&app.view->session.revision!=revision,"Mixer numeric field did not save automatically");
    app.edit("history.undo",{{"domain","all"}});restoreCheck(app.documentOperation("mixer.get",Json::object())==before,"Automatic mixer change needed more than one Undo");app.mixerControlCommand(mixerReload);
    app.mixerControlCommand(mixerMute);restoreCheck(!app.mixerDirty&&app.mixerDraft.at("mute")==true,"Mixer toggle still requires Apply");
    fixture.close();
  }
  std::cout<<"PASS automatic sample settings/loops, instrument properties, mixer fields/toggles, Undo, PCM, focus/caret, invalid and stale edits\n";
}
