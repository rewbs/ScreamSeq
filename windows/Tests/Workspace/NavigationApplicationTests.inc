void applicationNavigation(){
    RestoreFixture fixture;auto &app=*fixture.app;
    ScreamSeq::Tests::ownGuiWindow(app.window);resizeRestoreClient(app,1440,900);
    app.workspaceState.lowerVisible=false;app.workspaceState.panels[0].hidden=true;app.layoutControls();
    const auto song=songState(app),position=app.position(),selection=app.selection();
    app.command(mixingGraphPanelCommand);auto *owner=app.songRoutingWindow.get();
    restoreCheck(owner&&owner->docked()&&owner->visible()&&app.trackerWorkspaceVisible(),"Direct mixing graph route must show the graph beside Pattern");
    const auto handle=owner->window();ScreamSeq::Tests::ownGuiWindow(handle);
    restoreCheck(app.workspaceSnapshot().at("locations").at("mixingGraph")=="right","API does not describe the actual graph placement");
    // Expand the compact inspector and retain an intentionally incomplete raw
    // value through float/dock, same-owner restore and Pattern return.
    SendMessageW(handle,WM_COMMAND,MAKEWPARAM(3846,BN_CLICKED),LPARAM(GetDlgItem(handle,3846)));
    const auto gain=GetDlgItem(handle,3807);restoreCheck(IsWindowVisible(gain),"Compact graph inspector did not expose routing gain");
    const auto kind=GetDlgItem(handle,3804);SendMessageW(kind,CB_SETCURSEL,1,0);SendMessageW(handle,WM_COMMAND,MAKEWPARAM(3804,CBN_SELCHANGE),LPARAM(kind));
    SetFocus(gain);restoreCheck(GetFocus()==gain,"Compact routing gain cannot receive keyboard focus");
    const auto rawField=[&]{auto value=controlState(gain);value.erase("bounds");value.erase("visible");value.erase("enabled");return value;};
    SetWindowTextW(gain,L"-.");SendMessageW(gain,EM_SETSEL,1,2);
    const auto raw=rawField(),captured=owner->snapshot();
    app.command(mixingGraphFloatCommand);
    restoreCheck(app.songRoutingWindow.get()==owner&&!owner->docked()&&owner->visible()&&rawField()==raw,"Float replaced the graph owner or lost its raw field/caret");
    app.command(mixingGraphPanelCommand);
    restoreCheck(owner->docked()&&app.songRoutingWindow.get()==owner&&rawField()==raw,"Panel action followed remembered float placement or replaced its draft");
    const auto layout=app.layoutConfiguration();app.restoreLayoutConfiguration(layout);
    restoreCheck(app.songRoutingWindow.get()==owner&&rawField()==raw&&owner->snapshot().at("generation")==captured.at("generation"),"Saved layout restore reloaded the retained routing draft");
    app.command(navigationPatternCommand);
    auto returnedLayout=layout;returnedLayout["editors"]["compactSelection"]="pattern";
    restoreCheck(GetFocus()==app.window&&app.workspaceState.focus=="pattern"&&app.layoutConfiguration()==returnedLayout&&app.position()==position&&app.selection()==selection,"Pattern return changed layout/cursor/selection or missed keyboard focus");
    restoreCheck(songState(app)==song,"Navigation changed music or document history");
    // Compact fallback must keep its graph tab accessible and restore Pattern.
    resizeRestoreClient(app,900,600);app.command(mixingGraphPanelCommand);
    restoreCheck(owner->visible()&&IsWindowVisible(app.controls.at(editorMixingGraphTab)),"Narrow workspace lost its selected graph tab");
    app.command(navigationPatternCommand);restoreCheck(app.trackerWorkspaceVisible()&&GetFocus()==app.window,"Pattern could not be reached from compact graph");
    restoreCheck(rawField()==raw,"Compact navigation discarded routing text/caret");
    fixture.close();
}

void applicationRoutingRestoreAtomicity(){
    RestoreFixture fixture;auto &app=*fixture.app;auto next=app.layoutConfiguration();
    next["editors"]["locations"]["mixingGraph"]="right";next["editors"]["selected"]["right"]="mixingGraph";
    next["editors"]["compactSelection"]="mixingGraph";
    const auto before=app.layoutConfiguration(),song=songState(app);
    app.targetRead="graph.get";app.fault=RestoreApplication::Fault::read;
    restoreRejected([&]{app.restoreLayoutConfiguration(next);});
    restoreCheck(!app.songRoutingWindow&&app.layoutConfiguration()==before&&songState(app)==song,"Failed graph preparation partially adopted a layout/owner");
    app.fault=RestoreApplication::Fault::none;app.restoreLayoutConfiguration(next);
    restoreCheck(app.songRoutingWindow&&app.songRoutingWindow->docked()&&app.songRoutingWindow->visible(),"Prepared graph was not adopted into the selected region");
    ScreamSeq::Tests::ownGuiWindow(app.songRoutingWindow->window());fixture.close();
}
