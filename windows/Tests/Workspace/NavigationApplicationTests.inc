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
    restoreCheck(app.songRoutingWindow.get()==owner&&!owner->docked()&&owner->visible()&&rawField()==raw&&GetFocus()==gain,"Float replaced the graph owner or lost its raw field/caret/focus");
    app.command(mixingGraphPanelCommand);
    restoreCheck(owner->docked()&&app.songRoutingWindow.get()==owner&&rawField()==raw&&GetFocus()==gain,"Panel action followed remembered float placement or lost its draft/focus");
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

void applicationNavigationResizeAndDestinations(){
    RestoreFixture fixture;auto &app=*fixture.app;
    resizeRestoreClient(app,1300,720);app.command(navigationPluginsCommand);
    const auto lower=app.lowerEditorName();restoreCheck(app.workspaceState.lowerVisible,"Navigation fixture needs a visible lower panel");
    app.edit("mixer.enable",RestoreJson::object());const auto song=songState(app);
    app.command(mixingGraphPanelCommand);auto *owner=app.songRoutingWindow.get();
    restoreCheck(owner&&app.trackerWorkspaceVisible()&&owner->visible()&&owner->docked()&&!app.workspaceState.lowerVisible&&app.lowerEditorName()==lower,
        "Default graph action did not preserve Pattern beside the graph and retain the collapsed lower page");
    const auto handle=owner->window();ScreamSeq::Tests::ownGuiWindow(handle);
    const auto click=[&](int id){SendMessageW(handle,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),LPARAM(GetDlgItem(handle,id)));};
    click(3816);
    const auto centre=[&]{const auto canvas=owner->snapshot().at("canvas");const auto viewport=canvas.at("viewport");
        restoreCheck(viewport[2]>0&&viewport[3]>0&&!canvas.at("nodes").empty(),"Viewport fixture lost its canvas");
        const auto &node=canvas.at("nodes")[0];const auto zoom=canvas.at("zoom").get<float>();
        return std::array<float,3>{node.at("x").get<float>()+(viewport[0].get<float>()+viewport[2].get<float>()/2-node.at("rect")[0].get<float>())/zoom,
            node.at("y").get<float>()+(viewport[1].get<float>()+viewport[3].get<float>()/2-node.at("rect")[1].get<float>())/zoom,zoom};};
    const auto expected=centre();
    const auto sameCentre=[&]{const auto actual=centre();for(size_t i=0;i<actual.size();++i)restoreCheck(std::abs(actual[i]-expected[i])<.02f,"Graph resize/dock changed the world-space center or zoom");};
    resizeRestoreClient(app,1600,950);sameCentre();
    app.command(mixingGraphFloatCommand);sameCentre();
    app.command(mixingGraphPanelCommand);sameCentre();
    restoreCheck(owner->snapshot().at("compact")==true,"Expected compact docked graph");
    click(3846);restoreCheck(owner->snapshot().at("inspector")==true,"Inspector was not opened");
    resizeRestoreClient(app,1300,720);click(3846);sameCentre();
    app.command(mixingGraphFloatCommand);app.command(mixerRouting);
    restoreCheck(app.songRoutingWindow.get()==owner&&owner->docked(),"Mixer route followed remembered floating placement");
    app.command(navigationPatternCommand);const auto position=app.position();
    // Float is explicit even after a previous docked placement, and the lower
    // Precise notes tab must dock the same retained owner again.
    app.command(floatNotesCommand);auto *notes=app.preciseNoteWindow.get();
    restoreCheck(notes&&notes->visible()&&!notes->docked(),"Precise notes window command did not float");
    app.selectDockTab(dockNotes);
    restoreCheck(app.preciseNoteWindow.get()==notes&&notes->docked()&&notes->visible()&&app.workspaceSnapshot().at("locations").at("preciseNotes")=="bottom",
        "Precise notes panel tab reopened a floating window or replaced its owner");
    app.command(navigationPatternCommand);
    restoreCheck(app.position()==position&&GetFocus()==app.window&&songState(app)==song,"Presentation navigation changed music, cursor or Pattern focus");
    fixture.close();
}
