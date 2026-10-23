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
    press(window,5003);SetWindowTextW(GetDlgItem(window,5007),L"1");SetWindowTextW(GetDlgItem(window,5008),L"8");if(!before.at("loop").get<bool>())press(window,5006);settle(window);
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
  {
    RestoreFixture fixture;auto &app=*fixture.app;const auto plugin=addRestoreGain(app);
    const auto catalogue=app.documentOperation("plugin.parameters.get",{{"plugin",plugin}});
    const auto numeric=std::find_if(catalogue.begin(),catalogue.end(),[](const auto &p){const auto f=ScreamSeq::PluginParameterField::read(p);return f.editable()&&f.kind==ScreamSeq::PluginParameterField::Kind::Number&&f.minimum<f.maximum;});
    restoreCheck(numeric!=catalogue.end(),"Automation fixture has no numeric parameter");const auto id=numeric->at("id").get<uint32_t>();
    app.openParameterAutomation(plugin,id);auto *curve=app.parameterAutomationWindow.get();ScreamSeq::Tests::ownGuiWindow(curve->window());
    const auto pattern=curve->snapshot().at("pattern");const auto before=app.documentOperation("automation.pattern.get",{{"pattern",pattern}}).at("lanes");
    press(curve->window(),4212);settle(curve->window());
    restoreCheck(!curve->snapshot().at("dirty").get<bool>()&&!curve->snapshot().at("lane").get<std::string>().empty(),"Parameter ramp did not save automatically");
    app.edit("history.undo",{{"domain","all"}});restoreCheck(app.documentOperation("automation.pattern.get",{{"pattern",pattern}}).at("lanes")==before,"Automatic ramp Undo failed");
    curve->hide();app.openAbsoluteAutomation(plugin,id);auto *lane=app.absoluteAutomationWindow.get();const auto window=lane->window();ScreamSeq::Tests::ownGuiWindow(window);
    const auto saved=app.documentOperation("automation.get",Json::object()).at("points");const auto f=ScreamSeq::PluginParameterField::read(*numeric);const auto value=f.minimum+(f.maximum-f.minimum)*.4;
    SetWindowTextW(GetDlgItem(window,4608),L"1.25");SetWindowTextW(GetDlgItem(window,4609),std::to_wstring(value).c_str());press(window,4610);settle(window);
    const auto points=app.documentOperation("automation.get",Json::object()).at("points");
    restoreCheck(points.size()==saved.size()+1&&!lane->snapshot().at("dirty").get<bool>(),"New song automation point needed a second Apply");
    app.edit("history.undo",{{"domain","all"}});restoreCheck(app.documentOperation("automation.get",Json::object()).at("points")==saved,"Automatic song point Undo failed");fixture.close();
  }
  {
    RestoreFixture fixture;auto &app=*fixture.app;const auto plugin=addRestoreGain(app);
    // Original foreground take timestamps: only 507 frames (10.5625 ms)
    // apart. A one-second minimum left the two handles less than 9 DIP apart.
    const auto late=Json::array({{{"frame",7509566},{"value",-6.}},{{"frame",7510073},{"value",-3.}}});
    app.edit("automation.replaceLane",{{"slot",0},{"id",1},{"points",late}});
    app.openAbsoluteAutomation(plugin,1);auto &lane=*app.absoluteAutomationWindow;const auto window=lane.window();ScreamSeq::Tests::ownGuiWindow(window);
    const auto baseline=app.documentOperation("automation.get",Json::object());const auto revision=app.view->session.revision;
    const auto rangeMatches=[&]{const auto state=lane.snapshot();
      restoreCheck(std::abs(std::stod(restoreText(GetDlgItem(window,4627)))-state.at("startSeconds").get<double>())<1e-8&&
        std::abs(std::stod(restoreText(GetDlgItem(window,4628)))-state.at("endSeconds").get<double>())<1e-8,
        "Song automation range fields differ from the displayed viewport");return state;};
    const auto fitted=rangeMatches();
    restoreCheck(fitted.at("startSeconds").get<double>()>156&&fitted.at("startSeconds").get<double>()<7509566./48000&&
      fitted.at("endSeconds").get<double>()>7510073./48000&&fitted.at("endSeconds").get<double>()-fitted.at("startSeconds").get<double>()<.02,
      "A short late recording opened compressed against the entire song's end");
    restoreCheck(fitted.at("handles").size()==2&&
      fitted.at("handles")[1].at("x").get<double>()-fitted.at("handles")[0].at("x").get<double>()>fitted.at("canvas")[2].get<double>()*.8,
      "Fit does not separate nearby recorded points enough to select them");
    for(const int command:{4621,4623,4622,4620,4619}){press(window,command);rangeMatches();}
    SetWindowTextW(GetDlgItem(window,4627),L"150");SetWindowTextW(GetDlgItem(window,4628),L"170");press(window,4629);
    restoreCheck(rangeMatches().at("startSeconds")==150&&rangeMatches().at("endSeconds")==170,"Explicit recorded viewport was not retained");
    press(window,4615);restoreCheck(rangeMatches().at("startSeconds")==150&&rangeMatches().at("endSeconds")==170,"Read-only reload lost the retained viewport");
    press(window,4619);rangeMatches();
    restoreCheck(app.view->session.revision==revision&&app.documentOperation("automation.get",Json::object())==baseline,
      "Recorded viewport navigation wrote music or added history");
    const auto timeField=GetDlgItem(window,4608);SetActiveWindow(GetAncestor(window,GA_ROOT));SetFocus(timeField);
    SetWindowTextW(timeField,L"invalid");SendMessageW(timeField,EM_SETSEL,7,7);SendMessageW(timeField,WM_CHAR,VK_BACK,0);
    restoreCheck(GetFocus()==timeField&&restoreText(timeField)==L"invali"&&app.documentOperation("automation.get",Json::object())==baseline,
      "Song automation text Backspace deleted music or stole field focus");
    SendMessageW(timeField,WM_KEYDOWN,VK_ESCAPE,0);SetFocus(window);
    SendMessageW(window,WM_KEYDOWN,VK_TAB,0);
    const auto selected=lane.snapshot().at("selectedPoint").get<int>();
    restoreCheck(selected>=0,"Song automation canvas did not select a point from the keyboard");
    for(const auto key:{VK_BACK,VK_DELETE}){
      const auto oldPoints=lane.snapshot().at("points");const auto index=lane.snapshot().at("selectedPoint").get<int>();
      auto expected=oldPoints;expected.erase(expected.begin()+index);
      SendMessageW(window,WM_KEYDOWN,key,0);settle(window);
      restoreCheck(GetFocus()==window&&lane.snapshot().at("points")==expected&&!lane.snapshot().at("dirty").get<bool>(),
        "Song automation keyboard deletion did not save the selected point with canvas focus retained");
      app.edit("history.undo",{{"domain","all"}});press(window,4615);
      restoreCheck(app.documentOperation("automation.get",Json::object())==baseline,"Song automation point deletion did not Undo exactly once");
      SetFocus(window);SendMessageW(window,WM_KEYDOWN,VK_TAB,0);
    }
    for(const uint64_t frame:{uint64_t(0),uint64_t(7509566),uint64_t(604800)*48000-1}){
      app.edit("automation.replaceLane",{{"slot",0},{"id",1},{"points",Json::array({{{"frame",frame},{"value",-6.}},{{"frame",frame+1},{"value",-3.}}})}});
      press(window,4615);press(window,4619);const auto oneFrame=rangeMatches();
      restoreCheck(oneFrame.at("handles").size()==2&&oneFrame.at("handles")[1].at("x").get<double>()-oneFrame.at("handles")[0].at("x").get<double>()>oneFrame.at("canvas")[2].get<double>()*.8,
        "Fit collapsed adjacent sample-time automation points");
      const auto beforeZoom=oneFrame.at("endSeconds").get<double>()-oneFrame.at("startSeconds").get<double>();press(window,4621);const auto zoomed=rangeMatches();
      const auto afterZoom=zoomed.at("endSeconds").get<double>()-zoomed.at("startSeconds").get<double>();
      restoreCheck(afterZoom<=beforeZoom+1e-9&&afterZoom>=1./48000-1e-9,"Song-time zoom inherited the pattern quarter-row limit");
    }
    app.edit("automation.replaceLane",{{"slot",0},{"id",1},{"points",Json::array()}});press(window,4615);press(window,4619);
    restoreCheck(rangeMatches().at("startSeconds")==0&&rangeMatches().at("endSeconds")==8,"Empty lane lost its useful initial range");
    app.edit("automation.replaceLane",{{"slot",0},{"id",1},{"points",Json::array({{{"frame",0},{"value",-6.}},{{"frame",uint64_t(604800)*48000},{"value",-3.}}})}});
    press(window,4615);press(window,4619);const auto full=rangeMatches();
    restoreCheck(full.at("startSeconds")==0&&full.at("endSeconds").get<double>()>=604800&&full.at("endSeconds").get<double>()<=604801,
      "Fit clipped a legal song endpoint or escaped the viewport limit");fixture.close();
  }
  std::cout<<"PASS automatic sample settings/loops, instrument properties, mixer fields/toggles, Undo, PCM, focus/caret, invalid and stale edits, recorded viewport navigation\n";
}
