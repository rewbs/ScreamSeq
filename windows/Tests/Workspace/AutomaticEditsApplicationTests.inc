// Exercise automatic editing through the real Application and worker APIs.
void applicationAutomaticEdits(){
  using Json=RestoreJson;
  const auto settle=[](HWND window){SendMessageW(window,WM_TIMER,0x5345,0);};
  const auto press=[](HWND window,int id){SendMessageW(window,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),LPARAM(GetDlgItem(window,id)));};
  {
    RestoreFixture fixture;auto &app=*fixture.app;const auto plugin=addRestoreGain(app);
    app.edit("automation.replaceLane",{{"slot",0},{"id",1},{"points",Json::array({{{"frame",48000},{"value",-6.}}})}});
    app.openAbsoluteAutomation(plugin,1);auto &lane=*app.absoluteAutomationWindow;const auto window=lane.window();ScreamSeq::Tests::ownGuiWindow(window);
    const auto baseline=app.documentOperation("automation.get",Json::object());const auto revision=app.view->session.revision;
    const auto field=GetDlgItem(window,4609);SetActiveWindow(GetAncestor(window,GA_ROOT));SetFocus(field);
    SetWindowTextW(field,L"-9.000");SendMessageW(field,EM_SETSEL,1,3);
    app.busy=true;settle(window);settle(window);app.busy=false;
    DWORD first=0,last=0;SendMessageW(field,EM_GETSEL,WPARAM(&first),LPARAM(&last));
    restoreCheck(app.view->session.revision==revision&&lane.queuedMusicalEdit()&&lane.snapshot().at("fieldDraft").get<bool>()&&
      restoreText(field)==L"-9.000"&&GetFocus()==field&&first==1&&last==3,
      "Busy owner consumed a queued recorded edit or changed raw text/focus before admission");
    settle(window);auto expected=baseline;expected["points"][0]["value"]=-9.;
    restoreCheck(app.documentOperation("automation.get",Json::object())==expected&&!lane.queuedMusicalEdit(),
      "Unsent recorded point did not save automatically when its owner became available");
    settle(window);app.edit("history.undo",{{"domain","all"}});press(window,4615);
    restoreCheck(app.documentOperation("automation.get",Json::object())==baseline,"Deferred recorded point created more than one history step");
    SetWindowTextW(field,L"-12");app.busy=true;settle(window);app.busy=false;
    // Simulate a newer accepted document while the intent was waiting. It must
    // fail the existing revision guard, not rebase onto that new document.
    app.edit("document.patch",{{"title","Newer song"}});const auto newerRevision=app.view->session.revision;settle(window);const auto stale=lane.snapshot();
    restoreCheck(stale.at("fieldDraft").get<bool>()||stale.at("dirty").get<bool>(),"Stale unsent edit was discarded");
    restoreCheck(!lane.queuedMusicalEdit()&&restoreText(field)==L"-12"&&app.view->session.revision==newerRevision&&
      app.documentOperation("automation.get",Json::object())==baseline,"Stale intent was silently retried, rewritten or published");
    SendMessageW(window,WM_KEYDOWN,VK_ESCAPE,0);fixture.close();
  }
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
    for(const auto [control,name]:std::initializer_list<std::pair<int,const wchar_t *>>{
        {4601,L"Recorded automation plugin"},{4602,L"Search recorded parameters"},{4603,L"Recorded automation parameters"},
        {4604,L"Point time units"},{4630,L"Recorded timeline snap"},{4605,L"Point index, zero based"},
        {4608,L"Point time"},{4609,L"Point value, native units"},{4627,L"Visible start, seconds"},{4628,L"Visible end, seconds"},
        {4606,L"Previous recorded point"},{4607,L"Next recorded point"},{4622,L"Pan recorded timeline left"},
        {4623,L"Pan recorded timeline right"},{4620,L"Zoom recorded timeline out"},{4621,L"Zoom recorded timeline in"}})
      restoreCheck(ScreamSeq::Tests::accessibleName(GetDlgItem(window,control))==name,"Recorded editor control lacks a stable native accessible name");
    const std::vector<int> tabOrder{4601,4616,4617,4618,4625,4602,4603,4622,4620,4619,4621,4623,
      4627,4628,4629,4630,4605,4626,4606,4607,4604,4608,4609,4610,4611,4612,4615,4631,4632,4613,4633,4624};
    for(size_t i=0;i<tabOrder.size();++i){const auto control=GetDlgItem(window,tabOrder[i]);
      restoreCheck(GetNextDlgTabItem(window,control,FALSE)==GetDlgItem(window,tabOrder[(i+1)%tabOrder.size()])&&
        GetNextDlgTabItem(window,control,TRUE)==GetDlgItem(window,tabOrder[(i+tabOrder.size()-1)%tabOrder.size()]),
        "Recorded editor native forward/reverse traversal differs from its visible groups");}
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
    {
      // Moving to another point immediately after typing must finish the old
      // point, preserve the requested destination and add exactly one Undo.
      const auto valueField=GetDlgItem(window,4609),indexField=GetDlgItem(window,4605);
      for(const int command:{4626,4606,4607,4611}){
        const int start=command==4606?1:0;
        SetWindowTextW(indexField,start?L"1":L"0");press(window,4626);
        SetFocus(valueField);SetWindowTextW(valueField,L"-9");
        if(command==4626)SetWindowTextW(indexField,L"1");
        SetFocus(GetDlgItem(window,command));press(window,command);
        auto expected=late;expected[size_t(start)]["value"]=-9.;
        const auto state=lane.snapshot();
        restoreCheck(state.at("points")==expected&&!state.at("dirty").get<bool>()&&!state.at("fieldDraft").get<bool>()&&
          state.at("selectedPoint")== (command==4611?-1:command==4606?0:1)&&
          GetFocus()==GetDlgItem(window,command==4611?4608:command),
          "Point navigation lost a queued edit, requested destination or native focus");
        app.edit("history.undo",{{"domain","all"}});press(window,4615);
        restoreCheck(app.documentOperation("automation.get",Json::object())==baseline,"Point navigation did not preserve unrelated points and exact one-step Undo");
      }
      SetWindowTextW(indexField,L"0");press(window,4626);
      SetFocus(valueField);SetWindowTextW(valueField,L"invalid");
      for(const int command:{4626,4606,4607,4611}){
        SetWindowTextW(indexField,L"1");press(window,command);
        restoreCheck(lane.snapshot().at("selectedPoint")==0&&restoreText(valueField)==L"invalid"&&
          app.documentOperation("automation.get",Json::object())==baseline,"Point navigation discarded or retried a rejected raw draft");
      }
      SendMessageW(window,WM_KEYDOWN,VK_ESCAPE,0);
    }
    {
      const auto valueField=GetDlgItem(window,4609),indexField=GetDlgItem(window,4605);
      SetWindowTextW(indexField,L"0");press(window,4626);
      const auto clickPoint=[&](size_t index){const auto handle=lane.snapshot().at("handles")[index];const auto scale=GetDpiForWindow(window)/96.;
        const auto x=int(std::lround(handle.at("x").get<double>()*scale)),y=int(std::lround(handle.at("y").get<double>()*scale));
        const auto xy=MAKELPARAM(x,y);
        SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,xy);
        SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,xy);
        SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(x+1,y-1));
        SendMessageW(window,WM_MOUSEMOVE,0,MAKELPARAM(x+20,y-20));
        SendMessageW(window,WM_LBUTTONUP,0,xy);};
      const auto clickRevision=app.view->session.revision;
      clickPoint(1);lane.flushForProjectSave();
      restoreCheck(lane.snapshot().at("selectedPoint")==1&&lane.snapshot().at("points")==late&&
        app.view->session.revision==clickRevision&&app.documentOperation("automation.get",Json::object())==baseline,
        "Selecting a recorded point snapped its time/value or added history from stationary/jitter mouse moves");
      SetWindowTextW(indexField,L"0");press(window,4626);
      SetFocus(valueField);SetWindowTextW(valueField,L"-9");clickPoint(1);
      auto expected=late;expected[0]["value"]=-9.;
      restoreCheck(lane.snapshot().at("selectedPoint")==1&&lane.snapshot().at("points")==expected&&GetFocus()==window&&
        !lane.snapshot().at("fieldDraft").get<bool>()&&!lane.snapshot().at("dirty").get<bool>(),
        "A timeline click ignored a valid pending field or selected the wrong point");
      SetFocus(valueField);SetWindowTextW(valueField,L"invalid");clickPoint(0);
      restoreCheck(lane.snapshot().at("selectedPoint")==1&&lane.snapshot().at("points")==expected&&
        restoreText(valueField)==L"invalid"&&GetFocus()==valueField,
        "Timeline selection bypassed an invalid raw point or stole its field focus");
      SendMessageW(window,WM_KEYDOWN,VK_ESCAPE,0);app.edit("history.undo",{{"domain","all"}});press(window,4615);
      restoreCheck(app.documentOperation("automation.get",Json::object())==baseline,"Clicking after a field edit added history or changed unrelated points");
      const auto handle=lane.snapshot().at("handles")[1];const auto scale=GetDpiForWindow(window)/96.;
      const auto x=int(std::lround(handle.at("x").get<double>()*scale)),y=int(std::lround(handle.at("y").get<double>()*scale));
      const auto origin=MAKELPARAM(x,y),destination=MAKELPARAM(x-int(20*scale),y-int(20*scale));
      SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,origin);SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,destination);
      restoreCheck(lane.snapshot().at("points")!=late,"Recorded point drag stopped rendering after the native drag threshold");
      SendMessageW(window,WM_LBUTTONUP,0,destination);lane.flushForProjectSave();
      const auto dragged=app.documentOperation("automation.get",Json::object());
      restoreCheck(dragged!=baseline&&!lane.snapshot().at("dirty").get<bool>(),"Recorded drag did not automatically publish its changed point");
      app.edit("history.undo",{{"domain","all"}});press(window,4615);
      restoreCheck(app.documentOperation("automation.get",Json::object())==baseline,"Recorded drag did not restore exact unrelated music in one Undo");
      app.edit("history.redo",{{"domain","all"}});press(window,4615);
      restoreCheck(app.documentOperation("automation.get",Json::object())==dragged,"Recorded drag Redo did not restore the accepted lane");
      app.edit("history.undo",{{"domain","all"}});press(window,4615);
    }
    {
      const auto list=GetDlgItem(window,4603),valueField=GetDlgItem(window,4609),units=GetDlgItem(window,4604);
      const auto catalogue=app.documentOperation("plugin.parameters.get",{{"plugin",plugin}});
      const auto parameterRow=[&](uint32_t id){const auto it=std::find_if(catalogue.begin(),catalogue.end(),[&](const auto &p){return p.at("id")==id;});
        restoreCheck(it!=catalogue.end(),"Recorded navigation fixture target is missing from its actual catalogue");return int(it-catalogue.begin());};
      const auto selectParameter=[&](uint32_t id){SetFocus(list);SendMessageW(list,LB_SETCURSEL,parameterRow(id),0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(4603,LBN_SELCHANGE),LPARAM(list));};
      SetFocus(valueField);SetWindowTextW(valueField,L"-9");const auto edited=lane.snapshot().at("selectedPoint").get<int>();
      auto expected=late;expected[size_t(edited)]["value"]=-9.;
      selectParameter(2);
      restoreCheck(lane.snapshot().at("parameter")==2&&!lane.snapshot().at("dirty").get<bool>()&&GetFocus()==list,
        "Parameter navigation did not flush a valid automatic edit or retain its requested target/focus");
      selectParameter(1);
      restoreCheck(lane.snapshot().at("points")==expected,"Parameter navigation lost the latest field edit or changed unrelated points");
      app.edit("history.undo",{{"domain","all"}});press(window,4615);
      restoreCheck(app.documentOperation("automation.get",Json::object())==baseline,"Navigation-created edit was not exactly one Undo");
      SetFocus(valueField);SetWindowTextW(valueField,L"invalid");selectParameter(2);
      restoreCheck(lane.snapshot().at("parameter")==1&&restoreText(valueField)==L"invalid"&&SendMessageW(list,LB_GETCURSEL,0,0)==parameterRow(1)&&
        app.documentOperation("automation.get",Json::object())==baseline,"Parameter navigation dropped invalid raw input or redirected its draft");
      SendMessageW(units,CB_SETCURSEL,1,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(4604,CBN_SELCHANGE),LPARAM(units));
      restoreCheck(SendMessageW(units,CB_GETCURSEL,0,0)==0&&restoreText(valueField)==L"invalid"&&
        app.documentOperation("automation.get",Json::object())==baseline,"Unit navigation accepted invalid raw point fields");
      for(const int command:{4625,4616,4631}){
        press(window,command);
        restoreCheck(lane.snapshot().at("parameter")==1&&restoreText(valueField)==L"invalid"&&
          app.documentOperation("automation.get",Json::object())==baseline,"Target navigation or Undo discarded invalid pending input");
      }
      SendMessageW(window,WM_KEYDOWN,VK_ESCAPE,0);
      SetFocus(valueField);SetWindowTextW(valueField,L"-12");press(window,4631);
      restoreCheck(!lane.snapshot().at("dirty").get<bool>()&&app.documentOperation("automation.get",Json::object())==baseline,
        "Immediate Undo rejected or failed to undo the pending automatic field edit");
      SetFocus(valueField);SetWindowTextW(valueField,L"-15");SetFocus(units);SendMessageW(units,CB_SETCURSEL,1,0);
      SendMessageW(window,WM_COMMAND,MAKEWPARAM(4604,CBN_SELCHANGE),LPARAM(units));
      restoreCheck(SendMessageW(units,CB_GETCURSEL,0,0)==1&&!lane.snapshot().at("dirty").get<bool>()&&GetFocus()==units&&
        std::stod(restoreText(timeField))==lane.snapshot().at("points")[size_t(lane.snapshot().at("selectedPoint").get<int>())].at("frame").get<double>(),
        "Unit change lost its requested format or failed to flush the latest valid point");
      app.edit("history.undo",{{"domain","all"}});press(window,4615);
      restoreCheck(app.documentOperation("automation.get",Json::object())==baseline,"Unit-change flush changed unrelated music or history");
      SetFocus(valueField);SetWindowTextW(valueField,L"-18");app.edit("document.patch",{{"title","External edit before lane navigation"}});
      const auto staleRevision=app.view->session.revision;selectParameter(2);
      restoreCheck(lane.snapshot().at("parameter")==1&&lane.snapshot().at("dirty").get<bool>()&&lane.snapshot().at("stale").get<bool>()&&
        app.view->session.revision==staleRevision&&app.documentOperation("automation.get",Json::object())==baseline,
        "Parameter navigation rebased a stale field edit or redirected its retained draft");
      press(window,4615);
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
  {
    RestoreFixture fixture;auto &app=*fixture.app;const auto plugin=addRestoreGain(app);
    const auto point=Json::array({{{"frame",7509566},{"value",double(.2f)}}});
    app.edit("automation.replaceLane",{{"slot",0},{"id",1},{"points",point}});app.openAbsoluteAutomation(plugin,1);
    auto &lane=*app.absoluteAutomationWindow;const auto window=lane.window();ScreamSeq::Tests::ownGuiWindow(window);
    const auto field=GetDlgItem(window,4609),time=GetDlgItem(window,4608);
    restoreCheck(restoreText(field)==L"0.2","Recorded point value exposes promoted float noise");
    const auto revision=app.view->session.revision;const auto baseline=app.documentOperation("automation.get",Json::object());
    press(window,4610);settle(window);
    restoreCheck(app.view->session.revision==revision&&app.documentOperation("automation.get",Json::object())==baseline,
      "Accepting generated recorded-point text changed its scalar, timestamp or history");
    SetActiveWindow(GetAncestor(window,GA_ROOT));SetFocus(time);SetWindowTextW(time,L"156.45");settle(window);
    restoreCheck(lane.snapshot().at("points")[0].at("frame")==7509600&&lane.snapshot().at("points")[0].at("value")==double(.2f),
      "Editing only recorded time changed the formatted parameter value");
    SetFocus(field);SetWindowTextW(field,L"-0.30000");SendMessageW(field,EM_SETSEL,2,5);settle(window);
    DWORD first=0,last=0;SendMessageW(field,EM_GETSEL,WPARAM(&first),LPARAM(&last));
    restoreCheck(restoreText(field)==L"-0.30000"&&GetFocus()==field&&first==2&&last==5,
      "Recorded-point formatter rewrote active raw input or its caret");
    fixture.close();
  }
  for(const int command:{4612,4613}){
    RestoreFixture fixture;auto &app=*fixture.app;const auto plugin=addRestoreGain(app);
    const auto points=Json::array({{{"frame",48000},{"value",-6.}},{{"frame",96000},{"value",-3.}}});
    const auto other=Json::array({{{"frame",24000},{"value",25.}}});
    app.edit("automation.replaceLane",{{"slot",0},{"id",1},{"points",points}});
    app.edit("automation.replaceLane",{{"slot",0},{"id",2},{"points",other}});
    const auto baseline=app.documentOperation("automation.get",Json::object());
    app.openAbsoluteAutomation(plugin,1);auto &lane=*app.absoluteAutomationWindow;const auto window=lane.window();ScreamSeq::Tests::ownGuiWindow(window);
    const auto field=GetDlgItem(window,4609);
    SetFocus(field);SetWindowTextW(field,L"-9");press(window,command);settle(window);
    auto edited=baseline;for(auto &p:edited["points"])if(p.at("id")==1&&p.at("frame")==48000)p["value"]=-9.;
    auto removed=edited;auto &remaining=removed["points"];
    remaining.erase(std::remove_if(remaining.begin(),remaining.end(),[&](const auto &p){return p.at("id")==1&&(command==4613||p.at("frame")==48000);}),remaining.end());
    // Inventory metadata changes with point count; compare exact musical points
    // and then the complete snapshots at each chronological Undo boundary.
    restoreCheck(app.documentOperation("automation.get",Json::object()).at("points")==remaining&&
      !lane.snapshot().at("dirty").get<bool>()&&!lane.snapshot().at("fieldDraft").get<bool>(),
      "Delete or Clear lane rejected a valid queued field or changed another lane");
    app.edit("history.undo",{{"domain","all"}});
    restoreCheck(app.documentOperation("automation.get",Json::object())==edited,"Undo deletion did not restore the accepted field edit and other lane exactly");
    app.edit("history.undo",{{"domain","all"}});press(window,4615);
    restoreCheck(app.documentOperation("automation.get",Json::object())==baseline,"Undo field edit did not restore the original lanes exactly");
    SetFocus(field);SetWindowTextW(field,L"invalid");press(window,command);settle(window);
    restoreCheck(restoreText(field)==L"invalid"&&lane.snapshot().at("fieldDraft").get<bool>()&&
      app.documentOperation("automation.get",Json::object())==baseline,"Delete or Clear lane discarded invalid input or changed accepted music");
    SendMessageW(window,WM_KEYDOWN,VK_ESCAPE,0);fixture.close();
  }
  {
    RestoreFixture fixture;auto &app=*fixture.app;const auto plugin=addRestoreGain(app);
    const auto points=Json::array({{{"frame",48000},{"value",-6.}},{{"frame",96000},{"value",-3.}}});
    app.edit("automation.replaceLane",{{"slot",0},{"id",1},{"points",points}});
    const auto path=saveDepartureFixture(fixture);app.openAbsoluteAutomation(plugin,1);
    auto &lane=*app.absoluteAutomationWindow;const auto window=lane.window();ScreamSeq::Tests::ownGuiWindow(window);
    const auto field=GetDlgItem(window,4609);SetActiveWindow(GetAncestor(window,GA_ROOT));SetFocus(field);
    const auto baseline=app.documentOperation("automation.get",Json::object());
    SetWindowTextW(field,L"-9");SendMessageW(field,EM_SETSEL,1,2);
    restoreCheck(app.queuedMusicalEdit()&&!app.canRedoSong(),"Queued recorded point was absent from native history availability");
    restoreCheck(app.saveFile(),"Native Save did not finish the queued recorded point");
    const auto saved=app.documentOperation("automation.get",Json::object());
    auto expected=points;expected[0]["value"]=-9.;
    DWORD first=0,last=0;SendMessageW(field,EM_GETSEL,WPARAM(&first),LPARAM(&last));
    restoreCheck(lane.snapshot().at("points")==expected&&!lane.snapshot().at("dirty").get<bool>()&&!app.view->dirty&&
      GetFocus()==field&&first==1&&last==2&&restoreText(field)==L"-9",
      "Native Save lost the latest recorded value, raw text, selection or focus");
    const auto savedBytes=ScreamSeq::Project::readProjectBytes(path);
    SetWindowTextW(field,L"invalid");const auto revision=app.view->session.revision;
    restoreRejected([&]{app.saveFile();});
    restoreCheck(app.view->session.revision==revision&&app.documentOperation("automation.get",Json::object())==saved&&
      ScreamSeq::Project::readProjectBytes(path)==savedBytes&&restoreText(field)==L"invalid",
      "Invalid pending recorded value changed the saved song or discarded its raw draft");
    restoreRejected([&]{app.edit("history.undo",{{"domain","all"}});});
    restoreCheck(app.view->session.revision==revision&&restoreText(field)==L"invalid","Song Undo bypassed an invalid recorded draft");
    // Repeated Save may save accepted music, but cannot resubmit the failed edit.
    restoreCheck(app.saveFile()&&app.view->session.revision==revision&&restoreText(field)==L"invalid",
      "Repeated Save retried a rejected recorded edit");
    SendMessageW(window,WM_KEYDOWN,VK_ESCAPE,0);
    SetWindowTextW(field,L"-12");app.edit("history.undo",{{"domain","all"}});press(window,4615);
    restoreCheck(app.documentOperation("automation.get",Json::object())==saved&&app.canRedoSong(),
      "Song Undo did not undo exactly the latest queued recorded edit");
    app.edit("history.redo",{{"domain","all"}});press(window,4615);
    restoreCheck(lane.snapshot().at("points")[0].at("value")==-12.,"Song Redo did not restore the automatically flushed recorded point");
    app.edit("history.undo",{{"domain","all"}});press(window,4615);
    app.edit("history.undo",{{"domain","all"}});press(window,4615);
    restoreCheck(app.documentOperation("automation.get",Json::object())==baseline,"Native Save added a musical history entry");
    SetFocus(field);SetWindowTextW(field,L"-18");lane.hide();
    restoreCheck(!lane.visible()&&!lane.snapshot().at("dirty").get<bool>()&&lane.snapshot().at("points")[0].at("value")==-18.,
      "Closing the recorded editor cancelled its latest valid automatic edit");
    lane.openAt(plugin,1);app.edit("history.undo",{{"domain","all"}});press(window,4615);
    restoreCheck(app.documentOperation("automation.get",Json::object())==baseline,"Closing the recorded editor added more than one edit");
    SetFocus(field);SetWindowTextW(field,L"invalid");restoreRejected([&]{lane.hide();});
    restoreCheck(lane.visible()&&restoreText(field)==L"invalid"&&app.documentOperation("automation.get",Json::object())==baseline,
      "Close hid an unresolved automatic failure or changed accepted music");
    SendMessageW(window,WM_KEYDOWN,VK_ESCAPE,0);
    app.documentOperation("document.open",departureOpenRequest(app,path));
    restoreCheck(app.documentOperation("automation.get",Json::object())==saved,"Native Save/reopen omitted the pending recorded point");fixture.close();
  }
  std::cout<<"PASS automatic sample settings/loops, instrument properties, mixer fields/toggles, Undo, PCM, focus/caret, invalid and stale edits, recorded viewport navigation and pending-point Save/history\n";
}
