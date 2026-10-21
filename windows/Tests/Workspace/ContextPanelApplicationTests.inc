void applicationContextPanel() {
    RestoreFixture fixture;auto &app=*fixture.app;
    ScreamSeq::Tests::ownGuiWindow(app.window);SetFocus(app.window);
    app.command(patternCommand);app.refreshContextPanel();
    const auto originalSong=songState(app),position=app.position(),selection=app.selection();
    const auto shown=app.geometry().pattern;
    restoreCheck(app.contextPanelEditor=="pattern"&&IsWindowVisible(app.controls.at(patternChooser))&&
        IsWindowVisible(app.controls.at(soundChooser))&&!IsWindowVisible(app.controls.at(sampleCommandBase)),
        "Pattern context must expose note entry rather than an unrelated sample list");
    restoreCheck(GetNextDlgTabItem(app.window,app.controls.at(contextPanelCommand),FALSE)==app.controls.at(patternChooser)&&
        GetNextDlgTabItem(app.window,app.controls.at(soundChooser),FALSE)==app.controls.at(contextActionFirst),
        "Context controls do not follow their visible reading order with Tab");
    const auto step=app.controls.at(stepChooser);RECT stepRect{},octaveRect{};
    GetWindowRect(step,&stepRect);GetWindowRect(app.controls.at(octaveChooser),&octaveRect);
    const auto dc=GetDC(step);const auto font=reinterpret_cast<HFONT>(SendMessageW(step,WM_GETFONT,0,0));
    const auto previousFont=SelectObject(dc,font);SIZE stepText{};
    GetTextExtentPoint32W(dc,L"Step 16",7,&stepText);SelectObject(dc,previousFont);ReleaseDC(step,dc);
    restoreCheck(stepRect.top>=octaveRect.bottom&&stepRect.right-stepRect.left>=stepText.cx+MulDiv(40,GetDpiForWindow(step),96),
        "Context note-entry selectors clip the current step value or overlap");
    restoreCheck(!IsWindowVisible(app.controls.at(openCommand))&&!IsWindowVisible(app.controls.at(undoCommand))&&
        app.menuCommandState(openCommand).enabled,"Removed sidebar duplicates must remain available in menus");
    app.runWorkspaceShortcut(contextPanelCommand);
    const auto hidden=app.geometry().pattern;
    restoreCheck(!app.workspaceState.contextPanelVisible&&hidden.x==6&&shown.x==170&&hidden.w==shown.w+164&&
        !IsWindowVisible(app.controls.at(patternChooser))&&IsWindowVisible(app.controls.at(contextPanelCommand))&&
        !app.menuCommandState(contextPanelCommand).checked,"Hide did not reclaim width or retain an accessible restore command");
    const auto saved=app.layoutConfiguration();
    restoreCheck(saved.at("contextPanelVisible")==false&&app.workspaceSnapshot().at("contextPanel").at("visible")==false,
        "Hidden context panel was absent from saved layouts or inspection");
    app.runWorkspaceShortcut(contextPanelCommand);
    app.restoreLayoutConfiguration(saved);
    restoreCheck(!app.workspaceState.contextPanelVisible,"Saved layout did not restore hidden context panel");
    auto legacy=saved;legacy.erase("contextPanelVisible");app.restoreLayoutConfiguration(legacy);
    restoreCheck(app.workspaceState.contextPanelVisible,"Older layouts must retain their visible sidebar default");
    const auto stable=app.layoutConfiguration();auto invalid=saved;invalid["contextPanelVisible"]="false";bool refused=false;
    try{app.restoreLayoutConfiguration(invalid);}catch(const ScreamSeq::Api::ApiError &){refused=true;}
    restoreCheck(refused&&app.layoutConfiguration()==stable,"Malformed visibility was partially adopted");
    SetCapture(app.window);refused=false;
    try{app.toggleContextPanel();}catch(const ScreamSeq::Api::ApiError &error){refused=error.code==-32002;}
    ReleaseCapture();
    restoreCheck(refused&&app.workspaceState.contextPanelVisible,"Panel resized an active mouse gesture");

    app.command(samplesCommand);SetFocus(app.window);app.refreshContextPanel();
    restoreCheck(app.contextPanelEditor=="samples"&&IsWindowVisible(app.controls.at(sampleCommandBase))&&
        !IsWindowVisible(app.controls.at(octaveChooser)),"Sample context retained unrelated pattern entry controls");
    const auto sample=app.selectedSample();
    SetFocus(app.controls.at(sampleCommandBase));
    app.toggleContextPanel();
    restoreCheck(GetFocus()==app.window&&app.selectedSample()==sample&&!IsWindowVisible(app.controls.at(sampleCommandBase)),
        "Hiding a focused sample list lost its selection or left invisible keyboard focus");
    app.toggleContextPanel();
    const auto sampleField=app.controls.at(sampleStartField);
    SetFocus(sampleField);rawDraftText(sampleField,L"-");SendMessageW(sampleField,EM_SETSEL,0,1);
    for(unsigned toggle=0;toggle<2;++toggle){
        app.runWorkspaceShortcut(contextPanelCommand);
        wchar_t raw[8]{};GetWindowTextW(sampleField,raw,8);DWORD begin=0,end=0;
        SendMessageW(sampleField,EM_GETSEL,reinterpret_cast<WPARAM>(&begin),reinterpret_cast<LPARAM>(&end));
        restoreCheck(GetFocus()==sampleField&&std::wstring_view(raw)==L"-"&&begin==0&&end==1&&app.sampleDraft&&app.selectedSample()==sample,
            "Context shortcut lost the sample field's focus, partial text, selection or target");
    }
    app.key(VK_ESCAPE,false); // Explicitly cancel this disposable partial field.
    app.command(graphCommand);SetFocus(app.window);app.refreshContextPanel();
    restoreCheck(app.contextPanelEditor=="graph"&&app.contextActions[0].command==graphLoadPlugin&&
        !IsWindowVisible(app.controls.at(patternChooser))&&!IsWindowVisible(app.controls.at(sampleCommandBase)),
        "Graph context did not expose graph actions");
    SetFocus(app.controls.at(contextActionFirst+1));app.refreshContextPanel();
    restoreCheck(app.contextPanelEditor=="graph","Moving into contextual actions changed their target editor");

    // A private desktop may cap the fixture's initial physical size. Establish
    // an exact DIP client before asserting independent region geometry.
    resizeRestoreClient(app,1440,900);
    app.command(dockPreciseNotesCommand);
    restoreCheck(app.hasWorkspaceDock(),"Region-layout context test did not create a real native dock");
    const auto notes=notesState(app);
    restoreCheck(app.workspaceRegionGeometry().mode==ScreamSeq::WorkspaceRegions::Mode::regions&&
        app.workspaceRegionGeometry().pattern.x==170,"Visible context origin differs in region layout");
    app.toggleContextPanel();
    restoreCheck(app.workspaceRegionGeometry().pattern.x==6&&notesState(app)==notes,
        "Hiding context in region layout lost width or retargeted native notes");
    app.toggleContextPanel();
    restoreCheck(notesState(app)==notes,"Restoring context changed retained native note controls");
    SetFocus(app.preciseNoteWindow->window());resizeRestoreClient(app,1000,720);
    const auto compact=app.workspaceRegionGeometry();
    restoreCheck(compact.mode==ScreamSeq::WorkspaceRegions::Mode::tabs&&compact.tabs.x==170&&
        compact.hosts[2].visible&&compact.hosts[2].selected=="preciseNotes"&&!compact.pattern.h,
        "Compact context fixture did not retain the focused native notes tab");
    app.toggleContextPanel();
    const auto compactHidden=app.workspaceRegionGeometry();
    restoreCheck(compactHidden.mode==ScreamSeq::WorkspaceRegions::Mode::tabs&&compactHidden.tabs.x==6&&
        compactHidden.tabs.w==compact.tabs.w+164&&compactHidden.hosts[2].visible&&
        compactHidden.hosts[2].selected=="preciseNotes"&&notesState(app)==notes,
        "Hidden context did not reclaim compact tab width or preserve native notes");
    app.toggleContextPanel();resizeRestoreClient(app,1440,900);

    // A hidden or pinned native editor keeps its own fields, target and focus.
    app.command(patternCommand);app.openPatternTools();
    auto *tool=app.patternToolsWindow.get();const auto field=GetDlgItem(tool->window(),8006);
    ScreamSeq::Tests::ownGuiWindow(tool->window());rawDraftText(field,L"-");SetFocus(field);
    const auto draft=tool->snapshot().at("draft"),captured=tool->snapshot().at("captured");
    app.runWorkspaceShortcut(contextPanelCommand);
    app.runWorkspaceShortcut(contextPanelCommand);
    restoreCheck(GetFocus()==field&&tool->snapshot().at("draft")==draft&&tool->snapshot().at("captured")==captured,
        "Context panel toggle stole native focus or altered a retained draft");
    restoreCheck(songState(app)==originalSong&&app.position()==position&&app.selection()==selection,
        "Presentation-only context panel changed the song, history, cursor or selection");
}
